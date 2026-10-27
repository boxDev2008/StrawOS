#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SX_RGB(r, g, b)     (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define SX_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define SX_ALPHA(c)         ((c) >> 24)

typedef struct
{
    int x, y, w, h;
}
SxRect;

typedef struct
{
    uint32_t *pixels;
    int       width;
    int       height;
    int       stride;
    SxRect    clip;
}
SxCanvas;

typedef struct
{
    int8_t   xoff;
    int8_t   yoff;
    uint8_t  w, h;
    uint16_t advance;
    uint32_t offset;
}
SxGlyph;

#define SX_FONT_GLYPHS 95

typedef struct
{
    const char *file;
    int         size;
    bool        loaded;
    int         ascent;
    int         descent;
    int         line_height;
    SxGlyph     glyphs[SX_FONT_GLYPHS];
    uint8_t    *bitmap;
}
SxFont;

extern SxFont sx_font_sans;
extern SxFont sx_font_sans_bold;
extern SxFont sx_font_sans_small;
extern SxFont sx_font_sans_large;
extern SxFont sx_font_sans_huge;
extern SxFont sx_font_mono;
extern SxFont sx_font_sans_h1;
extern SxFont sx_font_sans_h2;
extern SxFont sx_font_sans_h3;
extern SxFont sx_font_sans_italic;
extern SxFont sx_font_mono_small;

SxRect sx_rect(int x, int y, int w, int h);
bool   sx_rect_empty(SxRect r);
SxRect sx_rect_intersect(SxRect a, SxRect b);
SxRect sx_rect_union(SxRect a, SxRect b);
bool   sx_rect_contains(SxRect r, int x, int y);

void sx_canvas_init(SxCanvas *c, uint32_t *pixels, int width, int height, int stride);
void sx_canvas_set_clip(SxCanvas *c, SxRect clip);
void sx_canvas_reset_clip(SxCanvas *c);

uint32_t sx_blend(uint32_t dst, uint32_t src, uint32_t alpha);
uint32_t sx_mix(uint32_t a, uint32_t b, int t);
uint32_t sx_lighten(uint32_t c, int amount);
uint32_t sx_darken(uint32_t c, int amount);

void sx_put_pixel(SxCanvas *c, int x, int y, uint32_t color);
void sx_blend_pixel(SxCanvas *c, int x, int y, uint32_t color, uint32_t coverage);

void sx_clear(SxCanvas *c, uint32_t color);
void sx_fill_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color);
void sx_draw_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color);
void sx_fill_gradient(SxCanvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void sx_fill_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color);
void sx_draw_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color);
void sx_fill_circle(SxCanvas *c, float cx, float cy, float r, uint32_t color);
void sx_draw_circle(SxCanvas *c, float cx, float cy, float r, float thickness, uint32_t color);
void sx_draw_line(SxCanvas *c, float x0, float y0, float x1, float y1, float thickness, uint32_t color);

void sx_blit(SxCanvas *c, int x, int y, const uint32_t *src, int w, int h, int src_stride);

int  sx_draw_text(SxCanvas *c, const SxFont *font, int x, int y, const char *text, uint32_t color);
int  sx_draw_text_n(SxCanvas *c, const SxFont *font, int x, int y, const char *text, int len, uint32_t color);
int  sx_text_width(const SxFont *font, const char *text);
int  sx_text_width_n(const SxFont *font, const char *text, int len);
int  sx_char_width(const SxFont *font, char ch);
int  sx_font_cap_height(const SxFont *font);
int  sx_font_line_height(const SxFont *font);
int  sx_text_y_centered(const SxFont *font, SxRect box);
void sx_draw_text_centered(SxCanvas *c, const SxFont *font, SxRect box, const char *text, uint32_t color);
