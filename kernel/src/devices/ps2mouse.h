#pragma once
#include <stdint.h>

typedef struct MouseState
{
    int32_t x, y;
    int32_t dx, dy;
    uint8_t buttons;
}
MouseState;

const MouseState *ps2mouse_get_state(void);
void ps2mouse_init(uint32_t screen_w, uint32_t screen_h);