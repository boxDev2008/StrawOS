#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "utils/errors.h"

#include "utils/utils.h"
#include "netsurf/types.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"

#include "nsx.h"

#define MAX_POLY_POINTS 4096

uint32_t nsx_colour(uint32_t ns)
{
    uint32_t alpha = 255 - ((ns >> 24) & 0xFF);
    return (alpha << 24) | ((ns & 0xFF) << 16) | (ns & 0xFF00) | ((ns >> 16) & 0xFF);
}

static SxCanvas *canvas_of(const struct redraw_context *ctx)
{
    return ctx->priv;
}

static void blend_span(SxCanvas *c, int x0, int x1, int y, uint32_t color)
{
    SxRect clip = c->clip;
    if (y < clip.y || y >= clip.y + clip.h)
        return;
    if (x0 < clip.x)
        x0 = clip.x;
    if (x1 > clip.x + clip.w)
        x1 = clip.x + clip.w;
    if (x0 >= x1)
        return;
    uint32_t a = color >> 24;
    uint32_t *row = c->pixels + y * c->stride;
    if (a >= 255)
    {
        for (int x = x0; x < x1; x++)
            row[x] = color;
        return;
    }
    for (int x = x0; x < x1; x++)
        row[x] = sx_blend(row[x], color, a);
}

static void fill_box(SxCanvas *c, int x0, int y0, int x1, int y1, uint32_t color)
{
    if ((color >> 24) == 0)
        return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    SxRect clip = c->clip;
    if (y0 < clip.y)
        y0 = clip.y;
    if (y1 > clip.y + clip.h)
        y1 = clip.y + clip.h;
    for (int y = y0; y < y1; y++)
        blend_span(c, x0, x1, y, color);
}

static int stroke_px(const plot_style_t *style)
{
    int w = plot_style_fixed_to_int(style->stroke_width);
    return w < 1 ? 1 : w;
}

static void dashed_hline(SxCanvas *c, int x0, int x1, int y, int width, plot_operation_type_t type, uint32_t color)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    int on = type == PLOT_OP_TYPE_DOT ? width : width * 3;
    int off = type == PLOT_OP_TYPE_DOT ? width : width * 2;
    for (int x = x0; x < x1; x += on + off)
        fill_box(c, x, y, x + on < x1 ? x + on : x1, y + width, color);
}

static void dashed_vline(SxCanvas *c, int x, int y0, int y1, int width, plot_operation_type_t type, uint32_t color)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int on = type == PLOT_OP_TYPE_DOT ? width : width * 3;
    int off = type == PLOT_OP_TYPE_DOT ? width : width * 2;
    for (int y = y0; y < y1; y += on + off)
        fill_box(c, x, y, x + width, y + on < y1 ? y + on : y1, color);
}

static void stroke_segment(SxCanvas *c, float x0, float y0, float x1, float y1, int width,
                           plot_operation_type_t type, uint32_t color)
{
    if ((color >> 24) == 0)
        return;
    if (y0 == y1)
    {
        int y = (int)y0 - width / 2;
        if (type == PLOT_OP_TYPE_SOLID)
            fill_box(c, (int)x0, y, (int)x1 + (x1 >= x0 ? 0 : 0), y + width, color);
        else
            dashed_hline(c, (int)x0, (int)x1, y, width, type, color);
        return;
    }
    if (x0 == x1)
    {
        int x = (int)x0 - width / 2;
        if (type == PLOT_OP_TYPE_SOLID)
            fill_box(c, x, (int)y0, x + width, (int)y1, color);
        else
            dashed_vline(c, x, (int)y0, (int)y1, width, type, color);
        return;
    }
    sx_draw_line(c, x0, y0, x1, y1, (float)width, color);
}

static nserror plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
    SxCanvas *c = canvas_of(ctx);
    sx_canvas_set_clip(c, sx_rect(clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0));
    return NSERROR_OK;
}

static nserror plot_arc(const struct redraw_context *ctx, const plot_style_t *style,
                        int x, int y, int radius, int angle1, int angle2)
{
    SxCanvas *c = canvas_of(ctx);
    uint32_t color = nsx_colour(style->fill_colour);
    if (angle2 < angle1)
        angle2 += 360;
    int steps = (angle2 - angle1) / 6 + 2;
    float px = x + radius * cosf(angle1 * (float)M_PI / 180.0f);
    float py = y - radius * sinf(angle1 * (float)M_PI / 180.0f);
    for (int i = 1; i <= steps; i++)
    {
        float a = (angle1 + (angle2 - angle1) * (float)i / steps) * (float)M_PI / 180.0f;
        float nx = x + radius * cosf(a);
        float ny = y - radius * sinf(a);
        sx_draw_line(c, px, py, nx, ny, 1.0f, color);
        px = nx;
        py = ny;
    }
    return NSERROR_OK;
}

static nserror plot_disc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius)
{
    SxCanvas *c = canvas_of(ctx);
    if (style->fill_type != PLOT_OP_TYPE_NONE)
        sx_fill_circle(c, (float)x, (float)y, (float)radius, nsx_colour(style->fill_colour));
    if (style->stroke_type != PLOT_OP_TYPE_NONE)
        sx_draw_circle(c, (float)x, (float)y, (float)radius, (float)stroke_px(style), nsx_colour(style->stroke_colour));
    return NSERROR_OK;
}

static nserror plot_line(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *line)
{
    SxCanvas *c = canvas_of(ctx);
    if (style->stroke_type == PLOT_OP_TYPE_NONE)
        return NSERROR_OK;
    stroke_segment(c, (float)line->x0, (float)line->y0, (float)line->x1, (float)line->y1, stroke_px(style),
                   style->stroke_type, nsx_colour(style->stroke_colour));
    return NSERROR_OK;
}

static nserror plot_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *r)
{
    SxCanvas *c = canvas_of(ctx);
    if (style->fill_type != PLOT_OP_TYPE_NONE)
        fill_box(c, r->x0, r->y0, r->x1, r->y1, nsx_colour(style->fill_colour));

    if (style->stroke_type != PLOT_OP_TYPE_NONE)
    {
        uint32_t color = nsx_colour(style->stroke_colour);
        int w = stroke_px(style);
        plot_operation_type_t t = style->stroke_type;
        if (t == PLOT_OP_TYPE_SOLID)
        {
            fill_box(c, r->x0, r->y0, r->x1, r->y0 + w, color);
            fill_box(c, r->x0, r->y1 - w + 1, r->x1 + 1, r->y1 + 1, color);
            fill_box(c, r->x0, r->y0, r->x0 + w, r->y1, color);
            fill_box(c, r->x1 - w + 1, r->y0, r->x1 + 1, r->y1, color);
        }
        else
        {
            dashed_hline(c, r->x0, r->x1, r->y0, w, t, color);
            dashed_hline(c, r->x0, r->x1, r->y1 - w + 1, w, t, color);
            dashed_vline(c, r->x0, r->y0, r->y1, w, t, color);
            dashed_vline(c, r->x1 - w + 1, r->y0, r->y1, w, t, color);
        }
    }
    return NSERROR_OK;
}

static int cmp_float(const void *a, const void *b)
{
    float fa = *(const float *)a, fb = *(const float *)b;
    return fa < fb ? -1 : fa > fb;
}

static void fill_polygons(SxCanvas *c, const float *xy, const int *counts, int polys, uint32_t color)
{
    if ((color >> 24) == 0)
        return;

    float miny = 1e9f, maxy = -1e9f;
    int total = 0;
    for (int p = 0; p < polys; p++)
        total += counts[p];
    for (int i = 0; i < total; i++)
    {
        if (xy[i * 2 + 1] < miny) miny = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > maxy) maxy = xy[i * 2 + 1];
    }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (y0 < c->clip.y)
        y0 = c->clip.y;
    if (y1 > c->clip.y + c->clip.h)
        y1 = c->clip.y + c->clip.h;

    float *xs = malloc(sizeof(float) * (size_t)(total + 2));
    if (!xs)
        return;

    for (int y = y0; y < y1; y++)
    {
        float sy = y + 0.5f;
        int n = 0, base = 0;
        for (int p = 0; p < polys; p++)
        {
            int cnt = counts[p];
            for (int i = 0; i < cnt; i++)
            {
                const float *a = &xy[(base + i) * 2];
                const float *b = &xy[(base + (i + 1) % cnt) * 2];
                if ((a[1] <= sy && b[1] > sy) || (b[1] <= sy && a[1] > sy))
                    xs[n++] = a[0] + (sy - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
            }
            base += cnt;
        }
        qsort(xs, (size_t)n, sizeof(float), cmp_float);
        for (int i = 0; i + 1 < n; i += 2)
            blend_span(c, (int)lroundf(xs[i]), (int)lroundf(xs[i + 1]), y, color);
    }
    free(xs);
}

static nserror plot_polygon(const struct redraw_context *ctx, const plot_style_t *style, const int *p, unsigned int n)
{
    SxCanvas *c = canvas_of(ctx);
    if (n < 3 || n > MAX_POLY_POINTS)
        return NSERROR_OK;
    float *xy = malloc(sizeof(float) * n * 2);
    if (!xy)
        return NSERROR_NOMEM;
    for (unsigned int i = 0; i < n * 2; i++)
        xy[i] = (float)p[i];
    int count = (int)n;
    if (style->fill_type != PLOT_OP_TYPE_NONE)
        fill_polygons(c, xy, &count, 1, nsx_colour(style->fill_colour));
    if (style->stroke_type != PLOT_OP_TYPE_NONE)
        for (unsigned int i = 0; i < n; i++)
            sx_draw_line(c, xy[i * 2], xy[i * 2 + 1], xy[((i + 1) % n) * 2], xy[((i + 1) % n) * 2 + 1],
                         (float)stroke_px(style), nsx_colour(style->stroke_colour));
    free(xy);
    return NSERROR_OK;
}

typedef struct
{
    float *xy;
    int   *counts;
    int    points, cap;
    int    polys, poly_cap;
    int    start;
}
PathBuilder;

static void path_point(PathBuilder *pb, const float t[6], float x, float y)
{
    if (pb->points == pb->cap)
    {
        pb->cap = pb->cap ? pb->cap * 2 : 64;
        pb->xy = realloc(pb->xy, sizeof(float) * (size_t)pb->cap * 2);
    }
    pb->xy[pb->points * 2] = t[0] * x + t[2] * y + t[4];
    pb->xy[pb->points * 2 + 1] = t[1] * x + t[3] * y + t[5];
    pb->points++;
}

static void path_end(PathBuilder *pb)
{
    int n = pb->points - pb->start;
    if (n <= 0)
        return;
    if (pb->polys == pb->poly_cap)
    {
        pb->poly_cap = pb->poly_cap ? pb->poly_cap * 2 : 8;
        pb->counts = realloc(pb->counts, sizeof(int) * (size_t)pb->poly_cap);
    }
    pb->counts[pb->polys++] = n;
    pb->start = pb->points;
}

static nserror plot_path(const struct redraw_context *ctx, const plot_style_t *style, const float *p,
                         unsigned int n, const float transform[6])
{
    SxCanvas *c = canvas_of(ctx);
    PathBuilder pb = { 0 };
    float cx = 0, cy = 0;
    unsigned int i = 0;
    while (i < n)
    {
        int cmd = (int)p[i];
        if (cmd == PLOTTER_PATH_MOVE && i + 2 < n + 1)
        {
            path_end(&pb);
            cx = p[i + 1];
            cy = p[i + 2];
            path_point(&pb, transform, cx, cy);
            i += 3;
        }
        else if (cmd == PLOTTER_PATH_LINE)
        {
            cx = p[i + 1];
            cy = p[i + 2];
            path_point(&pb, transform, cx, cy);
            i += 3;
        }
        else if (cmd == PLOTTER_PATH_BEZIER)
        {
            float x1 = p[i + 1], y1 = p[i + 2], x2 = p[i + 3], y2 = p[i + 4], x3 = p[i + 5], y3 = p[i + 6];
            for (int s = 1; s <= 12; s++)
            {
                float t = s / 12.0f, u = 1 - t;
                float bx = u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
                float by = u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
                path_point(&pb, transform, bx, by);
            }
            cx = x3;
            cy = y3;
            i += 7;
        }
        else if (cmd == PLOTTER_PATH_CLOSE)
        {
            path_end(&pb);
            i += 1;
        }
        else
        {
            break;
        }
    }
    path_end(&pb);

    if (pb.xy && style->fill_type != PLOT_OP_TYPE_NONE)
        fill_polygons(c, pb.xy, pb.counts, pb.polys, nsx_colour(style->fill_colour));
    if (pb.xy && style->stroke_type != PLOT_OP_TYPE_NONE)
    {
        int base = 0;
        for (int k = 0; k < pb.polys; k++)
        {
            for (int j = 0; j + 1 < pb.counts[k]; j++)
                sx_draw_line(c, pb.xy[(base + j) * 2], pb.xy[(base + j) * 2 + 1],
                             pb.xy[(base + j + 1) * 2], pb.xy[(base + j + 1) * 2 + 1],
                             (float)stroke_px(style), nsx_colour(style->stroke_colour));
            base += pb.counts[k];
        }
    }
    free(pb.xy);
    free(pb.counts);
    return NSERROR_OK;
}

static const uint32_t *bitmap_scaled(NsxBitmap *b, int w, int h)
{
    if (w == b->width && h == b->height)
        return b->pixels;
    if (b->scaled && b->scaled_w == w && b->scaled_h == h)
        return b->scaled;
    if ((int64_t)w * h > 4096 * 4096)
        return NULL;
    free(b->scaled);
    b->scaled = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
    if (!b->scaled)
        return NULL;
    b->scaled_w = w;
    b->scaled_h = h;

    bool down = w < b->width || h < b->height;
    for (int y = 0; y < h; y++)
    {
        uint32_t *dst = b->scaled + (size_t)y * w;
        if (!down)
        {
            int sy = (int)((int64_t)y * b->height / h);
            const uint32_t *src = b->pixels + (size_t)sy * b->width;
            for (int x = 0; x < w; x++)
                dst[x] = src[(int64_t)x * b->width / w];
            continue;
        }
        int sy0 = (int)((int64_t)y * b->height / h);
        int sy1 = (int)((int64_t)(y + 1) * b->height / h);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (int x = 0; x < w; x++)
        {
            int sx0 = (int)((int64_t)x * b->width / w);
            int sx1 = (int)((int64_t)(x + 1) * b->width / w);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            uint32_t a = 0, r = 0, g = 0, bl = 0, count = 0;
            for (int yy = sy0; yy < sy1; yy++)
            {
                const uint32_t *src = b->pixels + (size_t)yy * b->width;
                for (int xx = sx0; xx < sx1; xx++)
                {
                    uint32_t p = src[xx];
                    uint32_t pa = p >> 24;
                    a += pa;
                    r += ((p >> 16) & 0xFF) * pa;
                    g += ((p >> 8) & 0xFF) * pa;
                    bl += (p & 0xFF) * pa;
                    count++;
                }
            }
            if (a == 0)
            {
                dst[x] = 0;
                continue;
            }
            dst[x] = ((a / count) << 24) | ((r / a) << 16) | ((g / a) << 8) | (bl / a);
        }
    }
    return b->scaled;
}

static void blit_bitmap(SxCanvas *c, const uint32_t *px, int w, int h, int x, int y, bool opaque)
{
    SxRect clip = c->clip;
    int x0 = x > clip.x ? x : clip.x;
    int y0 = y > clip.y ? y : clip.y;
    int x1 = x + w < clip.x + clip.w ? x + w : clip.x + clip.w;
    int y1 = y + h < clip.y + clip.h ? y + h : clip.y + clip.h;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int yy = y0; yy < y1; yy++)
    {
        const uint32_t *src = px + (size_t)(yy - y) * w + (x0 - x);
        uint32_t *dst = c->pixels + yy * c->stride + x0;
        if (opaque)
        {
            for (int xx = x0; xx < x1; xx++)
                *dst++ = *src++ | 0xFF000000u;
            continue;
        }
        for (int xx = x0; xx < x1; xx++, src++, dst++)
        {
            uint32_t a = *src >> 24;
            if (a)
                *dst = sx_blend(*dst, *src, a);
        }
    }
}

static nserror plot_bitmap(const struct redraw_context *ctx, struct bitmap *bitmap, int x, int y,
                           int width, int height, colour bg, bitmap_flags_t flags)
{
    (void)bg;
    SxCanvas *c = canvas_of(ctx);
    NsxBitmap *b = (NsxBitmap *)bitmap;
    if (!b || !b->pixels || width <= 0 || height <= 0)
        return NSERROR_OK;

    bool rx = flags & BITMAPF_REPEAT_X;
    bool ry = flags & BITMAPF_REPEAT_Y;
    SxRect clip = c->clip;

    if (!rx && !ry && (x >= clip.x + clip.w || y >= clip.y + clip.h || x + width <= clip.x || y + height <= clip.y))
        return NSERROR_OK;

    if (width == 1 && height == 1 && (rx || ry))
    {
        uint32_t p = b->pixels[0];
        int x0 = rx ? clip.x : x, x1 = rx ? clip.x + clip.w : x + 1;
        int y0 = ry ? clip.y : y, y1 = ry ? clip.y + clip.h : y + 1;
        fill_box(c, x0, y0, x1, y1, p);
        return NSERROR_OK;
    }

    const uint32_t *px = bitmap_scaled(b, width, height);
    if (!px)
        return NSERROR_OK;

    int sx = x, sy = y;
    if (rx)
    {
        while (sx > clip.x)
            sx -= width;
        while (sx + width <= clip.x)
            sx += width;
    }
    if (ry)
    {
        while (sy > clip.y)
            sy -= height;
        while (sy + height <= clip.y)
            sy += height;
    }

    for (int ty = sy; ty < (ry ? clip.y + clip.h : sy + 1); ty += height)
        for (int tx = sx; tx < (rx ? clip.x + clip.w : sx + 1); tx += width)
            blit_bitmap(c, px, width, height, tx, ty, b->opaque);
    return NSERROR_OK;
}

static nserror plot_text(const struct redraw_context *ctx, const plot_font_style_t *fstyle, int x, int y,
                         const char *text, size_t length)
{
    SxCanvas *c = canvas_of(ctx);
    nsx_font_draw(c, fstyle, x, y, text, length, nsx_colour(fstyle->foreground));
    return NSERROR_OK;
}

const struct plotter_table nsx_plotters = {
    .clip = plot_clip,
    .arc = plot_arc,
    .disc = plot_disc,
    .line = plot_line,
    .rectangle = plot_rectangle,
    .polygon = plot_polygon,
    .path = plot_path,
    .bitmap = plot_bitmap,
    .text = plot_text,
    .option_knockout = true,
};
