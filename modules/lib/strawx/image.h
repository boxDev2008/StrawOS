#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "gfx.h"

typedef struct
{
    int       width;
    int       height;
    uint32_t *pixels;
}
SxImage;

SxImage    *sx_image_load(const char *path);
SxImage    *sx_image_decode(const uint8_t *data, size_t size);
bool        sx_image_info(const uint8_t *data, size_t size, int *width, int *height);
SxImage    *sx_image_create(int width, int height);
SxImage    *sx_image_scale(const SxImage *img, int width, int height);
void        sx_image_free(SxImage *img);
const char *sx_image_error(void);
bool        sx_image_is_supported(const char *path);

uint8_t *sx_inflate_alloc(const uint8_t *src, size_t size, size_t max_out, size_t *out_len);

void sx_draw_image(SxCanvas *c, const SxImage *img, int x, int y);
void sx_draw_image_scaled(SxCanvas *c, const SxImage *img, int x, int y, int w, int h);
