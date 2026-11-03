#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Vp8Dec Vp8Dec;

typedef struct
{
    const uint8_t *y, *u, *v;
    int            y_stride, uv_stride;
    int            width, height;
}
Vp8Image;

Vp8Dec *vp8dec_new(void);
void    vp8dec_free(Vp8Dec *d);
int     vp8dec_decode(Vp8Dec *d, const uint8_t *data, size_t len);
bool    vp8dec_image(const Vp8Dec *d, Vp8Image *img);
bool    vp8dec_is_keyframe(const uint8_t *data, size_t len);
