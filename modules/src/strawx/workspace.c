typedef enum
{
    SNAP_NONE,
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_MAX,
}
SnapKind;

static SxRect work_area(void)
{
    return sx_rect(0, BAR_H, S.width, S.height - BAR_H);
}

static SxRect frame_to_client(SxRect f)
{
    return sx_rect(f.x + 1, f.y + TITLE_H, f.w - 2, f.h - TITLE_H - 1);
}

static void window_place(Window *w, SxRect frame, bool animate)
{
    SxRect cr = frame_to_client(frame);
    if (cr.x == w->x && cr.y == w->y && cr.w == w->w && cr.h == w->h)
        return;

    AnimSnap before = animate ? anim_capture(w) : (AnimSnap){ 0 };
    damage_window(w);
    w->x = cr.x;
    w->y = cr.y;
    damage_window(w);
    window_set_size(w, cr.w, cr.h);
    if (before.px)
        anim_window_reframe(w, before);
}

static void workspace_switch(int ws)
{
    if (ws < 0 || ws >= WORKSPACES || ws == S.workspace)
        return;

    Window *carry = S.drag == DRAG_MOVE ? S.drag_win : NULL;
    if (!carry)
    {
        S.drag = DRAG_NONE;
        S.drag_win = NULL;
    }

    S.ws_focus[S.workspace] = S.focused;
    S.workspace = ws;
    if (carry)
        carry->workspace = ws;

    S.snap_active = false;
    Window *f = carry ? carry : S.ws_focus[ws];
    window_focus(window_visible(f) ? f : topmost_visible(NULL));
    damage_all();
    update_hover();
}

static void window_move_to_workspace(Window *w, int ws)
{
    if (!w || ws < 0 || ws >= WORKSPACES || w->workspace == ws)
        return;

    damage_window(w);
    if (S.ws_focus[w->workspace] == w)
        S.ws_focus[w->workspace] = NULL;
    w->workspace = ws;
    S.ws_focus[ws] = w;
    if (S.focused == w)
        window_focus(topmost_visible(NULL));
    damage_chrome();
    update_hover();
}

static void focus_neighbour(int dir)
{
    Window *list[MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < S.zcount; i++)
        if (window_visible(S.zorder[i]) && window_has_frame(S.zorder[i]))
            list[n++] = S.zorder[i];
    if (n == 0)
        return;

    int at = -1;
    for (int i = 0; i < n; i++)
        if (list[i] == S.focused)
            at = i;
    Window *w = list[at < 0 ? 0 : (at + dir + n) % n];
    window_raise(w);
    window_focus(w);
}

static void snap_set(bool active, SxRect r, SnapKind kind)
{
    if (S.snap_active == active && (!active || (S.snap_kind == (int)kind && S.snap_rect.x == r.x &&
        S.snap_rect.y == r.y && S.snap_rect.w == r.w && S.snap_rect.h == r.h)))
        return;

    if (S.snap_active)
        damage_add(S.snap_rect);
    S.snap_active = active;
    S.snap_rect = r;
    S.snap_kind = kind;
    if (active)
        damage_add(r);
}

static void snap_clear(void)
{
    snap_set(false, sx_rect(0, 0, 0, 0), SNAP_NONE);
}

static void snap_update(Window *w)
{
    if (!window_has_frame(w) || !(w->flags & SX_WINDOW_RESIZABLE))
    {
        snap_clear();
        return;
    }

    SxRect area = work_area();
    int half_w = (area.w - SNAP_GAP * 3) / 2;
    int h = area.h - SNAP_GAP * 2;
    if (S.mx <= SNAP_EDGE)
        snap_set(true, sx_rect(area.x + SNAP_GAP, area.y + SNAP_GAP, half_w, h), SNAP_LEFT);
    else if (S.mx >= S.width - 1 - SNAP_EDGE)
        snap_set(true, sx_rect(area.x + area.w - SNAP_GAP - half_w, area.y + SNAP_GAP, half_w, h), SNAP_RIGHT);
    else if (S.my <= BAR_H + SNAP_EDGE)
        snap_set(true, area, SNAP_MAX);
    else
        snap_clear();
}

static void window_set_maximized(Window *w, bool on, SxRect restore);

static void snap_apply(Window *w)
{
    if (!S.snap_active)
        return;

    if (S.snap_kind == SNAP_MAX)
    {
        window_set_maximized(w, true, S.drag_orig_rect);
    }
    else
    {
        w->restore = S.drag_orig_rect;
        w->snapped = true;
        window_place(w, S.snap_rect, true);
    }
    snap_clear();
}

// Lifts a maximized or snapped window out so it can be dragged at its normal size.
static void drag_detach(Window *w)
{
    SxRect old = window_client_rect(w);
    SxRect size = w->restore;
    w->maximized = false;
    w->snapped = false;

    // Keep the cursor at the same spot along the titlebar.
    float frac = old.w > 0 ? (float)(S.drag_start_mx - old.x) / (float)old.w : 0.5f;
    int nx = S.mx - (int)(frac * size.w);
    int ny = S.my - (S.drag_start_my - old.y);

    damage_window(w);
    w->x = nx;
    w->y = ny;
    damage_window(w);
    window_set_size(w, size.w, size.h);

    S.drag_start_mx = S.mx;
    S.drag_start_my = S.my;
    S.drag_start_rect = sx_rect(nx, ny, size.w, size.h);
    S.drag_orig_rect = S.drag_start_rect;
    S.drag_detach = false;
}
