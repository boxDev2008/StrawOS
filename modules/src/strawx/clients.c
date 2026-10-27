static void send_raw(Client *c, const void *msg, size_t size)
{
    write(c->fd, msg, size);
}

static void send_event(Window *w, uint16_t type, const SxEvtMsg *proto)
{
    if (!w || !w->client)
        return;

    SxEvtMsg msg = *proto;
    msg.hdr.type = type;
    msg.hdr.size = sizeof(msg);
    msg.hdr.window = w->id;
    send_raw(w->client, &msg, sizeof(msg));
}

static void client_accept(void)
{
    for (;;)
    {
        int fd = accept(S.listen_fd);
        if (fd < 0)
            return;

        Client *c = NULL;
        for (int i = 0; i < MAX_CLIENTS && !c; i++)
            if (!S.clients[i].used)
                c = &S.clients[i];

        if (!c)
        {
            close(fd);
            continue;
        }

        memset(c, 0, sizeof(*c));
        c->used = true;
        c->fd = fd;
        c->id = ++S.next_client_id;
    }
}

static void client_disconnect(Client *c)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (S.windows[i].used && S.windows[i].client == c)
            window_destroy(&S.windows[i]);

    close(c->fd);
    c->used = false;
    update_hover();
}

static void client_handle(Client *c, const SxMsgHeader *hdr)
{
    Window *w = hdr->window ? window_by_id(c, hdr->window) : NULL;

    switch (hdr->type)
    {
        case SX_REQ_HELLO:
        {
            SxEvtMsg ev;
            memset(&ev, 0, sizeof(ev));
            ev.hdr.type = SX_EVT_WELCOME;
            ev.hdr.size = sizeof(ev);
            ev.w = S.width;
            ev.h = S.height;
            ev.serial = c->id;
            send_raw(c, &ev, sizeof(ev));
            break;
        }

        case SX_REQ_CREATE_WINDOW:
        {
            if (hdr->size < sizeof(SxReqCreateWindow))
                break;
            const SxReqCreateWindow *req = (const SxReqCreateWindow *)hdr;
            Window *nw = window_create(c, req);

            SxEvtMsg ev;
            memset(&ev, 0, sizeof(ev));
            ev.hdr.type = SX_EVT_WINDOW_CREATED;
            ev.hdr.size = sizeof(ev);
            ev.hdr.window = nw ? nw->id : 0;
            ev.serial = req->serial;
            ev.w = nw ? nw->w : 0;
            ev.h = nw ? nw->h : 0;
            send_raw(c, &ev, sizeof(ev));
            break;
        }

        case SX_REQ_DESTROY_WINDOW:
            if (w)
            {
                window_destroy(w);
                update_hover();
            }
            break;

        case SX_REQ_SET_TITLE:
            if (w && hdr->size >= sizeof(SxReqSetTitle))
            {
                const SxReqSetTitle *req = (const SxReqSetTitle *)hdr;
                memcpy(w->title, req->title, SX_TITLE_MAX);
                w->title[SX_TITLE_MAX - 1] = '\0';
                if (window_visible(w))
                    damage_add(titlebar_rect(w));
                damage_panel();
            }
            break;

        case SX_REQ_ATTACH_BUFFER:
            if (w && hdr->size >= sizeof(SxReqAttachBuffer))
            {
                const SxReqAttachBuffer *req = (const SxReqAttachBuffer *)hdr;
                window_attach(w, (uint32_t *)req->pixels, req->w, req->h, req->stride);
            }
            break;

        case SX_REQ_DAMAGE:
            if (w && w->buf && hdr->size >= sizeof(SxReqRect))
            {
                const SxReqRect *req = (const SxReqRect *)hdr;
                if (!w->mapped)
                {
                    w->mapped = true;
                    window_raise(w);
                    if (window_has_frame(w))
                        window_focus(w);
                    damage_window(w);
                    damage_panel();
                    update_hover();
                    anim_window_open(w);
                }
                else if (window_visible(w))
                {
                    SxRect r = sx_rect(w->x + req->x, w->y + req->y, req->w, req->h);
                    damage_add(sx_rect_intersect(r, window_client_rect(w)));
                }
            }
            break;

        case SX_REQ_SET_VISIBLE:
            if (w && hdr->size >= sizeof(SxReqSetVisible))
            {
                const SxReqSetVisible *req = (const SxReqSetVisible *)hdr;
                bool hidden = !req->visible;
                if (hidden != w->hidden)
                {
                    if (hidden)
                        anim_window_close(w, true);
                    damage_window(w);
                    w->hidden = hidden;
                    damage_window(w);
                    if (!hidden)
                        anim_window_open(w);
                    damage_panel();
                    if (hidden && S.focused == w)
                        window_focus(topmost_visible(w));
                    update_hover();
                }
            }
            break;

        case SX_REQ_MOVE:
            if (w && hdr->size >= sizeof(SxReqRect))
            {
                const SxReqRect *req = (const SxReqRect *)hdr;
                damage_window(w);
                w->x = req->x;
                w->y = req->y;
                damage_window(w);
            }
            break;

        case SX_REQ_RELOAD_SETTINGS:
            settings_load();
            S.desktop_selected = -1;
            S.desktop_hover = -1;
            damage_all();
            break;

        case SX_REQ_RESIZE:
            if (w && hdr->size >= sizeof(SxReqRect) && !w->maximized)
            {
                const SxReqRect *req = (const SxReqRect *)hdr;
                int nw = req->w < 1 ? 1 : (req->w > S.width ? S.width : req->w);
                int nh = req->h < 1 ? 1 : (req->h > S.height ? S.height : req->h);
                window_set_size(w, nw, nh);
            }
            break;

        default:
            break;
    }
}

static bool client_read(Client *c)
{
    for (;;)
    {
        if (c->rx_len == CLIENT_RX_MAX)
            return false;

        ssize_t n = read(c->fd, c->rx + c->rx_len, CLIENT_RX_MAX - c->rx_len);
        if (n == -EAGAIN)
            break;
        if (n <= 0)
            return false;
        c->rx_len += (size_t)n;

        size_t off = 0;
        while (c->rx_len - off >= sizeof(SxMsgHeader))
        {
            union
            {
                SxMsgHeader hdr;
                uint8_t     bytes[SX_MSG_MAX];
            }
            msg;

            memcpy(&msg.hdr, c->rx + off, sizeof(SxMsgHeader));
            if (msg.hdr.size < sizeof(SxMsgHeader) || msg.hdr.size > SX_MSG_MAX)
                return false;
            if (c->rx_len - off < msg.hdr.size)
                break;

            memcpy(msg.bytes, c->rx + off, msg.hdr.size);
            off += msg.hdr.size;
            client_handle(c, &msg.hdr);
        }

        memmove(c->rx, c->rx + off, c->rx_len - off);
        c->rx_len -= off;
    }
    return true;
}
