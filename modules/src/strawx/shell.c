#define MAX_APPS 48

typedef struct
{
    SxDesktopEntry entry;
    SxImage       *icon_desk;
    SxImage       *icon_menu;
    SxImage       *icon_dock;
    char           exe[TASKINFO_NAME_MAX];
    bool           has_args;
}
ShellApp;

static ShellApp shell_apps[MAX_APPS];
static int      shell_app_count;
static uint32_t shell_apps_signature;
static uint64_t shell_apps_checked_ms;

#define PANEL_BG          ((sx_theme->panel & 0xFFFFFFu) | (214u << 24))
#define DOCK_BG           ((sx_theme->panel & 0xFFFFFFu) | (200u << 24))
#define PANEL_TEXT        (sx_theme->text)
#define PANEL_TEXT_DIM    (sx_theme->text_dim)
#define ACCENT            (sx_theme->accent)
#define MENU_BG           (sx_theme->bg)

#define MENU_COLS         6
#define MENU_TILE_W       100
#define MENU_TILE_H       96
#define MENU_HEADER_H     64
#define MENU_PAD          14

#define DESK_CELL_W       96
#define DESK_CELL_H       96
#define DESK_ICON         52
#define MENU_ICON         48

#define DOCK_RADIUS       18
#define DOCK_DIVIDER      14
#define DOCK_TIP_H        24
#define DOCK_TIP_GAP      10
#define DOCK_LIFT         5

#define BAR_NONE          -1
#define BAR_LOGO          -2
#define BAR_WS            100

static const char *weekday_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *month_names[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static void workspace_switch(int ws);

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

static bool shell_name_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
    {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return true;
    }
    return n == 0;
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
        sx_image_free(shell_apps[i].icon_dock);
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
        size_t len = strcspn(app->entry.exec, " ");
        if (len >= sizeof(app->exe))
            len = sizeof(app->exe) - 1;
        memcpy(app->exe, app->entry.exec, len);
        app->exe[len] = '\0';
        app->has_args = app->entry.exec[len] != '\0';

        SxImage *img = app->entry.icon[0] ? sx_image_load(app->entry.icon) : NULL;
        if (!img)
            img = sx_image_load("/share/icons/application.png");
        if (!img)
            continue;
        app->icon_desk = sx_image_scale(img, DESK_ICON, DESK_ICON);
        app->icon_menu = sx_image_scale(img, MENU_ICON, MENU_ICON);
        app->icon_dock = sx_image_scale(img, DOCK_ICON, DOCK_ICON);
        sx_image_free(img);
    }

    S.desktop_selected = -1;
    S.desktop_hover = -1;
    S.menu_hover = -1;
    S.dock_hover = -1;
    S.menu_scroll = 0;
}

// The app a window belongs to, matched by the binary its process was started from.
static int app_for_window(Window *w)
{
    if (!w->client || !w->client->exe[0])
        return -1;
    int best = -1;
    for (int i = 0; i < shell_app_count; i++)
    {
        if (strcmp(shell_apps[i].exe, w->client->exe) != 0)
            continue;
        if (!shell_apps[i].has_args)
            return i;
        if (best < 0)
            best = i;
    }
    return best;
}

static bool window_listed(Window *w)
{
    return w->used && w->mapped && !w->hidden && window_has_frame(w);
}

static int app_windows(int app, Window **out)
{
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
    {
        Window *w = &S.windows[i];
        if (window_listed(w) && app_for_window(w) == app)
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

static void draw_app_icon(SxCanvas *c, const char *name, const SxImage *icon, int x, int y, int size)
{
    if (icon)
    {
        sx_draw_image(c, icon, x, y);
        return;
    }

    sx_fill_rounded_rect(c, x, y, size, size, size / 4, SX_RGB(0x6A, 0x72, 0x84));
    char letter[2] = { (char)toupper((unsigned char)name[0]), '\0' };
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

static void draw_text_fit(SxCanvas *c, const SxFont *font, SxRect box, int y, const char *text, uint32_t color)
{
    char buf[128];
    int len = (int)strlen(text);
    if (len > (int)sizeof(buf) - 4)
        len = (int)sizeof(buf) - 4;
    memcpy(buf, text, (size_t)len);
    buf[len] = '\0';
    if (sx_text_width(font, buf) > box.w)
    {
        while (len > 0 && sx_text_width_n(font, buf, len) + sx_text_width(font, "...") > box.w)
            len--;
        memcpy(buf + len, "...", 4);
    }
    sx_draw_text(c, font, box.x + (box.w - sx_text_width(font, buf)) / 2, y, buf, color);
}

static SxRect desktop_cell(int i)
{
    int rows = (S.height - BAR_H - DOCK_H - DOCK_MARGIN - 14) / (DESK_CELL_H + 6);
    if (rows < 1)
        rows = 1;
    return sx_rect(14 + (i / rows) * (DESK_CELL_W + 6), BAR_H + 14 + (i % rows) * (DESK_CELL_H + 6),
                   DESK_CELL_W, DESK_CELL_H);
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
            sx_fill_rounded_rect(c, cell.x, cell.y, cell.w, cell.h, 12, SX_RGBA(255, 255, 255, 40));
        else if (hover > 0.0f)
            sx_fill_rounded_rect(c, cell.x, cell.y, cell.w, cell.h, 12, SX_RGBA(255, 255, 255, (uint32_t)(18 * hover)));

        int ix = cell.x + (cell.w - DESK_ICON) / 2;
        int iy = cell.y + 10;
        draw_app_icon(c, shell_apps[i].entry.name, shell_apps[i].icon_desk, ix, iy, DESK_ICON);

        const SxFont *font = &sx_font_sans_small;
        const char *label = shell_apps[i].entry.name;
        int tw = sx_text_width(font, label);
        int tx = cell.x + (cell.w - tw) / 2;
        int ty = iy + DESK_ICON + 6;
        sx_draw_text(c, font, tx + 1, ty + 1, label, SX_RGBA(0, 0, 0, 160));
        sx_draw_text(c, font, tx, ty, label, SX_RGB(255, 255, 255));
    }
}

static SxRect bar_rect(void)
{
    return sx_rect(0, 0, S.width, BAR_H);
}

static SxRect bar_logo_rect(void)
{
    return sx_rect(8, 4, 34, BAR_H - 8);
}

static SxRect bar_ws_rect(int i)
{
    return sx_rect(50 + i * 30, 6, 26, BAR_H - 12);
}

static SxRect bar_clock_rect(void)
{
    return sx_rect(S.width - 170, 0, 160, BAR_H);
}

static SxRect bar_item_rect(int id)
{
    if (id == BAR_LOGO)
        return bar_logo_rect();
    if (id >= BAR_WS && id < BAR_WS + WORKSPACES)
        return bar_ws_rect(id - BAR_WS);
    return sx_rect(0, 0, 0, 0);
}

static int bar_hit(int x, int y)
{
    if (sx_rect_contains(bar_logo_rect(), x, y))
        return BAR_LOGO;
    for (int i = 0; i < WORKSPACES; i++)
        if (sx_rect_contains(bar_ws_rect(i), x, y))
            return BAR_WS + i;
    return BAR_NONE;
}

static bool workspace_occupied(int ws)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (window_listed(&S.windows[i]) && S.windows[i].workspace == ws)
            return true;
    return false;
}

static void draw_bar_highlight(SxCanvas *c, int id, bool on)
{
    SxRect r = bar_item_rect(id);
    float hover = fade_level(FADE_BAR, id);
    uint32_t a = on ? 34 : (uint32_t)(20 * hover);
    if (a)
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, r.h / 2, SX_RGBA(255, 255, 255, a));
}

static void shell_draw_bar(SxCanvas *c)
{
    SxRect b = bar_rect();
    sx_fill_rect(c, b.x, b.y, b.w, b.h, PANEL_BG);
    sx_fill_rect(c, b.x, b.y + b.h - 1, b.w, 1, SX_RGBA(255, 255, 255, 18));

    draw_bar_highlight(c, BAR_LOGO, S.menu_open);
    SxRect lr = bar_logo_rect();
    draw_logo(c, lr.x + (lr.w - 18) / 2, lr.y + (lr.h - 18) / 2, 18);

    for (int i = 0; i < WORKSPACES; i++)
    {
        SxRect r = bar_ws_rect(i);
        bool current = S.workspace == i;
        bool occupied = workspace_occupied(i);
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);

        if (current)
        {
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, r.h / 2, ACCENT);
            sx_draw_text_centered(c, &sx_font_sans_bold, r, label, sx_theme->accent_text);
            continue;
        }
        draw_bar_highlight(c, BAR_WS + i, false);
        sx_draw_text_centered(c, &sx_font_sans_bold, r, label, occupied ? PANEL_TEXT : SX_THEME_DISABLED);
        if (occupied)
            sx_fill_circle(c, r.x + r.w * 0.5f, r.y + r.h - 2.0f, 1.5f, ACCENT);
    }

    Window *f = S.focused;
    if (f && window_visible(f) && window_has_frame(f))
    {
        int left = bar_ws_rect(WORKSPACES - 1).x + 40;
        int right = bar_clock_rect().x - 12;
        int span = (right - left) < (S.width - 2 * left) ? right - left : S.width - 2 * left;
        SxRect box = sx_rect((S.width - span) / 2, 0, span, BAR_H);
        if (box.w > 40)
            draw_text_fit(c, &sx_font_sans_bold, box, sx_text_y_centered(&sx_font_sans_bold, box), f->title, PANEL_TEXT);
    }

    SxRect cr = bar_clock_rect();
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

        int right = cr.x + cr.w;
        int tw = sx_text_width(&sx_font_sans_bold, time_buf);
        sx_draw_text(c, &sx_font_sans_bold, right - tw, sx_text_y_centered(&sx_font_sans_bold, cr), time_buf, PANEL_TEXT);
        if (S.show_date)
        {
            int dw = sx_text_width(&sx_font_sans, date_buf);
            sx_draw_text(c, &sx_font_sans, right - tw - 10 - dw, sx_text_y_centered(&sx_font_sans, cr), date_buf,
                         PANEL_TEXT_DIM);
        }
    }
}

static void shell_close_menu(void);
static void shell_toggle_menu(void);

static void shell_bar_click(int id)
{
    if (id == BAR_LOGO)
        shell_toggle_menu();
    else if (id >= BAR_WS && id < BAR_WS + WORKSPACES)
        workspace_switch(id - BAR_WS);
}

typedef struct
{
    int     app;
    Window *win;
}
DockItem;

// Every installed app, then windows of programs that have no desktop entry.
static int dock_items(DockItem *out)
{
    int n = 0;
    for (int i = 0; i < shell_app_count; i++)
        out[n++] = (DockItem){ i, NULL };

    Window *extra[MAX_WINDOWS];
    int e = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (window_listed(&S.windows[i]) && app_for_window(&S.windows[i]) < 0)
            extra[e++] = &S.windows[i];
    for (int i = 1; i < e; i++)
    {
        Window *k = extra[i];
        int j = i - 1;
        while (j >= 0 && extra[j]->created_order > k->created_order)
        {
            extra[j + 1] = extra[j];
            j--;
        }
        extra[j + 1] = k;
    }
    for (int i = 0; i < e; i++)
        out[n++] = (DockItem){ -1, extra[i] };
    return n;
}

static SxRect dock_base_rect(void)
{
    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    int w = n * DOCK_ICON + (n > 0 ? n - 1 : 0) * DOCK_GAP + DOCK_PAD * 2;
    if (n > shell_app_count && shell_app_count > 0)
        w += DOCK_DIVIDER;
    if (w > S.width - 20)
        w = S.width - 20;
    return sx_rect((S.width - w) / 2, S.height - DOCK_MARGIN - DOCK_H, w, DOCK_H);
}

static SxRect dock_rect(void)
{
    SxRect r = dock_base_rect();
    float u = 1.0f - S.dock_level;
    r.y += (int)(u * u * (DOCK_H + DOCK_MARGIN + SHADOW_SIZE));
    return r;
}

static SxRect dock_outer_rect(void)
{
    SxRect r = dock_rect();
    int top = DOCK_TIP_H + DOCK_TIP_GAP + DOCK_LIFT;
    return sx_rect(r.x - SHADOW_SIZE - 60, r.y - top, r.w + SHADOW_SIZE * 2 + 120, r.h + top + SHADOW_SIZE + SHADOW_OFF_Y);
}

static SxRect dock_item_rect(int i)
{
    SxRect d = dock_rect();
    int x = d.x + DOCK_PAD + i * (DOCK_ICON + DOCK_GAP);
    if (i >= shell_app_count && shell_app_count > 0)
        x += DOCK_DIVIDER;
    return sx_rect(x, d.y + DOCK_PAD, DOCK_ICON, DOCK_ICON);
}

static const char *dock_item_label(const DockItem *it)
{
    return it->win ? it->win->title : shell_apps[it->app].entry.name;
}

static SxRect dock_tip_rect(int i, const char *label)
{
    SxRect r = dock_item_rect(i);
    int tw = sx_text_width(&sx_font_sans, label) + 20;
    return sx_rect(r.x + r.w / 2 - tw / 2, dock_rect().y - DOCK_TIP_GAP - DOCK_TIP_H, tw, DOCK_TIP_H);
}

static SxRect dock_fade_rect(int i)
{
    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    if (i < 0 || i >= n)
        return sx_rect(0, 0, 0, 0);
    SxRect r = dock_item_rect(i);
    r.y -= DOCK_LIFT;
    r.h += DOCK_LIFT;
    return sx_rect_union(r, dock_tip_rect(i, dock_item_label(&items[i])));
}

static int dock_hit(int x, int y)
{
    if (S.dock_level < 0.9f)
        return -1;
    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    SxRect d = dock_rect();
    for (int i = 0; i < n; i++)
    {
        SxRect r = dock_item_rect(i);
        r = sx_rect(r.x - DOCK_GAP / 2, d.y, r.w + DOCK_GAP, d.h);
        if (sx_rect_contains(r, x, y))
            return i;
    }
    return -1;
}

static bool dock_should_show(void)
{
    if (S.menu_open)
        return true;

    SxRect base = dock_base_rect();
    if (S.drag == DRAG_NONE)
    {
        SxRect keep = sx_rect(base.x - 20, base.y - 30, base.w + 40, S.height - base.y + 30);
        if (S.dock_level > 0.0f && sx_rect_contains(keep, S.mx, S.my))
            return true;
        if (S.my >= S.height - DOCK_REVEAL)
            return true;
    }

    for (int i = 0; i < S.zcount; i++)
    {
        Window *w = S.zorder[i];
        if (window_visible(w) && window_has_frame(w) &&
            !sx_rect_empty(sx_rect_intersect(window_frame_rect(w), base)))
            return false;
    }
    return true;
}

static void shell_draw_dock(SxCanvas *c)
{
    SxRect d = dock_rect();
    draw_shadow(c, d, DOCK_RADIUS, 100);
    sx_fill_rounded_rect(c, d.x, d.y, d.w, d.h, DOCK_RADIUS, DOCK_BG);
    sx_draw_rounded_rect(c, d.x, d.y, d.w, d.h, DOCK_RADIUS, SX_RGBA(255, 255, 255, 30));

    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    Window *focused_win = S.focused && window_listed(S.focused) ? S.focused : NULL;
    int focused_app = focused_win ? app_for_window(focused_win) : -1;

    if (n > shell_app_count && shell_app_count > 0)
    {
        SxRect last = dock_item_rect(shell_app_count - 1);
        int x = last.x + last.w + (DOCK_GAP + DOCK_DIVIDER) / 2;
        sx_fill_rect(c, x, d.y + 12, 1, d.h - 24, SX_RGBA(255, 255, 255, 40));
    }

    for (int i = 0; i < n; i++)
    {
        SxRect r = dock_item_rect(i);
        if (r.x + r.w > d.x + d.w)
            break;
        float hover = fade_level(FADE_DOCK, i);
        int lift = (int)(DOCK_LIFT * hover + 0.5f);
        const DockItem *it = &items[i];

        if (it->win)
            draw_app_icon(c, it->win->title, NULL, r.x, r.y - lift, DOCK_ICON);
        else
            draw_app_icon(c, shell_apps[it->app].entry.name, shell_apps[it->app].icon_dock, r.x, r.y - lift, DOCK_ICON);

        Window *wins[MAX_WINDOWS];
        int count = it->win ? 1 : app_windows(it->app, wins);
        bool hot = it->win ? it->win == focused_win : (focused_app >= 0 && it->app == focused_app);
        if (count > 3)
            count = 3;
        for (int k = 0; k < count; k++)
        {
            float cx = r.x + r.w * 0.5f + (k - (count - 1) * 0.5f) * 7.0f;
            sx_fill_circle(c, cx, d.y + d.h - 5.0f, 2.0f, hot ? ACCENT : PANEL_TEXT_DIM);
        }
    }

    for (int i = 0; i < n; i++)
    {
        float hover = fade_level(FADE_DOCK, i);
        if (hover <= 0.0f)
            continue;
        const char *label = dock_item_label(&items[i]);
        SxRect tip = dock_tip_rect(i, label);
        uint32_t a = (uint32_t)(hover * 235.0f);
        sx_fill_rounded_rect(c, tip.x, tip.y, tip.w, tip.h, tip.h / 2, (MENU_BG & 0xFFFFFFu) | (a << 24));
        sx_draw_text_centered(c, &sx_font_sans, tip, label, sx_mix(MENU_BG, PANEL_TEXT, (int)(hover * 255.0f)));
    }
}

static void shell_activate_window(Window *w)
{
    if (w->workspace != S.workspace)
        workspace_switch(w->workspace);
    window_raise(w);
    window_focus(w);
    if (w->minimized)
        window_set_minimized(w, false);
}

static void shell_dock_click(int index)
{
    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    if (index < 0 || index >= n)
        return;

    Window *wins[MAX_WINDOWS];
    int count;
    if (items[index].win)
    {
        wins[0] = items[index].win;
        count = 1;
    }
    else
    {
        count = app_windows(items[index].app, wins);
        if (count == 0)
        {
            shell_launch(items[index].app);
            return;
        }
    }

    int at = -1;
    for (int i = 0; i < count; i++)
        if (wins[i] == S.focused && window_visible(wins[i]))
            at = i;

    if (at >= 0 && count == 1)
    {
        window_set_minimized(wins[0], true);
        return;
    }

    Window *next = NULL;
    if (at >= 0)
        next = wins[(at + 1) % count];
    for (int i = 0; i < count && !next; i++)
        if (wins[i]->workspace == S.workspace)
            next = wins[i];
    shell_activate_window(next ? next : wins[0]);
}

// Where a window minimizes to: its icon in the dock.
static SxRect dock_target_for(Window *w)
{
    DockItem items[MAX_APPS + MAX_WINDOWS];
    int n = dock_items(items);
    int app = app_for_window(w);
    for (int i = 0; i < n; i++)
    {
        if (items[i].win == w || (!items[i].win && app >= 0 && items[i].app == app))
        {
            SxRect r = dock_item_rect(i);
            r.y += dock_base_rect().y - dock_rect().y;
            return r;
        }
    }
    return sx_rect(0, 0, 0, 0);
}

static int menu_filtered(int *out)
{
    int n = 0;
    for (int i = 0; i < shell_app_count; i++)
        if (shell_name_contains(shell_apps[i].entry.name, S.menu_query))
            out[n++] = i;
    return n;
}

static int menu_cols(void)
{
    int fit = (S.width - 40 - MENU_PAD * 2) / MENU_TILE_W;
    int cols = MENU_COLS < fit ? MENU_COLS : fit;
    return cols < 1 ? 1 : cols;
}

static int menu_content_h(void)
{
    int list[MAX_APPS];
    int n = menu_filtered(list);
    int cols = menu_cols();
    int rows = (n + cols - 1) / cols;
    return (rows < 1 ? 1 : rows) * MENU_TILE_H;
}

// Sized for the full app list so the panel does not jump around while filtering.
static SxRect menu_rect(void)
{
    int cols = menu_cols();
    int rows = (shell_app_count + cols - 1) / cols;
    int w = cols * MENU_TILE_W + MENU_PAD * 2;
    int h = MENU_HEADER_H + (rows < 1 ? 1 : rows) * MENU_TILE_H + MENU_PAD * 2;
    int max_h = (S.height - BAR_H) * 3 / 4;
    if (h > max_h)
        h = max_h;
    int y = BAR_H + (S.height - BAR_H - h) / 3;
    return sx_rect((S.width - w) / 2, y + menu_slide_offset(), w, h);
}

static SxRect menu_content_rect(void)
{
    SxRect m = menu_rect();
    return sx_rect(m.x, m.y + MENU_HEADER_H, m.w, m.h - MENU_HEADER_H);
}

static int menu_max_scroll(void)
{
    int over = menu_content_h() + MENU_PAD * 2 - menu_content_rect().h;
    return over > 0 ? over : 0;
}

static void menu_scroll_by(int dy)
{
    int s = S.menu_scroll + dy;
    int max = menu_max_scroll();
    if (s > max) s = max;
    if (s < 0) s = 0;
    if (s == S.menu_scroll)
        return;
    S.menu_scroll = s;
    damage_add(menu_rect());
}

static SxRect menu_item_rect(int i)
{
    SxRect m = menu_content_rect();
    int cols = menu_cols();
    return sx_rect(m.x + MENU_PAD + (i % cols) * MENU_TILE_W,
                   m.y + MENU_PAD + (i / cols) * MENU_TILE_H - S.menu_scroll, MENU_TILE_W, MENU_TILE_H);
}

static SxRect menu_outer_rect(void)
{
    SxRect m = menu_rect();
    return sx_rect(m.x - SHADOW_SIZE, m.y - SHADOW_SIZE, m.w + SHADOW_SIZE * 2, m.h + SHADOW_SIZE * 2 + SHADOW_OFF_Y);
}

static int menu_hit(int x, int y)
{
    if (!sx_rect_contains(menu_content_rect(), x, y))
        return -1;
    int list[MAX_APPS];
    int n = menu_filtered(list);
    for (int i = 0; i < n; i++)
        if (sx_rect_contains(menu_item_rect(i), x, y))
            return i;
    return -1;
}

static void menu_scroll_to_sel(void)
{
    SxRect content = menu_content_rect();
    int row_top = MENU_PAD + (S.menu_sel / menu_cols()) * MENU_TILE_H;
    int view = content.h - MENU_PAD;
    if (row_top < S.menu_scroll)
        menu_scroll_by(row_top - MENU_PAD - S.menu_scroll);
    else if (row_top + MENU_TILE_H > S.menu_scroll + view)
        menu_scroll_by(row_top + MENU_TILE_H - view - S.menu_scroll);
}

static void shell_draw_menu(SxCanvas *c)
{
    SxRect m = menu_rect();
    if (sx_rect_empty(sx_rect_intersect(menu_outer_rect(), c->clip)))
        return;

    draw_shadow(c, m, 18, 140);
    sx_fill_rounded_rect(c, m.x, m.y, m.w, m.h, 18, MENU_BG);
    sx_draw_rounded_rect(c, m.x, m.y, m.w, m.h, 18, SX_RGBA(255, 255, 255, 26));

    SxRect field = sx_rect(m.x + MENU_PAD, m.y + MENU_PAD, m.w - MENU_PAD * 2, MENU_HEADER_H - MENU_PAD - 6);
    sx_fill_rounded_rect(c, field.x, field.y, field.w, field.h, field.h / 2, SX_THEME_SURFACE);
    sx_draw_rounded_rect(c, field.x, field.y, field.w, field.h, field.h / 2, SX_THEME_ACCENT_A(120));
    float gx = field.x + 22.0f, gy = field.y + field.h * 0.5f - 1.0f;
    sx_draw_circle(c, gx, gy, 5.5f, 1.6f, PANEL_TEXT_DIM);
    sx_draw_line(c, gx + 4.0f, gy + 4.0f, gx + 8.0f, gy + 8.0f, 1.6f, PANEL_TEXT_DIM);

    int tx = field.x + 40;
    int ty = sx_text_y_centered(&sx_font_sans, field);
    if (S.menu_query[0])
    {
        int end = sx_draw_text(c, &sx_font_sans, tx, ty, S.menu_query, PANEL_TEXT);
        sx_fill_rect(c, end + 1, field.y + 9, 2, field.h - 18, ACCENT);
    }
    else
    {
        sx_fill_rect(c, tx, field.y + 9, 2, field.h - 18, ACCENT);
        sx_draw_text(c, &sx_font_sans, tx + 6, ty, "Type to search apps", PANEL_TEXT_DIM);
    }

    int list[MAX_APPS];
    int n = menu_filtered(list);

    SxRect content = menu_content_rect();
    content = sx_rect(content.x, content.y, content.w, content.h - 6);
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, content));

    if (n == 0)
        sx_draw_text_centered(c, &sx_font_sans, sx_rect(content.x, content.y, content.w, MENU_TILE_H),
                              "No apps match", PANEL_TEXT_DIM);

    for (int i = 0; i < n; i++)
    {
        SxRect r = menu_item_rect(i);
        if (sx_rect_empty(sx_rect_intersect(r, c->clip)))
            continue;

        const ShellApp *app = &shell_apps[list[i]];
        SxRect tile = sx_rect(r.x + 4, r.y + 4, r.w - 8, r.h - 8);
        float hover = fade_level(FADE_MENU, i);
        if (i == S.menu_sel)
            sx_fill_rounded_rect(c, tile.x, tile.y, tile.w, tile.h, 14, SX_THEME_ACCENT_A(56));
        else if (hover > 0.0f)
            sx_fill_rounded_rect(c, tile.x, tile.y, tile.w, tile.h, 14, SX_RGBA(255, 255, 255, (uint32_t)(20 * hover)));

        draw_app_icon(c, app->entry.name, app->icon_menu, r.x + (r.w - MENU_ICON) / 2, r.y + 12, MENU_ICON);
        draw_text_fit(c, &sx_font_sans_small, sx_rect(tile.x + 4, tile.y, tile.w - 8, tile.h), r.y + 12 + MENU_ICON + 8,
                      app->entry.name, PANEL_TEXT);
    }

    int max_scroll = menu_max_scroll();
    if (max_scroll > 0)
    {
        int track = content.h - 8;
        int thumb = track * content.h / (content.h + max_scroll);
        if (thumb < 24)
            thumb = 24;
        int ty2 = content.y + 4 + (track - thumb) * S.menu_scroll / max_scroll;
        sx_fill_rounded_rect(c, m.x + m.w - 7, ty2, 3, thumb, 1, SX_RGBA(255, 255, 255, 60));
    }
    c->clip = old;
}

static void shell_close_menu(void)
{
    if (!S.menu_open)
        return;
    S.menu_open = false;
    damage_add(menu_outer_rect());
    damage_add(bar_logo_rect());
}

static void shell_toggle_menu(void)
{
    if (S.menu_open)
    {
        shell_close_menu();
        return;
    }
    S.menu_open = true;
    S.menu_hover = -1;
    S.menu_sel = 0;
    S.menu_scroll = 0;
    S.menu_query[0] = '\0';
    damage_add(menu_outer_rect());
    damage_add(bar_logo_rect());
}

static void shell_launch(int app_index)
{
    if (app_index < 0 || app_index >= shell_app_count)
        return;
    if (!sx_desktop_entry_launch(&shell_apps[app_index].entry))
        printf("strawx: failed to launch %s\n", shell_apps[app_index].entry.exec);
}

static void shell_menu_launch(int filtered_index)
{
    int list[MAX_APPS];
    int n = menu_filtered(list);
    if (filtered_index < 0 || filtered_index >= n)
        return;
    shell_close_menu();
    shell_launch(list[filtered_index]);
}

static void shell_launch_exe(const char *exe)
{
    for (int i = 0; i < shell_app_count; i++)
        if (strcmp(shell_apps[i].exe, exe) == 0 && !shell_apps[i].has_args)
        {
            shell_launch(i);
            return;
        }
}

// Returns true when the launcher took the key.
static bool shell_menu_key(uint16_t key, char ch)
{
    if (!S.menu_open)
        return false;

    int list[MAX_APPS];
    int n = menu_filtered(list);
    int cols = menu_cols();
    size_t len = strlen(S.menu_query);

    switch (key)
    {
        case SX_KEY_ESCAPE:
            shell_close_menu();
            return true;
        case SX_KEY_ENTER:
            shell_menu_launch(S.menu_sel);
            return true;
        case SX_KEY_LEFT:  S.menu_sel--; break;
        case SX_KEY_RIGHT: S.menu_sel++; break;
        case SX_KEY_UP:    S.menu_sel -= cols; break;
        case SX_KEY_DOWN:  S.menu_sel += cols; break;
        case SX_KEY_BACKSPACE:
            if (len > 0)
                S.menu_query[len - 1] = '\0';
            S.menu_sel = 0;
            S.menu_scroll = 0;
            break;
        default:
            if (ch >= 32 && ch < 127 && len < sizeof(S.menu_query) - 1)
            {
                S.menu_query[len] = ch;
                S.menu_query[len + 1] = '\0';
                S.menu_sel = 0;
                S.menu_scroll = 0;
            }
            else if (!ch)
            {
                return true;
            }
            break;
    }

    n = menu_filtered(list);
    if (S.menu_sel >= n)
        S.menu_sel = n - 1;
    if (S.menu_sel < 0)
        S.menu_sel = 0;
    menu_scroll_to_sel();
    damage_add(menu_outer_rect());
    return true;
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
            damage_add(bar_clock_rect());
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
