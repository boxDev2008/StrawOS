#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "utils/messages.h"
#include "netsurf/types.h"
#include "netsurf/window.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"
#include "netsurf/mouse.h"
#include "netsurf/content.h"
#include "netsurf/plotters.h"
#include "desktop/browser_history.h"
#include "desktop/textinput.h"

#include "nsx.h"

#define TOOLBAR_H      48
#define SCROLLBAR_W    12
#define URL_LEN        2048
#define HOME_URL       "https://html.duckduckgo.com/html/"
#define SEARCH_URL     "https://html.duckduckgo.com/html/?q="
#define DOUBLE_CLICK_MS 400

typedef enum
{
    HIT_NONE,
    HIT_BACK,
    HIT_FORWARD,
    HIT_RELOAD,
    HIT_HOME,
    HIT_ADDRESS,
    HIT_CONTENT,
    HIT_VSCROLL,
    HIT_HSCROLL,
}
HitKind;

struct gui_window
{
    struct browser_window *bw;
    SxWindow  *win;

    uint32_t  *store;
    int        store_w, store_h;
    SxCanvas   canvas;
    bool       dirty;
    struct rect dirty_rect;
    bool       present;

    int        scroll_x, scroll_y;
    int        extent_w, extent_h;

    char       address[URL_LEN];
    int        addr_len;
    int        addr_cursor;
    int        addr_scroll;
    bool       addr_focus;
    bool       addr_fresh;

    char       status[256];
    bool       loading;
    uint64_t   throb_start;

    HitKind    hover;
    HitKind    pressed;
    int        drag_offset;

    browser_mouse_state mouse_state;
    int        press_x, press_y;
    bool       press_moved;
    uint64_t   last_click_ms;
    int        last_click_x, last_click_y;

    bool       caret_visible;
    int        caret_x, caret_y, caret_h;

    struct gui_window *next;
};

static struct gui_window *windows;

static int view_w(struct gui_window *g)
{
    int w = sx_window_width(g->win) - SCROLLBAR_W;
    return w > 1 ? w : 1;
}

static bool hscroll_visible(struct gui_window *g)
{
    return g->extent_w > view_w(g);
}

static int view_h(struct gui_window *g)
{
    int h = sx_window_height(g->win) - TOOLBAR_H - (hscroll_visible(g) ? SCROLLBAR_W : 0);
    return h > 1 ? h : 1;
}

static SxRect back_rect(void)    { return sx_rect(10, 9, 32, 30); }
static SxRect forward_rect(void) { return sx_rect(46, 9, 32, 30); }
static SxRect reload_rect(void)  { return sx_rect(82, 9, 32, 30); }
static SxRect home_rect(void)    { return sx_rect(118, 9, 32, 30); }

static SxRect address_rect(struct gui_window *g)
{
    return sx_rect(160, 8, sx_window_width(g->win) - 172, 32);
}

static void request_present(struct gui_window *g)
{
    g->present = true;
}

static void mark_dirty(struct gui_window *g, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->store_w) x1 = g->store_w;
    if (y1 > g->store_h) y1 = g->store_h;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (!g->dirty)
    {
        g->dirty_rect = (struct rect){ x0, y0, x1, y1 };
        g->dirty = true;
    }
    else
    {
        if (x0 < g->dirty_rect.x0) g->dirty_rect.x0 = x0;
        if (y0 < g->dirty_rect.y0) g->dirty_rect.y0 = y0;
        if (x1 > g->dirty_rect.x1) g->dirty_rect.x1 = x1;
        if (y1 > g->dirty_rect.y1) g->dirty_rect.y1 = y1;
    }
    request_present(g);
}

static void mark_all_dirty(struct gui_window *g)
{
    mark_dirty(g, 0, 0, g->store_w, g->store_h);
}

static void ensure_store(struct gui_window *g)
{
    int w = view_w(g), h = view_h(g);
    if (g->store && g->store_w == w && g->store_h == h)
        return;
    free(g->store);
    g->store = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
    g->store_w = g->store ? w : 0;
    g->store_h = g->store ? h : 0;
    if (g->store)
    {
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++)
            g->store[i] = 0xFFFFFFFFu;
        sx_canvas_init(&g->canvas, g->store, w, h, w);
    }
    mark_all_dirty(g);
}

static int max_scroll_x(struct gui_window *g)
{
    int m = g->extent_w - view_w(g);
    return m > 0 ? m : 0;
}

static int max_scroll_y(struct gui_window *g)
{
    int m = g->extent_h - view_h(g);
    return m > 0 ? m : 0;
}

static void scroll_to(struct gui_window *g, int sx, int sy)
{
    if (sx > max_scroll_x(g)) sx = max_scroll_x(g);
    if (sy > max_scroll_y(g)) sy = max_scroll_y(g);
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx == g->scroll_x && sy == g->scroll_y)
        return;

    int dy = sy - g->scroll_y;
    int dx = sx - g->scroll_x;
    g->scroll_x = sx;
    g->scroll_y = sy;

    if (g->store && dx == 0 && dy != 0 && abs(dy) < g->store_h && !g->dirty)
    {
        int w = g->store_w, h = g->store_h;
        if (dy > 0)
        {
            memmove(g->store, g->store + (size_t)dy * w, sizeof(uint32_t) * (size_t)w * (size_t)(h - dy));
            mark_dirty(g, 0, h - dy, w, h);
        }
        else
        {
            memmove(g->store - (size_t)dy * w, g->store, sizeof(uint32_t) * (size_t)w * (size_t)(h + dy));
            mark_dirty(g, 0, 0, w, -dy);
        }
        return;
    }
    mark_all_dirty(g);
}

static void set_address(struct gui_window *g, const char *url)
{
    snprintf(g->address, sizeof(g->address), "%s", url);
    g->addr_len = (int)strlen(g->address);
    g->addr_cursor = g->addr_len;
    g->addr_scroll = 0;
}

static void update_extent(struct gui_window *g)
{
    int w = 0, h = 0;
    if (browser_window_get_extents(g->bw, true, &w, &h) == NSERROR_OK)
    {
        bool had_h = hscroll_visible(g);
        g->extent_w = w;
        g->extent_h = h;
        if (had_h != hscroll_visible(g))
            ensure_store(g);
    }
    scroll_to(g, g->scroll_x, g->scroll_y);
    request_present(g);
}

static struct gui_window *window_create(struct browser_window *bw, struct gui_window *existing,
                                        gui_window_create_flags flags)
{
    (void)flags;
    struct gui_window *g = calloc(1, sizeof(struct gui_window));
    if (!g)
        return NULL;
    g->bw = bw;

    int w = sx_screen_width() > 1400 ? 1100 : 960;
    int h = sx_screen_height() > 1000 ? 820 : 640;
    if (existing)
    {
        w = sx_window_width(existing->win);
        h = sx_window_height(existing->win);
    }
    g->win = sx_window_create("NetSurf", w, h, SX_WINDOW_RESIZABLE);
    if (!g->win)
    {
        free(g);
        return NULL;
    }
    g->win->user = g;
    ensure_store(g);
    g->addr_focus = true;
    g->addr_fresh = true;
    g->next = windows;
    windows = g;
    return g;
}

static void window_destroy(struct gui_window *g)
{
    for (struct gui_window **p = &windows; *p; p = &(*p)->next)
    {
        if (*p == g)
        {
            *p = g->next;
            break;
        }
    }
    sx_window_destroy(g->win);
    free(g->store);
    free(g);
    if (!windows)
        nsx_quit = true;
}

static nserror window_invalidate(struct gui_window *g, const struct rect *r)
{
    if (!r)
    {
        mark_all_dirty(g);
        return NSERROR_OK;
    }
    mark_dirty(g, r->x0 - g->scroll_x, r->y0 - g->scroll_y, r->x1 - g->scroll_x, r->y1 - g->scroll_y);
    return NSERROR_OK;
}

static bool window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
    *sx = g->scroll_x;
    *sy = g->scroll_y;
    return true;
}

static nserror window_set_scroll(struct gui_window *g, const struct rect *r)
{
    scroll_to(g, r->x0, r->y0);
    request_present(g);
    return NSERROR_OK;
}

static nserror window_get_dimensions(struct gui_window *g, int *width, int *height)
{
    *width = view_w(g);
    *height = view_h(g);
    return NSERROR_OK;
}

static nserror window_event(struct gui_window *g, enum gui_window_event event)
{
    switch (event)
    {
        case GW_EVENT_UPDATE_EXTENT:
            update_extent(g);
            break;
        case GW_EVENT_REMOVE_CARET:
            if (g->caret_visible)
            {
                g->caret_visible = false;
                request_present(g);
            }
            break;
        case GW_EVENT_START_THROBBER:
            g->loading = true;
            g->throb_start = sx_time_ms();
            request_present(g);
            break;
        case GW_EVENT_STOP_THROBBER:
            g->loading = false;
            if (!strchr(g->status, ':'))
                g->status[0] = '\0';
            request_present(g);
            break;
        case GW_EVENT_NEW_CONTENT:
            g->scroll_x = g->scroll_y = 0;
            g->caret_visible = false;
            mark_all_dirty(g);
            break;
        default:
            break;
    }
    return NSERROR_OK;
}

static void window_set_title(struct gui_window *g, const char *title)
{
    char buf[SX_TITLE_MAX];
    if (title && *title)
        snprintf(buf, sizeof(buf), "%s - NetSurf", title);
    else
        snprintf(buf, sizeof(buf), "NetSurf");
    sx_window_set_title(g->win, buf);
}

static nserror window_set_url(struct gui_window *g, struct nsurl *url)
{
    const char *s = nsurl_access(url);
    if (!g->addr_focus || g->addr_fresh)
    {
        set_address(g, s);
        if (s[0])
            g->addr_focus = false;
    }
    request_present(g);
    return NSERROR_OK;
}

static void window_set_icon(struct gui_window *g, struct hlcache_handle *icon)
{
    (void)g;
    (void)icon;
}

static void window_set_status(struct gui_window *g, const char *text)
{
    if (!g->loading && text && !strchr(text, ':'))
        text = "";
    snprintf(g->status, sizeof(g->status), "%s", text ? text : "");
    request_present(g);
}

static void window_set_pointer(struct gui_window *g, enum gui_pointer_shape shape)
{
    (void)g;
    (void)shape;
}

static void window_place_caret(struct gui_window *g, int x, int y, int height, const struct rect *clip)
{
    (void)clip;
    g->caret_visible = true;
    g->caret_x = x;
    g->caret_y = y;
    g->caret_h = height;
    request_present(g);
}

static bool window_drag_start(struct gui_window *g, gui_drag_type type, const struct rect *rect)
{
    (void)g;
    (void)type;
    (void)rect;
    return true;
}

static nserror window_save_link(struct gui_window *g, struct nsurl *url, const char *title)
{
    (void)g;
    (void)url;
    (void)title;
    return NSERROR_OK;
}

static void window_console_log(struct gui_window *g, browser_window_console_source src, const char *msg,
                               size_t len, browser_window_console_flags flags)
{
    (void)g;
    (void)src;
    (void)msg;
    (void)len;
    (void)flags;
}

static struct gui_window_table window_table = {
    .create = window_create,
    .destroy = window_destroy,
    .invalidate = window_invalidate,
    .get_scroll = window_get_scroll,
    .set_scroll = window_set_scroll,
    .get_dimensions = window_get_dimensions,
    .event = window_event,
    .set_title = window_set_title,
    .set_url = window_set_url,
    .set_icon = window_set_icon,
    .set_status = window_set_status,
    .set_pointer = window_set_pointer,
    .place_caret = window_place_caret,
    .drag_start = window_drag_start,
    .save_link = window_save_link,
    .console_log = window_console_log,
};

struct gui_window_table *nsx_window_table = &window_table;

static void draw_nav_icon(SxCanvas *c, struct gui_window *g, SxRect r, HitKind kind, bool enabled)
{
    bool hot = g->hover == kind && enabled;
    bool down = hot && g->pressed == kind;
    if (hot)
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 7, down ? SX_RGBA(255, 255, 255, 12) : SX_RGBA(255, 255, 255, 22));

    uint32_t col = enabled ? SX_THEME_TEXT : SX_RGB(0x55, 0x5A, 0x66);
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    switch (kind)
    {
        case HIT_BACK:
            sx_draw_line(c, cx + 2, cy - 5, cx - 3, cy, 1.8f, col);
            sx_draw_line(c, cx - 3, cy, cx + 2, cy + 5, 1.8f, col);
            break;
        case HIT_FORWARD:
            sx_draw_line(c, cx - 2, cy - 5, cx + 3, cy, 1.8f, col);
            sx_draw_line(c, cx + 3, cy, cx - 2, cy + 5, 1.8f, col);
            break;
        case HIT_RELOAD:
            if (g->loading)
            {
                sx_draw_line(c, cx - 5, cy - 5, cx + 5, cy + 5, 1.8f, col);
                sx_draw_line(c, cx + 5, cy - 5, cx - 5, cy + 5, 1.8f, col);
            }
            else
            {
                sx_draw_circle(c, cx, cy, 6.0f, 1.7f, col);
                sx_fill_rect(c, (int)cx + 1, (int)cy - 8, 6, 5, SX_THEME_SURFACE);
                sx_draw_line(c, cx + 2, cy - 9, cx + 6, cy - 6, 1.6f, col);
                sx_draw_line(c, cx + 6, cy - 6, cx + 2, cy - 3, 1.6f, col);
            }
            break;
        case HIT_HOME:
            sx_draw_line(c, cx - 7, cy, cx, cy - 7, 1.7f, col);
            sx_draw_line(c, cx, cy - 7, cx + 7, cy, 1.7f, col);
            sx_draw_rect(c, (int)cx - 5, (int)cy - 1, 10, 7, col);
            break;
        default:
            break;
    }
}

static void draw_toolbar(SxCanvas *c, struct gui_window *g)
{
    int w = c->width;
    sx_fill_rect(c, 0, 0, w, TOOLBAR_H, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, TOOLBAR_H - 1, w, 1, SX_RGBA(0, 0, 0, 90));

    draw_nav_icon(c, g, back_rect(), HIT_BACK, browser_window_back_available(g->bw));
    draw_nav_icon(c, g, forward_rect(), HIT_FORWARD, browser_window_forward_available(g->bw));
    draw_nav_icon(c, g, reload_rect(), HIT_RELOAD, true);
    draw_nav_icon(c, g, home_rect(), HIT_HOME, true);

    SxRect a = address_rect(g);
    sx_fill_rounded_rect(c, a.x, a.y, a.w, a.h, 9, SX_RGB(0x17, 0x19, 0x1F));
    if (g->addr_focus)
        sx_draw_rounded_rect(c, a.x, a.y, a.w, a.h, 9, SX_THEME_ACCENT);

    const SxFont *font = &sx_font_sans;
    SxRect old = c->clip;
    SxRect inner = sx_rect(a.x + 12, a.y, a.w - 24, a.h);
    sx_canvas_set_clip(c, sx_rect_intersect(old, inner));

    int ty = sx_text_y_centered(font, a);
    if (g->addr_len == 0)
        sx_draw_text(c, font, inner.x, ty, "Search or type a web address",
                     g->addr_focus ? SX_RGB(0x5A, 0x60, 0x6C) : SX_THEME_TEXT_DIM);

    int cursor_x = sx_text_width_n(font, g->address, g->addr_cursor);
    if (cursor_x - g->addr_scroll > inner.w - 8)
        g->addr_scroll = cursor_x - inner.w + 8;
    if (cursor_x < g->addr_scroll)
        g->addr_scroll = cursor_x;
    int x0 = inner.x - g->addr_scroll;

    if (g->addr_focus && g->addr_fresh && g->addr_len)
        sx_fill_rounded_rect(c, x0 - 2, a.y + 7, sx_text_width_n(font, g->address, g->addr_len) + 4, a.h - 14, 4,
                             SX_RGBA(0xF0, 0xB4, 0x3C, 70));

    if (!g->addr_focus && strncmp(g->address, "https://", 8) == 0)
    {
        sx_draw_text(c, font, x0, ty, "https://", SX_THEME_GREEN);
        sx_draw_text(c, font, x0 + sx_text_width(font, "https://"), ty, g->address + 8, SX_THEME_TEXT);
    }
    else if (g->addr_len)
    {
        sx_draw_text_n(c, font, x0, ty, g->address, g->addr_len, SX_THEME_TEXT);
    }

    if (g->addr_focus && ((sx_time_ms() / 530) & 1) == 0)
        sx_fill_rect(c, x0 + cursor_x, a.y + 8, 2, a.h - 16, SX_THEME_ACCENT);
    c->clip = old;

    if (g->loading)
    {
        int span = w / 4;
        int t = (int)((sx_time_ms() - g->throb_start) % 1400);
        int pos = (w + span) * t / 1400 - span;
        sx_fill_rect(c, pos < 0 ? 0 : pos, TOOLBAR_H - 2, pos < 0 ? span + pos : span, 2, SX_THEME_ACCENT);
    }
}

static void draw_scrollbars(SxCanvas *c, struct gui_window *g)
{
    int top = TOOLBAR_H;
    int vw = view_w(g), vh = view_h(g);
    uint32_t track = SX_RGB(0xF0, 0xF1, 0xF4);
    uint32_t thumb = SX_RGB(0xB8, 0xBE, 0xC8);
    uint32_t thumb_hot = SX_RGB(0x90, 0x96, 0xA2);

    sx_fill_rect(c, vw, top, SCROLLBAR_W, c->height - top, track);
    if (max_scroll_y(g) > 0)
    {
        int track_h = vh - 8;
        int th = (int)((int64_t)track_h * vh / g->extent_h);
        if (th < 30) th = 30;
        int ty = top + 4 + (int)((int64_t)(track_h - th) * g->scroll_y / max_scroll_y(g));
        bool hot = g->hover == HIT_VSCROLL || g->pressed == HIT_VSCROLL;
        sx_fill_rounded_rect(c, vw + 3, ty, SCROLLBAR_W - 6, th, 3, hot ? thumb_hot : thumb);
    }

    if (hscroll_visible(g))
    {
        int y = top + vh;
        sx_fill_rect(c, 0, y, vw, SCROLLBAR_W, track);
        int track_w = vw - 8;
        int tw = (int)((int64_t)track_w * vw / g->extent_w);
        if (tw < 30) tw = 30;
        int tx = 4 + (max_scroll_x(g) ? (int)((int64_t)(track_w - tw) * g->scroll_x / max_scroll_x(g)) : 0);
        bool hot = g->hover == HIT_HSCROLL || g->pressed == HIT_HSCROLL;
        sx_fill_rounded_rect(c, tx, y + 3, tw, SCROLLBAR_W - 6, 3, hot ? thumb_hot : thumb);
    }
}

static void draw_status(SxCanvas *c, struct gui_window *g)
{
    if (!g->status[0])
        return;
    const SxFont *font = &sx_font_sans_small;
    char shown[200];
    snprintf(shown, sizeof(shown), "%s", g->status);
    int max_w = c->width / 2;
    int len = (int)strlen(shown);
    while (len > 4 && sx_text_width(font, shown) > max_w)
    {
        shown[len - 4] = '.';
        shown[len - 3] = '.';
        shown[len - 2] = '.';
        shown[len - 1] = '\0';
        len--;
    }
    int w = sx_text_width(font, shown) + 20;
    int bottom = TOOLBAR_H + view_h(g);
    SxRect r = sx_rect(0, bottom - 24, w, 24);
    sx_fill_rect(c, r.x, r.y, r.w, r.h, SX_RGBA(0x23, 0x26, 0x2F, 235));
    sx_draw_text(c, font, r.x + 10, sx_text_y_centered(font, r), shown, SX_THEME_TEXT);
}

static void render(struct gui_window *g)
{
    ensure_store(g);
    if (!g->store)
        return;

    if (g->dirty)
    {
        struct rect clip = g->dirty_rect;
        g->dirty = false;
        sx_canvas_set_clip(&g->canvas, sx_rect(clip.x0, clip.y0, clip.x1 - clip.x0, clip.y1 - clip.y0));
        for (int y = clip.y0; y < clip.y1; y++)
        {
            uint32_t *row = g->store + (size_t)y * g->store_w;
            for (int x = clip.x0; x < clip.x1; x++)
                row[x] = 0xFFFFFFFFu;
        }
        struct redraw_context ctx = {
            .interactive = true,
            .background_images = true,
            .plot = &nsx_plotters,
            .priv = &g->canvas,
        };
        browser_window_redraw(g->bw, -g->scroll_x, -g->scroll_y, &clip, &ctx);
        sx_canvas_reset_clip(&g->canvas);
    }

    SxCanvas *c = sx_window_canvas(g->win);
    sx_canvas_reset_clip(c);
    sx_blit(c, 0, TOOLBAR_H, g->store, g->store_w, g->store_h, g->store_w);

    if (g->caret_visible && !g->addr_focus)
    {
        int cx = g->caret_x - g->scroll_x;
        int cy = g->caret_y - g->scroll_y + TOOLBAR_H;
        SxRect view = sx_rect(0, TOOLBAR_H, view_w(g), view_h(g));
        sx_canvas_set_clip(c, view);
        sx_fill_rect(c, cx, cy, 1, g->caret_h, SX_RGB(0, 0, 0));
        sx_canvas_reset_clip(c);
    }

    draw_scrollbars(c, g);
    draw_toolbar(c, g);
    draw_status(c, g);
    sx_window_commit(g->win);
    g->present = false;
}

bool nsx_windows_need_redraw(void)
{
    for (struct gui_window *g = windows; g; g = g->next)
        if (g->present || g->dirty || g->loading || g->addr_focus)
            return true;
    return false;
}

void nsx_windows_redraw(void)
{
    static uint64_t last_anim;
    uint64_t now = sx_time_ms();
    bool anim = now - last_anim >= 60;
    if (anim)
        last_anim = now;
    for (struct gui_window *g = windows; g; g = g->next)
        if (g->present || g->dirty || (anim && (g->loading || g->addr_focus)))
            render(g);
}

bool nsx_windows_open(void)
{
    return windows != NULL;
}

void nsx_windows_init(void)
{
    windows = NULL;
}

static void navigate_text(struct gui_window *g, const char *input)
{
    while (*input == ' ')
        input++;
    if (!*input)
        return;

    char url[URL_LEN];
    bool has_scheme = strstr(input, "://") != NULL || strncmp(input, "about:", 6) == 0 ||
                      strncmp(input, "file:", 5) == 0 || strncmp(input, "data:", 5) == 0;
    bool looks_like_host = !strchr(input, ' ') && (strchr(input, '.') || strncmp(input, "localhost", 9) == 0);
    if (has_scheme)
    {
        snprintf(url, sizeof(url), "%s", input);
    }
    else if (input[0] == '/')
    {
        snprintf(url, sizeof(url), "file://%s", input);
    }
    else if (looks_like_host)
    {
        snprintf(url, sizeof(url), "https://%s", input);
    }
    else
    {
        static const char hex[] = "0123456789ABCDEF";
        size_t k = (size_t)snprintf(url, sizeof(url), "%s", SEARCH_URL);
        for (const unsigned char *p = (const unsigned char *)input; *p && k + 4 < sizeof(url); p++)
        {
            if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~')
                url[k++] = (char)*p;
            else if (*p == ' ')
                url[k++] = '+';
            else
            {
                url[k++] = '%';
                url[k++] = hex[*p >> 4];
                url[k++] = hex[*p & 15];
            }
        }
        url[k] = '\0';
    }

    nsurl *target;
    if (nsurl_create(url, &target) != NSERROR_OK)
        return;
    g->addr_focus = false;
    g->addr_fresh = false;
    set_address(g, url);
    browser_window_navigate(g->bw, target, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
    nsurl_unref(target);
    request_present(g);
}

static void go_home(struct gui_window *g)
{
    nsurl *url;
    if (nsurl_create(HOME_URL, &url) == NSERROR_OK)
    {
        browser_window_navigate(g->bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
        nsurl_unref(url);
    }
    set_address(g, "");
    g->addr_focus = true;
    g->addr_fresh = true;
}

static void reload_or_stop(struct gui_window *g)
{
    if (g->loading)
        browser_window_stop(g->bw);
    else
        browser_window_reload(g->bw, false);
}

static HitKind hit_test(struct gui_window *g, int x, int y)
{
    if (sx_rect_contains(back_rect(), x, y)) return HIT_BACK;
    if (sx_rect_contains(forward_rect(), x, y)) return HIT_FORWARD;
    if (sx_rect_contains(reload_rect(), x, y)) return HIT_RELOAD;
    if (sx_rect_contains(home_rect(), x, y)) return HIT_HOME;
    if (sx_rect_contains(address_rect(g), x, y)) return HIT_ADDRESS;
    if (y < TOOLBAR_H) return HIT_NONE;
    if (x >= view_w(g)) return HIT_VSCROLL;
    if (y >= TOOLBAR_H + view_h(g)) return HIT_HSCROLL;
    return HIT_CONTENT;
}

static void address_insert(struct gui_window *g, char ch)
{
    if (g->addr_fresh)
    {
        g->addr_len = g->addr_cursor = 0;
        g->addr_fresh = false;
    }
    if (g->addr_len >= URL_LEN - 1)
        return;
    memmove(g->address + g->addr_cursor + 1, g->address + g->addr_cursor, (size_t)(g->addr_len - g->addr_cursor));
    g->address[g->addr_cursor++] = ch;
    g->address[++g->addr_len] = '\0';
}

static void address_key(struct gui_window *g, const SxEvent *ev)
{
    bool ctrl = ev->mods & SX_MOD_CTRL;
    switch (ev->key)
    {
        case SX_KEY_ENTER:
        {
            char text[URL_LEN];
            snprintf(text, sizeof(text), "%s", g->address);
            navigate_text(g, text);
            return;
        }
        case SX_KEY_ESCAPE:
        {
            g->addr_focus = false;
            g->addr_fresh = false;
            window_set_url(g, browser_window_access_url(g->bw));
            return;
        }
        case SX_KEY_BACKSPACE:
            if (g->addr_fresh)
            {
                g->addr_len = g->addr_cursor = 0;
                g->address[0] = '\0';
                g->addr_fresh = false;
            }
            else if (g->addr_cursor > 0)
            {
                memmove(g->address + g->addr_cursor - 1, g->address + g->addr_cursor,
                        (size_t)(g->addr_len - g->addr_cursor + 1));
                g->addr_cursor--;
                g->addr_len--;
            }
            return;
        case SX_KEY_DELETE:
            if (g->addr_cursor < g->addr_len)
            {
                memmove(g->address + g->addr_cursor, g->address + g->addr_cursor + 1,
                        (size_t)(g->addr_len - g->addr_cursor));
                g->addr_len--;
            }
            return;
        case SX_KEY_LEFT:  g->addr_fresh = false; if (g->addr_cursor > 0) g->addr_cursor--; return;
        case SX_KEY_RIGHT: g->addr_fresh = false; if (g->addr_cursor < g->addr_len) g->addr_cursor++; return;
        case SX_KEY_HOME:  g->addr_fresh = false; g->addr_cursor = 0; return;
        case SX_KEY_END:   g->addr_fresh = false; g->addr_cursor = g->addr_len; return;
        default:
            break;
    }
    if (ctrl && (ev->key == 'a' || ev->key == 'A'))
    {
        g->addr_fresh = true;
        g->addr_cursor = g->addr_len;
        return;
    }
    if (!ctrl && ev->ch >= 32 && ev->ch < 127)
        address_insert(g, (char)ev->ch);
}

static uint32_t map_key(const SxEvent *ev)
{
    bool ctrl = ev->mods & SX_MOD_CTRL;
    bool shift = ev->mods & SX_MOD_SHIFT;
    if (ctrl)
    {
        switch (tolower(ev->key))
        {
            case 'a': return NS_KEY_SELECT_ALL;
            case 'c': return NS_KEY_COPY_SELECTION;
            case 'x': return NS_KEY_CUT_SELECTION;
            case 'v': return NS_KEY_PASTE;
            case 'z': return shift ? NS_KEY_REDO : NS_KEY_UNDO;
            case 'y': return NS_KEY_REDO;
        }
        switch (ev->key)
        {
            case SX_KEY_LEFT:      return NS_KEY_WORD_LEFT;
            case SX_KEY_RIGHT:     return NS_KEY_WORD_RIGHT;
            case SX_KEY_HOME:      return NS_KEY_TEXT_START;
            case SX_KEY_END:       return NS_KEY_TEXT_END;
            case SX_KEY_BACKSPACE: return NS_KEY_DELETE_WORD_LEFT;
            case SX_KEY_DELETE:    return NS_KEY_DELETE_WORD_RIGHT;
        }
        return 0;
    }
    switch (ev->key)
    {
        case SX_KEY_BACKSPACE: return NS_KEY_DELETE_LEFT;
        case SX_KEY_DELETE:    return NS_KEY_DELETE_RIGHT;
        case SX_KEY_ENTER:     return NS_KEY_NL;
        case SX_KEY_TAB:       return shift ? NS_KEY_SHIFT_TAB : NS_KEY_TAB;
        case SX_KEY_ESCAPE:    return NS_KEY_ESCAPE;
        case SX_KEY_LEFT:      return NS_KEY_LEFT;
        case SX_KEY_RIGHT:     return NS_KEY_RIGHT;
        case SX_KEY_UP:        return NS_KEY_UP;
        case SX_KEY_DOWN:      return NS_KEY_DOWN;
        case SX_KEY_HOME:      return NS_KEY_LINE_START;
        case SX_KEY_END:       return NS_KEY_LINE_END;
        case SX_KEY_PAGE_UP:   return NS_KEY_PAGE_UP;
        case SX_KEY_PAGE_DOWN: return NS_KEY_PAGE_DOWN;
    }
    if (ev->ch >= 32)
        return (uint32_t)ev->ch;
    return 0;
}

static void page_key(struct gui_window *g, const SxEvent *ev)
{
    uint32_t key = map_key(ev);
    if (key && browser_window_key_press(g->bw, key))
        return;

    int line = 48, page = view_h(g) - 48;
    switch (ev->key)
    {
        case SX_KEY_DOWN:      scroll_to(g, g->scroll_x, g->scroll_y + line); break;
        case SX_KEY_UP:        scroll_to(g, g->scroll_x, g->scroll_y - line); break;
        case SX_KEY_LEFT:      scroll_to(g, g->scroll_x - line, g->scroll_y); break;
        case SX_KEY_RIGHT:     scroll_to(g, g->scroll_x + line, g->scroll_y); break;
        case SX_KEY_PAGE_DOWN: scroll_to(g, g->scroll_x, g->scroll_y + page); break;
        case SX_KEY_PAGE_UP:   scroll_to(g, g->scroll_x, g->scroll_y - page); break;
        case SX_KEY_SPACE:     scroll_to(g, g->scroll_x, g->scroll_y + ((ev->mods & SX_MOD_SHIFT) ? -page : page)); break;
        case SX_KEY_HOME:      scroll_to(g, g->scroll_x, 0); break;
        case SX_KEY_END:       scroll_to(g, g->scroll_x, max_scroll_y(g)); break;
        case SX_KEY_BACKSPACE: browser_window_history_back(g->bw, false); break;
        default: break;
    }
}

static void handle_key(struct gui_window *g, const SxEvent *ev)
{
    bool ctrl = ev->mods & SX_MOD_CTRL;
    bool alt = ev->mods & SX_MOD_ALT;

    if ((ctrl && tolower(ev->key) == 'l') || ev->key == SX_KEY_F6)
    {
        g->addr_focus = true;
        g->addr_fresh = true;
        g->addr_cursor = g->addr_len;
        browser_window_remove_caret(g->bw, false);
        return;
    }
    if (alt && ev->key == SX_KEY_LEFT) { browser_window_history_back(g->bw, false); return; }
    if (alt && ev->key == SX_KEY_RIGHT) { browser_window_history_forward(g->bw, false); return; }
    if (alt && ev->key == SX_KEY_HOME) { go_home(g); return; }
    if (ev->key == SX_KEY_F5 || (ctrl && tolower(ev->key) == 'r'))
    {
        browser_window_reload(g->bw, ev->mods & SX_MOD_SHIFT);
        return;
    }
    if (ev->key == SX_KEY_ESCAPE && g->loading && !g->addr_focus)
    {
        browser_window_stop(g->bw);
        return;
    }

    if (g->addr_focus)
        address_key(g, ev);
    else
        page_key(g, ev);
}

static void scroll_from_pointer(struct gui_window *g, HitKind bar, int x, int y)
{
    if (bar == HIT_VSCROLL)
    {
        int track = view_h(g);
        int pos = y - TOOLBAR_H - g->drag_offset;
        scroll_to(g, g->scroll_x, (int)((int64_t)pos * g->extent_h / track));
    }
    else
    {
        int track = view_w(g);
        int pos = x - g->drag_offset;
        scroll_to(g, (int)((int64_t)pos * g->extent_w / track), g->scroll_y);
    }
}

static browser_mouse_state mods_state(int mods)
{
    browser_mouse_state s = 0;
    if (mods & SX_MOD_SHIFT) s |= BROWSER_MOUSE_MOD_1;
    if (mods & SX_MOD_CTRL) s |= BROWSER_MOUSE_MOD_2;
    if (mods & SX_MOD_ALT) s |= BROWSER_MOUSE_MOD_3;
    return s;
}

static void content_coords(struct gui_window *g, int x, int y, int *cx, int *cy)
{
    *cx = x + g->scroll_x;
    *cy = y - TOOLBAR_H + g->scroll_y;
}

static void mouse_down(struct gui_window *g, const SxEvent *ev)
{
    g->pressed = hit_test(g, ev->x, ev->y);
    switch (g->pressed)
    {
        case HIT_ADDRESS:
            if (!g->addr_focus)
            {
                g->addr_focus = true;
                g->addr_fresh = true;
                g->addr_cursor = g->addr_len;
                browser_window_remove_caret(g->bw, false);
            }
            else
            {
                g->addr_fresh = false;
                SxRect a = address_rect(g);
                int rel = ev->x - (a.x + 12) + g->addr_scroll;
                int k = 0;
                while (k < g->addr_len && sx_text_width_n(&sx_font_sans, g->address, k + 1) < rel)
                    k++;
                g->addr_cursor = k;
            }
            break;

        case HIT_VSCROLL:
        case HIT_HSCROLL:
        {
            g->drag_offset = 0;
            if (g->pressed == HIT_VSCROLL && max_scroll_y(g) > 0)
            {
                int track_h = view_h(g) - 8;
                int th = (int)((int64_t)track_h * view_h(g) / g->extent_h);
                if (th < 30) th = 30;
                int ty = TOOLBAR_H + 4 + (int)((int64_t)(track_h - th) * g->scroll_y / max_scroll_y(g));
                if (ev->y >= ty && ev->y < ty + th)
                    g->drag_offset = ev->y - TOOLBAR_H - (int)((int64_t)g->scroll_y * view_h(g) / g->extent_h);
                else
                    g->drag_offset = (int)((int64_t)view_h(g) * view_h(g) / g->extent_h) / 2;
            }
            else if (g->pressed == HIT_HSCROLL && max_scroll_x(g) > 0)
            {
                g->drag_offset = (int)((int64_t)view_w(g) * view_w(g) / g->extent_w) / 2;
            }
            scroll_from_pointer(g, g->pressed, ev->x, ev->y);
            break;
        }

        case HIT_CONTENT:
        {
            if (ev->button != SX_BUTTON_LEFT && ev->button != SX_BUTTON_MIDDLE)
                break;
            g->addr_focus = false;
            int cx, cy;
            content_coords(g, ev->x, ev->y, &cx, &cy);
            g->press_x = cx;
            g->press_y = cy;
            g->press_moved = false;
            g->mouse_state = (ev->button == SX_BUTTON_LEFT ? BROWSER_MOUSE_PRESS_1 : BROWSER_MOUSE_PRESS_2) |
                             mods_state(ev->mods);
            uint64_t now = sx_time_ms();
            if (now - g->last_click_ms < DOUBLE_CLICK_MS && abs(ev->x - g->last_click_x) < 5 &&
                abs(ev->y - g->last_click_y) < 5)
                g->mouse_state |= BROWSER_MOUSE_DOUBLE_CLICK;
            g->last_click_ms = now;
            g->last_click_x = ev->x;
            g->last_click_y = ev->y;
            browser_window_mouse_click(g->bw, g->mouse_state, cx, cy);
            break;
        }

        default:
            break;
    }
    request_present(g);
}

static void mouse_up(struct gui_window *g, const SxEvent *ev)
{
    HitKind released = hit_test(g, ev->x, ev->y);
    HitKind pressed = g->pressed;
    g->pressed = HIT_NONE;

    if (pressed == HIT_CONTENT && g->mouse_state)
    {
        int cx, cy;
        content_coords(g, ev->x, ev->y, &cx, &cy);
        if (g->mouse_state & BROWSER_MOUSE_PRESS_1)
            g->mouse_state ^= BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_CLICK_1;
        else if (g->mouse_state & BROWSER_MOUSE_PRESS_2)
            g->mouse_state ^= BROWSER_MOUSE_PRESS_2 | BROWSER_MOUSE_CLICK_2;

        if (g->mouse_state & (BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2))
            browser_window_mouse_click(g->bw, g->mouse_state, cx, cy);
        else
            browser_window_mouse_track(g->bw, 0, cx, cy);
        g->mouse_state = 0;
    }
    else if (released == pressed)
    {
        switch (pressed)
        {
            case HIT_BACK:    browser_window_history_back(g->bw, false); break;
            case HIT_FORWARD: browser_window_history_forward(g->bw, false); break;
            case HIT_RELOAD:  reload_or_stop(g); break;
            case HIT_HOME:    go_home(g); break;
            default: break;
        }
    }
    request_present(g);
}

static void mouse_move(struct gui_window *g, const SxEvent *ev)
{
    if ((g->pressed == HIT_VSCROLL || g->pressed == HIT_HSCROLL) && (ev->buttons & SX_BUTTON_LEFT))
    {
        scroll_from_pointer(g, g->pressed, ev->x, ev->y);
        return;
    }

    HitKind h = hit_test(g, ev->x, ev->y);
    if (h != g->hover)
    {
        g->hover = h;
        request_present(g);
    }

    if (h != HIT_CONTENT && g->pressed != HIT_CONTENT)
        return;

    int cx, cy;
    content_coords(g, ev->x, ev->y, &cx, &cy);
    if (g->mouse_state & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2))
    {
        if (abs(cx - g->press_x) < 5 && abs(cy - g->press_y) < 5 && !g->press_moved)
            return;
        g->press_moved = true;
        if (g->mouse_state & BROWSER_MOUSE_PRESS_1)
        {
            browser_window_mouse_click(g->bw, BROWSER_MOUSE_DRAG_1 | mods_state(ev->mods), g->press_x, g->press_y);
            g->mouse_state ^= BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_HOLDING_1;
        }
        else
        {
            browser_window_mouse_click(g->bw, BROWSER_MOUSE_DRAG_2 | mods_state(ev->mods), g->press_x, g->press_y);
            g->mouse_state ^= BROWSER_MOUSE_PRESS_2 | BROWSER_MOUSE_HOLDING_2;
        }
        g->mouse_state |= BROWSER_MOUSE_DRAG_ON;
    }
    browser_window_mouse_track(g->bw, g->mouse_state, cx, cy);
}

static void mouse_wheel(struct gui_window *g, const SxEvent *ev)
{
    int cx, cy;
    content_coords(g, ev->x, ev->y, &cx, &cy);
    int dy = -ev->wheel * 60;
    bool horizontal = ev->mods & SX_MOD_SHIFT;
    if (browser_window_scroll_at_point(g->bw, cx, cy, horizontal ? dy : 0, horizontal ? 0 : dy))
        return;
    if (horizontal)
        scroll_to(g, g->scroll_x + dy, g->scroll_y);
    else
        scroll_to(g, g->scroll_x, g->scroll_y + dy);
}

void nsx_windows_handle_event(const SxEvent *ev)
{
    struct gui_window *g = ev->window ? ev->window->user : NULL;
    if (!g)
        return;

    switch (ev->type)
    {
        case SX_EVENT_CLOSE:
            browser_window_destroy(g->bw);
            break;
        case SX_EVENT_RESIZE:
            ensure_store(g);
            browser_window_schedule_reformat(g->bw);
            request_present(g);
            break;
        case SX_EVENT_MOUSE_MOVE:
            mouse_move(g, ev);
            break;
        case SX_EVENT_MOUSE_LEAVE:
            g->hover = HIT_NONE;
            request_present(g);
            break;
        case SX_EVENT_MOUSE_DOWN:
            mouse_down(g, ev);
            break;
        case SX_EVENT_MOUSE_UP:
            mouse_up(g, ev);
            break;
        case SX_EVENT_MOUSE_WHEEL:
            mouse_wheel(g, ev);
            break;
        case SX_EVENT_KEY_DOWN:
            handle_key(g, ev);
            request_present(g);
            break;
        default:
            break;
    }
}

void nsx_windows_disconnect(void)
{
    while (windows)
        browser_window_destroy(windows->bw);
}
