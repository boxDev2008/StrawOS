#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strawx.h>

#include "watch.h"

#define BG SX_RGB(0x11, 0x12, 0x14)

struct Watch
{
    SxWindow *win;
    char      status[160];
    uint32_t *frame;
    int       fw, fh;
    int      *xmap;
    int       xmap_len;
};

static uint8_t clamp255(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void draw(Watch *w)
{
    SxCanvas *c = sx_window_canvas(w->win);
    int cw = c->width, ch = c->height;
    sx_fill_rect(c, 0, 0, cw, ch, BG);
    if (!w->frame)
    {
        sx_draw_text_centered(c, &sx_font_sans, sx_rect(0, 0, cw, ch), w->status, SX_RGB(0xB5, 0xBA, 0xC1));
        sx_window_commit(w->win);
        return;
    }
    int dw = cw, dh = (int)((int64_t)cw * w->fh / w->fw);
    if (dh > ch)
    {
        dh = ch;
        dw = (int)((int64_t)ch * w->fw / w->fh);
    }
    if (dw < 1 || dh < 1)
    {
        sx_window_commit(w->win);
        return;
    }
    int ox = (cw - dw) / 2, oy = (ch - dh) / 2;
    if (w->xmap_len < dw)
    {
        free(w->xmap);
        w->xmap = malloc(sizeof(int) * (size_t)dw);
        w->xmap_len = w->xmap ? dw : 0;
        if (!w->xmap)
            return;
    }
    for (int x = 0; x < dw; x++)
        w->xmap[x] = (int)((int64_t)x * w->fw / dw);
    for (int y = 0; y < dh; y++)
    {
        const uint32_t *src = w->frame + (size_t)((int64_t)y * w->fh / dh) * (size_t)w->fw;
        uint32_t *dst = c->pixels + (size_t)(oy + y) * (size_t)c->stride + ox;
        for (int x = 0; x < dw; x++)
            dst[x] = src[w->xmap[x]];
    }
    sx_window_commit(w->win);
}

Watch *watch_open(const char *title)
{
    if (sx_connect() != 0)
        return NULL;
    Watch *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->win = sx_window_create(title, 960, 560, SX_WINDOW_RESIZABLE);
    if (!w->win)
    {
        free(w);
        return NULL;
    }
    snprintf(w->status, sizeof(w->status), "Connecting to the stream...");
    draw(w);
    return w;
}

void watch_close(Watch *w)
{
    if (!w)
        return;
    sx_window_destroy(w->win);
    free(w->frame);
    free(w->xmap);
    free(w);
}

bool watch_pump(Watch *w)
{
    bool redraw = false;
    SxEvent ev;
    int rc;
    while ((rc = sx_next_event(&ev, 0)) != 0)
    {
        if (rc < 0)
            return false;
        if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
            return false;
        if (ev.type == SX_EVENT_RESIZE)
            redraw = true;
    }
    if (redraw)
        draw(w);
    return true;
}

void watch_status(Watch *w, const char *msg)
{
    snprintf(w->status, sizeof(w->status), "%s", msg);
    free(w->frame);
    w->frame = NULL;
    draw(w);
}

void watch_frame(Watch *w, const VideoImage *img)
{
    if (img->width != w->fw || img->height != w->fh || !w->frame)
    {
        free(w->frame);
        w->frame = malloc((size_t)img->width * (size_t)img->height * 4);
        if (!w->frame)
            return;
        w->fw = img->width;
        w->fh = img->height;
    }
    for (int y = 0; y < img->height; y++)
    {
        const uint8_t *yp = img->y + (size_t)y * img->y_stride;
        const uint8_t *up = img->u + (size_t)(y / 2) * img->uv_stride;
        const uint8_t *vp = img->v + (size_t)(y / 2) * img->uv_stride;
        uint32_t *out = w->frame + (size_t)y * img->width;
        for (int x = 0; x < img->width; x++)
        {
            int c = 298 * (yp[x] - 16), d = up[x / 2] - 128, e = vp[x / 2] - 128;
            out[x] = (uint32_t)clamp255((c + 409 * e + 128) >> 8) << 16 |
                     (uint32_t)clamp255((c - 100 * d - 208 * e + 128) >> 8) << 8 |
                     clamp255((c + 516 * d + 128) >> 8);
        }
    }
    draw(w);
}
