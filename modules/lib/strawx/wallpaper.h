#pragma once

#include <stdint.h>

#define SX_WALLPAPER_DEFAULT "default"

void sx_render_default_wallpaper(uint32_t *px, int w, int h);
void sx_render_wallpaper(const char *spec, uint32_t *px, int w, int h);
