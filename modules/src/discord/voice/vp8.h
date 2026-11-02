#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Vp8Enc Vp8Enc;

typedef struct
{
    uint8_t *y, *u, *v;
    int      y_stride, uv_stride;
}
Vp8Planes;

Vp8Enc   *vp8_new(int width, int height);
void      vp8_free(Vp8Enc *e);
Vp8Planes vp8_input(Vp8Enc *e);
size_t    vp8_encode(Vp8Enc *e, bool key, int q, uint8_t *out, size_t cap);
Vp8Planes vp8_recon(Vp8Enc *e);
