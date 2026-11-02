#include <stdlib.h>
#include <string.h>
#include <strawx.h>

#include "screen.h"
#include "vp8.h"

struct Screen
{
    int       max_w, max_h;
    int       sw, sh;
    int       w, h;
    uint32_t *cur;
    uint32_t *last;
    size_t    cap;
    bool      have_last;
    Vp8Enc   *enc;
    int      *xmap;
    int      *ymap;
};

static void fit(int sw, int sh, int max_w, int max_h, int *w, int *h)
{
    int64_t tw = sw, th = sh;
    if (tw > max_w)
    {
        th = th * max_w / tw;
        tw = max_w;
    }
    if (th > max_h)
    {
        tw = tw * max_h / th;
        th = max_h;
    }
    *w = (int)tw & ~1;
    *h = (int)th & ~1;
}

static bool setup(Screen *s, int sw, int sh)
{
    free(s->cur);
    free(s->last);
    free(s->xmap);
    free(s->ymap);
    vp8_free(s->enc);
    s->cur = s->last = NULL;
    s->xmap = s->ymap = NULL;
    s->enc = NULL;
    s->have_last = false;

    s->sw = sw;
    s->sh = sh;
    fit(sw, sh, s->max_w, s->max_h, &s->w, &s->h);
    s->cap = (size_t)sw * (size_t)sh;
    s->cur = malloc(s->cap * 4);
    s->last = malloc(s->cap * 4);
    s->xmap = malloc(sizeof(int) * (size_t)(s->w + 1));
    s->ymap = malloc(sizeof(int) * (size_t)(s->h + 1));
    s->enc = vp8_new(s->w, s->h);
    if (!s->cur || !s->last || !s->xmap || !s->ymap || !s->enc)
        return false;
    for (int x = 0; x <= s->w; x++)
        s->xmap[x] = (int)((int64_t)x * sw / s->w);
    for (int y = 0; y <= s->h; y++)
        s->ymap[y] = (int)((int64_t)y * sh / s->h);
    return true;
}

Screen *screen_open(int max_w, int max_h)
{
    int sw = 0, sh = 0;
    sx_screen_capture(NULL, 0, &sw, &sh);
    if (sw < 16 || sh < 16)
        return NULL;
    Screen *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->max_w = max_w;
    s->max_h = max_h;
    if (!setup(s, sw, sh))
    {
        screen_close(s);
        return NULL;
    }
    return s;
}

void screen_close(Screen *s)
{
    if (!s)
        return;
    free(s->cur);
    free(s->last);
    free(s->xmap);
    free(s->ymap);
    vp8_free(s->enc);
    free(s);
}

int screen_width(const Screen *s)
{
    return s->w;
}

int screen_height(const Screen *s)
{
    return s->h;
}

static uint32_t sample(const Screen *s, const uint32_t *px, int x, int y)
{
    if (s->w == s->sw && s->h == s->sh)
        return px[(size_t)y * s->sw + x];
    int x0 = s->xmap[x], x1 = s->xmap[x + 1], y0 = s->ymap[y], y1 = s->ymap[y + 1];
    if (x1 <= x0)
        x1 = x0 + 1;
    if (y1 <= y0)
        y1 = y0 + 1;
    uint32_t r = 0, g = 0, b = 0, n = 0;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            uint32_t p = px[(size_t)yy * s->sw + xx];
            r += (p >> 16) & 0xFF;
            g += (p >> 8) & 0xFF;
            b += p & 0xFF;
            n++;
        }
    return (r / n) << 16 | (g / n) << 8 | (b / n);
}

static void convert(Screen *s, const uint32_t *px)
{
    Vp8Planes p = vp8_input(s->enc);
    for (int y = 0; y < s->h; y += 2)
    {
        uint8_t *y0 = p.y + (size_t)y * p.y_stride, *y1 = y0 + p.y_stride;
        uint8_t *u = p.u + (size_t)(y / 2) * p.uv_stride, *v = p.v + (size_t)(y / 2) * p.uv_stride;
        for (int x = 0; x < s->w; x += 2)
        {
            uint32_t q[4] = { sample(s, px, x, y), sample(s, px, x + 1, y), sample(s, px, x, y + 1),
                              sample(s, px, x + 1, y + 1) };
            int rs = 0, gs = 0, bs = 0;
            for (int i = 0; i < 4; i++)
            {
                int r = (q[i] >> 16) & 0xFF, g = (q[i] >> 8) & 0xFF, b = q[i] & 0xFF;
                uint8_t yv = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                (i < 2 ? y0 : y1)[x + (i & 1)] = yv;
                rs += r;
                gs += g;
                bs += b;
            }
            rs = (rs + 2) >> 2;
            gs = (gs + 2) >> 2;
            bs = (bs + 2) >> 2;
            u[x / 2] = (uint8_t)(((-38 * rs - 74 * gs + 112 * bs + 128) >> 8) + 128);
            v[x / 2] = (uint8_t)(((112 * rs - 94 * gs - 18 * bs + 128) >> 8) + 128);
        }
    }
}

int screen_grab(Screen *s)
{
    int sw = 0, sh = 0;
    if (!sx_screen_capture(s->cur, s->cap, &sw, &sh))
    {
        if (sw < 16 || sh < 16 || (sw == s->sw && sh == s->sh) || !setup(s, sw, sh))
            return -1;
        if (!sx_screen_capture(s->cur, s->cap, &sw, &sh))
            return -1;
    }
    else if (sw != s->sw || sh != s->sh)
    {
        if (!setup(s, sw, sh) || !sx_screen_capture(s->cur, s->cap, &sw, &sh))
            return -1;
    }
    if (s->have_last && memcmp(s->cur, s->last, s->cap * 4) == 0)
        return 0;
    uint32_t *t = s->last;
    s->last = s->cur;
    s->cur = t;
    s->have_last = true;
    convert(s, s->last);
    return 1;
}

size_t screen_encode(Screen *s, bool key, int q, uint8_t *out, size_t cap)
{
    if (!s->have_last)
        return 0;
    return vp8_encode(s->enc, key, q, out, cap);
}
