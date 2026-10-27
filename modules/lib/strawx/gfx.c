#define SX_SQRTF(x) __builtin_sqrtf(x)

static inline int sx_imin(int a, int b) { return a < b ? a : b; }
static inline int sx_imax(int a, int b) { return a > b ? a : b; }
static inline float sx_fminf(float a, float b) { return a < b ? a : b; }
static inline float sx_fmaxf(float a, float b) { return a > b ? a : b; }
static inline float sx_fabsf(float a) { return a < 0 ? -a : a; }
static inline float sx_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

SxRect sx_rect(int x, int y, int w, int h)
{
    SxRect r = { x, y, w, h };
    return r;
}

bool sx_rect_empty(SxRect r)
{
    return r.w <= 0 || r.h <= 0;
}

SxRect sx_rect_intersect(SxRect a, SxRect b)
{
    int x0 = sx_imax(a.x, b.x);
    int y0 = sx_imax(a.y, b.y);
    int x1 = sx_imin(a.x + a.w, b.x + b.w);
    int y1 = sx_imin(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return sx_rect(0, 0, 0, 0);
    return sx_rect(x0, y0, x1 - x0, y1 - y0);
}

SxRect sx_rect_union(SxRect a, SxRect b)
{
    if (sx_rect_empty(a))
        return b;
    if (sx_rect_empty(b))
        return a;
    int x0 = sx_imin(a.x, b.x);
    int y0 = sx_imin(a.y, b.y);
    int x1 = sx_imax(a.x + a.w, b.x + b.w);
    int y1 = sx_imax(a.y + a.h, b.y + b.h);
    return sx_rect(x0, y0, x1 - x0, y1 - y0);
}

bool sx_rect_contains(SxRect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void sx_canvas_init(SxCanvas *c, uint32_t *pixels, int width, int height, int stride)
{
    c->pixels = pixels;
    c->width = width;
    c->height = height;
    c->stride = stride;
    c->clip = sx_rect(0, 0, width, height);
}

void sx_canvas_set_clip(SxCanvas *c, SxRect clip)
{
    c->clip = sx_rect_intersect(clip, sx_rect(0, 0, c->width, c->height));
}

void sx_canvas_reset_clip(SxCanvas *c)
{
    c->clip = sx_rect(0, 0, c->width, c->height);
}

uint32_t sx_blend(uint32_t dst, uint32_t src, uint32_t a)
{
    if (a >= 255)
        return src | 0xFF000000u;
    if (a == 0)
        return dst;

    uint32_t inv = 255 - a;
    uint32_t rb = (src & 0xFF00FF) * a + (dst & 0xFF00FF) * inv + 0x800080;
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    uint32_t g = (src & 0x00FF00) * a + (dst & 0x00FF00) * inv + 0x008000;
    g = ((g + ((g >> 8) & 0x00FF00)) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

uint32_t sx_mix(uint32_t a, uint32_t b, int t)
{
    return sx_blend(a, b, (uint32_t)sx_imax(0, sx_imin(255, t)));
}

uint32_t sx_lighten(uint32_t c, int amount)
{
    return (c & 0xFF000000u) | (sx_blend(c, 0xFFFFFF, (uint32_t)amount) & 0xFFFFFF);
}

uint32_t sx_darken(uint32_t c, int amount)
{
    return (c & 0xFF000000u) | (sx_blend(c, 0x000000, (uint32_t)amount) & 0xFFFFFF);
}

void sx_put_pixel(SxCanvas *c, int x, int y, uint32_t color)
{
    if (!sx_rect_contains(c->clip, x, y))
        return;
    c->pixels[y * c->stride + x] = color;
}

void sx_blend_pixel(SxCanvas *c, int x, int y, uint32_t color, uint32_t coverage)
{
    if (!sx_rect_contains(c->clip, x, y))
        return;
    uint32_t a = (SX_ALPHA(color) * coverage + 127) / 255;
    uint32_t *p = &c->pixels[y * c->stride + x];
    *p = sx_blend(*p, color, a);
}

static inline void sx_cover(SxCanvas *c, int x, int y, uint32_t color, float cov)
{
    if (cov <= 0.0f)
        return;
    uint32_t a = (uint32_t)(SX_ALPHA(color) * cov + 0.5f);
    uint32_t *p = &c->pixels[y * c->stride + x];
    *p = sx_blend(*p, color, a);
}

void sx_clear(SxCanvas *c, uint32_t color)
{
    sx_fill_rect(c, 0, 0, c->width, c->height, color | 0xFF000000u);
}

static void sx_span(SxCanvas *c, int x0, int x1, int y, uint32_t color)
{
    uint32_t *row = c->pixels + y * c->stride;
    uint32_t a = SX_ALPHA(color);
    if (a == 255)
    {
        for (int x = x0; x < x1; x++)
            row[x] = color;
    }
    else if (a > 0)
    {
        for (int x = x0; x < x1; x++)
            row[x] = sx_blend(row[x], color, a);
    }
}

void sx_fill_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color)
{
    SxRect r = sx_rect_intersect(sx_rect(x, y, w, h), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
        sx_span(c, r.x, r.x + r.w, yy, color);
}

void sx_draw_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;
    sx_fill_rect(c, x, y, w, 1, color);
    if (h > 1)
        sx_fill_rect(c, x, y + h - 1, w, 1, color);
    if (h > 2)
    {
        sx_fill_rect(c, x, y + 1, 1, h - 2, color);
        if (w > 1)
            sx_fill_rect(c, x + w - 1, y + 1, 1, h - 2, color);
    }
}

void sx_fill_gradient(SxCanvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    if (h <= 0)
        return;
    SxRect r = sx_rect_intersect(sx_rect(x, y, w, h), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        int t = h > 1 ? ((yy - y) * 255) / (h - 1) : 0;
        uint32_t col = sx_blend(top, bottom, (uint32_t)t);
        uint32_t a = (SX_ALPHA(top) * (255 - t) + SX_ALPHA(bottom) * t) / 255;
        sx_span(c, r.x, r.x + r.w, yy, (col & 0xFFFFFF) | (a << 24));
    }
}

static inline float sx_rrect_dist(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = sx_fabsf(px - cx) - (hw - r);
    float qy = sx_fabsf(py - cy) - (hh - r);
    float ox = sx_fmaxf(qx, 0.0f);
    float oy = sx_fmaxf(qy, 0.0f);
    return SX_SQRTF(ox * ox + oy * oy) + sx_fminf(sx_fmaxf(qx, qy), 0.0f) - r;
}

void sx_fill_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;

    radius = sx_imin(radius, sx_imin(w, h) / 2);
    if (radius <= 0)
    {
        sx_fill_rect(c, x, y, w, h, color);
        return;
    }

    SxRect r = sx_rect_intersect(sx_rect(x, y, w, h), c->clip);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;

    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        bool corner_row = yy < y + radius || yy >= y + h - radius;
        if (!corner_row)
        {
            sx_span(c, r.x, r.x + r.w, yy, color);
            continue;
        }

        int solid0 = x + radius, solid1 = x + w - radius;
        for (int xx = r.x; xx < r.x + r.w; xx++)
        {
            if (xx >= solid0 && xx < solid1)
            {
                int end = sx_imin(solid1, r.x + r.w);
                sx_span(c, xx, end, yy, color);
                xx = end - 1;
                continue;
            }
            float d = sx_rrect_dist(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, (float)radius);
            sx_cover(c, xx, yy, color, sx_clamp01(0.5f - d));
        }
    }
}

void sx_draw_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;

    radius = sx_imin(radius, sx_imin(w, h) / 2);
    if (radius <= 0)
    {
        sx_draw_rect(c, x, y, w, h, color);
        return;
    }

    sx_fill_rect(c, x + radius, y, w - 2 * radius, 1, color);
    sx_fill_rect(c, x + radius, y + h - 1, w - 2 * radius, 1, color);
    sx_fill_rect(c, x, y + radius, 1, h - 2 * radius, color);
    sx_fill_rect(c, x + w - 1, y + radius, 1, h - 2 * radius, color);

    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;
    int corner_x[2] = { x, x + w - radius };
    int corner_y[2] = { y, y + h - radius };

    for (int cyi = 0; cyi < 2; cyi++)
    {
        for (int cxi = 0; cxi < 2; cxi++)
        {
            SxRect box = sx_rect_intersect(sx_rect(corner_x[cxi], corner_y[cyi], radius, radius), c->clip);
            for (int yy = box.y; yy < box.y + box.h; yy++)
            {
                for (int xx = box.x; xx < box.x + box.w; xx++)
                {
                    float d = sx_rrect_dist(xx + 0.5f, yy + 0.5f, cx, cy, hw, hh, (float)radius);
                    sx_cover(c, xx, yy, color, sx_clamp01(1.0f - sx_fabsf(d + 0.5f)));
                }
            }
        }
    }
}

void sx_fill_circle(SxCanvas *c, float cx, float cy, float r, uint32_t color)
{
    SxRect box = sx_rect_intersect(
        sx_rect((int)(cx - r - 1), (int)(cy - r - 1), (int)(2 * r + 3), (int)(2 * r + 3)), c->clip);
    float inner = r - 0.75f;
    float inner2 = inner > 0 ? inner * inner : 0;

    for (int yy = box.y; yy < box.y + box.h; yy++)
    {
        float dy = yy + 0.5f - cy;
        for (int xx = box.x; xx < box.x + box.w; xx++)
        {
            float dx = xx + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 < inner2)
            {
                sx_cover(c, xx, yy, color, 1.0f);
                continue;
            }
            float d = SX_SQRTF(d2) - r;
            sx_cover(c, xx, yy, color, sx_clamp01(0.5f - d));
        }
    }
}

void sx_draw_circle(SxCanvas *c, float cx, float cy, float r, float thickness, uint32_t color)
{
    float half = thickness * 0.5f;
    float outer = r + half + 1;
    SxRect box = sx_rect_intersect(
        sx_rect((int)(cx - outer), (int)(cy - outer), (int)(2 * outer + 2), (int)(2 * outer + 2)), c->clip);

    for (int yy = box.y; yy < box.y + box.h; yy++)
    {
        float dy = yy + 0.5f - cy;
        for (int xx = box.x; xx < box.x + box.w; xx++)
        {
            float dx = xx + 0.5f - cx;
            float d = sx_fabsf(SX_SQRTF(dx * dx + dy * dy) - r);
            sx_cover(c, xx, yy, color, sx_clamp01(half + 0.5f - d));
        }
    }
}

void sx_draw_line(SxCanvas *c, float x0, float y0, float x1, float y1, float thickness, uint32_t color)
{
    float half = thickness * 0.5f;
    float pad = half + 1.0f;

    int bx0 = (int)(sx_fminf(x0, x1) - pad);
    int by0 = (int)(sx_fminf(y0, y1) - pad);
    int bx1 = (int)(sx_fmaxf(x0, x1) + pad) + 1;
    int by1 = (int)(sx_fmaxf(y0, y1) + pad) + 1;
    SxRect box = sx_rect_intersect(sx_rect(bx0, by0, bx1 - bx0, by1 - by0), c->clip);

    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    float inv_len2 = len2 > 0.0f ? 1.0f / len2 : 0.0f;

    for (int yy = box.y; yy < box.y + box.h; yy++)
    {
        float py = yy + 0.5f;
        for (int xx = box.x; xx < box.x + box.w; xx++)
        {
            float px = xx + 0.5f;
            float t = ((px - x0) * vx + (py - y0) * vy) * inv_len2;
            t = sx_clamp01(t);
            float dx = px - (x0 + t * vx);
            float dy = py - (y0 + t * vy);
            float d = SX_SQRTF(dx * dx + dy * dy);
            sx_cover(c, xx, yy, color, sx_clamp01(half + 0.5f - d));
        }
    }
}

void sx_blit(SxCanvas *c, int x, int y, const uint32_t *src, int w, int h, int src_stride)
{
    SxRect r = sx_rect_intersect(sx_rect(x, y, w, h), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        const uint32_t *s = src + (yy - y) * src_stride + (r.x - x);
        memcpy(c->pixels + yy * c->stride + r.x, s, (size_t)r.w * 4);
    }
}

static const SxGlyph *sx_glyph(const SxFont *font, char ch)
{
    sx_font_load((SxFont *)font);
    unsigned char u = (unsigned char)ch;
    if (u < 32 || u > 126)
        u = '?';
    return &font->glyphs[u - 32];
}

int sx_char_width(const SxFont *font, char ch)
{
    return (sx_glyph(font, ch)->advance + 32) >> 6;
}

int sx_text_width_n(const SxFont *font, const char *text, int len)
{
    int pen = 0;
    for (int i = 0; i < len && text[i]; i++)
        pen += sx_glyph(font, text[i])->advance;
    return (pen + 32) >> 6;
}

int sx_text_width(const SxFont *font, const char *text)
{
    return sx_text_width_n(font, text, 1 << 30);
}

int sx_draw_text_n(SxCanvas *c, const SxFont *font, int x, int y, const char *text, int len, uint32_t color)
{
    sx_font_load((SxFont *)font);
    int pen = x << 6;
    int baseline = y + font->ascent;
    uint32_t alpha = SX_ALPHA(color);

    for (int i = 0; i < len && text[i]; i++)
    {
        const SxGlyph *g = sx_glyph(font, text[i]);
        int gx = ((pen + 32) >> 6) + g->xoff;
        int gy = baseline + g->yoff;
        pen += g->advance;

        if (g->w == 0)
            continue;

        SxRect r = sx_rect_intersect(sx_rect(gx, gy, g->w, g->h), c->clip);
        const uint8_t *bits = font->bitmap + g->offset;

        for (int yy = r.y; yy < r.y + r.h; yy++)
        {
            const uint8_t *src = bits + (yy - gy) * g->w + (r.x - gx);
            uint32_t *dst = c->pixels + yy * c->stride + r.x;
            for (int xx = 0; xx < r.w; xx++)
            {
                uint32_t cov = src[xx];
                if (cov)
                    dst[xx] = sx_blend(dst[xx], color, (cov * alpha + 127) / 255);
            }
        }
    }

    return ((pen + 32) >> 6) - x;
}

int sx_draw_text(SxCanvas *c, const SxFont *font, int x, int y, const char *text, uint32_t color)
{
    return sx_draw_text_n(c, font, x, y, text, 1 << 30, color);
}

int sx_font_cap_height(const SxFont *font)
{
    return -sx_glyph(font, 'H')->yoff;
}

int sx_font_line_height(const SxFont *font)
{
    sx_font_load((SxFont *)font);
    return font->line_height;
}

int sx_text_y_centered(const SxFont *font, SxRect box)
{
    int baseline = box.y + (box.h + sx_font_cap_height(font) + 1) / 2;
    return baseline - font->ascent;
}

void sx_draw_text_centered(SxCanvas *c, const SxFont *font, SxRect box, const char *text, uint32_t color)
{
    int tw = sx_text_width(font, text);
    int tx = box.x + (box.w - tw) / 2;
    sx_draw_text(c, font, tx, sx_text_y_centered(font, box), text, color);
}
