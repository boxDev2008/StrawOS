#pragma once

#include <stdint.h>

typedef struct
{
    const char *id;
    const char *name;

    uint32_t bg;
    uint32_t surface;
    uint32_t surface_hi;
    uint32_t border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;
    uint32_t accent_text;
    uint32_t blue;
    uint32_t red;
    uint32_t green;

    uint32_t title;          // focused window titlebar
    uint32_t title_inactive;
    uint32_t panel;          // taskbar, drawn translucent

    uint32_t ansi[16];       // terminal colors: black, red, green, yellow, blue, magenta, cyan, white, then bright
}
SxTheme;

extern const SxTheme *sx_theme;
extern const SxTheme sx_themes[];
extern const int sx_theme_count;

// Points sx_theme at the theme named by the "theme" setting, falling back to the first one.
void sx_theme_load(void);
const SxTheme *sx_theme_find(const char *id);
