#pragma once

#include "gfx.h"
#include "theme.h"

#define SX_THEME_BG          (sx_theme->bg)
#define SX_THEME_SURFACE     (sx_theme->surface)
#define SX_THEME_SURFACE_HI  (sx_theme->surface_hi)
#define SX_THEME_BORDER      (sx_theme->border)
#define SX_THEME_TEXT        (sx_theme->text)
#define SX_THEME_TEXT_DIM    (sx_theme->text_dim)
#define SX_THEME_ACCENT      (sx_theme->accent)
#define SX_THEME_ACCENT_TEXT (sx_theme->accent_text)
#define SX_THEME_BLUE        (sx_theme->blue)
#define SX_THEME_RED         (sx_theme->red)
#define SX_THEME_GREEN       (sx_theme->green)
#define SX_THEME_SUNKEN      sx_darken(sx_theme->bg, 25)
#define SX_THEME_DISABLED    sx_mix(sx_theme->text_dim, sx_theme->bg, 110)
#define SX_THEME_ACCENT_A(a) ((sx_theme->accent & 0xFFFFFFu) | ((uint32_t)(a) << 24))
#define SX_THEME_RED_A(a)    ((sx_theme->red & 0xFFFFFFu) | ((uint32_t)(a) << 24))

typedef enum
{
    SX_BUTTON_NORMAL,
    SX_BUTTON_HOVER,
    SX_BUTTON_PRESSED,
}
SxButtonState;

void sx_ui_button(SxCanvas *c, SxRect r, const char *label, const SxFont *font,
                  SxButtonState state, uint32_t base, uint32_t text);

void sx_ui_panel(SxCanvas *c, SxRect r, int radius, uint32_t fill);
