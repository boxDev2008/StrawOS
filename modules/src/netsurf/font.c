#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>

#include "utils/utf8.h"
#include "utils/nsoption.h"
#include "netsurf/layout.h"
#include "netsurf/plot_style.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "nsx.h"

#define FONT_DIR     "/share/fonts/"
#define GLYPH_SLOTS  4096
#define GLYPH_BUDGET (6 * 1024 * 1024)

typedef enum
{
    FACE_SANS,
    FACE_SANS_BOLD,
    FACE_SANS_ITALIC,
    FACE_SANS_BOLD_ITALIC,
    FACE_SERIF,
    FACE_SERIF_BOLD,
    FACE_SERIF_ITALIC,
    FACE_SERIF_BOLD_ITALIC,
    FACE_MONO,
    FACE_MONO_BOLD,
    FACE_COUNT,
}
FaceId;

static const char *const face_files[FACE_COUNT] = {
    "NotoSans-Regular.ttf",
    "NotoSans-Bold.ttf",
    "NotoSans-Italic.ttf",
    "NotoSans-BoldItalic.ttf",
    "NotoSerif-Regular.ttf",
    "NotoSerif-Bold.ttf",
    "NotoSerif-Italic.ttf",
    "NotoSerif-BoldItalic.ttf",
    "NotoSansMono-Regular.ttf",
    "NotoSansMono-Bold.ttf",
};

static const FaceId face_fallback[FACE_COUNT] = {
    FACE_SANS, FACE_SANS, FACE_SANS, FACE_SANS_ITALIC,
    FACE_SANS, FACE_SERIF, FACE_SERIF, FACE_SERIF_ITALIC,
    FACE_SANS, FACE_MONO,
};

typedef struct
{
    bool           tried;
    bool           ok;
    unsigned char *data;
    stbtt_fontinfo info;
    int            ascent, descent, line_gap;
}
Face;

typedef struct Glyph Glyph;
struct Glyph
{
    uint32_t       codepoint;
    uint16_t       px;
    uint8_t        face;
    bool           used;
    int            advance_fp;
    int            x0, y0, w, h;
    unsigned char *alpha;
};

static Face faces[FACE_COUNT];
static Glyph glyphs[GLYPH_SLOTS];
static size_t glyph_bytes;

static Face *face_get(FaceId id)
{
    for (int guard = 0; guard < 4; guard++)
    {
        Face *f = &faces[id];
        if (!f->tried)
        {
            f->tried = true;
            char path[128];
            snprintf(path, sizeof(path), FONT_DIR "%s", face_files[id]);
            FILE *fp = fopen(path, "rb");
            if (fp)
            {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                f->data = size > 0 ? malloc((size_t)size) : NULL;
                if (f->data && fread(f->data, 1, (size_t)size, fp) == (size_t)size &&
                    stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0)))
                {
                    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);
                    f->ok = true;
                }
                else
                {
                    free(f->data);
                    f->data = NULL;
                }
                fclose(fp);
            }
        }
        if (f->ok)
            return f;
        if (face_fallback[id] == id)
            break;
        id = face_fallback[id];
    }
    return faces[FACE_SANS].ok ? &faces[FACE_SANS] : NULL;
}

static bool family_matches(const char *name, const char *const *words)
{
    for (; *words; words++)
        if (strcasestr(name, *words))
            return true;
    return false;
}

static FaceId face_for_style(const plot_font_style_t *fs)
{
    static const char *const mono_words[] = { "mono", "courier", "consolas", "menlo", "code", "fixed", NULL };
    static const char *const serif_words[] = { "times", "georgia", "garamond", "palatino", "cambria", "book", NULL };
    static const char *const sans_words[] = { "sans", "arial", "helvetica", "verdana", "segoe", "roboto", "inter",
                                              "system-ui", "ubuntu", "tahoma", NULL };

    int family = fs->family;
    if (fs->families)
    {
        for (lwc_string *const *f = fs->families; *f; f++)
        {
            const char *name = lwc_string_data(*f);
            if (family_matches(name, mono_words)) { family = PLOT_FONT_FAMILY_MONOSPACE; break; }
            if (family_matches(name, sans_words)) { family = PLOT_FONT_FAMILY_SANS_SERIF; break; }
            if (family_matches(name, serif_words) || strcasecmp(name, "serif") == 0) { family = PLOT_FONT_FAMILY_SERIF; break; }
        }
    }

    bool bold = fs->weight >= 600;
    bool italic = (fs->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) != 0;
    switch (family)
    {
        case PLOT_FONT_FAMILY_MONOSPACE:
            return bold ? FACE_MONO_BOLD : FACE_MONO;
        case PLOT_FONT_FAMILY_SERIF:
            return italic ? (bold ? FACE_SERIF_BOLD_ITALIC : FACE_SERIF_ITALIC) : (bold ? FACE_SERIF_BOLD : FACE_SERIF);
        default:
            return italic ? (bold ? FACE_SANS_BOLD_ITALIC : FACE_SANS_ITALIC) : (bold ? FACE_SANS_BOLD : FACE_SANS);
    }
}

static int style_px(const plot_font_style_t *fs)
{
    int px = (int)((int64_t)fs->size * 96 / 72 / PLOT_STYLE_SCALE);
    if (px < 6)
        px = 6;
    if (px > 400)
        px = 400;
    return px;
}

static void glyph_cache_flush(void)
{
    for (int i = 0; i < GLYPH_SLOTS; i++)
    {
        free(glyphs[i].alpha);
        glyphs[i].alpha = NULL;
        glyphs[i].used = false;
    }
    glyph_bytes = 0;
}

static Glyph *glyph_get(FaceId id, int px, uint32_t cp, bool need_bitmap)
{
    uint32_t hash = (cp * 2654435761u) ^ ((uint32_t)px * 40503u) ^ ((uint32_t)id * 97u);
    int slot = (int)(hash % GLYPH_SLOTS);
    for (int probe = 0; probe < 8; probe++)
    {
        Glyph *g = &glyphs[(slot + probe) % GLYPH_SLOTS];
        if (g->used && g->codepoint == cp && g->px == px && g->face == id)
        {
            if (need_bitmap && !g->alpha && g->w > 0 && g->h > 0)
                break;
            return g;
        }
        if (!g->used)
            break;
    }

    Face *f = face_get(id);
    if (!f)
        return NULL;

    Glyph *g = &glyphs[slot];
    for (int probe = 0; probe < 8; probe++)
    {
        Glyph *c = &glyphs[(slot + probe) % GLYPH_SLOTS];
        if (!c->used || (c->codepoint == cp && c->px == px && c->face == id))
        {
            g = c;
            break;
        }
    }

    if (glyph_bytes > GLYPH_BUDGET)
        glyph_cache_flush();

    if (g->used)
    {
        glyph_bytes -= g->alpha ? (size_t)(g->w * g->h) : 0;
        free(g->alpha);
    }
    memset(g, 0, sizeof(*g));
    g->used = true;
    g->codepoint = cp;
    g->px = (uint16_t)px;
    g->face = (uint8_t)id;

    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    int glyph = stbtt_FindGlyphIndex(&f->info, (int)cp);
    if (glyph == 0 && cp != ' ' && id != FACE_SANS)
    {
        Face *fb = face_get(FACE_SANS);
        if (fb && stbtt_FindGlyphIndex(&fb->info, (int)cp))
        {
            f = fb;
            scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
            glyph = stbtt_FindGlyphIndex(&f->info, (int)cp);
        }
    }

    int advance, lsb;
    stbtt_GetGlyphHMetrics(&f->info, glyph, &advance, &lsb);
    g->advance_fp = (int)lroundf(advance * scale * 64.0f);

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&f->info, glyph, scale, scale, &x0, &y0, &x1, &y1);
    g->x0 = x0;
    g->y0 = y0;
    g->w = x1 - x0;
    g->h = y1 - y0;
    if (g->w <= 0 || g->h <= 0 || g->w >= 1024 || g->h >= 1024)
    {
        g->w = g->h = 0;
        return g;
    }
    if (need_bitmap)
    {
        g->alpha = malloc((size_t)(g->w * g->h));
        if (g->alpha)
        {
            stbtt_MakeGlyphBitmap(&f->info, g->alpha, g->w, g->h, g->w, scale, scale, glyph);
            glyph_bytes += (size_t)(g->w * g->h);
        }
    }
    return g;
}

static uint32_t next_codepoint(const char *s, size_t len, size_t *i)
{
    const unsigned char *u = (const unsigned char *)s;
    uint32_t c = u[*i];
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (c >= 0x80 && c < 0xC0)
        n = 1;
    if (*i + (size_t)n > len)
        n = 1;
    if (n == 1)
    {
        (*i)++;
        return c >= 0x80 ? 0xFFFD : c;
    }
    uint32_t v = c & (0x7F >> n);
    for (int k = 1; k < n; k++)
        v = v << 6 | (u[*i + k] & 0x3F);
    *i += (size_t)n;
    return v;
}

static int advance_of(FaceId id, int px, uint32_t cp, bool smallcaps)
{
    if (cp == 0xA0)
        cp = ' ';
    if (smallcaps && cp >= 'a' && cp <= 'z')
    {
        Glyph *g = glyph_get(id, px * 3 / 4, cp - 32, false);
        return g ? g->advance_fp : 0;
    }
    Glyph *g = glyph_get(id, px, cp, false);
    return g ? g->advance_fp : 0;
}

static nserror nsx_font_width(const plot_font_style_t *fs, const char *string, size_t length, int *width)
{
    FaceId id = face_for_style(fs);
    int px = style_px(fs);
    bool sc = fs->flags & FONTF_SMALLCAPS;
    int64_t total = 0;
    size_t i = 0;
    while (i < length)
        total += advance_of(id, px, next_codepoint(string, length, &i), sc);
    *width = (int)((total + 32) / 64);
    return NSERROR_OK;
}

static nserror nsx_font_position(const plot_font_style_t *fs, const char *string, size_t length,
                                 int x, size_t *char_offset, int *actual_x)
{
    FaceId id = face_for_style(fs);
    int px = style_px(fs);
    bool sc = fs->flags & FONTF_SMALLCAPS;
    int64_t pos = 0;
    int64_t target = (int64_t)x * 64;
    size_t i = 0;
    while (i < length)
    {
        size_t start = i;
        int adv = advance_of(id, px, next_codepoint(string, length, &i), sc);
        if (pos + adv / 2 > target)
        {
            i = start;
            break;
        }
        pos += adv;
    }
    *char_offset = i;
    *actual_x = (int)((pos + 32) / 64);
    return NSERROR_OK;
}

static nserror nsx_font_split(const plot_font_style_t *fs, const char *string, size_t length,
                              int x, size_t *char_offset, int *actual_x)
{
    FaceId id = face_for_style(fs);
    int px = style_px(fs);
    bool sc = fs->flags & FONTF_SMALLCAPS;
    int64_t pos = 0;
    int64_t limit = (int64_t)x * 64;
    size_t last_space = 0;
    int64_t last_space_x = 0;
    size_t i = 0;
    while (i < length)
    {
        if (string[i] == ' ')
        {
            last_space = i;
            last_space_x = pos;
            if (pos > limit)
            {
                *char_offset = i;
                *actual_x = (int)((pos + 32) / 64);
                return NSERROR_OK;
            }
        }
        pos += advance_of(id, px, next_codepoint(string, length, &i), sc);
        if (pos > limit && last_space > 0)
        {
            *char_offset = last_space;
            *actual_x = (int)((last_space_x + 32) / 64);
            return NSERROR_OK;
        }
    }
    *char_offset = length;
    *actual_x = (int)((pos + 32) / 64);
    return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
    .width = nsx_font_width,
    .position = nsx_font_position,
    .split = nsx_font_split,
};

struct gui_layout_table *nsx_layout_table = &layout_table;

static void blend_glyph(SxCanvas *c, Glyph *g, int gx, int gy, uint32_t color)
{
    SxRect clip = c->clip;
    int x0 = gx > clip.x ? gx : clip.x;
    int y0 = gy > clip.y ? gy : clip.y;
    int x1 = gx + g->w < clip.x + clip.w ? gx + g->w : clip.x + clip.w;
    int y1 = gy + g->h < clip.y + clip.h ? gy + g->h : clip.y + clip.h;
    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t alpha_mul = (color >> 24) & 0xFF;
    for (int y = y0; y < y1; y++)
    {
        const unsigned char *src = g->alpha + (y - gy) * g->w + (x0 - gx);
        uint32_t *dst = c->pixels + y * c->stride + x0;
        for (int x = x0; x < x1; x++, src++, dst++)
        {
            uint32_t a = *src;
            if (!a)
                continue;
            if (alpha_mul != 255)
                a = a * alpha_mul / 255;
            *dst = sx_blend(*dst, color | 0xFF000000u, a);
        }
    }
}

int nsx_font_draw(SxCanvas *c, const plot_font_style_t *fs, int x, int y, const char *text, size_t length,
                  uint32_t color)
{
    FaceId id = face_for_style(fs);
    int px = style_px(fs);
    bool sc = fs->flags & FONTF_SMALLCAPS;
    int64_t pen = (int64_t)x * 64;
    size_t i = 0;
    while (i < length)
    {
        uint32_t cp = next_codepoint(text, length, &i);
        if (cp == 0xA0)
            cp = ' ';
        int gpx = px;
        if (sc && cp >= 'a' && cp <= 'z')
        {
            cp -= 32;
            gpx = px * 3 / 4;
        }
        Glyph *g = glyph_get(id, gpx, cp, true);
        if (!g)
            continue;
        if (g->alpha && cp != ' ')
            blend_glyph(c, g, (int)((pen + 32) / 64) + g->x0, y + g->y0, color);
        pen += g->advance_fp;
    }
    return (int)((pen + 32) / 64) - x;
}

void nsx_font_metrics(const plot_font_style_t *fs, int *ascent, int *descent)
{
    Face *f = face_get(face_for_style(fs));
    int px = style_px(fs);
    if (!f)
    {
        *ascent = px * 4 / 5;
        *descent = px / 5;
        return;
    }
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    *ascent = (int)ceilf(f->ascent * scale);
    *descent = (int)ceilf(-f->descent * scale);
}
