#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strawx.h>
#include <ui.h>

#define TOOLBAR_H 52

static const uint32_t palette[] = {
    SX_RGB(0x1E, 0x20, 0x27), SX_RGB(0xFF, 0xFF, 0xFF), SX_RGB(0x9A, 0xA0, 0xAD),
    SX_RGB(0xE5, 0x48, 0x4D), SX_RGB(0xFF, 0x8A, 0x3D), SX_RGB(0xF0, 0xB4, 0x3C),
    SX_RGB(0x46, 0xC2, 0x7A), SX_RGB(0x2E, 0xB8, 0xA6), SX_RGB(0x5B, 0x8C, 0xFF),
    SX_RGB(0xC0, 0x5C, 0xF0), SX_RGB(0xFF, 0x6B, 0xB5), SX_RGB(0x8B, 0x5E, 0x3C),
};
#define PALETTE_N ((int)(sizeof(palette) / sizeof(palette[0])))

static const float sizes[] = { 2.0f, 5.0f, 10.0f, 20.0f };
#define SIZES_N ((int)(sizeof(sizes) / sizeof(sizes[0])))

static struct
{
    SxWindow *win;

    uint32_t *pic;
    int       pic_w, pic_h;
    SxCanvas  pic_canvas;

    int   color;
    int   size;
    bool  eraser;

    bool  drawing;
    float last_x, last_y;

    int   hover;
    SxRect dirty;
}
P;

enum { ITEM_NONE = -1, ITEM_ERASER = 100, ITEM_CLEAR = 101, ITEM_SIZE0 = 200 };

static SxRect swatch_rect(int i)
{
    return sx_rect(12 + i * 30, (TOOLBAR_H - 24) / 2, 24, 24);
}

static SxRect size_rect(int i)
{
    int x0 = 12 + PALETTE_N * 30 + 14;
    return sx_rect(x0 + i * 32, (TOOLBAR_H - 30) / 2, 30, 30);
}

static SxRect eraser_rect(void)
{
    SxRect last = size_rect(SIZES_N - 1);
    return sx_rect(last.x + last.w + 14, (TOOLBAR_H - 30) / 2, 70, 30);
}

static SxRect clear_rect(void)
{
    SxRect e = eraser_rect();
    return sx_rect(e.x + e.w + 8, e.y, 64, 30);
}

static int toolbar_hit(int x, int y)
{
    for (int i = 0; i < PALETTE_N; i++)
        if (sx_rect_contains(swatch_rect(i), x, y))
            return i;
    for (int i = 0; i < SIZES_N; i++)
        if (sx_rect_contains(size_rect(i), x, y))
            return ITEM_SIZE0 + i;
    if (sx_rect_contains(eraser_rect(), x, y))
        return ITEM_ERASER;
    if (sx_rect_contains(clear_rect(), x, y))
        return ITEM_CLEAR;
    return ITEM_NONE;
}

static void picture_resize(int w, int h)
{
    if (w <= P.pic_w && h <= P.pic_h)
        return;

    int nw = w > P.pic_w ? w : P.pic_w;
    int nh = h > P.pic_h ? h : P.pic_h;
    uint32_t *n = malloc((size_t)nw * (size_t)nh * 4);
    if (!n)
        return;

    for (int i = 0; i < nw * nh; i++)
        n[i] = 0xFFFFFFFF;
    for (int y = 0; y < P.pic_h; y++)
        memcpy(n + y * nw, P.pic + y * P.pic_w, (size_t)P.pic_w * 4);

    free(P.pic);
    P.pic = n;
    P.pic_w = nw;
    P.pic_h = nh;
    sx_canvas_init(&P.pic_canvas, P.pic, nw, nh, nw);
}

static void draw_toolbar(SxCanvas *c)
{
    sx_fill_rect(c, 0, 0, c->width, TOOLBAR_H, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, TOOLBAR_H - 1, c->width, 1, SX_RGBA(0, 0, 0, 90));

    for (int i = 0; i < PALETTE_N; i++)
    {
        SxRect r = swatch_rect(i);
        bool selected = !P.eraser && P.color == i;
        if (selected)
            sx_draw_rounded_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 9, SX_THEME_ACCENT);
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 7, palette[i]);
        sx_draw_rounded_rect(c, r.x, r.y, r.w, r.h, 7, SX_RGBA(255, 255, 255, P.hover == i ? 90 : 30));
    }

    for (int i = 0; i < SIZES_N; i++)
    {
        SxRect r = size_rect(i);
        bool selected = P.size == i;
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8,
                             selected ? SX_THEME_SURFACE_HI : (P.hover == ITEM_SIZE0 + i ? SX_RGB(0x2D, 0x31, 0x3B) : SX_THEME_SURFACE));
        if (selected)
            sx_draw_rounded_rect(c, r.x, r.y, r.w, r.h, 8, SX_RGBA(255, 255, 255, 40));
        float d = sizes[i] < 18 ? sizes[i] : 18;
        sx_fill_circle(c, r.x + r.w * 0.5f, r.y + r.h * 0.5f, d * 0.5f + 0.5f, SX_THEME_TEXT);
    }

    SxRect e = eraser_rect();
    sx_ui_button(c, e, "Eraser", &sx_font_sans,
                 P.hover == ITEM_ERASER ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL,
                 P.eraser ? SX_THEME_ACCENT : SX_THEME_SURFACE_HI,
                 P.eraser ? SX_THEME_ACCENT_TEXT : SX_THEME_TEXT);

    sx_ui_button(c, clear_rect(), "Clear", &sx_font_sans,
                 P.hover == ITEM_CLEAR ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL,
                 SX_THEME_SURFACE_HI, SX_THEME_TEXT);
}

static void present(SxRect r)
{
    SxCanvas *c = sx_window_canvas(P.win);
    r = sx_rect_intersect(r, sx_rect(0, TOOLBAR_H, c->width, c->height - TOOLBAR_H));
    if (sx_rect_empty(r))
        return;

    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(c->pixels + y * c->stride + r.x, P.pic + (y - TOOLBAR_H) * P.pic_w + r.x, (size_t)r.w * 4);
    sx_window_damage(P.win, r.x, r.y, r.w, r.h);
}

static void draw_all(void)
{
    P.dirty = sx_rect(0, 0, 0, 0);
    SxCanvas *c = sx_window_canvas(P.win);
    picture_resize(c->width, c->height - TOOLBAR_H);
    draw_toolbar(c);
    for (int y = TOOLBAR_H; y < c->height; y++)
        memcpy(c->pixels + y * c->stride, P.pic + (y - TOOLBAR_H) * P.pic_w, (size_t)c->width * 4);
    sx_window_commit(P.win);
}

static void redraw_toolbar(void)
{
    SxCanvas *c = sx_window_canvas(P.win);
    draw_toolbar(c);
    sx_window_damage(P.win, 0, 0, c->width, TOOLBAR_H);
}

static void stroke_to(float x, float y)
{
    float th = sizes[P.size] * (P.eraser ? 2.5f : 1.0f);
    uint32_t color = P.eraser ? 0xFFFFFFFF : palette[P.color];

    float py0 = P.last_y - TOOLBAR_H, py1 = y - TOOLBAR_H;
    sx_draw_line(&P.pic_canvas, P.last_x, py0, x, py1, th, color);

    int pad = (int)th + 2;
    int x0 = (int)(P.last_x < x ? P.last_x : x) - pad;
    int y0 = (int)(P.last_y < y ? P.last_y : y) - pad;
    int x1 = (int)(P.last_x > x ? P.last_x : x) + pad;
    int y1 = (int)(P.last_y > y ? P.last_y : y) + pad;
    P.dirty = sx_rect_union(P.dirty, sx_rect(x0, y0, x1 - x0, y1 - y0));

    P.last_x = x;
    P.last_y = y;
}

static void toolbar_click(int item)
{
    if (item >= 0 && item < PALETTE_N)
    {
        P.color = item;
        P.eraser = false;
    }
    else if (item >= ITEM_SIZE0 && item < ITEM_SIZE0 + SIZES_N)
    {
        P.size = item - ITEM_SIZE0;
    }
    else if (item == ITEM_ERASER)
    {
        P.eraser = !P.eraser;
    }
    else if (item == ITEM_CLEAR)
    {
        for (int i = 0; i < P.pic_w * P.pic_h; i++)
            P.pic[i] = 0xFFFFFFFF;
        draw_all();
        return;
    }
    redraw_toolbar();
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("paint: cannot connect to the display server\n");
        return 1;
    }

    P.win = sx_window_create("Paint", 720, 500, SX_WINDOW_RESIZABLE);
    if (!P.win)
        return 1;

    P.color = 8;
    P.size = 1;
    P.hover = ITEM_NONE;
    draw_all();

    SxEvent ev;
    while (sx_next_event(&ev, -1) > 0)
    {
        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                draw_all();
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.y < TOOLBAR_H)
                {
                    toolbar_click(toolbar_hit(ev.x, ev.y));
                }
                else if (ev.button == SX_BUTTON_LEFT || ev.button == SX_BUTTON_RIGHT)
                {
                    bool was_eraser = P.eraser;
                    if (ev.button == SX_BUTTON_RIGHT)
                        P.eraser = true;
                    P.drawing = true;
                    P.last_x = ev.x + 0.5f;
                    P.last_y = ev.y + 0.5f;
                    stroke_to(ev.x + 0.5f, ev.y + 0.5f);
                    if (ev.button == SX_BUTTON_RIGHT)
                        P.eraser = was_eraser;
                }
                break;

            case SX_EVENT_MOUSE_MOVE:
                if (P.drawing)
                {
                    bool was_eraser = P.eraser;
                    if (ev.buttons & SX_BUTTON_RIGHT)
                        P.eraser = true;
                    stroke_to(ev.x + 0.5f, ev.y + 0.5f);
                    P.eraser = was_eraser;
                }
                else
                {
                    int hover = ev.y < TOOLBAR_H ? toolbar_hit(ev.x, ev.y) : ITEM_NONE;
                    if (hover != P.hover)
                    {
                        P.hover = hover;
                        redraw_toolbar();
                    }
                }
                break;

            case SX_EVENT_MOUSE_UP:
                if (!(ev.buttons & (SX_BUTTON_LEFT | SX_BUTTON_RIGHT)))
                    P.drawing = false;
                break;

            case SX_EVENT_MOUSE_LEAVE:
                if (P.hover != ITEM_NONE)
                {
                    P.hover = ITEM_NONE;
                    redraw_toolbar();
                }
                break;

            case SX_EVENT_KEY_DOWN:
                if (ev.key == 'e')
                    toolbar_click(ITEM_ERASER);
                else if (ev.key >= '1' && ev.key <= '4')
                    toolbar_click(ITEM_SIZE0 + ev.key - '1');
                break;

            default:
                break;
        }

        if (!sx_rect_empty(P.dirty) && !sx_pending())
        {
            present(P.dirty);
            P.dirty = sx_rect(0, 0, 0, 0);
        }
    }

out:
    sx_window_destroy(P.win);
    sx_disconnect();
    return 0;
}
