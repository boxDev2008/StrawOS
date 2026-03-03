#pragma once
#include <stdint.h>

typedef struct
{
    int32_t x, y;
    int32_t dx, dy;
    uint8_t buttons;    /* bit 0 = left, bit 1 = right, bit 2 = middle   */
}
MouseState;

void        ps2mouse_init(uint32_t screen_w, uint32_t screen_h);
MouseState *ps2mouse_state(void);