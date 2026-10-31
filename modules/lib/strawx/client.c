#define SX_QUEUE_MAX 256
#define SX_RX_MAX    8192

static struct
{
    int       fd;
    bool      connected;
    int       screen_w, screen_h;
    SxWindow *windows;
    uint32_t  next_serial;

    bool      welcomed;
    uint32_t  created_serial;
    uint32_t  created_id;

    uint8_t   rx[SX_RX_MAX];
    size_t    rx_len;

    SxEvent   queue[SX_QUEUE_MAX];
    int       q_head, q_count;
}
sx_conn = { .fd = -1 };

uint64_t sx_time_ms(void)
{
    return uptime_ms();
}

int sx_fd(void)
{
    return sx_conn.fd;
}

int sx_screen_width(void)
{
    return sx_conn.screen_w;
}

int sx_screen_height(void)
{
    return sx_conn.screen_h;
}

static bool sx_send(void *msg, uint16_t type, uint16_t size, uint32_t window)
{
    if (!sx_conn.connected)
        return false;

    SxMsgHeader *hdr = (SxMsgHeader *)msg;
    hdr->type = type;
    hdr->size = size;
    hdr->window = window;

    const uint8_t *p = (const uint8_t *)msg;
    size_t done = 0;
    while (done < size)
    {
        ssize_t n = write(sx_conn.fd, p + done, size - done);
        if (n <= 0)
        {
            sx_conn.connected = false;
            return false;
        }
        done += (size_t)n;
    }
    return true;
}

static SxWindow *sx_find_window(uint32_t id)
{
    for (SxWindow *w = sx_conn.windows; w; w = w->next)
        if (w->id == id)
            return w;
    return NULL;
}

static void sx_push_event(const SxEvent *ev)
{
    if (sx_conn.q_count == SX_QUEUE_MAX)
    {
        sx_conn.q_head = (sx_conn.q_head + 1) % SX_QUEUE_MAX;
        sx_conn.q_count--;
    }
    int tail = (sx_conn.q_head + sx_conn.q_count) % SX_QUEUE_MAX;
    sx_conn.queue[tail] = *ev;
    sx_conn.q_count++;
}

static void sx_free_buffer(SxBuffer *b)
{
    free(b->pixels);
    memset(b, 0, sizeof(*b));
}

static bool sx_alloc_buffer(SxBuffer *b, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    uint32_t *pixels = malloc((size_t)w * (size_t)h * 4);
    if (!pixels)
        return false;

    sx_free_buffer(b);
    b->pixels = pixels;
    b->w = w;
    b->h = h;
    return true;
}

static void sx_handle_message(const SxEvtMsg *m)
{
    SxEvent ev;
    memset(&ev, 0, sizeof(ev));

    switch (m->hdr.type)
    {
        case SX_EVT_WELCOME:
            sx_conn.screen_w = m->w;
            sx_conn.screen_h = m->h;
            sx_conn.welcomed = true;
            return;

        case SX_EVT_WINDOW_CREATED:
            sx_conn.created_serial = m->serial;
            sx_conn.created_id = m->hdr.window;
            return;

        case SX_EVT_THEME_CHANGED:
            // Every app redraws on resize, so a same-size resize repaints it in the new colors.
            sx_theme_load();
            for (SxWindow *w = sx_conn.windows; w; w = w->next)
            {
                ev.type = SX_EVENT_RESIZE;
                ev.window = w;
                ev.width = w->width;
                ev.height = w->height;
                sx_push_event(&ev);
            }
            return;

        default:
            break;
    }

    SxWindow *win = sx_find_window(m->hdr.window);
    if (!win)
        return;

    ev.window = win;
    ev.x = m->x;
    ev.y = m->y;
    ev.button = (int)m->button;
    ev.buttons = (int)m->buttons;
    ev.wheel = m->wheel;
    ev.key = (int)m->key;
    ev.ch = (int)m->ch;
    ev.mods = (int)m->mods;

    switch (m->hdr.type)
    {
        case SX_EVT_BUFFER_RELEASE:
            for (int i = 0; i < 2; i++)
                if ((uint64_t)win->buffers[i].pixels == m->buffer)
                    win->buffers[i].busy = false;
            return;

        case SX_EVT_CONFIGURE:
            win->width = m->w > 0 ? m->w : 1;
            win->height = m->h > 0 ? m->h : 1;
            ev.type = SX_EVENT_RESIZE;
            ev.width = win->width;
            ev.height = win->height;
            break;

        case SX_EVT_MOUSE_MOVE:  ev.type = SX_EVENT_MOUSE_MOVE;  break;
        case SX_EVT_MOUSE_DOWN:  ev.type = SX_EVENT_MOUSE_DOWN;  break;
        case SX_EVT_MOUSE_UP:    ev.type = SX_EVENT_MOUSE_UP;    break;
        case SX_EVT_MOUSE_WHEEL: ev.type = SX_EVENT_MOUSE_WHEEL; break;
        case SX_EVT_MOUSE_ENTER: ev.type = SX_EVENT_MOUSE_ENTER; break;
        case SX_EVT_MOUSE_LEAVE: ev.type = SX_EVENT_MOUSE_LEAVE; break;
        case SX_EVT_KEY_DOWN:    ev.type = SX_EVENT_KEY_DOWN;    break;
        case SX_EVT_KEY_UP:      ev.type = SX_EVENT_KEY_UP;      break;
        case SX_EVT_FOCUS_IN:    ev.type = SX_EVENT_FOCUS_IN;  win->focused = true;  break;
        case SX_EVT_FOCUS_OUT:   ev.type = SX_EVENT_FOCUS_OUT; win->focused = false; break;
        case SX_EVT_CLOSE:       ev.type = SX_EVENT_CLOSE;       break;
        default:
            return;
    }

    sx_push_event(&ev);
}

static int sx_pump(int timeout_ms)
{
    if (!sx_conn.connected)
        return -1;

    struct pollfd pfd = { sx_conn.fd, POLLIN, 0 };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0)
        return 0;

    ssize_t n = read(sx_conn.fd, sx_conn.rx + sx_conn.rx_len, SX_RX_MAX - sx_conn.rx_len);
    if (n <= 0)
    {
        sx_conn.connected = false;
        return -1;
    }
    sx_conn.rx_len += (size_t)n;

    size_t off = 0;
    while (sx_conn.rx_len - off >= sizeof(SxMsgHeader))
    {
        const SxMsgHeader *hdr = (const SxMsgHeader *)(sx_conn.rx + off);
        if (hdr->size < sizeof(SxMsgHeader) || hdr->size > SX_MSG_MAX)
        {
            sx_conn.connected = false;
            return -1;
        }
        if (sx_conn.rx_len - off < hdr->size)
            break;

        SxEvtMsg msg;
        memset(&msg, 0, sizeof(msg));
        memcpy(&msg, hdr, hdr->size < sizeof(msg) ? hdr->size : sizeof(msg));
        off += hdr->size;

        sx_handle_message(&msg);
    }

    memmove(sx_conn.rx, sx_conn.rx + off, sx_conn.rx_len - off);
    sx_conn.rx_len -= off;
    return 1;
}

int sx_connect(void)
{
    if (sx_conn.connected)
        return 0;

    for (int attempt = 0; attempt < 50; attempt++)
    {
        int fd = socket(0);
        if (fd < 0)
            return -1;

        if (connect(fd, SX_SOCKET_NAME) == 0)
        {
            sx_conn.fd = fd;
            sx_conn.connected = true;
            break;
        }

        close(fd);
        wait(100);
    }

    if (!sx_conn.connected)
        return -1;

    dup2(-1, 0);

    SxReqHello hello;
    memset(&hello, 0, sizeof(hello));
    hello.version = SX_PROTOCOL_VERSION;
    hello.pid = getpid();
    sx_send(&hello, SX_REQ_HELLO, sizeof(hello), 0);
    sx_theme_load();

    while (!sx_conn.welcomed)
        if (sx_pump(-1) < 0)
            return -1;

    return 0;
}

void sx_disconnect(void)
{
    while (sx_conn.windows)
        sx_window_destroy(sx_conn.windows);

    if (sx_conn.fd >= 0)
        close(sx_conn.fd);
    sx_conn.fd = -1;
    sx_conn.connected = false;
}

SxWindow *sx_window_create_at(const char *title, int x, int y, int width, int height, uint32_t flags)
{
    if (!sx_conn.connected && sx_connect() != 0)
        return NULL;

    SxWindow *win = calloc(1, sizeof(SxWindow));
    if (!win)
        return NULL;

    strncpy(win->title, title ? title : "", SX_TITLE_MAX - 1);
    win->flags = flags;
    win->width = width > 0 ? width : 1;
    win->height = height > 0 ? height : 1;
    win->back = -1;
    win->front = -1;

    SxReqCreateWindow req;
    memset(&req, 0, sizeof(req));
    req.serial = ++sx_conn.next_serial;
    req.x = x;
    req.y = y;
    req.w = width;
    req.h = height;
    req.flags = flags;
    strncpy(req.title, win->title, SX_TITLE_MAX - 1);

    sx_conn.created_serial = 0;
    if (!sx_send(&req, SX_REQ_CREATE_WINDOW, sizeof(req), 0))
        goto fail;

    while (sx_conn.created_serial != req.serial)
        if (sx_pump(-1) < 0)
            goto fail;

    win->id = sx_conn.created_id;
    if (win->id == 0)
        goto fail;

    win->next = sx_conn.windows;
    sx_conn.windows = win;
    return win;

fail:
    free(win);
    return NULL;
}

SxWindow *sx_window_create(const char *title, int width, int height, uint32_t flags)
{
    return sx_window_create_at(title, SX_POS_AUTO, SX_POS_AUTO, width, height, flags);
}

void sx_window_destroy(SxWindow *win)
{
    if (!win)
        return;

    SxMsgHeader msg;
    sx_send(&msg, SX_REQ_DESTROY_WINDOW, sizeof(msg), win->id);

    SxWindow **pp = &sx_conn.windows;
    while (*pp && *pp != win)
        pp = &(*pp)->next;
    if (*pp)
        *pp = win->next;

    for (int i = 0; i < sx_conn.q_count; i++)
    {
        SxEvent *ev = &sx_conn.queue[(sx_conn.q_head + i) % SX_QUEUE_MAX];
        if (ev->window == win)
            ev->type = SX_EVENT_NONE;
    }

    sx_free_buffer(&win->buffers[0]);
    sx_free_buffer(&win->buffers[1]);
    free(win);
}

SxCanvas *sx_window_canvas(SxWindow *win)
{
    if (win->back >= 0)
    {
        SxBuffer *b = &win->buffers[win->back];
        if (b->w == win->width && b->h == win->height)
            return &win->canvas;
        win->back = -1;
    }

    int pick = -1;
    while (pick < 0)
    {
        for (int i = 0; i < 2 && pick < 0; i++)
            if (!win->buffers[i].busy && i != win->front)
                pick = i;
        for (int i = 0; i < 2 && pick < 0; i++)
            if (!win->buffers[i].busy)
                pick = i;
        if (pick < 0 && sx_pump(-1) < 0)
            pick = win->front == 0 ? 1 : 0;
    }

    SxBuffer *b = &win->buffers[pick];
    if (!b->pixels || b->w != win->width || b->h != win->height)
    {
        if (!sx_alloc_buffer(b, win->width, win->height))
        {
            if (!b->pixels)
            {
                static uint32_t dummy;
                sx_canvas_init(&win->canvas, &dummy, 1, 1, 1);
                return &win->canvas;
            }
        }
    }

    if (win->front >= 0 && win->front != pick)
    {
        SxBuffer *f = &win->buffers[win->front];
        if (f->pixels && f->w == b->w && f->h == b->h)
            memcpy(b->pixels, f->pixels, (size_t)b->w * (size_t)b->h * 4);
    }

    win->back = pick;
    sx_canvas_init(&win->canvas, b->pixels, b->w, b->h, b->w);
    return &win->canvas;
}

int sx_window_width(SxWindow *win)
{
    return win->width;
}

int sx_window_height(SxWindow *win)
{
    return win->height;
}

void sx_window_damage(SxWindow *win, int x, int y, int w, int h)
{
    if (win->back < 0)
        sx_window_canvas(win);

    SxBuffer *b = &win->buffers[win->back];

    SxReqAttachBuffer att;
    memset(&att, 0, sizeof(att));
    att.pixels = (uint64_t)b->pixels;
    att.w = b->w;
    att.h = b->h;
    att.stride = b->w;
    sx_send(&att, SX_REQ_ATTACH_BUFFER, sizeof(att), win->id);

    SxReqRect dmg;
    memset(&dmg, 0, sizeof(dmg));
    dmg.x = x;
    dmg.y = y;
    dmg.w = w;
    dmg.h = h;
    sx_send(&dmg, SX_REQ_DAMAGE, sizeof(dmg), win->id);

    b->busy = true;
    win->front = win->back;
    win->back = -1;
}

void sx_window_commit(SxWindow *win)
{
    int w = win->back >= 0 ? win->buffers[win->back].w : win->width;
    int h = win->back >= 0 ? win->buffers[win->back].h : win->height;
    sx_window_damage(win, 0, 0, w, h);
}

void sx_window_set_title(SxWindow *win, const char *title)
{
    SxReqSetTitle req;
    memset(&req, 0, sizeof(req));
    strncpy(win->title, title, SX_TITLE_MAX - 1);
    strncpy(req.title, title, SX_TITLE_MAX - 1);
    sx_send(&req, SX_REQ_SET_TITLE, sizeof(req), win->id);
}

void sx_window_set_visible(SxWindow *win, bool visible)
{
    SxReqSetVisible req;
    memset(&req, 0, sizeof(req));
    req.visible = visible;
    sx_send(&req, SX_REQ_SET_VISIBLE, sizeof(req), win->id);
}

void sx_window_move(SxWindow *win, int x, int y)
{
    SxReqRect req;
    memset(&req, 0, sizeof(req));
    req.x = x;
    req.y = y;
    sx_send(&req, SX_REQ_MOVE, sizeof(req), win->id);
}

void sx_window_resize(SxWindow *win, int width, int height)
{
    SxReqRect req;
    memset(&req, 0, sizeof(req));
    req.w = width;
    req.h = height;
    sx_send(&req, SX_REQ_RESIZE, sizeof(req), win->id);
}

bool sx_pending(void)
{
    return sx_conn.q_count > 0;
}

static bool sx_pop_event(SxEvent *ev)
{
    while (sx_conn.q_count > 0)
    {
        *ev = sx_conn.queue[sx_conn.q_head];
        sx_conn.q_head = (sx_conn.q_head + 1) % SX_QUEUE_MAX;
        sx_conn.q_count--;
        if (ev->type != SX_EVENT_NONE)
            return true;
    }
    return false;
}

int sx_next_event(SxEvent *ev, int timeout_ms)
{
    uint64_t start = uptime_ms();

    for (;;)
    {
        if (sx_pop_event(ev))
            return 1;

        int wait_ms = -1;
        if (timeout_ms >= 0)
        {
            uint64_t elapsed = uptime_ms() - start;
            if (elapsed >= (uint64_t)timeout_ms)
                wait_ms = 0;
            else
                wait_ms = timeout_ms - (int)elapsed;
        }

        int rc = sx_pump(wait_ms);
        if (rc < 0)
        {
            memset(ev, 0, sizeof(*ev));
            ev->type = SX_EVENT_DISCONNECTED;
            return sx_pop_event(ev) ? 1 : -1;
        }
        if (rc == 0 && wait_ms == 0)
            return sx_pop_event(ev) ? 1 : 0;
        if (rc == 0 && timeout_ms >= 0 && uptime_ms() - start >= (uint64_t)timeout_ms)
            return sx_pop_event(ev) ? 1 : 0;
    }
}
