typedef enum
{
    SOCKET_UNBOUND,
    SOCKET_BOUND,
    SOCKET_LISTENING,
    SOCKET_CONNECTED,
}
SocketState;

typedef struct Socket Socket;

struct Socket
{
    VNode        node;
    SocketState  state;
    uint32_t     flags;
    char         name[SOCK_NAME_MAX];

    Socket      *peer;
    bool         was_connected;

    uint8_t     *rx;
    size_t       rx_head;
    size_t       rx_count;

    Socket      *backlog[SOCK_BACKLOG];
    int          backlog_len;

    Socket      *next_listener;
};

static Socket *sock_listeners = NULL;

static VFileOps socket_file_ops;

static Socket *sock_from_file(VFile *file)
{
    if (!file || !file->node || file->node->type != VNODE_SOCKET)
        return NULL;
    return (Socket *)file->node->private_data;
}

static Socket *sock_from_fd(int fd)
{
    if (!fd_valid(fd))
        return NULL;
    return sock_from_file(&fd_table[fd].file);
}

static Socket *sock_new(uint32_t flags)
{
    Socket *s = kcalloc(1, sizeof(Socket));
    if (!s)
        return NULL;

    s->state = SOCKET_UNBOUND;
    s->flags = flags;
    s->node.type = VNODE_SOCKET;
    s->node.file_ops = &socket_file_ops;
    s->node.private_data = s;
    strncpy(s->node.name, "socket", VFS_NAME_MAX - 1);
    return s;
}

static bool sock_alloc_rx(Socket *s)
{
    s->rx = pmm_alloc_pages(SOCK_BUF_SIZE / PAGE_SIZE);
    s->rx_head = 0;
    s->rx_count = 0;
    return s->rx != NULL;
}

static int sock_install_fd(Socket *s)
{
    int fd = fd_alloc();
    if (fd == FD_INVALID)
        return -EMFILE;

    fd_table[fd].file.node = &s->node;
    fd_table[fd].file.flags = VFS_O_RDWR;
    return fd;
}

static void sock_destroy(Socket *s)
{
    if (s->state == SOCKET_LISTENING)
    {
        Socket **pp = &sock_listeners;
        while (*pp && *pp != s)
            pp = &(*pp)->next_listener;
        if (*pp)
            *pp = s->next_listener;

        for (int i = 0; i < s->backlog_len; i++)
            sock_destroy(s->backlog[i]);
        s->backlog_len = 0;
    }

    if (s->peer)
    {
        s->peer->peer = NULL;
        s->peer = NULL;
    }

    if (s->rx)
        pmm_free_pages(s->rx, SOCK_BUF_SIZE / PAGE_SIZE);

    kfree(s);
    task_wake_waiters();
}

static ssize_t sock_read(VFile *file, void *buf, size_t count)
{
    Socket *s = sock_from_file(file);
    if (!s)
        return -EBADF;
    if (count == 0)
        return 0;

    uint64_t flags = irq_save();

    for (;;)
    {
        if (s->state != SOCKET_CONNECTED)
        {
            irq_restore(flags);
            return -EINVAL;
        }

        if (s->rx_count > 0)
        {
            size_t n = MIN(count, s->rx_count);
            uint8_t *out = (uint8_t *)buf;

            size_t first = MIN(n, SOCK_BUF_SIZE - s->rx_head);
            memcpy(out, s->rx + s->rx_head, first);
            memcpy(out + first, s->rx, n - first);

            s->rx_head = (s->rx_head + n) % SOCK_BUF_SIZE;
            s->rx_count -= n;

            task_wake_waiters();
            irq_restore(flags);
            return (ssize_t)n;
        }

        if (!s->peer)
        {
            irq_restore(flags);
            return 0;
        }

        if (s->flags & SOCK_NONBLOCK)
        {
            irq_restore(flags);
            return -EAGAIN;
        }

        task_block(TASK_BLOCK_FOREVER);
    }
}

static void sock_push(Socket *dst, const uint8_t *src, size_t n)
{
    size_t tail = (dst->rx_head + dst->rx_count) % SOCK_BUF_SIZE;
    size_t first = MIN(n, SOCK_BUF_SIZE - tail);
    memcpy(dst->rx + tail, src, first);
    memcpy(dst->rx, src + first, n - first);
    dst->rx_count += n;
}

static ssize_t sock_write(VFile *file, const void *buf, size_t count)
{
    Socket *s = sock_from_file(file);
    if (!s)
        return -EBADF;

    const uint8_t *src = (const uint8_t *)buf;
    size_t written = 0;

    uint64_t flags = irq_save();

    if (s->state != SOCKET_CONNECTED)
    {
        irq_restore(flags);
        return -EINVAL;
    }

    while (written < count)
    {
        Socket *peer = s->peer;
        if (!peer)
        {
            irq_restore(flags);
            return written ? (ssize_t)written : -EPIPE;
        }

        size_t remaining = count - written;
        size_t space = SOCK_BUF_SIZE - peer->rx_count;

        if (s->flags & SOCK_NONBLOCK)
        {
            if (space == 0 || (remaining <= SOCK_BUF_SIZE && space < remaining))
            {
                irq_restore(flags);
                return written ? (ssize_t)written : -EAGAIN;
            }
        }

        if (space > 0)
        {
            size_t n = MIN(space, remaining);
            sock_push(peer, src + written, n);
            written += n;
            task_wake_waiters();
            continue;
        }

        if (s->flags & SOCK_NONBLOCK)
            break;

        task_block(TASK_BLOCK_FOREVER);
    }

    irq_restore(flags);
    return (ssize_t)written;
}

static int sock_close(VFile *file)
{
    Socket *s = sock_from_file(file);
    if (!s)
        return -1;

    uint64_t flags = irq_save();
    sock_destroy(s);
    irq_restore(flags);
    return 0;
}

static int sock_poll(VFile *file)
{
    Socket *s = sock_from_file(file);
    if (!s)
        return POLLNVAL;

    int r = 0;
    switch (s->state)
    {
        case SOCKET_LISTENING:
            if (s->backlog_len > 0)
                r |= POLLIN;
            break;

        case SOCKET_CONNECTED:
            if (s->rx_count > 0)
                r |= POLLIN;
            if (!s->peer)
                r |= POLLIN | POLLHUP;
            else if (s->peer->rx_count < SOCK_BUF_SIZE)
                r |= POLLOUT;
            break;

        default:
            break;
    }
    return r;
}

int64_t sock_sys_socket(uint32_t flags)
{
    if (flags & (SOCK_TCP | SOCK_UDP))
        return inet_socket_create(flags);

    Socket *s = sock_new(flags & SOCK_NONBLOCK);
    if (!s)
        return -ENOMEM;

    int fd = sock_install_fd(s);
    if (fd < 0)
        kfree(s);
    return fd;
}

static Socket *sock_find_listener(const char *name)
{
    for (Socket *l = sock_listeners; l; l = l->next_listener)
        if (strcmp(l->name, name) == 0)
            return l;
    return NULL;
}

int64_t sock_sys_bind(int fd, const char *name)
{
    if (fd_valid(fd) && inet_is_socket(&fd_table[fd].file))
        return inet_bind(&fd_table[fd].file, name);

    Socket *s = sock_from_fd(fd);
    if (!s || !name)
        return -EBADF;

    size_t len = strlen(name);
    if (len == 0 || len >= SOCK_NAME_MAX)
        return -EINVAL;

    uint64_t flags = irq_save();

    if (s->state != SOCKET_UNBOUND)
    {
        irq_restore(flags);
        return -EINVAL;
    }

    if (sock_find_listener(name))
    {
        irq_restore(flags);
        return -EADDRINUSE;
    }

    memcpy(s->name, name, len + 1);
    s->state = SOCKET_BOUND;

    irq_restore(flags);
    return 0;
}

int64_t sock_sys_listen(int fd)
{
    if (fd_valid(fd) && inet_is_socket(&fd_table[fd].file))
        return inet_listen(&fd_table[fd].file);

    Socket *s = sock_from_fd(fd);
    if (!s)
        return -EBADF;

    uint64_t flags = irq_save();

    if (s->state != SOCKET_BOUND || sock_find_listener(s->name))
    {
        irq_restore(flags);
        return -EINVAL;
    }

    s->state = SOCKET_LISTENING;
    s->next_listener = sock_listeners;
    sock_listeners = s;

    irq_restore(flags);
    return 0;
}

int64_t sock_sys_accept(int fd)
{
    if (fd_valid(fd) && inet_is_socket(&fd_table[fd].file))
        return inet_accept(&fd_table[fd].file);

    Socket *s = sock_from_fd(fd);
    if (!s)
        return -EBADF;

    uint64_t flags = irq_save();

    for (;;)
    {
        if (s->state != SOCKET_LISTENING)
        {
            irq_restore(flags);
            return -EINVAL;
        }

        if (s->backlog_len > 0)
        {
            Socket *conn = s->backlog[0];
            for (int i = 1; i < s->backlog_len; i++)
                s->backlog[i - 1] = s->backlog[i];
            s->backlog_len--;

            conn->flags = s->flags & SOCK_NONBLOCK;

            int nfd = sock_install_fd(conn);
            if (nfd < 0)
                sock_destroy(conn);

            irq_restore(flags);
            return nfd;
        }

        if (s->flags & SOCK_NONBLOCK)
        {
            irq_restore(flags);
            return -EAGAIN;
        }

        task_block(TASK_BLOCK_FOREVER);
    }
}

int64_t sock_sys_connect(int fd, const char *name)
{
    if (fd_valid(fd) && inet_is_socket(&fd_table[fd].file))
        return inet_connect(&fd_table[fd].file, name);

    Socket *s = sock_from_fd(fd);
    if (!s || !name)
        return -EBADF;

    Socket *conn = sock_new(0);
    if (!conn)
        return -ENOMEM;
    if (!sock_alloc_rx(conn) || (!s->rx && !sock_alloc_rx(s)))
    {
        if (conn->rx)
            pmm_free_pages(conn->rx, SOCK_BUF_SIZE / PAGE_SIZE);
        kfree(conn);
        return -ENOMEM;
    }

    uint64_t flags = irq_save();

    Socket *listener = sock_find_listener(name);
    int64_t rc = 0;

    if (s->state != SOCKET_UNBOUND)
        rc = -EINVAL;
    else if (!listener)
        rc = -ECONNREFUSED;
    else if (listener->backlog_len >= SOCK_BACKLOG)
        rc = -EAGAIN;

    if (rc != 0)
    {
        sock_destroy(conn);
        irq_restore(flags);
        return rc;
    }

    conn->state = SOCKET_CONNECTED;
    conn->was_connected = true;
    conn->peer = s;

    s->state = SOCKET_CONNECTED;
    s->was_connected = true;
    s->peer = conn;

    listener->backlog[listener->backlog_len++] = conn;
    task_wake_waiters();

    irq_restore(flags);
    return 0;
}

void socket_init(void)
{
    socket_file_ops.read  = sock_read;
    socket_file_ops.write = sock_write;
    socket_file_ops.ioctl = NULL;
    socket_file_ops.close = sock_close;
    socket_file_ops.poll  = sock_poll;
}
