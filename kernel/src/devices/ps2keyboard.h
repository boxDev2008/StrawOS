#pragma once

#include <stdbool.h>
#include <stdint.h>

uint8_t ps2keyboard_poll_scancode(uint8_t *out_scancode);
void ps2keyboard_init(void);