#define FD_MAX 1024
#define FD_INVALID (-1)

typedef struct
{
    VFile file;
    bool  used;
    Task *owner;
}
FdSlot;

typedef struct
{
    int32_t fd;
    int16_t events;
    int16_t revents;
}
PollFd;

typedef struct
{
    char name[VFS_NAME_MAX];
    VNodeType type;
}
FsDirent;

static FdSlot fd_table[FD_MAX];

void fs_kcalls_init(void)
{
    memset(fd_table, 0, sizeof(fd_table));
    fd_table[0].used = true;
    fd_table[1].used = true;
    fd_table[2].used = true;
}

static int fd_alloc(void)
{
    uint64_t flags = irq_save();
    for (int i = 0; i < FD_MAX; i++)
    {
        if (!fd_table[i].used)
        {
            fd_table[i].used = true;
            fd_table[i].owner = task_current();
            irq_restore(flags);
            return i;
        }
    }
    irq_restore(flags);
    return FD_INVALID;
}

static void fd_release(int fd)
{
    task_forget_fd(fd);
    memset(&fd_table[fd].file, 0, sizeof(VFile));
    fd_table[fd].owner = NULL;
    fd_table[fd].used = false;
}

void fs_release_task(Task *t)
{
    for (int i = 3; i < FD_MAX; i++)
    {
        if (fd_table[i].used && fd_table[i].owner == t)
        {
            vfs_close(&fd_table[i].file);
            fd_release(i);
        }
    }
}

static bool fd_valid(int fd)
{
    return fd >= 0 && fd < FD_MAX && fd_table[fd].used;
}

static int fd_resolve(int fd)
{
    if (fd >= 0 && fd < 3)
    {
        Task *t = task_current();
        if (t && t->stdio[fd] >= 0 && fd_valid(t->stdio[fd]))
            return t->stdio[fd];
    }
    return fd;
}

int64_t fs_sys_dup2(int fd, int target)
{
    if (target < 0 || target > 2)
        return -1;

    Task *t = task_current();
    if (!t)
        return -1;

    if (fd < 0)
    {
        t->stdio[target] = -1;
        if (target == 0)
            t->detached = true;
        return 0;
    }

    fd = fd_resolve(fd);
    if (!fd_valid(fd) || fd < 3)
        return -1;

    t->stdio[target] = fd;
    return target;
}

int64_t fs_sys_open(const char *path, uint32_t flags)
{
    if (!path)
        return FD_INVALID;

    int fd = fd_alloc();
    if (fd == FD_INVALID)
    {
        kprintf("fs_kcalls: open(%s) failed, fd table full\n", path);
        return FD_INVALID;
    }

    if (vfs_open(path, flags, &fd_table[fd].file) != 0)
    {
        fd_release(fd);
        return FD_INVALID;
    }

    return fd;
}

int64_t fs_sys_close(int fd)
{
    if (fd < 3)
    {
        Task *t = task_current();
        if (t && fd >= 0)
            t->stdio[fd] = -1;
        return 0;
    }
    if (!fd_valid(fd))
        return -1;

    int rc = vfs_close(&fd_table[fd].file);
    fd_release(fd);
    return rc;
}

int64_t fs_sys_read(int fd, void *buf, size_t count)
{
    fd = fd_resolve(fd);
    if (!fd_valid(fd) || !buf)
        return -1;

    return (int64_t)vfs_read(&fd_table[fd].file, buf, count);
}

int64_t fs_sys_write(int fd, const void *buf, size_t count)
{
    fd = fd_resolve(fd);
    if (!fd_valid(fd) || !buf)
        return -1;
    if (fd == 1 || fd == 2)
    {
        term_puts(buf, count);
        return (int64_t)count;
    }

    return (int64_t)vfs_write(&fd_table[fd].file, buf, count);
}

int64_t fs_sys_ioctl(int fd, uint64_t request, void *arg)
{
    fd = fd_resolve(fd);
    if (!fd_valid(fd))
        return -1;

    return vfs_ioctl(&fd_table[fd].file, request, arg);
}

int64_t fs_sys_mkdir(const char *path)
{
    if (!path)
        return -1;

    return vfs_mkdir(path) != NULL ? 0 : -1;
}

int64_t fs_sys_readdir(int fd, FsDirent *out)
{
    if (!fd_valid(fd) || !out)
        return -1;

    VFile *f = &fd_table[fd].file;
    if (!f->node || f->node->type != VNODE_DIR)
        return -1;

    char name[VFS_NAME_MAX];
    int rc = vfs_readdir(f->node, (int)f->offset, name);
    if (rc != 0)
        return 0;

    size_t len = strlen(name);
    if (len >= VFS_NAME_MAX)
        len = VFS_NAME_MAX - 1;
    memcpy(out->name, name, len);
    out->name[len] = '\0';

    VNode *child = f->node->ops && f->node->ops->lookup
        ? f->node->ops->lookup(f->node, name) : NULL;
    out->type = child ? child->type : VNODE_FILE;

    f->offset++;
    return 1;
}

int64_t fs_sys_stat(const char *path, VfsStat *out)
{
    if (!path || !out)
        return -1;

    return vfs_stat(path, out);
}

int64_t fs_sys_fstat(int fd, VfsStat *out)
{
    fd = fd_resolve(fd);
    if (!fd_valid(fd) || !out)
        return -1;

    return vfs_fstat(&fd_table[fd].file, out);
}

int64_t fs_sys_lseek(int fd, int64_t offset, int whence)
{
    if (!fd_valid(fd))
        return -1;

    return (int64_t)vfs_lseek(&fd_table[fd].file, offset, whence);
}

int64_t fs_sys_unlink(const char *path)
{
    if (!path)
        return -1;

    return vfs_unlink(path);
}
int64_t fs_sys_rename(const char *from, const char *to)
{
    if (!from || !to)
        return -1;

    return vfs_rename(from, to);
}

int64_t fs_sys_poll(PollFd *fds, uint64_t count, int64_t timeout_ms)
{
    if (!fds && count)
        return -1;

    uint64_t start = task_uptime_ms();

    for (;;)
    {
        uint64_t flags = irq_save();

        int ready = 0;
        for (uint64_t i = 0; i < count; i++)
        {
            int fd = fd_resolve(fds[i].fd);
            int16_t revents;

            if (fd < 0)
                revents = 0;
            else if (!fd_valid(fd) || !fd_table[fd].file.node)
                revents = POLLNVAL;
            else
                revents = (int16_t)(vfs_poll(&fd_table[fd].file) & (fds[i].events | POLLERR | POLLHUP | POLLNVAL));

            fds[i].revents = revents;
            if (revents)
                ready++;
        }

        if (ready || timeout_ms == 0)
        {
            irq_restore(flags);
            return ready;
        }

        uint32_t wait_ms = TASK_BLOCK_FOREVER;
        if (timeout_ms > 0)
        {
            uint64_t elapsed = task_uptime_ms() - start;
            if (elapsed >= (uint64_t)timeout_ms)
            {
                irq_restore(flags);
                return 0;
            }
            wait_ms = (uint32_t)((uint64_t)timeout_ms - elapsed);
        }

        task_block(wait_ms);
        irq_restore(flags);
    }
}
