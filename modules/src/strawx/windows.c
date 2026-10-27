static Window *window_by_id(Client *client, uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
    {
        Window *w = &S.windows[i];
        if (w->used && w->id == id && w->client == client)
            return w;
    }
    return NULL;
}

static void damage_window(Window *w)
{
    damage_add(window_outer_rect(w));
}

static void damage_panel(void)
{
    damage_add(panel_rect());
}

static Window *topmost_visible(Window *except)
{
    for (int i = S.zcount - 1; i >= 0; i--)
    {
        Window *w = S.zorder[i];
        if (w != except && window_visible(w) && window_has_frame(w))
            return w;
    }
    return NULL;
}

static void window_focus(Window *w)
{
    if (S.focused == w)
        return;

    Window *old = S.focused;
    S.focused = w;

    SxEvtMsg ev;
    memset(&ev, 0, sizeof(ev));
    if (old)
    {
        send_event(old, SX_EVT_FOCUS_OUT, &ev);
        damage_window(old);
    }
    if (w)
    {
        send_event(w, SX_EVT_FOCUS_IN, &ev);
        damage_window(w);
    }
    damage_panel();
}

static void window_raise(Window *w)
{
    int idx = -1;
    for (int i = 0; i < S.zcount; i++)
        if (S.zorder[i] == w)
            idx = i;
    if (idx < 0 || idx == S.zcount - 1)
        return;

    for (int i = idx; i < S.zcount - 1; i++)
        S.zorder[i] = S.zorder[i + 1];
    S.zorder[S.zcount - 1] = w;
    damage_window(w);
}

static Window *window_create(Client *client, const SxReqCreateWindow *req)
{
    Window *w = NULL;
    for (int i = 0; i < MAX_WINDOWS && !w; i++)
        if (!S.windows[i].used)
            w = &S.windows[i];
    if (!w)
        return NULL;

    memset(w, 0, sizeof(*w));
    w->used = true;
    w->id = ++S.next_window_id;
    w->client = client;
    w->flags = req->flags;
    w->created_order = ++S.order_counter;
    memcpy(w->title, req->title, SX_TITLE_MAX);
    w->title[SX_TITLE_MAX - 1] = '\0';

    int min_w = window_has_frame(w) ? MIN_WIN_W : 1;
    int min_h = window_has_frame(w) ? 24 : 1;
    w->w = req->w < min_w ? min_w : (req->w > S.width ? S.width : req->w);
    w->h = req->h < min_h ? min_h : (req->h > S.height ? S.height : req->h);

    if (req->x == SX_POS_AUTO || req->y == SX_POS_AUTO)
    {
        int work_h = S.height - PANEL_H;
        int off = (S.cascade++ % 8) * 30 - 105;
        w->x = (S.width - w->w) / 2 + off;
        w->y = (work_h - w->h + TITLE_H) / 2 + off;
        if (w->x < 8)
            w->x = 8;
        if (w->y < TITLE_H + 8)
            w->y = TITLE_H + 8;
        if (w->x + w->w > S.width - 8)
            w->x = S.width - 8 - w->w > 1 ? S.width - 8 - w->w : 1;
        if (w->y + w->h > work_h - 8)
            w->y = work_h - 8 - w->h > TITLE_H ? work_h - 8 - w->h : TITLE_H;
    }
    else
    {
        w->x = req->x;
        w->y = req->y;
    }

    S.zorder[S.zcount++] = w;
    return w;
}

static void window_send_configure(Window *w)
{
    w->size_driven = true;
    if (w->cfg_pending)
        return;

    SxEvtMsg ev;
    memset(&ev, 0, sizeof(ev));
    ev.w = w->w;
    ev.h = w->h;
    send_event(w, SX_EVT_CONFIGURE, &ev);
    w->cfg_pending = true;
}

static void window_set_size(Window *w, int nw, int nh)
{
    if (nw == w->w && nh == w->h && !w->size_driven)
        return;

    damage_window(w);
    w->w = nw;
    w->h = nh;
    damage_window(w);
    window_send_configure(w);
}

static void window_attach(Window *w, uint32_t *buf, int bw, int bh, int stride)
{
    if (!buf || bw <= 0 || bh <= 0 || stride < bw)
        return;

    if (w->buf && w->buf != buf)
    {
        SxEvtMsg ev;
        memset(&ev, 0, sizeof(ev));
        ev.buffer = (uint64_t)w->buf;
        send_event(w, SX_EVT_BUFFER_RELEASE, &ev);
    }

    bool size_changed = bw != w->buf_w || bh != w->buf_h;

    w->buf = buf;
    w->buf_w = bw;
    w->buf_h = bh;
    w->buf_stride = stride;
    w->cfg_pending = false;

    if (w->size_driven)
    {
        if (bw == w->w && bh == w->h)
            w->size_driven = false;
        else
            window_send_configure(w);
    }
    else if (size_changed)
    {
        damage_window(w);
        w->w = bw;
        w->h = bh;
    }

    if (size_changed)
        damage_window(w);
}

static void window_set_minimized(Window *w, bool minimized)
{
    if (w->minimized == minimized)
        return;

    if (minimized)
        anim_window_minimize(w);

    damage_window(w);
    w->minimized = minimized;
    damage_window(w);
    damage_panel();

    if (!minimized)
        anim_window_restore(w);

    if (minimized && S.focused == w)
        window_focus(topmost_visible(w));
}

static void window_toggle_maximize(Window *w)
{
    if (!(w->flags & SX_WINDOW_RESIZABLE))
        return;

    AnimSnap before = anim_capture(w);
    damage_window(w);
    if (w->maximized)
    {
        w->maximized = false;
        w->x = w->restore.x;
        w->y = w->restore.y;
        window_set_size(w, w->restore.w, w->restore.h);
    }
    else
    {
        w->maximized = true;
        w->restore = window_client_rect(w);
        w->x = 1;
        w->y = TITLE_H;
        window_set_size(w, S.width - 2, S.height - PANEL_H - TITLE_H - 1);
    }
    damage_window(w);
    anim_window_reframe(w, before);
}

static void window_request_close(Window *w)
{
    anim_note_close_request(w);

    SxEvtMsg ev;
    memset(&ev, 0, sizeof(ev));
    send_event(w, SX_EVT_CLOSE, &ev);
}

static void window_destroy(Window *w)
{
    if (!w || !w->used)
        return;

    // The client frees its buffer right after asking for this, so only screen pixels are safe to use.
    anim_window_close(w, false);

    if (window_visible(w))
        damage_window(w);
    damage_panel();

    int idx = -1;
    for (int i = 0; i < S.zcount; i++)
        if (S.zorder[i] == w)
            idx = i;
    if (idx >= 0)
    {
        for (int i = idx; i < S.zcount - 1; i++)
            S.zorder[i] = S.zorder[i + 1];
        S.zcount--;
    }

    if (S.hover_win == w)
        S.hover_win = NULL;
    if (S.hover_frame_win == w)
        S.hover_frame_win = NULL;
    if (S.drag_win == w)
    {
        S.drag = DRAG_NONE;
        S.drag_win = NULL;
    }
    if (S.last_click_win == w)
        S.last_click_win = NULL;

    bool was_focused = S.focused == w;
    w->used = false;

    if (was_focused)
    {
        S.focused = NULL;
        window_focus(topmost_visible(NULL));
    }
}
