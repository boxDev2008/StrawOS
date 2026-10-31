#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <strawx.h>
#include <ui.h>

#define WIN_W        860
#define WIN_H        580
#define SIDEBAR_W    200
#define PAD          28
#define THUMB_W      150
#define THUMB_H      94
#define THUMB_GAP    16
#define MAX_WALLS    24
#define SWATCH       34
#define PREVIEW_W    288
#define PREVIEW_H    162
#define CARD_W       188
#define CARD_H       112
#define CARD_GAP     18

typedef enum
{
    PAGE_APPEARANCE,
    PAGE_WALLPAPER,
    PAGE_DESKTOP,
    PAGE_MOUSE,
    PAGE_ABOUT,
    PAGE_COUNT,
}
Page;

static const char *page_names[PAGE_COUNT] = { "Appearance", "Wallpaper", "Desktop & Panel", "Mouse", "About" };

typedef struct
{
    char     spec[SX_SETTING_MAX];
    char     label[64];
    SxImage *thumb;
}
WallOption;

static const char *solid_colors[] = {
    "1E2230", "2B3A55", "1F4E5F", "2E5E3E", "5E2E4E", "6B3A1E", "3A3A3A", "0B0C10",
};
#define SOLID_N ((int)(sizeof(solid_colors) / sizeof(solid_colors[0])))

typedef struct
{
    const char *key;
    const char *title;
    const char *description;
    int         def;
}
Toggle;

static const Toggle toggles[] = {
    { "desktop_icons", "Show icons on the desktop", "Shortcuts from /desktop appear on the wallpaper", 0 },
    { "clock_24h",     "24-hour clock",             "Show 14:30 instead of 2:30 PM in the top bar", 1 },
    { "show_date",     "Show the date",             "Display the date next to the clock in the top bar", 1 },
    { "animations",    "Animations",                "Animate windows, menus and hover highlights", 1 },
};
#define TOGGLE_N ((int)(sizeof(toggles) / sizeof(toggles[0])))

#define SPEED_MIN    25
#define SPEED_MAX    300
#define SPEED_STEP   5
#define SPEED_DEF    100

typedef enum
{
    HIT_NONE,
    HIT_PAGE,
    HIT_THEME,
    HIT_WALL,
    HIT_SOLID,
    HIT_TOGGLE,
    HIT_SPEED,
}
HitKind;

typedef struct
{
    HitKind kind;
    int     index;
}
Hit;

static struct
{
    SxWindow  *win;
    Page       page;
    Hit        hover;

    SxImage   *card_bg;     // current wallpaper, shown behind each theme preview

    WallOption walls[MAX_WALLS];
    int        wall_count;
    bool       walls_loaded;
    SxImage   *solid_thumbs[SOLID_N];

    char       current[SX_SETTING_MAX];
    SxImage   *preview;
    SxImage   *logo;
    int        scroll;

    int        speed;       // slider value, saved when the drag ends
    bool       dragging_speed;
}
G;

static int name_compare(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static SxImage *render_thumb(const char *spec, int w, int h)
{
    SxImage *img = sx_image_create(w, h);
    if (img)
        sx_render_wallpaper(spec, img->pixels, w, h);
    return img;
}

static void add_wall(const char *spec, const char *label)
{
    if (G.wall_count >= MAX_WALLS)
        return;
    WallOption *w = &G.walls[G.wall_count++];
    strncpy(w->spec, spec, sizeof(w->spec) - 1);
    strncpy(w->label, label, sizeof(w->label) - 1);
    w->thumb = render_thumb(spec, THUMB_W, THUMB_H);
}

static void load_walls(void)
{
    if (G.walls_loaded)
        return;
    G.walls_loaded = true;

    add_wall(SX_WALLPAPER_DEFAULT, "StrawOS");

    char names[MAX_WALLS][NAME_MAX];
    int n = 0;
    int fd = open("/pictures", O_RDONLY);
    if (fd >= 0)
    {
        struct dirent de;
        while (n < MAX_WALLS - 1 && readdir(fd, &de) == 1)
            if (de.type == STAT_FILE && sx_image_is_supported(de.name))
                strcpy(names[n++], de.name);
        close(fd);
    }

    for (int i = 1; i < n; i++)
    {
        char key[NAME_MAX];
        strcpy(key, names[i]);
        int j = i - 1;
        while (j >= 0 && name_compare(names[j], key) > 0)
        {
            strcpy(names[j + 1], names[j]);
            j--;
        }
        strcpy(names[j + 1], key);
    }

    for (int i = 0; i < n; i++)
    {
        char spec[SX_SETTING_MAX];
        snprintf(spec, sizeof(spec), "/pictures/%s", names[i]);
        add_wall(spec, names[i]);
    }

    for (int i = 0; i < SOLID_N; i++)
    {
        char spec[32];
        snprintf(spec, sizeof(spec), "color:%s", solid_colors[i]);
        G.solid_thumbs[i] = render_thumb(spec, SWATCH, SWATCH);
    }
}

static void refresh_current(void)
{
    if (!sx_setting_get("wallpaper", G.current, sizeof(G.current)) || !G.current[0])
        strcpy(G.current, SX_WALLPAPER_DEFAULT);

    sx_image_free(G.preview);
    G.preview = render_thumb(G.current, PREVIEW_W, PREVIEW_H);
    sx_image_free(G.card_bg);
    G.card_bg = render_thumb(G.current, CARD_W, CARD_H);
}

static void apply_wallpaper(const char *spec)
{
    sx_setting_set("wallpaper", spec);
    sx_settings_apply();
    refresh_current();
}

static void apply_theme(const SxTheme *t)
{
    sx_setting_set("theme", t->id);
    sx_settings_apply();
    sx_theme_load();
}

static SxRect page_rect(int i)
{
    return sx_rect(12, 70 + i * 40, SIDEBAR_W - 24, 34);
}

static int content_x(void)
{
    return SIDEBAR_W + PAD;
}

static int content_w(void)
{
    return sx_window_width(G.win) - SIDEBAR_W - 2 * PAD;
}

static int wall_cols(void)
{
    int cols = (content_w() + THUMB_GAP) / (THUMB_W + THUMB_GAP);
    return cols > 0 ? cols : 1;
}

static int walls_top(void)
{
    return 76 + PREVIEW_H + 40 - G.scroll;
}

static SxRect wall_rect(int i)
{
    int col = i % wall_cols(), row = i / wall_cols();
    return sx_rect(content_x() + col * (THUMB_W + THUMB_GAP), walls_top() + row * (THUMB_H + 40), THUMB_W, THUMB_H);
}

static int solids_top(void)
{
    int rows = (G.wall_count + wall_cols() - 1) / wall_cols();
    return walls_top() + rows * (THUMB_H + 40) + 34;
}

static SxRect solid_rect(int i)
{
    return sx_rect(content_x() + i * (SWATCH + 12), solids_top(), SWATCH, SWATCH);
}

static int content_height(void)
{
    return solids_top() + G.scroll + SWATCH + PAD;
}

static SxRect theme_rect(int i)
{
    int cols = (content_w() + CARD_GAP) / (CARD_W + CARD_GAP);
    if (cols < 1)
        cols = 1;
    return sx_rect(content_x() + (i % cols) * (CARD_W + CARD_GAP), 84 + (i / cols) * (CARD_H + 46), CARD_W, CARD_H);
}

static SxRect toggle_row_rect(int i)
{
    return sx_rect(content_x(), 80 + i * 76, content_w(), 64);
}

static SxRect toggle_switch_rect(int i)
{
    SxRect r = toggle_row_rect(i);
    return sx_rect(r.x + r.w - 18 - 46, r.y + (r.h - 26) / 2, 46, 26);
}

static SxRect speed_track_rect(void)
{
    return sx_rect(content_x() + 12, 128, content_w() - 24, 6);
}

// The whole band around the track, so the slider is easy to grab.
static SxRect speed_hit_rect(void)
{
    return sx_rect(content_x(), 112, content_w(), 38);
}

static int speed_x(int speed)
{
    SxRect t = speed_track_rect();
    return t.x + (speed - SPEED_MIN) * t.w / (SPEED_MAX - SPEED_MIN);
}

static int clamp_speed(int speed)
{
    speed = (speed + SPEED_STEP / 2) / SPEED_STEP * SPEED_STEP;
    return speed < SPEED_MIN ? SPEED_MIN : (speed > SPEED_MAX ? SPEED_MAX : speed);
}

static void load_speed(void)
{
    G.speed = clamp_speed(sx_setting_get_int("mouse_speed", SPEED_DEF));
}

static void save_speed(void)
{
    if (sx_setting_get_int("mouse_speed", SPEED_DEF) == G.speed)
        return;
    sx_setting_set_int("mouse_speed", G.speed);
    sx_settings_apply();
}

static bool set_speed_from_x(int x)
{
    SxRect t = speed_track_rect();
    int old = G.speed;
    G.speed = clamp_speed(SPEED_MIN + (x - t.x) * (SPEED_MAX - SPEED_MIN) / t.w);
    return G.speed != old;
}

static bool hovering(HitKind kind, int index)
{
    return G.hover.kind == kind && G.hover.index == index;
}

static void draw_title(SxCanvas *c, const char *title, const char *subtitle)
{
    sx_draw_text(c, &sx_font_sans_large, content_x(), 18, title, SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans, content_x(), 50, subtitle, SX_THEME_TEXT_DIM);
}

static void draw_thumb(SxCanvas *c, const SxImage *img, SxRect r, bool selected, bool hot)
{
    if (img)
        sx_draw_image(c, img, r.x, r.y);
    if (selected)
    {
        sx_draw_rounded_rect(c, r.x - 4, r.y - 4, r.w + 8, r.h + 8, 8, SX_THEME_ACCENT);
        sx_draw_rounded_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7, SX_THEME_ACCENT);
    }
    else if (hot)
    {
        sx_draw_rounded_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7, SX_RGBA(255, 255, 255, 90));
    }
    sx_draw_rect(c, r.x, r.y, r.w, r.h, SX_RGBA(255, 255, 255, 30));
}

static void draw_wallpaper_page(SxCanvas *c)
{
    SxRect area = sx_rect(SIDEBAR_W, 0, c->width - SIDEBAR_W, c->height);
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, area));

    int top = -G.scroll;
    sx_draw_text(c, &sx_font_sans_large, content_x(), 18 + top, "Wallpaper", SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans, content_x(), 50 + top, "Pick a picture or a color for the desktop", SX_THEME_TEXT_DIM);

    SxRect pv = sx_rect(content_x(), 76 + top, PREVIEW_W, PREVIEW_H);
    sx_fill_rounded_rect(c, pv.x - 8, pv.y - 8, pv.w + 16, pv.h + 16, 12, sx_darken(SX_THEME_BG, 40));
    if (G.preview)
        sx_draw_image(c, G.preview, pv.x, pv.y);
    sx_fill_rect(c, pv.x, pv.y, pv.w, 8, (sx_theme->panel & 0xFFFFFFu) | (214u << 24));
    sx_fill_rounded_rect(c, pv.x + 4, pv.y + 2, 6, 4, 2, SX_THEME_ACCENT);
    sx_fill_rounded_rect(c, pv.x + pv.w / 2 - 30, pv.y + pv.h - 14, 60, 10, 5, (sx_theme->panel & 0xFFFFFFu) | (200u << 24));
    for (int i = 0; i < 3; i++)
        sx_fill_rounded_rect(c, pv.x + 10, pv.y + 16 + i * 20, 12, 12, 3, SX_RGBA(255, 255, 255, 150));
    sx_fill_rounded_rect(c, pv.x + 90, pv.y + 34, 110, 70, 4, sx_theme->title);
    sx_fill_rect(c, pv.x + 90, pv.y + 44, 110, 60, SX_THEME_BG);

    const char *label = G.current;
    for (int i = 0; i < G.wall_count; i++)
        if (strcmp(G.walls[i].spec, G.current) == 0)
            label = G.walls[i].label;
    if (strncmp(G.current, "color:", 6) == 0)
        label = "Solid color";
    sx_draw_text(c, &sx_font_sans_small, pv.x + pv.w + 28, pv.y + 6, "CURRENT", SX_THEME_TEXT_DIM);
    sx_draw_text(c, &sx_font_sans_bold, pv.x + pv.w + 28, pv.y + 26, label, SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans_small, pv.x + pv.w + 28, pv.y + 50, "Tip: right-click any image in Files", SX_THEME_TEXT_DIM);
    sx_draw_text(c, &sx_font_sans_small, pv.x + pv.w + 28, pv.y + 68, "and choose Set as Wallpaper.", SX_THEME_TEXT_DIM);

    sx_draw_text(c, &sx_font_sans_small, content_x(), walls_top() - 24, "PICTURES", SX_THEME_TEXT_DIM);
    for (int i = 0; i < G.wall_count; i++)
    {
        SxRect r = wall_rect(i);
        draw_thumb(c, G.walls[i].thumb, r, strcmp(G.walls[i].spec, G.current) == 0, hovering(HIT_WALL, i));

        char name[64];
        strncpy(name, G.walls[i].label, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        while (strlen(name) > 3 && sx_text_width(&sx_font_sans_small, name) > THUMB_W)
            name[strlen(name) - 1] = '\0';
        sx_draw_text(c, &sx_font_sans_small, r.x, r.y + r.h + 8, name, SX_THEME_TEXT_DIM);
    }

    sx_draw_text(c, &sx_font_sans_small, content_x(), solids_top() - 24, "COLORS", SX_THEME_TEXT_DIM);
    for (int i = 0; i < SOLID_N; i++)
    {
        SxRect r = solid_rect(i);
        char spec[32];
        snprintf(spec, sizeof(spec), "color:%s", solid_colors[i]);
        uint32_t col = G.solid_thumbs[i] ? G.solid_thumbs[i]->pixels[0] : 0xFF000000u;
        bool selected = strcmp(spec, G.current) == 0;
        if (selected)
            sx_draw_circle(c, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w * 0.5f + 4, 2.0f, SX_THEME_ACCENT);
        else if (hovering(HIT_SOLID, i))
            sx_draw_circle(c, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w * 0.5f + 3, 1.5f, SX_RGBA(255, 255, 255, 90));
        sx_fill_circle(c, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w * 0.5f, col);
        sx_draw_circle(c, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w * 0.5f - 0.5f, 1.0f, SX_RGBA(255, 255, 255, 30));
    }

    c->clip = old;
}

static void draw_switch(SxCanvas *c, SxRect r, bool on, bool hot)
{
    uint32_t track = on ? SX_THEME_ACCENT : SX_THEME_BORDER;
    if (hot)
        track = sx_lighten(track, 20);
    sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, r.h / 2, track);
    float kx = on ? r.x + r.w - r.h * 0.5f : r.x + r.h * 0.5f;
    sx_fill_circle(c, kx, r.y + r.h * 0.5f + 1, r.h * 0.5f - 3, SX_RGBA(0, 0, 0, 60));
    sx_fill_circle(c, kx, r.y + r.h * 0.5f, r.h * 0.5f - 3, SX_RGB(255, 255, 255));
}

// A miniature desktop in the theme's colors, over the current wallpaper.
static void draw_theme_card(SxCanvas *c, int i, SxRect r)
{
    const SxTheme *t = &sx_themes[i];
    if (G.card_bg)
        sx_draw_image(c, G.card_bg, r.x, r.y);
    else
        sx_fill_rect(c, r.x, r.y, r.w, r.h, t->bg);

    int wx = r.x + 24, wy = r.y + 14, ww = r.w - 48, wh = r.h - 34;
    sx_fill_rounded_rect(c, wx + 2, wy + 4, ww, wh, 6, SX_RGBA(0, 0, 0, 70));
    sx_fill_rounded_rect(c, wx, wy, ww, wh, 6, t->bg);
    sx_fill_rounded_rect(c, wx, wy, ww, 16, 6, t->title);
    sx_fill_rect(c, wx, wy + 10, ww, 6, t->title);
    sx_fill_circle(c, wx + ww - 9.0f, wy + 8.0f, 3.0f, t->red);
    sx_fill_rounded_rect(c, wx + 8, wy + 23, ww / 2, 5, 2, t->text);
    sx_fill_rounded_rect(c, wx + 8, wy + 32, ww / 3, 4, 2, t->text_dim);
    sx_fill_rounded_rect(c, wx + 8, wy + 42, ww - 16, wh - 50, 4, t->surface);
    sx_fill_rounded_rect(c, wx + ww - 42, wy + wh - 20, 28, 10, 4, t->accent);

    sx_fill_rect(c, r.x, r.y, r.w, 8, (t->panel & 0xFFFFFFu) | (214u << 24));
    sx_fill_rounded_rect(c, r.x + 5, r.y + 2, 6, 4, 2, t->accent);
    sx_fill_rounded_rect(c, r.x + r.w / 2 - 24, r.y + r.h - 12, 48, 8, 4, (t->panel & 0xFFFFFFu) | (200u << 24));

    bool selected = sx_theme == t;
    if (selected)
    {
        sx_draw_rounded_rect(c, r.x - 4, r.y - 4, r.w + 8, r.h + 8, 8, SX_THEME_ACCENT);
        sx_draw_rounded_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7, SX_THEME_ACCENT);
    }
    else if (hovering(HIT_THEME, i))
    {
        sx_draw_rounded_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7, SX_RGBA(255, 255, 255, 90));
    }
    sx_draw_rect(c, r.x, r.y, r.w, r.h, SX_RGBA(255, 255, 255, 30));

    sx_draw_text(c, &sx_font_sans_bold, r.x, r.y + r.h + 10, t->name, selected ? SX_THEME_TEXT : SX_THEME_TEXT_DIM);
    const uint32_t dots[4] = { t->accent, t->blue, t->red, t->green };
    for (int d = 0; d < 4; d++)
        sx_fill_circle(c, r.x + r.w - 5.0f - (3 - d) * 13.0f, r.y + r.h + 19.0f, 5.0f, dots[d]);
}

static void draw_appearance_page(SxCanvas *c)
{
    draw_title(c, "Appearance", "Color themes for windows, the top bar, apps and the terminal");
    for (int i = 0; i < sx_theme_count; i++)
        draw_theme_card(c, i, theme_rect(i));
}

static void draw_desktop_page(SxCanvas *c)
{
    draw_title(c, "Desktop & Panel", "How the desktop, top bar and dock look");

    for (int i = 0; i < TOGGLE_N; i++)
    {
        SxRect r = toggle_row_rect(i);
        sx_ui_panel(c, r, 10, hovering(HIT_TOGGLE, i) ? SX_THEME_SURFACE_HI : SX_THEME_SURFACE);
        sx_draw_text(c, &sx_font_sans_bold, r.x + 18, r.y + 12, toggles[i].title, SX_THEME_TEXT);
        sx_draw_text(c, &sx_font_sans_small, r.x + 18, r.y + 34, toggles[i].description, SX_THEME_TEXT_DIM);
        draw_switch(c, toggle_switch_rect(i), sx_setting_get_int(toggles[i].key, toggles[i].def) != 0, hovering(HIT_TOGGLE, i));
    }
}

static void draw_mouse_page(SxCanvas *c)
{
    draw_title(c, "Mouse", "Adjust how the pointer moves");

    sx_draw_text(c, &sx_font_sans_bold, content_x(), 84, "Pointer speed", SX_THEME_TEXT);

    char value[16];
    snprintf(value, sizeof(value), "%d%%", G.speed);
    int vw = sx_text_width(&sx_font_sans, value);
    sx_draw_text(c, &sx_font_sans, content_x() + content_w() - vw, 84, value, SX_THEME_TEXT_DIM);

    SxRect t = speed_track_rect();
    int kx = speed_x(G.speed);
    int dx = speed_x(SPEED_DEF);
    sx_fill_rounded_rect(c, t.x, t.y, t.w, t.h, t.h / 2, sx_darken(SX_THEME_BG, 35));
    sx_fill_rounded_rect(c, t.x, t.y, kx - t.x, t.h, t.h / 2, SX_THEME_ACCENT);
    sx_fill_rect(c, dx, t.y + t.h + 5, 1, 6, SX_THEME_TEXT_DIM);

    bool hot = G.dragging_speed || hovering(HIT_SPEED, 0);
    float ky = t.y + t.h * 0.5f;
    sx_fill_circle(c, (float)kx, ky + 1, hot ? 11.0f : 10.0f, SX_RGBA(0, 0, 0, 60));
    sx_fill_circle(c, (float)kx, ky, hot ? 11.0f : 10.0f, SX_RGB(255, 255, 255));

    sx_draw_text(c, &sx_font_sans_small, t.x, t.y + 18, "Slow", SX_THEME_TEXT_DIM);
    int fw = sx_text_width(&sx_font_sans_small, "Fast");
    sx_draw_text(c, &sx_font_sans_small, t.x + t.w - fw, t.y + 18, "Fast", SX_THEME_TEXT_DIM);

    sx_draw_text(c, &sx_font_sans_small, content_x(), 176,
                 "Applies straight away. In QEMU, click inside the window to capture the mouse",
                 SX_THEME_TEXT_DIM);
    sx_draw_text(c, &sx_font_sans_small, content_x(), 194, "and press Ctrl+Alt+G to release it.", SX_THEME_TEXT_DIM);
}

static void draw_about_page(SxCanvas *c)
{
    int x = content_x();
    if (!G.logo)
    {
        SxImage *img = sx_image_load("/share/icons/strawos.png");
        G.logo = sx_image_scale(img, 64, 64);
        sx_image_free(img);
    }
    if (G.logo)
        sx_draw_image(c, G.logo, x, 30);
    sx_draw_text(c, &sx_font_sans_large, x + 84, 34, "StrawOS", SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans, x + 85, 66, "A small hobby operating system", SX_THEME_TEXT_DIM);

    struct meminfo mem;
    meminfo(&mem);
    uint64_t total_mb = mem.total_pages * mem.page_size / (1024 * 1024);
    uint64_t free_mb = mem.free_pages * mem.page_size / (1024 * 1024);
    uint64_t up = uptime_ms() / 1000;

    char rows[6][2][96];
    snprintf(rows[0][0], 96, "Display");
    snprintf(rows[0][1], 96, "%d x %d", sx_screen_width(), sx_screen_height());
    snprintf(rows[1][0], 96, "Memory");
    snprintf(rows[1][1], 96, "%llu MiB free of %llu MiB", (unsigned long long)free_mb, (unsigned long long)total_mb);
    snprintf(rows[2][0], 96, "Uptime");
    snprintf(rows[2][1], 96, "%llu:%02llu:%02llu", (unsigned long long)(up / 3600), (unsigned long long)(up / 60 % 60),
             (unsigned long long)(up % 60));
    snprintf(rows[3][0], 96, "Display server");
    snprintf(rows[3][1], 96, "StrawX, protocol v%d", SX_PROTOCOL_VERSION);
    snprintf(rows[4][0], 96, "Architecture");
    snprintf(rows[4][1], 96, "x86_64, booted by Limine");
    snprintf(rows[5][0], 96, "Settings file");
    snprintf(rows[5][1], 96, "%s", SX_SETTINGS_PATH);

    SxRect card = sx_rect(x, 122, content_w(), 6 * 36 + 16);
    sx_ui_panel(c, card, 12, SX_THEME_SURFACE);
    for (int i = 0; i < 6; i++)
    {
        int y = card.y + 14 + i * 36;
        sx_draw_text(c, &sx_font_sans, card.x + 18, y, rows[i][0], SX_THEME_TEXT_DIM);
        int vw = sx_text_width(&sx_font_sans, rows[i][1]);
        sx_draw_text(c, &sx_font_sans, card.x + card.w - 18 - vw, y, rows[i][1], SX_THEME_TEXT);
        if (i < 5)
            sx_fill_rect(c, card.x + 18, y + 28, card.w - 36, 1, SX_RGBA(255, 255, 255, 12));
    }

    sx_draw_text(c, &sx_font_sans_small, x, card.y + card.h + 16,
                 "Settings live in a RAM filesystem, so they reset when the machine restarts.", SX_THEME_TEXT_DIM);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(G.win);
    sx_clear(c, SX_THEME_BG);

    switch (G.page)
    {
        case PAGE_APPEARANCE: draw_appearance_page(c); break;
        case PAGE_WALLPAPER: draw_wallpaper_page(c); break;
        case PAGE_DESKTOP:   draw_desktop_page(c); break;
        case PAGE_MOUSE:     draw_mouse_page(c); break;
        default:             draw_about_page(c); break;
    }

    sx_fill_rect(c, 0, 0, SIDEBAR_W, c->height, sx_darken(SX_THEME_BG, 25));
    sx_fill_rect(c, SIDEBAR_W - 1, 0, 1, c->height, SX_RGBA(0, 0, 0, 90));
    sx_draw_text(c, &sx_font_sans_large, 22, 18, "Settings", SX_THEME_TEXT);

    for (int i = 0; i < PAGE_COUNT; i++)
    {
        SxRect r = page_rect(i);
        bool current = (int)G.page == i;
        if (current)
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8, (SX_THEME_ACCENT & 0xFFFFFFu) | (40u << 24));
        else if (hovering(HIT_PAGE, i))
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8, SX_RGBA(255, 255, 255, 14));
        if (current)
            sx_fill_rounded_rect(c, r.x, r.y + 8, 3, r.h - 16, 1, SX_THEME_ACCENT);
        sx_draw_text(c, &sx_font_sans, r.x + 14, sx_text_y_centered(&sx_font_sans, r), page_names[i],
                     current ? SX_THEME_TEXT : SX_THEME_TEXT_DIM);
    }

    sx_window_commit(G.win);
}

static Hit hit_test(int x, int y)
{
    Hit h = { HIT_NONE, 0 };

    for (int i = 0; i < PAGE_COUNT; i++)
        if (sx_rect_contains(page_rect(i), x, y))
            return (Hit){ HIT_PAGE, i };

    if (x < SIDEBAR_W)
        return h;

    switch (G.page)
    {
        case PAGE_APPEARANCE:
            for (int i = 0; i < sx_theme_count; i++)
                if (sx_rect_contains(theme_rect(i), x, y))
                    return (Hit){ HIT_THEME, i };
            break;
        case PAGE_WALLPAPER:
            for (int i = 0; i < G.wall_count; i++)
                if (sx_rect_contains(wall_rect(i), x, y))
                    return (Hit){ HIT_WALL, i };
            for (int i = 0; i < SOLID_N; i++)
                if (sx_rect_contains(solid_rect(i), x, y))
                    return (Hit){ HIT_SOLID, i };
            break;
        case PAGE_DESKTOP:
            for (int i = 0; i < TOGGLE_N; i++)
                if (sx_rect_contains(toggle_row_rect(i), x, y))
                    return (Hit){ HIT_TOGGLE, i };
            break;
        case PAGE_MOUSE:
            if (sx_rect_contains(speed_hit_rect(), x, y))
                return (Hit){ HIT_SPEED, 0 };
            break;
        default:
            break;
    }
    return h;
}

static void clamp_scroll(void)
{
    int max = content_height() - sx_window_height(G.win);
    if (G.scroll > max)
        G.scroll = max;
    if (G.scroll < 0)
        G.scroll = 0;
}

static void activate(Hit h)
{
    switch (h.kind)
    {
        case HIT_PAGE:
            G.page = (Page)h.index;
            G.scroll = 0;
            if (G.page == PAGE_WALLPAPER)
            {
                load_walls();
                refresh_current();
            }
            break;
        case HIT_THEME:
            apply_theme(&sx_themes[h.index]);
            break;
        case HIT_WALL:
            apply_wallpaper(G.walls[h.index].spec);
            break;
        case HIT_SOLID:
        {
            char spec[32];
            snprintf(spec, sizeof(spec), "color:%s", solid_colors[h.index]);
            apply_wallpaper(spec);
            break;
        }
        case HIT_TOGGLE:
        {
            const char *key = toggles[h.index].key;
            sx_setting_set_int(key, sx_setting_get_int(key, toggles[h.index].def) ? 0 : 1);
            sx_settings_apply();
            break;
        }
        default:
            break;
    }
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("settings: cannot connect to the display server\n");
        return 1;
    }

    G.win = sx_window_create("Settings", WIN_W, WIN_H, SX_WINDOW_RESIZABLE);
    if (!G.win)
        return 1;

    if (argc > 1)
        for (int i = 0; i < PAGE_COUNT; i++)
            if (name_compare(argv[1], page_names[i]) == 0)
                G.page = (Page)i;

    if (G.page == PAGE_WALLPAPER)
        load_walls();
    refresh_current();
    load_speed();
    draw();

    uint64_t last_about = sx_time_ms();
    bool dirty = false;
    for (;;)
    {
        SxEvent ev;
        int rc = sx_next_event(&ev, G.page == PAGE_ABOUT ? 1000 : -1);
        if (rc < 0)
            break;
        if (rc == 0)
        {
            if (G.page == PAGE_ABOUT && sx_time_ms() - last_about >= 1000)
            {
                last_about = sx_time_ms();
                draw();
            }
            continue;
        }

        bool redraw = false;
        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                clamp_scroll();
                redraw = true;
                break;

            case SX_EVENT_FOCUS_IN:
                refresh_current();
                if (!G.dragging_speed)
                    load_speed();
                redraw = true;
                break;

            case SX_EVENT_MOUSE_MOVE:
            {
                if (G.dragging_speed)
                {
                    redraw |= set_speed_from_x(ev.x);
                    break;
                }
                Hit h = hit_test(ev.x, ev.y);
                if (h.kind != G.hover.kind || h.index != G.hover.index)
                {
                    G.hover = h;
                    redraw = true;
                }
                break;
            }

            case SX_EVENT_MOUSE_LEAVE:
                G.hover.kind = HIT_NONE;
                redraw = true;
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.button == SX_BUTTON_LEFT)
                {
                    Hit h = hit_test(ev.x, ev.y);
                    if (h.kind == HIT_SPEED)
                    {
                        G.dragging_speed = true;
                        set_speed_from_x(ev.x);
                    }
                    else
                        activate(h);
                    G.hover = hit_test(ev.x, ev.y);
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_UP:
                if (ev.button == SX_BUTTON_LEFT && G.dragging_speed)
                {
                    G.dragging_speed = false;
                    save_speed();
                    G.hover = hit_test(ev.x, ev.y);
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_WHEEL:
                if (G.page == PAGE_WALLPAPER)
                {
                    G.scroll -= ev.wheel * 40;
                    clamp_scroll();
                    G.hover = hit_test(ev.x, ev.y);
                    redraw = true;
                }
                break;

            case SX_EVENT_KEY_DOWN:
                if (G.page == PAGE_MOUSE && (ev.key == SX_KEY_LEFT || ev.key == SX_KEY_RIGHT))
                {
                    G.speed = clamp_speed(G.speed + (ev.key == SX_KEY_RIGHT ? SPEED_STEP : -SPEED_STEP));
                    save_speed();
                    redraw = true;
                    break;
                }
                if (ev.key == SX_KEY_DOWN && G.page < PAGE_COUNT - 1)
                    G.page = (Page)(G.page + 1);
                else if (ev.key == SX_KEY_UP && G.page > 0)
                    G.page = (Page)(G.page - 1);
                G.scroll = 0;
                redraw = true;
                break;

            default:
                break;
        }

        dirty |= redraw;
        if (dirty && !sx_pending())
        {
            draw();
            dirty = false;
        }
    }

out:
    sx_window_destroy(G.win);
    sx_disconnect();
    return 0;
}
