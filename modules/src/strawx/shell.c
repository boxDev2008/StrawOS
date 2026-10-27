#define MAX_APPS 48

typedef struct
{
    SxDesktopEntry entry;
    SxImage       *icon_desk;
    SxImage       *icon_menu;
}
ShellApp;

static ShellApp shell_apps[MAX_APPS];
static int      shell_app_count;
static uint32_t shell_apps_signature;
static uint64_t shell_apps_checked_ms;

#define PANEL_BG          ((sx_theme->panel & 0xFFFFFFu) | (214u << 24))
#define PANEL_TEXT        (sx_theme->text)
#define PANEL_TEXT_DIM    (sx_theme->text_dim)
#define ACCENT            (sx_theme->accent)
#define MENU_BG           (sx_theme->bg)

#define MENU_W            300
#define MENU_HEADER_H     64
#define MENU_ITEM_H       54
#define MENU_PAD          8

#define DESK_CELL_W       96
#define DESK_CELL_H       96
#define DESK_ICON         52
#define MENU_ICON         36

#define PANEL_START_W     40
#define PANEL_BTN_MAX_W   200
#define PANEL_CLOCK_W     110

static const char *weekday_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *month_names[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static int shell_name_compare(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint32_t shell_scan_signature(void)
{
    uint32_t h = 2166136261u;
    int fd = open(SX_DESKTOP_DIR, O_RDONLY);
    if (fd < 0)
        return 0;

    struct dirent de;
    while (readdir(fd, &de) == 1)
    {
        if (!sx_is_desktop_file(de.name))
            continue;
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", SX_DESKTOP_DIR, de.name);
        struct stat st;
        uint64_t size = stat(path, &st) == 0 ? st.size : 0;
        for (const char *p = de.name; *p; p++)
            h = (h ^ (uint8_t)*p) * 16777619u;
        h = (h ^ (uint32_t)size) * 16777619u;
    }
    close(fd);
    return h;
}

static void shell_free_apps(void)
{
    for (int i = 0; i < shell_app_count; i++)
    {
        sx_image_free(shell_apps[i].icon_desk);
        sx_image_free(shell_apps[i].icon_menu);
    }
    shell_app_count = 0;
}

static void shell_load_apps(void)
{
    shell_free_apps();
    shell_apps_signature = shell_scan_signature();

    int fd = open(SX_DESKTOP_DIR, O_RDONLY);
    if (fd < 0)
        return;

    struct dirent de;
    while (shell_app_count < MAX_APPS && readdir(fd, &de) == 1)
    {
        if (!sx_is_desktop_file(de.name))
            continue;
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", SX_DESKTOP_DIR, de.name);
        ShellApp *app = &shell_apps[shell_app_count];
        memset(app, 0, sizeof(*app));
        if (sx_desktop_entry_load(path, &app->entry))
            shell_app_count++;
    }
    close(fd);

    for (int i = 1; i < shell_app_count; i++)
    {
        ShellApp key = shell_apps[i];
        int j = i - 1;
        while (j >= 0 && shell_name_compare(shell_apps[j].entry.name, key.entry.name) > 0)
        {
            shell_apps[j + 1] = shell_apps[j];
            j--;
        }
        shell_apps[j + 1] = key;
    }

    for (int i = 0; i < shell_app_count; i++)
    {
        ShellApp *app = &shell_apps[i];
        SxImage *img = app->entry.icon[0] ? sx_image_load(app->entry.icon) : NULL;
        if (!img)
            img = sx_image_load("/share/icons/application.png");
        if (!img)
            continue;
        app->icon_desk = sx_image_scale(img, DESK_ICON, DESK_ICON);
        app->icon_menu = sx_image_scale(img, MENU_ICON, MENU_ICON);
        sx_image_free(img);
    }

    S.desktop_selected = -1;
    S.desktop_hover = -1;
    S.menu_hover = -1;
}

static void draw_app_icon(SxCanvas *c, const ShellApp *app, const SxImage *icon, int x, int y, int size)
{
    if (icon)
    {
        sx_draw_image(c, icon, x, y);
        return;
    }

    sx_fill_rounded_rect(c, x, y, size, size, size / 4, SX_RGB(0x6A, 0x72, 0x84));
    char letter[2] = { app->entry.name[0], '\0' };
    const SxFont *font = size > 40 ? &sx_font_sans_large : &sx_font_sans_bold;
    sx_draw_text_centered(c, font, sx_rect(x, y, size, size), letter, SX_RGB(255, 255, 255));
}

static void draw_logo(SxCanvas *c, int x, int y, int size)
{
    // scaled copies of the logo, one per size the shell draws it at
    static SxImage *cache[2];
    SxImage **slot = &cache[size > 24];
    if (!*slot || (*slot)->width != size)
    {
        SxImage *img = sx_image_load("/share/icons/strawos.png");
        if (!img)
            return;
        sx_image_free(*slot);
        *slot = sx_image_scale(img, size, size);
        sx_image_free(img);
        if (!*slot)
            return;
    }
    sx_draw_image(c, *slot, x, y);
}

static SxRect desktop_cell(int i)
{
    int rows = (S.height - PANEL_H - 14) / (DESK_CELL_H + 6);
    if (rows < 1)
        rows = 1;
    return sx_rect(14 + (i / rows) * (DESK_CELL_W + 6), 14 + (i % rows) * (DESK_CELL_H + 6), DESK_CELL_W, DESK_CELL_H);
}

static int desktop_hit(int x, int y)
{
    if (!S.desktop_icons)
        return -1;
    for (int i = 0; i < shell_app_count; i++)
        if (sx_rect_contains(desktop_cell(i), x, y))
            return i;
    return -1;
}

static void shell_draw_desktop(SxCanvas *c)
{
    if (!S.desktop_icons)
        return;
    for (int i = 0; i < shell_app_count; i++)
    {
        SxRect cell = desktop_cell(i);
        if (sx_rect_empty(sx_rect_intersect(cell, c->clip)))
            continue;

        float hover = fade_level(FADE_DESK, i);
        if (S.desktop_selected == i)
            sx_fill_rounded_rect(c, cell.x, cell.y, cell.w, cell.h, 10, SX_RGBA(255, 255, 255, 40));
        else if (hover > 0.0f)
            sx_fill_rounded_rect(c, cell.x, cell.y, cell.w, cell.h, 10, SX_RGBA(255, 255, 255, (uint32_t)(18 * hover)));

        int ix = cell.x + (cell.w - DESK_ICON) / 2;
        int iy = cell.y + 10;
        draw_app_icon(c, &shell_apps[i], shell_apps[i].icon_desk, ix, iy, DESK_ICON);

        const SxFont *font = &sx_font_sans_small;
        const char *label = shell_apps[i].entry.name;
        int tw = sx_text_width(font, label);
        int tx = cell.x + (cell.w - tw) / 2;
        int ty = iy + DESK_ICON + 6;
        sx_draw_text(c, font, tx + 1, ty + 1, label, SX_RGBA(0, 0, 0, 160));
        sx_draw_text(c, font, tx, ty, label, SX_RGB(255, 255, 255));
    }
}

static SxRect panel_rect(void)
{
    return sx_rect(0, S.height - PANEL_H, S.width, PANEL_H);
}

static SxRect panel_start_rect(void)
{
    SxRect p = panel_rect();
    return sx_rect(p.x + 6, p.y + 6, PANEL_START_W, PANEL_H - 12);
}

static SxRect panel_clock_rect(void)
{
    SxRect p = panel_rect();
    return sx_rect(p.x + p.w - PANEL_CLOCK_W - 6, p.y, PANEL_CLOCK_W, PANEL_H);
}

static int panel_windows(Window **out)
{
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
    {
        Window *w = &S.windows[i];
        if (w->used && w->mapped && !w->hidden && (w->flags & SX_WINDOW_NO_FRAME) == 0)
            out[n++] = w;
    }

    for (int i = 1; i < n; i++)
    {
        Window *k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j]->created_order > k->created_order)
        {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = k;
    }
    return n;
}

static SxRect panel_button_rect(int index, int count)
{
    SxRect p = panel_rect();
    int start = panel_start_rect().x + PANEL_START_W + 10;
    int end = panel_clock_rect().x - 10;
    int avail = end - start;
    int bw = PANEL_BTN_MAX_W;
    if (count > 0 && count * (bw + 4) > avail)
        bw = avail / count - 4;
    return sx_rect(start + index * (bw + 4), p.y + 6, bw, PANEL_H - 12);
}

static void shell_draw_panel(SxCanvas *c)
{
    SxRect p = panel_rect();
    sx_fill_rect(c, p.x, p.y, p.w, p.h, PANEL_BG);
    sx_fill_rect(c, p.x, p.y, p.w, 1, SX_RGBA(255, 255, 255, 22));

    SxRect sb = panel_start_rect();
    uint32_t start_alpha = S.menu_open ? 34 : (uint32_t)(20 * fade_level(FADE_PANEL, -2));
    if (start_alpha)
        sx_fill_rounded_rect(c, sb.x, sb.y, sb.w, sb.h, 8, SX_RGBA(255, 255, 255, start_alpha));
    draw_logo(c, sb.x + (sb.w - 24) / 2, sb.y + (sb.h - 24) / 2, 24);

    Window *list[MAX_WINDOWS];
    int n = panel_windows(list);
    for (int i = 0; i < n; i++)
    {
        Window *w = list[i];
        SxRect b = panel_button_rect(i, n);
        if (sx_rect_empty(sx_rect_intersect(b, c->clip)))
            continue;

        bool active = S.focused == w && !w->minimized;
        float hover = fade_level(FADE_PANEL, i);
        if (active)
            sx_fill_rounded_rect(c, b.x, b.y, b.w, b.h, 8, SX_RGBA(255, 255, 255, 30));
        else if (hover > 0.0f)
            sx_fill_rounded_rect(c, b.x, b.y, b.w, b.h, 8, SX_RGBA(255, 255, 255, (uint32_t)(16 * hover)));

        uint32_t dot = w->minimized ? sx_theme->border : ACCENT;
        sx_fill_circle(c, b.x + 14.0f, b.y + b.h * 0.5f, 3.5f, dot);

        const SxFont *font = &sx_font_sans;
        int max_w = b.w - 34;
        int len = (int)strlen(w->title);
        char title[SX_TITLE_MAX + 4];
        memcpy(title, w->title, (size_t)len + 1);
        if (sx_text_width(font, title) > max_w)
        {
            while (len > 0 && sx_text_width_n(font, title, len) + sx_text_width(font, "...") > max_w)
                len--;
            memcpy(title + len, "...", 4);
        }

        SxRect old = c->clip;
        sx_canvas_set_clip(c, sx_rect_intersect(old, b));
        sx_draw_text(c, font, b.x + 26, sx_text_y_centered(font, b), title,
                     w->minimized ? PANEL_TEXT_DIM : PANEL_TEXT);
        c->clip = old;

        if (active)
            sx_fill_rounded_rect(c, b.x + b.w / 2 - 10, b.y + b.h - 3, 20, 3, 1, ACCENT);
    }

    SxRect cr = panel_clock_rect();
    if (!sx_rect_empty(sx_rect_intersect(cr, c->clip)))
    {
        struct rtc_time t;
        rtc_time(&t);

        char time_buf[16], date_buf[32];
        if (S.clock_24h)
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", t.hour, t.minute);
        else
            snprintf(time_buf, sizeof(time_buf), "%d:%02d %s", t.hour % 12 ? t.hour % 12 : 12, t.minute,
                     t.hour < 12 ? "AM" : "PM");
        snprintf(date_buf, sizeof(date_buf), "%s %d %s", weekday_names[t.weekday % 7], t.day,
                 month_names[(t.month + 11) % 12]);

        int tw = sx_text_width(&sx_font_sans_bold, time_buf);
        int dw = sx_text_width(&sx_font_sans_small, date_buf);
        int right = cr.x + cr.w - 8;
        if (S.show_date)
        {
            sx_draw_text(c, &sx_font_sans_bold, right - tw, cr.y + 4, time_buf, PANEL_TEXT);
            sx_draw_text(c, &sx_font_sans_small, right - dw, cr.y + 23, date_buf, PANEL_TEXT_DIM);
        }
        else
        {
            sx_draw_text(c, &sx_font_sans_bold, right - tw, sx_text_y_centered(&sx_font_sans_bold, cr), time_buf,
                         PANEL_TEXT);
        }
    }
}

static int panel_hit(int x, int y)
{
    if (sx_rect_contains(panel_start_rect(), x, y))
        return -2;

    Window *list[MAX_WINDOWS];
    int n = panel_windows(list);
    for (int i = 0; i < n; i++)
        if (sx_rect_contains(panel_button_rect(i, n), x, y))
            return i;
    return -1;
}

static void shell_panel_click(int index)
{
    if (index == -2)
    {
        S.menu_open = !S.menu_open;
        S.menu_hover = -1;
        damage_add(menu_rect());
        damage_add(panel_start_rect());
        return;
    }

    Window *list[MAX_WINDOWS];
    int n = panel_windows(list);
    if (index < 0 || index >= n)
        return;

    Window *w = list[index];
    if (w->minimized)
    {
        // Raise and focus first so the restore animation shows the window as active.
        window_raise(w);
        window_focus(w);
        window_set_minimized(w, false);
    }
    else if (S.focused == w && S.zorder[S.zcount - 1] == w)
    {
        window_set_minimized(w, true);
    }
    else
    {
        window_raise(w);
        window_focus(w);
    }
}

static SxRect menu_rect(void)
{
    int h = MENU_HEADER_H + shell_app_count * MENU_ITEM_H + MENU_PAD * 2;
    return sx_rect(8, S.height - PANEL_H - h - 8 + menu_slide_offset(), MENU_W, h);
}

static SxRect menu_item_rect(int i)
{
    SxRect m = menu_rect();
    return sx_rect(m.x + MENU_PAD, m.y + MENU_HEADER_H + MENU_PAD + i * MENU_ITEM_H, m.w - MENU_PAD * 2, MENU_ITEM_H);
}

static SxRect menu_outer_rect(void)
{
    SxRect m = menu_rect();
    return sx_rect(m.x - SHADOW_SIZE, m.y - SHADOW_SIZE, m.w + SHADOW_SIZE * 2, m.h + SHADOW_SIZE * 2 + SHADOW_OFF_Y);
}

static int menu_hit(int x, int y)
{
    for (int i = 0; i < shell_app_count; i++)
        if (sx_rect_contains(menu_item_rect(i), x, y))
            return i;
    return -1;
}

static void shell_draw_menu(SxCanvas *c)
{
    SxRect m = menu_rect();
    if (sx_rect_empty(sx_rect_intersect(menu_outer_rect(), c->clip)))
        return;

    draw_shadow(c, m, 14, 130);
    sx_fill_rounded_rect(c, m.x, m.y, m.w, m.h, 14, MENU_BG);
    sx_draw_rounded_rect(c, m.x, m.y, m.w, m.h, 14, SX_RGBA(255, 255, 255, 26));

    draw_logo(c, m.x + 18, m.y + 16, 32);
    sx_draw_text(c, &sx_font_sans_bold, m.x + 62, m.y + 14, "StrawOS", PANEL_TEXT);
    sx_draw_text(c, &sx_font_sans_small, m.x + 62, m.y + 32, "Applications", PANEL_TEXT_DIM);
    sx_fill_rect(c, m.x + 16, m.y + MENU_HEADER_H - 1, m.w - 32, 1, SX_RGBA(255, 255, 255, 18));

    for (int i = 0; i < shell_app_count; i++)
    {
        SxRect r = menu_item_rect(i);
        float hover = fade_level(FADE_MENU, i);
        if (hover > 0.0f)
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 10, SX_RGBA(255, 255, 255, (uint32_t)(20 * hover)));

        draw_app_icon(c, &shell_apps[i], shell_apps[i].icon_menu, r.x + 10, r.y + (r.h - MENU_ICON) / 2, MENU_ICON);
        sx_draw_text(c, &sx_font_sans_bold, r.x + 58, r.y + 8, shell_apps[i].entry.name, PANEL_TEXT);
        sx_draw_text(c, &sx_font_sans_small, r.x + 58, r.y + 28, shell_apps[i].entry.comment, PANEL_TEXT_DIM);
    }
}

static void shell_close_menu(void)
{
    if (!S.menu_open)
        return;
    S.menu_open = false;
    damage_add(menu_outer_rect());
    damage_add(panel_start_rect());
}

static void shell_launch(int app_index)
{
    if (app_index < 0 || app_index >= shell_app_count)
        return;
    if (!sx_desktop_entry_launch(&shell_apps[app_index].entry))
        printf("strawx: failed to launch %s\n", shell_apps[app_index].entry.exec);
}

static void shell_tick(void)
{
    static uint64_t clock_checked_ms;
    uint64_t now = uptime_ms();

    if (S.last_minute < 0 || now - clock_checked_ms >= 500)
    {
        clock_checked_ms = now;
        struct rtc_time t;
        if (rtc_time(&t) == 0 && t.minute != S.last_minute)
        {
            S.last_minute = t.minute;
            damage_add(panel_clock_rect());
        }
    }

    if (now - shell_apps_checked_ms >= 2000)
    {
        shell_apps_checked_ms = now;
        if (shell_scan_signature() != shell_apps_signature)
        {
            shell_load_apps();
            damage_all();
        }
    }
}
