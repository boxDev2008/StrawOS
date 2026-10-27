#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "discord.h"

#define FONT_DIR    "/share/fonts/"
#define GLYPH_SLOTS 4096

typedef struct
{
    bool           ok;
    unsigned char *data;
    stbtt_fontinfo info;
    int            ascent, descent, line_gap;
}
Face;

typedef struct
{
    uint32_t       cp;
    uint16_t       px;
    uint8_t        face;
    bool           used;
    bool           missing;
    float          advance;
    int            x0, y0, w, h;
    unsigned char *alpha;
}
Glyph;

static const char *const face_files[FACE_COUNT] = {
    "NotoSans-Regular.ttf",
    "NotoSans-Bold.ttf",
    "NotoSans-Italic.ttf",
    "NotoSansMono-Regular.ttf",
};

static Face   faces[FACE_COUNT];
static Glyph  glyphs[GLYPH_SLOTS];

static unsigned char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(f);
        return NULL;
    }
    unsigned char *data = malloc((size_t)size);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

bool font_init(void)
{
    for (int i = 0; i < FACE_COUNT; i++)
    {
        char path[128];
        snprintf(path, sizeof(path), FONT_DIR "%s", face_files[i]);
        Face *f = &faces[i];
        f->data = read_file(path);
        if (!f->data || !stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0)))
            continue;
        stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);
        f->ok = true;
    }
    return faces[FACE_REGULAR].ok;
}

static Face *face_get(int face)
{
    if (face >= 0 && face < FACE_COUNT && faces[face].ok)
        return &faces[face];
    return &faces[FACE_REGULAR];
}

static float face_scale(Face *f, int px)
{
    return stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
}

int font_ascent(int face, int px)
{
    Face *f = face_get(face);
    if (!f->ok)
        return px;
    return (int)ceilf(f->ascent * face_scale(f, px));
}

int font_line_height(int face, int px)
{
    Face *f = face_get(face);
    if (!f->ok)
        return px + 4;
    float s = face_scale(f, px);
    return (int)ceilf((f->ascent - f->descent + f->line_gap) * s);
}

static Glyph *glyph_get(int face, int px, uint32_t cp)
{
    uint32_t h = (cp * 2654435761u) ^ ((uint32_t)px * 40503u) ^ ((uint32_t)face * 977u);
    Glyph *slot = &glyphs[h % GLYPH_SLOTS];
    if (slot->used && slot->cp == cp && slot->px == px && slot->face == face)
        return slot;

    if (slot->used)
        free(slot->alpha);
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->cp = cp;
    slot->px = (uint16_t)px;
    slot->face = (uint8_t)face;

    Face *f = face_get(face);
    if (!f->ok)
    {
        slot->advance = px * 0.5f;
        slot->missing = true;
        return slot;
    }
    int index = stbtt_FindGlyphIndex(&f->info, (int)cp);
    if (index == 0 && face != FACE_REGULAR && faces[FACE_REGULAR].ok)
    {
        f = &faces[FACE_REGULAR];
        index = stbtt_FindGlyphIndex(&f->info, (int)cp);
    }
    float scale = face_scale(f, px);
    if (index == 0 && cp > 32)
    {
        slot->missing = true;
        slot->advance = roundf(px * 0.6f);
        return slot;
    }
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, index, &adv, &lsb);
    slot->advance = adv * scale;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&f->info, index, scale, scale, &x0, &y0, &x1, &y1);
    slot->x0 = x0;
    slot->y0 = y0;
    slot->w = x1 - x0;
    slot->h = y1 - y0;
    if (slot->w > 0 && slot->h > 0)
    {
        slot->alpha = malloc((size_t)slot->w * (size_t)slot->h);
        if (slot->alpha)
        {
            stbtt_MakeGlyphBitmap(&f->info, slot->alpha, slot->w, slot->h, slot->w, scale, scale, index);
        }
        else
        {
            slot->w = slot->h = 0;
        }
    }
    return slot;
}

int font_width(int face, int px, const char *s, int len)
{
    float x = 0;
    int i = 0;
    while (i < len)
    {
        uint32_t cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0)
            break;
        i += n;
        if (cp == 0xFE0F || cp == 0x200D)
            continue;
        x += glyph_get(face, px, cp)->advance;
    }
    return (int)ceilf(x);
}

int font_fit(int face, int px, const char *s, int len, int max_width)
{
    float x = 0;
    int i = 0;
    while (i < len)
    {
        uint32_t cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0)
            break;
        float adv = glyph_get(face, px, cp)->advance;
        if (x + adv > max_width)
            break;
        x += adv;
        i += n;
    }
    return i;
}

static void blend(uint32_t *dst, uint32_t color, unsigned a)
{
    if (a == 0)
        return;
    a = a * SX_ALPHA(color) / 255;
    if (a >= 255)
    {
        *dst = color | 0xFF000000u;
        return;
    }
    uint32_t d = *dst;
    unsigned ia = 255 - a;
    unsigned r = (((color >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
    unsigned g = (((color >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
    unsigned b = ((color & 0xFF) * a + (d & 0xFF) * ia) / 255;
    *dst = 0xFF000000u | r << 16 | g << 8 | b;
}

static void draw_missing(SxCanvas *c, int x, int y, int px, int adv, uint32_t color)
{
    int w = adv - 3;
    int h = px * 7 / 10;
    if (w < 3)
        w = 3;
    uint32_t col = (color & 0x00FFFFFFu) | 0x90000000u;
    sx_draw_rect(c, x + 1, y - h, w, h, col);
}

int font_draw(SxCanvas *c, int face, int px, int x, int y, const char *s, int len, uint32_t color)
{
    float pen = (float)x;
    int i = 0;
    SxRect clip = c->clip;
    while (i < len)
    {
        uint32_t cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0)
            break;
        i += n;
        if (cp == 0xFE0F || cp == 0x200D)
            continue;
        Glyph *g = glyph_get(face, px, cp);
        int gx = (int)floorf(pen + 0.5f);
        if (gx > clip.x + clip.w)
            break;
        if (g->missing)
        {
            draw_missing(c, gx, y, px, (int)g->advance, color);
        }
        else if (g->alpha)
        {
            int ox = gx + g->x0;
            int oy = y + g->y0;
            for (int row = 0; row < g->h; row++)
            {
                int yy = oy + row;
                if (yy < clip.y || yy >= clip.y + clip.h)
                    continue;
                uint32_t *line = c->pixels + (size_t)yy * c->stride;
                const unsigned char *src = g->alpha + (size_t)row * g->w;
                for (int col = 0; col < g->w; col++)
                {
                    int xx = ox + col;
                    if (xx < clip.x || xx >= clip.x + clip.w)
                        continue;
                    blend(&line[xx], color, src[col]);
                }
            }
        }
        pen += g->advance;
    }
    return (int)ceilf(pen) - x;
}

int font_draw_ellipsis(SxCanvas *c, int face, int px, int x, int y, const char *s, int max_width, uint32_t color)
{
    int len = (int)strlen(s);
    if (font_width(face, px, s, len) <= max_width)
        return font_draw(c, face, px, x, y, s, len, color);
    int dots = font_width(face, px, "...", 3);
    int fit = font_fit(face, px, s, len, max_width - dots);
    int w = font_draw(c, face, px, x, y, s, fit, color);
    return w + font_draw(c, face, px, x + w, y, "...", 3, color);
}
