#define COLOR_TITLE_ACTIVE    (sx_theme->title)
#define COLOR_TITLE_INACTIVE  (sx_theme->title_inactive)
#define COLOR_TITLE_TEXT      (sx_theme->text)
#define COLOR_TITLE_TEXT_DIM  (sx_theme->text_dim)
#define COLOR_CONTENT_FILL    (sx_theme->bg)
#define COLOR_FRAME_BORDER    SX_RGBA(255, 255, 255, 30)
#define COLOR_CLOSE_HOVER     (sx_theme->red)

#define TB_BUTTON_W 24
#define TB_BUTTON_H 24
#define TB_BUTTON_GAP 6

static SxRect screen_rect(void)
{
    return sx_rect(0, 0, S.width, S.height);
}

static bool rects_near(SxRect a, SxRect b)
{
    const int slack = 24;
    return a.x - slack < b.x + b.w && b.x - slack < a.x + a.w &&
           a.y - slack < b.y + b.h && b.y - slack < a.y + a.h;
}

static void damage_add(SxRect r)
{
    r = sx_rect_intersect(r, screen_rect());
    if (sx_rect_empty(r))
        return;

    bool merged = true;
    while (merged)
    {
        merged = false;
        for (int i = 0; i < S.damage_count; i++)
        {
            if (rects_near(S.damage[i], r))
            {
                r = sx_rect_union(r, S.damage[i]);
                S.damage[i] = S.damage[--S.damage_count];
                merged = true;
                break;
            }
        }
    }

    if (S.damage_count == MAX_DAMAGE)
    {
        for (int i = 1; i < S.damage_count; i++)
            S.damage[0] = sx_rect_union(S.damage[0], S.damage[i]);
        S.damage_count = 1;
        S.damage[0] = sx_rect_union(S.damage[0], r);
        return;
    }

    S.damage[S.damage_count++] = r;
}

static void damage_all(void)
{
    S.damage_count = 0;
    damage_add(screen_rect());
}

static bool window_has_frame(Window *w)
{
    return !(w->flags & SX_WINDOW_NO_FRAME);
}

static SxRect window_client_rect(Window *w)
{
    return sx_rect(w->x, w->y, w->w, w->h);
}

static SxRect window_frame_rect(Window *w)
{
    if (!window_has_frame(w))
        return window_client_rect(w);
    return sx_rect(w->x - 1, w->y - TITLE_H, w->w + 2, w->h + TITLE_H + 1);
}

static SxRect frame_outer_rect(SxRect f)
{
    return sx_rect(f.x - SHADOW_SIZE, f.y - SHADOW_SIZE + SHADOW_OFF_Y,
                   f.w + 2 * SHADOW_SIZE, f.h + 2 * SHADOW_SIZE);
}

static SxRect window_outer_rect(Window *w)
{
    return frame_outer_rect(window_frame_rect(w));
}

static bool window_visible(Window *w)
{
    return w && w->used && w->mapped && !w->hidden && !w->minimized && w->workspace == S.workspace;
}

static SxRect titlebar_button_rect(Window *w, HitZone zone)
{
    SxRect f = window_frame_rect(w);
    int index;
    switch (zone)
    {
        case HIT_CLOSE:    index = 0; break;
        case HIT_MAXIMIZE: index = 1; break;
        case HIT_MINIMIZE: index = (w->flags & SX_WINDOW_RESIZABLE) ? 2 : 1; break;
        default: return sx_rect(0, 0, 0, 0);
    }
    if (zone == HIT_MAXIMIZE && !(w->flags & SX_WINDOW_RESIZABLE))
        return sx_rect(0, 0, 0, 0);

    int right = f.x + f.w - 10;
    int bx = right - (index + 1) * TB_BUTTON_W - index * TB_BUTTON_GAP;
    int by = f.y + (TITLE_H - TB_BUTTON_H) / 2;
    return sx_rect(bx, by, TB_BUTTON_W, TB_BUTTON_H);
}

static SxRect titlebar_rect(Window *w)
{
    SxRect f = window_frame_rect(w);
    return sx_rect(f.x, f.y, f.w, TITLE_H);
}

static void wallpaper_generate(void)
{
    char spec[SX_SETTING_MAX];
    if (!sx_setting_get("wallpaper", spec, sizeof(spec)))
        spec[0] = '\0';
    sx_render_wallpaper(spec, S.wallpaper, S.width, S.height);
}

static inline float rr_dist(float px, float py, SxRect r, float radius)
{
    float hw = r.w * 0.5f, hh = r.h * 0.5f;
    float qx = (px > r.x + hw ? px - (r.x + hw) : (r.x + hw) - px) - (hw - radius);
    float qy = (py > r.y + hh ? py - (r.y + hh) : (r.y + hh) - py) - (hh - radius);
    float ox = qx > 0 ? qx : 0;
    float oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    if (inside > 0)
        inside = 0;
    return __builtin_sqrtf(ox * ox + oy * oy) + inside - radius;
}

static void draw_shadow(SxCanvas *c, SxRect r, int radius, int strength)
{
    SxRect sr = sx_rect(r.x, r.y + SHADOW_OFF_Y, r.w, r.h);
    SxRect area = sx_rect(sr.x - SHADOW_SIZE, sr.y - SHADOW_SIZE, sr.w + 2 * SHADOW_SIZE, sr.h + 2 * SHADOW_SIZE);
    area = sx_rect_intersect(area, c->clip);

    for (int y = area.y; y < area.y + area.h; y++)
    {
        bool row_inside = y >= r.y + radius && y < r.y + r.h - radius;
        uint32_t *row = c->pixels + y * c->stride;

        for (int x = area.x; x < area.x + area.w; x++)
        {
            if (row_inside && x >= r.x && x < r.x + r.w)
            {
                x = r.x + r.w - 1;
                continue;
            }

            float d = rr_dist(x + 0.5f, y + 0.5f, sr, (float)radius + 4.0f);
            float t = (d + 6.0f) / (SHADOW_SIZE + 6.0f);
            if (t >= 1.0f)
                continue;
            if (t < 0.0f)
                t = 0.0f;
            float k = (1.0f - t);
            k = k * k * k;
            uint32_t a = (uint32_t)(strength * k);
            if (a)
                row[x] = sx_blend(row[x], 0x000000, a);
        }
    }
}

static void draw_titlebar_button(SxCanvas *c, Window *w, HitZone zone, bool active)
{
    SxRect b = titlebar_button_rect(w, zone);
    if (sx_rect_empty(b))
        return;

    bool hover = S.hover_frame_win == w && S.hover_zone == zone;
    bool pressed = hover && S.drag == DRAG_TITLE_BUTTON && S.drag_win == w && S.drag_zone == zone;
    float level = pressed ? 1.0f : fade_level(FADE_TITLE, ((int64_t)w->id << 4) | zone);

    float cx = b.x + b.w * 0.5f;
    float cy = b.y + b.h * 0.5f;
    float radius = b.w * 0.5f;

    uint32_t icon = active ? sx_theme->text : sx_theme->text_dim;
    sx_fill_circle(c, cx, cy, radius, SX_RGBA(255, 255, 255, active ? 16 : 8));
    if (level > 0.0f)
    {
        uint32_t a = (uint32_t)(level * 255.0f);
        if (zone == HIT_CLOSE)
        {
            uint32_t red = pressed ? sx_darken(COLOR_CLOSE_HOVER, 40) : COLOR_CLOSE_HOVER;
            sx_fill_circle(c, cx, cy, radius, (red & 0xFFFFFFu) | (a << 24));
            icon = sx_mix(icon, SX_RGB(255, 255, 255), (int)a);
        }
        else
        {
            sx_fill_circle(c, cx, cy, radius, SX_RGBA(255, 255, 255, (uint32_t)((pressed ? 14 : 26) * level)));
            icon = sx_mix(icon, sx_theme->text, (int)a);
        }
    }

    switch (zone)
    {
        case HIT_CLOSE:
            sx_draw_line(c, cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, 1.5f, icon);
            sx_draw_line(c, cx + 3.5f, cy - 3.5f, cx - 3.5f, cy + 3.5f, 1.5f, icon);
            break;
        case HIT_MINIMIZE:
            sx_draw_line(c, cx - 4.0f, cy + 0.5f, cx + 4.0f, cy + 0.5f, 1.5f, icon);
            break;
        case HIT_MAXIMIZE:
            if (w->maximized)
            {
                sx_draw_line(c, cx - 4.0f, cy - 1.0f, cx - 1.0f, cy - 1.0f, 1.5f, icon);
                sx_draw_line(c, cx - 1.0f, cy - 1.0f, cx - 1.0f, cy - 4.0f, 1.5f, icon);
                sx_draw_line(c, cx + 4.0f, cy + 1.0f, cx + 1.0f, cy + 1.0f, 1.5f, icon);
                sx_draw_line(c, cx + 1.0f, cy + 1.0f, cx + 1.0f, cy + 4.0f, 1.5f, icon);
            }
            else
            {
                sx_draw_line(c, cx - 4.0f, cy + 2.5f, cx, cy - 2.0f, 1.5f, icon);
                sx_draw_line(c, cx, cy - 2.0f, cx + 4.0f, cy + 2.5f, 1.5f, icon);
            }
            break;
        default:
            break;
    }
}

static void draw_window_content(SxCanvas *c, Window *w, SxRect f, bool framed)
{
    SxRect cr = sx_rect_intersect(window_client_rect(w), c->clip);
    if (sx_rect_empty(cr))
        return;

    int corner_top = w->y + w->h - FRAME_RADIUS;

    for (int y = cr.y; y < cr.y + cr.h; y++)
    {
        uint32_t *dst = c->pixels + y * c->stride;
        int sy = y - w->y;
        int x0 = cr.x, x1 = cr.x + cr.w;

        int bx0 = x0, bx1 = x1;
        if (framed && y >= corner_top)
        {
            while (bx0 < bx1 && rr_dist(bx0 + 0.5f, y + 0.5f, f, FRAME_RADIUS) > -0.5f)
                bx0++;
            while (bx1 > bx0 && rr_dist(bx1 - 0.5f, y + 0.5f, f, FRAME_RADIUS) > -0.5f)
                bx1--;
        }

        if (w->buf && sy < w->buf_h)
        {
            const uint32_t *src = w->buf + sy * w->buf_stride;
            int copy_end = bx1 < w->x + w->buf_w ? bx1 : w->x + w->buf_w;
            if (copy_end > bx0)
                memcpy(dst + bx0, src + (bx0 - w->x), (size_t)(copy_end - bx0) * 4);
            for (int x = copy_end > bx0 ? copy_end : bx0; x < bx1; x++)
                dst[x] = COLOR_CONTENT_FILL;
        }
        else
        {
            for (int x = bx0; x < bx1; x++)
                dst[x] = COLOR_CONTENT_FILL;
        }
    }
}

// Draws the frame and content without the drop shadow.
static void draw_window_body(SxCanvas *c, Window *w)
{
    bool active = S.focused == w;
    bool framed = window_has_frame(w);
    SxRect f = window_frame_rect(w);

    if (!framed)
    {
        draw_window_content(c, w, f, false);
        return;
    }

    uint32_t title_col = active ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;

    SxRect tb = titlebar_rect(w);
    if (!sx_rect_empty(sx_rect_intersect(tb, c->clip)))
    {
        sx_fill_rounded_rect(c, f.x, f.y, f.w, TITLE_H + FRAME_RADIUS * 2, FRAME_RADIUS, title_col);
        sx_fill_rect(c, f.x + FRAME_RADIUS, f.y + 1, f.w - 2 * FRAME_RADIUS, 1, SX_RGBA(255, 255, 255, active ? 18 : 8));
    }
    SxRect bottom = sx_rect(f.x, f.y + f.h - FRAME_RADIUS * 2, f.w, FRAME_RADIUS * 2);
    if (!sx_rect_empty(sx_rect_intersect(bottom, c->clip)))
    {
        SxRect old_clip = c->clip;
        sx_canvas_set_clip(c, sx_rect_intersect(old_clip, sx_rect(f.x, f.y + f.h - FRAME_RADIUS, f.w, FRAME_RADIUS)));
        sx_fill_rounded_rect(c, f.x, f.y + f.h - FRAME_RADIUS * 3, f.w, FRAME_RADIUS * 3, FRAME_RADIUS, COLOR_CONTENT_FILL);
        c->clip = old_clip;
    }

    draw_window_content(c, w, f, true);

    if (!sx_rect_empty(sx_rect_intersect(tb, c->clip)))
    {
        SxRect leftmost = titlebar_button_rect(w, HIT_MINIMIZE);
        int right_space = f.x + f.w - leftmost.x;
        int max_w = f.w - 2 * right_space - 8;

        const SxFont *font = &sx_font_sans_bold;
        char title[SX_TITLE_MAX + 4];
        strncpy(title, w->title, SX_TITLE_MAX);
        title[SX_TITLE_MAX] = '\0';
        int len = (int)strlen(title);
        if (max_w > 0 && sx_text_width(font, title) > max_w)
        {
            while (len > 0 && sx_text_width_n(font, title, len) + sx_text_width(font, "...") > max_w)
                len--;
            memcpy(title + len, "...", 4);
        }

        if (max_w > 0)
            sx_draw_text_centered(c, font, sx_rect(f.x, f.y, f.w, TITLE_H), title,
                                  active ? COLOR_TITLE_TEXT : COLOR_TITLE_TEXT_DIM);

        draw_titlebar_button(c, w, HIT_MINIMIZE, active);
        draw_titlebar_button(c, w, HIT_MAXIMIZE, active);
        draw_titlebar_button(c, w, HIT_CLOSE, active);

        sx_fill_rect(c, w->x, w->y - 1, w->w, 1, SX_RGBA(0, 0, 0, 90));
    }

    sx_draw_rounded_rect(c, f.x, f.y, f.w, f.h, FRAME_RADIUS, COLOR_FRAME_BORDER);
}

static void draw_window(SxCanvas *c, Window *w)
{
    draw_shadow(c, window_frame_rect(w), window_has_frame(w) ? FRAME_RADIUS : 0, S.focused == w ? 120 : 80);
    draw_window_body(c, w);
}

#define CURSOR_MAX 24

typedef struct
{
    const char *art[CURSOR_MAX];
    int hot_x, hot_y;
}
CursorArt;

static const CursorArt cursor_arts[] = {
    [CURSOR_ARROW] = { {
        "X",
        "XX",
        "X.X",
        "X..X",
        "X...X",
        "X....X",
        "X.....X",
        "X......X",
        "X.......X",
        "X........X",
        "X.........X",
        "X..........X",
        "X......XXXXXX",
        "X...X..X",
        "X..XX..X",
        "X.X  X..X",
        "XX   X..X",
        "X     X..X",
        "      X..X",
        "       XX",
        NULL }, 0, 0 },
    [CURSOR_RESIZE_H] = { {
        "    X        X",
        "   XX        XX",
        "  X.X        X.X",
        " X..XXXXXXXXXX..X",
        "X................X",
        " X..XXXXXXXXXX..X",
        "  X.X        X.X",
        "   XX        XX",
        "    X        X",
        NULL }, 9, 4 },
    [CURSOR_RESIZE_V] = { {
        "    X",
        "   X.X",
        "  X...X",
        " X.....X",
        "XXXX.XXXX",
        "   X.X",
        "   X.X",
        "   X.X",
        "   X.X",
        "   X.X",
        "   X.X",
        "XXXX.XXXX",
        " X.....X",
        "  X...X",
        "   X.X",
        "    X",
        NULL }, 4, 8 },
    [CURSOR_RESIZE_D] = { {
        "XXXXXX",
        "X....X",
        "X...X",
        "X....X",
        "X..X..X",
        "XX  X..X",
        "     X..X",
        "      X..X  XX",
        "       X..X..X",
        "        X....X",
        "         X...X",
        "        X....X",
        "        XXXXXX",
        NULL }, 7, 6 },
    [CURSOR_RESIZE_D2] = { {
        "        XXXXXX",
        "        X....X",
        "         X...X",
        "        X....X",
        "       X..X..X",
        "      X..X  XX",
        "     X..X",
        "XX  X..X",
        "X..X..X",
        "X....X",
        "X...X",
        "X....X",
        "XXXXXX",
        NULL }, 6, 6 },
};

typedef struct
{
    int w, h, hot_x, hot_y;
    uint32_t px[CURSOR_MAX * CURSOR_MAX];
}
CursorImage;

static CursorImage cursor_images[CURSOR_COUNT];

static void cursor_build(CursorKind kind)
{
    const CursorArt *art = &cursor_arts[kind];
    CursorImage *img = &cursor_images[kind];
    memset(img, 0, sizeof(*img));

    int w = 0, h = 0;
    while (art->art[h])
    {
        int len = (int)strlen(art->art[h]);
        if (len > w)
            w = len;
        h++;
    }

    img->w = w + 2;
    img->h = h + 2;
    img->hot_x = art->hot_x;
    img->hot_y = art->hot_y;

    for (int pass = 0; pass < 2; pass++)
    {
        for (int y = 0; y < h; y++)
        {
            const char *row = art->art[y];
            for (int x = 0; row[x]; x++)
            {
                if (row[x] == ' ')
                    continue;
                if (pass == 0)
                {
                    uint32_t *p = &img->px[(y + 1) * CURSOR_MAX + (x + 1)];
                    if (SX_ALPHA(*p) < 70)
                        *p = SX_RGBA(0, 0, 0, 70);
                }
                else
                {
                    img->px[y * CURSOR_MAX + x] = row[x] == 'X' ? SX_RGB(20, 20, 24) : SX_RGB(255, 255, 255);
                }
            }
        }
    }
}

static SxRect cursor_rect_at(int x, int y, CursorKind kind)
{
    CursorImage *img = &cursor_images[kind];
    return sx_rect(x - img->hot_x, y - img->hot_y, img->w, img->h);
}

static void draw_cursor(SxCanvas *c)
{
    CursorImage *img = &cursor_images[S.cursor];
    SxRect r = sx_rect_intersect(S.cursor_rect, c->clip);

    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint32_t *dst = c->pixels + y * c->stride;
        const uint32_t *src = &img->px[(y - S.cursor_rect.y) * CURSOR_MAX];
        for (int x = r.x; x < r.x + r.w; x++)
        {
            uint32_t s = src[x - S.cursor_rect.x];
            if (SX_ALPHA(s))
                dst[x] = sx_blend(dst[x], s, SX_ALPHA(s));
        }
    }
}

static void cursor_set(CursorKind kind)
{
    if (S.cursor == kind)
        return;
    damage_add(S.cursor_rect);
    S.cursor = kind;
    S.cursor_rect = cursor_rect_at(S.mx, S.my, kind);
    damage_add(S.cursor_rect);
}

static void cursor_moved(void)
{
    SxRect r = cursor_rect_at(S.mx, S.my, S.cursor);
    if (r.x == S.cursor_rect.x && r.y == S.cursor_rect.y)
        return;
    damage_add(S.cursor_rect);
    S.cursor_rect = r;
    damage_add(r);
}

static void shell_draw_desktop(SxCanvas *c);
static void shell_draw_bar(SxCanvas *c);
static void shell_draw_dock(SxCanvas *c);
static void shell_draw_menu(SxCanvas *c);

static void flush_rect(SxRect r)
{
    if (S.fb_is_xrgb)
    {
        for (int y = r.y; y < r.y + r.h; y++)
            memcpy(S.fb_mem + (size_t)y * S.fb.pitch + (size_t)r.x * 4,
                   S.back + (size_t)y * S.width + r.x, (size_t)r.w * 4);
        return;
    }

    int bytes = S.fb.bpp / 8;
    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint8_t *dst = S.fb_mem + (size_t)y * S.fb.pitch + (size_t)r.x * bytes;
        const uint32_t *src = S.back + (size_t)y * S.width + r.x;
        for (int x = 0; x < r.w; x++)
        {
            uint32_t p = src[x];
            uint32_t rr = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, bb = p & 0xFF;
            uint32_t v = ((rr >> (8 - S.fb.red_mask_size)) << S.fb.red_mask_shift) |
                         ((gg >> (8 - S.fb.green_mask_size)) << S.fb.green_mask_shift) |
                         ((bb >> (8 - S.fb.blue_mask_size)) << S.fb.blue_mask_shift);
            for (int i = 0; i < bytes; i++)
                dst[x * bytes + i] = (uint8_t)(v >> (8 * i));
        }
    }
}

static void compose_rect(SxRect r)
{
    SxCanvas *c = &S.canvas;
    sx_canvas_set_clip(c, r);

    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(S.back + (size_t)y * S.width + r.x, S.wallpaper + (size_t)y * S.width + r.x, (size_t)r.w * 4);

    shell_draw_desktop(c);

    // While a snap preview shows, the dragged window goes on top of it so it stays readable.
    Window *lifted = S.snap_active && S.drag == DRAG_MOVE ? S.drag_win : NULL;
    for (int layer = 0; layer < 2; layer++)
    {
        if (layer == 1 && S.snap_active && !sx_rect_empty(sx_rect_intersect(S.snap_rect, r)))
        {
            SxRect p = S.snap_rect;
            sx_fill_rounded_rect(c, p.x, p.y, p.w, p.h, FRAME_RADIUS, SX_THEME_ACCENT_A(46));
            sx_draw_rounded_rect(c, p.x, p.y, p.w, p.h, FRAME_RADIUS, SX_THEME_ACCENT_A(170));
        }

        for (int i = 0; i < S.zcount; i++)
        {
            Window *w = S.zorder[i];
            if (!window_visible(w))
                continue;
            if ((w == lifted) != (layer == 1) || anim_draw_window(c, w))
                continue;
            if (sx_rect_empty(sx_rect_intersect(window_outer_rect(w), r)))
                continue;
            draw_window(c, w);
        }
    }

    anim_draw_ghosts(c);

    if (!sx_rect_empty(sx_rect_intersect(bar_rect(), r)))
        shell_draw_bar(c);

    if (S.dock_level > 0.0f && !sx_rect_empty(sx_rect_intersect(dock_outer_rect(), r)))
        shell_draw_dock(c);

    if (S.menu_level > 0.0f)
        draw_faded(c, menu_outer_rect(), shell_draw_menu, S.menu_level);

    if (!sx_rect_empty(sx_rect_intersect(S.cursor_rect, r)))
        draw_cursor(c);

    flush_rect(r);
}

static void compose(void)
{
    int count = S.damage_count;
    SxRect rects[MAX_DAMAGE];
    memcpy(rects, S.damage, sizeof(SxRect) * (size_t)count);
    S.damage_count = 0;

    for (int i = 0; i < count; i++)
        compose_rect(rects[i]);
}
