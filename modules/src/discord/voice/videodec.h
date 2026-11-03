#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const uint8_t *y, *u, *v;
    int            y_stride, uv_stride;
    int            width, height;
}
VideoImage;

typedef struct VideoDec VideoDec;

VideoDec *videodec_new(bool vp9);
void      videodec_free(VideoDec *d);
int       videodec_decode(VideoDec *d, const uint8_t *data, size_t len);
bool      videodec_image(VideoDec *d, VideoImage *img);
bool      videodec_is_keyframe(bool vp9, const uint8_t *data, size_t len);
