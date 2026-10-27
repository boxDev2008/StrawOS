#include <math.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define SX_FONT_DIR "/share/fonts/"

typedef struct
{
    const char    *file;
    bool           ok;
    unsigned char *data;
    stbtt_fontinfo info;
}
SxFace;

static SxFace sx_faces[8];

SxFont sx_font_sans        = { "NotoSans-Regular.ttf",     14 };
SxFont sx_font_sans_bold   = { "NotoSans-Bold.ttf",        14 };
SxFont sx_font_sans_small  = { "NotoSans-Regular.ttf",     12 };
SxFont sx_font_sans_large  = { "NotoSans-Regular.ttf",     24 };
SxFont sx_font_sans_huge   = { "NotoSans-Light.ttf",       44 };
SxFont sx_font_mono        = { "NotoSansMono-Regular.ttf", 15 };
SxFont sx_font_sans_h1     = { "NotoSans-Bold.ttf",        26 };
SxFont sx_font_sans_h2     = { "NotoSans-Bold.ttf",        21 };
SxFont sx_font_sans_h3     = { "NotoSans-Bold.ttf",        17 };
SxFont sx_font_sans_italic = { "NotoSans-Italic.ttf",      14 };
SxFont sx_font_mono_small  = { "NotoSansMono-Regular.ttf", 13 };

static unsigned char *sx_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static SxFace *sx_face_get(const char *file)
{
    SxFace *slot = NULL;
    for (int i = 0; i < (int)(sizeof(sx_faces) / sizeof(sx_faces[0])); i++)
    {
        if (sx_faces[i].file && strcmp(sx_faces[i].file, file) == 0)
            return sx_faces[i].ok ? &sx_faces[i] : NULL;
        if (!sx_faces[i].file && !slot)
            slot = &sx_faces[i];
    }
    if (!slot)
        return NULL;

    char path[128];
    snprintf(path, sizeof(path), SX_FONT_DIR "%s", file);
    slot->file = file;
    slot->data = sx_read_file(path);
    if (slot->data && stbtt_InitFont(&slot->info, slot->data, stbtt_GetFontOffsetForIndex(slot->data, 0)))
    {
        slot->ok = true;
        return slot;
    }
    free(slot->data);
    slot->data = NULL;
    return NULL;
}

static void sx_font_fallback(SxFont *font)
{
    font->ascent = font->size;
    font->descent = font->size / 4;
    font->line_height = font->ascent + font->descent;
    for (int i = 0; i < SX_FONT_GLYPHS; i++)
        font->glyphs[i] = (SxGlyph){ 0, (int8_t)-font->size, 0, 0, (uint16_t)(font->size * 32), 0 };
}

static void sx_font_load(SxFont *font)
{
    if (font->loaded)
        return;
    font->loaded = true;

    SxFace *face = sx_face_get(font->file);
    if (!face)
    {
        sx_font_fallback(font);
        return;
    }

    float scale = stbtt_ScaleForMappingEmToPixels(&face->info, (float)font->size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &line_gap);
    font->ascent = (int)ceilf(ascent * scale);
    font->descent = (int)ceilf(-descent * scale);
    font->line_height = font->ascent + font->descent;

    int boxes[SX_FONT_GLYPHS][4];
    size_t total = 0;
    for (int i = 0; i < SX_FONT_GLYPHS; i++)
    {
        int *b = boxes[i];
        stbtt_GetCodepointBitmapBox(&face->info, 32 + i, scale, scale, &b[0], &b[1], &b[2], &b[3]);
        total += (size_t)(b[2] - b[0]) * (size_t)(b[3] - b[1]);
    }

    font->bitmap = malloc(total ? total : 1);
    if (!font->bitmap)
    {
        sx_font_fallback(font);
        return;
    }

    uint32_t offset = 0;
    for (int i = 0; i < SX_FONT_GLYPHS; i++)
    {
        int *b = boxes[i];
        int w = b[2] - b[0], h = b[3] - b[1];
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&face->info, 32 + i, &advance, &lsb);

        SxGlyph *g = &font->glyphs[i];
        g->advance = (uint16_t)lroundf(advance * scale * 64.0f);
        if (w <= 0 || h <= 0)
        {
            g->xoff = g->yoff = 0;
            g->w = g->h = 0;
            g->offset = 0;
            continue;
        }
        g->xoff = (int8_t)b[0];
        g->yoff = (int8_t)b[1];
        g->w = (uint8_t)w;
        g->h = (uint8_t)h;
        g->offset = offset;
        stbtt_MakeCodepointBitmap(&face->info, font->bitmap + offset, w, h, w, scale, scale, 32 + i);
        offset += (uint32_t)(w * h);
    }
}
