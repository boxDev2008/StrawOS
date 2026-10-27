#define ANIM_MAX              16
#define FADE_MAX              16
#define ANIM_FRAME_MS         16

#define ANIM_OPEN_MS          180
#define ANIM_CLOSE_MS         150
#define ANIM_MINIMIZE_MS      230
#define ANIM_MAXIMIZE_MS      200
#define ANIM_MENU_MS          150
#define ANIM_HOVER_MS         110

#define ANIM_OPEN_SCALE       0.94f
#define CLOSE_SNAP_MAX_AGE_MS 3000

#define FADE_NONE             INT64_MIN
#define MENU_SLIDE            12

typedef struct
{
    bool      active;
    Window   *owner;      // cleared when the owner is destroyed
    bool      in_place;   // drawn instead of the owner at its stacking position
    AnimSnap  snap;
    bool      framed;
    bool      active_shadow;
    bool      ease_in_out;
    SxRect    from, to;
    float     alpha_from, alpha_to;
    uint64_t  start;
    int       duration;
    SxRect    cur;
    float     cur_alpha;
}
Anim;

typedef struct
{
    bool     used;
    FadeKind kind;
    int64_t  key;
    float    level;
}
Fade;

static Anim anims[ANIM_MAX];
static Fade fades[FADE_MAX];

static float ease_out(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static float ease_in_out(float t)
{
    if (t < 0.5f)
        return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * 0.5f;
}

static SxRect rect_lerp(SxRect a, SxRect b, float t)
{
    return sx_rect(a.x + (int)((b.x - a.x) * t), a.y + (int)((b.y - a.y) * t),
                   a.w + (int)((b.w - a.w) * t), a.h + (int)((b.h - a.h) * t));
}

static SxRect rect_scale(SxRect r, float s)
{
    int w = (int)(r.w * s), h = (int)(r.h * s);
    return sx_rect(r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h);
}

static void snap_free(AnimSnap *s)
{
    free(s->px);
    memset(s, 0, sizeof(*s));
}

// Replaces the alpha channel with the frame's rounded-corner coverage.
static void snap_mask(AnimSnap *s, bool framed)
{
    SxRect r = sx_rect(0, 0, s->w, s->h);
    for (int y = 0; y < s->h; y++)
    {
        uint32_t *row = s->px + (size_t)y * s->w;
        bool corner_row = framed && (y < FRAME_RADIUS || y >= s->h - FRAME_RADIUS);
        for (int x = 0; x < s->w; x++)
        {
            uint32_t a = 255;
            if (corner_row && (x < FRAME_RADIUS || x >= s->w - FRAME_RADIUS))
            {
                float cov = 0.5f - rr_dist(x + 0.5f, y + 0.5f, r, FRAME_RADIUS);
                a = cov <= 0.0f ? 0 : cov >= 1.0f ? 255 : (uint32_t)(cov * 255.0f);
            }
            row[x] = (row[x] & 0xFFFFFFu) | (a << 24);
        }
    }
}

// Renders the window's frame and content offscreen; the client buffer must still be valid.
static AnimSnap anim_capture(Window *w)
{
    AnimSnap s = { 0 };
    if (!S.animations || !window_visible(w))
        return s;

    SxRect f = window_frame_rect(w);
    s.px = calloc((size_t)f.w * (size_t)f.h, 4);
    if (!s.px)
        return s;
    s.w = f.w;
    s.h = f.h;
    s.frame = f;

    SxCanvas c;
    sx_canvas_init(&c, s.px, f.w, f.h, f.w);
    int ox = w->x, oy = w->y;
    w->x -= f.x;
    w->y -= f.y;
    draw_window_body(&c, w);
    w->x = ox;
    w->y = oy;

    snap_mask(&s, window_has_frame(w));
    return s;
}

// Copies the window from the composited screen when nothing covers it. Used once the client
// buffer may already be freed, so the window cannot be re-rendered.
static AnimSnap anim_capture_screen(Window *w)
{
    AnimSnap s = { 0 };
    if (!S.animations || !window_visible(w))
        return s;

    SxRect f = window_frame_rect(w);
    if (f.x < 0 || f.y < 0 || f.x + f.w > S.width || f.y + f.h > S.height)
        return s;
    if (!sx_rect_empty(sx_rect_intersect(f, panel_rect())) || !sx_rect_empty(sx_rect_intersect(f, S.cursor_rect)))
        return s;
    if (S.menu_level > 0.0f && !sx_rect_empty(sx_rect_intersect(f, menu_outer_rect())))
        return s;

    bool above = false;
    for (int i = 0; i < S.zcount; i++)
    {
        Window *o = S.zorder[i];
        if (o == w)
            above = true;
        else if (above && window_visible(o) && !sx_rect_empty(sx_rect_intersect(window_outer_rect(o), f)))
            return s;
    }
    for (int i = 0; i < ANIM_MAX; i++)
        if (anims[i].active && !sx_rect_empty(sx_rect_intersect(frame_outer_rect(anims[i].cur), f)))
            return s;

    s.px = malloc((size_t)f.w * (size_t)f.h * 4);
    if (!s.px)
        return s;
    s.w = f.w;
    s.h = f.h;
    s.frame = f;
    for (int y = 0; y < f.h; y++)
        memcpy(s.px + (size_t)y * f.w, S.back + (size_t)(f.y + y) * S.width + f.x, (size_t)f.w * 4);

    snap_mask(&s, window_has_frame(w));
    return s;
}

static void anim_finish(Anim *a)
{
    damage_add(frame_outer_rect(a->cur));
    if (a->in_place && a->owner && window_visible(a->owner))
        damage_window(a->owner);
    snap_free(&a->snap);
    a->active = false;
}

static void anim_cancel(Window *w)
{
    for (int i = 0; i < ANIM_MAX; i++)
        if (anims[i].active && anims[i].owner == w)
            anim_finish(&anims[i]);
}

// Takes ownership of snap. Returns NULL (and frees snap) when animations are off or no slot is free.
static Anim *anim_begin(Window *owner, bool in_place, AnimSnap snap, SxRect from, SxRect to,
                        float alpha_from, float alpha_to, int duration, bool ease_in_out)
{
    if (!S.animations || !snap.px)
    {
        snap_free(&snap);
        return NULL;
    }

    anim_cancel(owner);

    Anim *a = NULL;
    for (int i = 0; i < ANIM_MAX && !a; i++)
        if (!anims[i].active)
            a = &anims[i];
    if (!a)
    {
        snap_free(&snap);
        return NULL;
    }

    memset(a, 0, sizeof(*a));
    a->active = true;
    a->owner = owner;
    a->in_place = in_place;
    a->snap = snap;
    a->framed = window_has_frame(owner);
    a->active_shadow = S.focused == owner;
    a->ease_in_out = ease_in_out;
    a->from = from;
    a->to = to;
    a->alpha_from = alpha_from;
    a->alpha_to = alpha_to;
    a->start = uptime_ms();
    a->duration = duration;
    a->cur = from;
    a->cur_alpha = alpha_from;
    damage_add(frame_outer_rect(from));
    S.anim_busy = true;
    return a;
}

static void anim_window_open(Window *w)
{
    AnimSnap s = anim_capture(w);
    SxRect f = window_frame_rect(w);
    float scale = window_has_frame(w) ? ANIM_OPEN_SCALE : 0.98f;
    anim_begin(w, true, s, rect_scale(f, scale), f, 0.0f, 1.0f, ANIM_OPEN_MS, false);
}

// Starts the fade-out for a window that is being destroyed or hidden. buffer_valid says whether
// the client buffer can still be read.
static void anim_window_close(Window *w, bool buffer_valid)
{
    AnimSnap s = { 0 };
    SxRect from = window_frame_rect(w);
    float alpha = 1.0f;

    for (int i = 0; i < ANIM_MAX; i++)
    {
        Anim *a = &anims[i];
        if (!a->active || a->owner != w)
            continue;
        if (a->in_place && !s.px)
        {
            // Continue from wherever the running animation has got to.
            s = a->snap;
            a->snap.px = NULL;
            from = a->cur;
            alpha = a->cur_alpha;
        }
        if (a->in_place)
            anim_finish(a);
        else
            a->owner = NULL;
    }

    uint64_t now = uptime_ms();
    if (!s.px && w->close_snap.px && now - w->close_snap_ms < CLOSE_SNAP_MAX_AGE_MS &&
        w->close_snap.w == from.w && w->close_snap.h == from.h)
    {
        s = w->close_snap;
        w->close_snap.px = NULL;
    }
    snap_free(&w->close_snap);

    if (!s.px)
        s = buffer_valid ? anim_capture(w) : anim_capture_screen(w);

    Anim *a = anim_begin(w, false, s, from, rect_scale(from, ANIM_OPEN_SCALE), alpha, 0.0f, ANIM_CLOSE_MS, false);
    if (a && !buffer_valid)
        a->owner = NULL;
}

// Remembers what the window looks like while its buffer is certainly valid, in case the client
// destroys it in response to the close request.
static void anim_note_close_request(Window *w)
{
    snap_free(&w->close_snap);
    w->close_snap = anim_capture(w);
    w->close_snap_ms = uptime_ms();
}

static SxRect anim_panel_target(Window *w, SxRect f)
{
    Window *list[MAX_WINDOWS];
    int n = panel_windows(list);
    for (int i = 0; i < n; i++)
    {
        if (list[i] != w)
            continue;
        SxRect b = panel_button_rect(i, n);
        int tw = b.w;
        int th = f.w ? f.h * tw / f.w : b.h;
        return sx_rect(b.x, b.y + b.h / 2 - th / 2, tw, th);
    }
    return rect_scale(f, 0.2f);
}

// Call before the window is marked minimized.
static void anim_window_minimize(Window *w)
{
    AnimSnap s = anim_capture(w);
    SxRect f = window_frame_rect(w);
    anim_begin(w, false, s, f, anim_panel_target(w, f), 1.0f, 0.0f, ANIM_MINIMIZE_MS, true);
}

// Call after the window is marked visible again.
static void anim_window_restore(Window *w)
{
    AnimSnap s = anim_capture(w);
    SxRect f = window_frame_rect(w);
    anim_begin(w, true, s, anim_panel_target(w, f), f, 0.2f, 1.0f, ANIM_MINIMIZE_MS, false);
}

// before was captured ahead of a maximize or restore; animates from it to the new frame.
static void anim_window_reframe(Window *w, AnimSnap before)
{
    anim_begin(w, true, before, before.frame, window_frame_rect(w), 1.0f, 1.0f, ANIM_MAXIMIZE_MS, false);
}

static void anim_blit(SxCanvas *c, const AnimSnap *s, SxRect dst, float alpha)
{
    SxRect area = sx_rect_intersect(dst, c->clip);
    uint32_t global = (uint32_t)(alpha * 255.0f + 0.5f);
    if (sx_rect_empty(area) || global == 0 || dst.w <= 0 || dst.h <= 0)
        return;

    uint32_t xstep = (uint32_t)(((uint64_t)s->w << 16) / (uint64_t)dst.w);
    for (int y = area.y; y < area.y + area.h; y++)
    {
        int sy = (int)((int64_t)(y - dst.y) * s->h / dst.h);
        const uint32_t *src = s->px + (size_t)sy * s->w;
        uint32_t *out = c->pixels + (size_t)y * c->stride;
        uint32_t fx = (uint32_t)(area.x - dst.x) * xstep;
        for (int x = area.x; x < area.x + area.w; x++, fx += xstep)
        {
            uint32_t p = src[fx >> 16];
            uint32_t a = ((p >> 24) * global + 127) / 255;
            if (a)
                out[x] = sx_blend(out[x], p, a);
        }
    }
}

static void anim_draw(SxCanvas *c, Anim *a)
{
    if (sx_rect_empty(sx_rect_intersect(frame_outer_rect(a->cur), c->clip)))
        return;

    int strength = (int)((a->active_shadow ? 120 : 80) * a->cur_alpha);
    int radius = a->framed && a->snap.w ? FRAME_RADIUS * a->cur.w / a->snap.w : 0;
    if (strength > 0 && a->cur.w > 2 * radius && a->cur.h > 2 * radius)
        draw_shadow(c, a->cur, radius, strength);
    anim_blit(c, &a->snap, a->cur, a->cur_alpha);
}

// Draws the window's in-place animation, if it has one. Returns false when the window
// should be drawn normally.
static bool anim_draw_window(SxCanvas *c, Window *w)
{
    for (int i = 0; i < ANIM_MAX; i++)
    {
        if (anims[i].active && anims[i].in_place && anims[i].owner == w)
        {
            anim_draw(c, &anims[i]);
            return true;
        }
    }
    return false;
}

static void anim_draw_ghosts(SxCanvas *c)
{
    for (int i = 0; i < ANIM_MAX; i++)
        if (anims[i].active && !anims[i].in_place)
            anim_draw(c, &anims[i]);
}

// Draws fn into the screen at the given opacity, using S.layer as scratch space.
static void draw_faded(SxCanvas *c, SxRect bounds, void (*fn)(SxCanvas *), float alpha)
{
    SxRect r = sx_rect_intersect(bounds, c->clip);
    uint32_t a = (uint32_t)(alpha * 255.0f + 0.5f);
    if (sx_rect_empty(r) || a == 0)
        return;
    if (!S.layer || a >= 255)
    {
        fn(c);
        return;
    }

    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(S.layer + (size_t)y * S.width + r.x, c->pixels + (size_t)y * c->stride + r.x, (size_t)r.w * 4);

    SxCanvas layer;
    sx_canvas_init(&layer, S.layer, S.width, S.height, S.width);
    sx_canvas_set_clip(&layer, r);
    fn(&layer);

    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint32_t *dst = c->pixels + (size_t)y * c->stride;
        const uint32_t *src = S.layer + (size_t)y * S.width;
        for (int x = r.x; x < r.x + r.w; x++)
            dst[x] = sx_blend(dst[x], src[x], a);
    }
}

static int menu_slide_offset(void)
{
    return (int)((1.0f - ease_out(S.menu_level)) * MENU_SLIDE);
}

static SxRect menu_anim_bounds(void)
{
    SxRect o = menu_outer_rect();
    return sx_rect(o.x, o.y - menu_slide_offset(), o.w, o.h + MENU_SLIDE);
}

static int64_t fade_current(FadeKind kind)
{
    switch (kind)
    {
        case FADE_TITLE:
            return S.hover_frame_win ? ((int64_t)S.hover_frame_win->id << 4) | S.hover_zone : FADE_NONE;
        case FADE_PANEL:
            return S.panel_hover != -1 ? S.panel_hover : FADE_NONE;
        case FADE_MENU:
            return S.menu_open && S.menu_hover >= 0 ? S.menu_hover : FADE_NONE;
        case FADE_DESK:
            return S.desktop_hover >= 0 ? S.desktop_hover : FADE_NONE;
        default:
            return FADE_NONE;
    }
}

static SxRect fade_rect(FadeKind kind, int64_t key)
{
    switch (kind)
    {
        case FADE_TITLE:
            for (int i = 0; i < MAX_WINDOWS; i++)
            {
                Window *w = &S.windows[i];
                if (w->used && w->id == (uint32_t)(key >> 4) && window_visible(w))
                    return titlebar_button_rect(w, (HitZone)(key & 0xF));
            }
            break;
        case FADE_PANEL:
        {
            if (key == -2)
                return panel_start_rect();
            Window *list[MAX_WINDOWS];
            return panel_button_rect((int)key, panel_windows(list));
        }
        case FADE_MENU:
            if (key < shell_app_count)
                return menu_item_rect((int)key);
            break;
        case FADE_DESK:
            if (key < shell_app_count)
                return desktop_cell((int)key);
            break;
        default:
            break;
    }
    return sx_rect(0, 0, 0, 0);
}

// How strongly a hover highlight should be drawn, from 0 to 1.
static float fade_level(FadeKind kind, int64_t key)
{
    for (int i = 0; i < FADE_MAX; i++)
        if (fades[i].used && fades[i].kind == kind && fades[i].key == key)
            return fades[i].level;
    return 0.0f;
}

static bool fade_tick(float step)
{
    bool busy = false;
    int64_t current[FADE_KINDS];
    for (int k = 0; k < FADE_KINDS; k++)
    {
        current[k] = fade_current((FadeKind)k);
        if (current[k] == FADE_NONE)
            continue;

        bool found = false;
        for (int i = 0; i < FADE_MAX && !found; i++)
            found = fades[i].used && fades[i].kind == (FadeKind)k && fades[i].key == current[k];
        for (int i = 0; i < FADE_MAX && !found; i++)
        {
            if (!fades[i].used)
            {
                fades[i] = (Fade){ true, (FadeKind)k, current[k], 0.0f };
                found = true;
            }
        }
    }

    for (int i = 0; i < FADE_MAX; i++)
    {
        Fade *f = &fades[i];
        if (!f->used)
            continue;

        bool target = f->key == current[f->kind];
        float level = !S.animations ? (target ? 1.0f : 0.0f)
                    : target ? f->level + step : f->level - step;
        if (level > 1.0f) level = 1.0f;
        if (level < 0.0f) level = 0.0f;

        if (level != f->level)
        {
            f->level = level;
            damage_add(fade_rect(f->kind, f->key));
        }
        if (level == 0.0f && !target)
            f->used = false;
        else if (level != (target ? 1.0f : 0.0f))
            busy = true;
    }
    return busy;
}

static bool menu_tick(float step)
{
    float target = S.menu_open ? 1.0f : 0.0f;
    if (S.menu_level == target)
        return false;

    damage_add(menu_anim_bounds());
    if (!S.animations)
        S.menu_level = target;
    else if (S.menu_level < target)
        S.menu_level = S.menu_level + step > target ? target : S.menu_level + step;
    else
        S.menu_level = S.menu_level - step < target ? target : S.menu_level - step;
    damage_add(menu_anim_bounds());
    return S.menu_level != target;
}

static void anim_tick(void)
{
    static uint64_t last;
    uint64_t now = uptime_ms();
    // After an idle stretch, treat the first step as a single frame rather than a jump.
    uint64_t dt = S.anim_busy ? now - last : ANIM_FRAME_MS;
    if (dt > 50)
        dt = 50;
    last = now;

    bool busy = false;
    for (int i = 0; i < ANIM_MAX; i++)
    {
        Anim *a = &anims[i];
        if (!a->active)
            continue;

        float t = a->duration > 0 ? (float)(now - a->start) / (float)a->duration : 1.0f;
        if (t >= 1.0f || !S.animations)
        {
            a->cur = a->to;
            anim_finish(a);
            continue;
        }

        float e = a->ease_in_out ? ease_in_out(t) : ease_out(t);
        damage_add(frame_outer_rect(a->cur));
        a->cur = rect_lerp(a->from, a->to, e);
        a->cur_alpha = a->alpha_from + (a->alpha_to - a->alpha_from) * e;
        damage_add(frame_outer_rect(a->cur));
        busy = true;
    }

    busy |= menu_tick((float)dt / ANIM_MENU_MS);
    busy |= fade_tick((float)dt / ANIM_HOVER_MS);
    S.anim_busy = busy;
}
