#pragma once

#include "syscall.h"

#define DEVICE_FRAMEBUFFER 0
#define DEVICE_PS2MOUSE 2

typedef struct FramebufferDevice
{
    uint32_t *address;
    uint32_t width, height, pitch;
}
FramebufferDevice;

typedef struct MouseDevice
{
    int32_t x, y;
    int32_t dx, dy;
    uint8_t buttons;
}
MouseDevice;

int device(int device_id, void *data);