#include <stdlib.h>
#include <string.h>
#include <math.h>


#include "discord.h"

#define IMG_SLOTS     384
#define IMG_MAX_SIDE  4096

typedef struct
{
    char     *url;
    int       w, h;
    ImgState  state;
    SxImage  *img;
    uint64_t  used;
}
ImgEntry;

static ImgEntry entries[IMG_SLOTS];
static bool     changed;
static uint64_t clock_tick;

static SxImage *decode(const uint8_t *data, size_t len, int w, int h)
{
    int iw, ih;
    if (!sx_image_info(data, len, &iw, &ih) || iw > IMG_MAX_SIDE || ih > IMG_MAX_SIDE)
        return NULL;
    SxImage *img = sx_image_decode(data, len);
    if (img && w > 0 && h > 0 && (w != img->width || h != img->height))
    {
        SxImage *scaled = sx_image_scale(img, w, h);
        if (scaled)
        {
            sx_image_free(img);
            img = scaled;
        }
    }
    return img;
}

static void on_loaded(int status, const uint8_t *body, size_t len, void *user)
{
    ImgEntry *e = user;
    if (e->state != IMG_LOADING)
        return;
    SxImage *img = status >= 200 && status < 300 ? decode(body, len, e->w, e->h) : NULL;
    e->img = img;
    e->state = img ? IMG_READY : IMG_FAILED;
    changed = true;
}

static void entry_clear(ImgEntry *e)
{
    if (e->state == IMG_LOADING)
        http_cancel(e);
    free(e->url);
    if (e->img)
        sx_image_free(e->img);
    memset(e, 0, sizeof(*e));
}

ImgState img_request(const char *url, int w, int h, SxImage **out)
{
    *out = NULL;
    if (!url || !*url)
        return IMG_FAILED;
    clock_tick++;
    ImgEntry *victim = NULL;
    for (int i = 0; i < IMG_SLOTS; i++)
    {
        ImgEntry *e = &entries[i];
        if (e->url && e->w == w && e->h == h && strcmp(e->url, url) == 0)
        {
            e->used = clock_tick;
            *out = e->img;
            return e->state;
        }
        if (!victim || (!e->url && victim->url) || (e->url && victim->url && e->used < victim->used))
            victim = e;
    }
    entry_clear(victim);
    victim->url = xstrdup(url);
    victim->w = w;
    victim->h = h;
    victim->used = clock_tick;
    victim->state = IMG_LOADING;
    if (!http_request("GET", url, NULL, NULL, 0, on_loaded, victim))
        victim->state = IMG_FAILED;
    return victim->state;
}

bool img_take_changed(void)
{
    bool c = changed;
    changed = false;
    return c;
}

static void blend_px(uint32_t *dst, uint32_t src, unsigned cover)
{
    unsigned a = SX_ALPHA(src) * cover / 255;
    if (a == 0)
        return;
    if (a >= 255)
    {
        *dst = src | 0xFF000000u;
        return;
    }
    uint32_t d = *dst;
    unsigned ia = 255 - a;
    unsigned r = (((src >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
    unsigned g = (((src >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
    unsigned b = ((src & 0xFF) * a + (d & 0xFF) * ia) / 255;
    *dst = 0xFF000000u | r << 16 | g << 8 | b;
}

static unsigned corner_cover(int x, int y, int w, int h, int r)
{
    if (r <= 0)
        return 255;
    float cx, cy;
    if (x < r)
        cx = (float)r - 0.5f;
    else if (x >= w - r)
        cx = (float)(w - r) - 0.5f;
    else
        return 255;
    if (y < r)
        cy = (float)r - 0.5f;
    else if (y >= h - r)
        cy = (float)(h - r) - 0.5f;
    else
        return 255;
    float dx = (float)x - cx;
    float dy = (float)y - cy;
    float d = sqrtf(dx * dx + dy * dy);
    float cover = (float)r - d + 0.5f;
    if (cover <= 0)
        return 0;
    if (cover >= 1)
        return 255;
    return (unsigned)(cover * 255);
}

void img_draw(SxCanvas *c, const SxImage *img, int x, int y, int radius)
{
    if (!img)
        return;
    SxRect clip = c->clip;
    for (int row = 0; row < img->height; row++)
    {
        int yy = y + row;
        if (yy < clip.y || yy >= clip.y + clip.h)
            continue;
        uint32_t *line = c->pixels + (size_t)yy * c->stride;
        const uint32_t *src = img->pixels + (size_t)row * img->width;
        for (int col = 0; col < img->width; col++)
        {
            int xx = x + col;
            if (xx < clip.x || xx >= clip.x + clip.w)
                continue;
            unsigned cover = corner_cover(col, row, img->width, img->height, radius);
            if (cover)
                blend_px(&line[xx], src[col], cover);
        }
    }
}

void img_draw_circle(SxCanvas *c, const SxImage *img, int x, int y, int d)
{
    if (!img || img->width != d || img->height != d)
    {
        if (img)
        {
            SxImage *scaled = sx_image_scale(img, d, d);
            if (scaled)
            {
                img_draw(c, scaled, x, y, d / 2);
                sx_image_free(scaled);
            }
        }
        return;
    }
    img_draw(c, img, x, y, d / 2);
}
