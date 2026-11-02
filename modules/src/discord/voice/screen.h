#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Screen Screen;

Screen *screen_open(int max_w, int max_h);
void    screen_close(Screen *s);
int     screen_width(const Screen *s);
int     screen_height(const Screen *s);
int     screen_grab(Screen *s);
size_t  screen_encode(Screen *s, bool key, int q, uint8_t *out, size_t cap);
