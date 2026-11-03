#pragma once

#include <gfx.h>

typedef enum
{
    ICON_HASH,
    ICON_SPEAKER,
    ICON_MIC,
    ICON_MIC_OFF,
    ICON_HEADPHONES,
    ICON_HEADPHONES_OFF,
    ICON_PHONE,
    ICON_HANGUP,
    ICON_SCREEN,
    ICON_SCREEN_STOP,
    ICON_LOGOUT,
    ICON_HOME,
    ICON_FILE,
    ICON_SIGNAL,
    ICON_ARROW,
    ICON_AT,
    ICON_LOCK,
    ICON_CLOSE,
    ICON_EYE,
    ICON_DOWNLOAD,
    ICON_EXTERNAL,
    ICON_CHECK,
    ICON_COUNT,
}
IconId;

void icon_draw(SxCanvas *c, IconId id, int x, int y, int size, uint32_t color);
void icon_spinner(SxCanvas *c, int cx, int cy, int radius, float width, float phase, uint32_t color);
void icon_arc(SxCanvas *c, float cx, float cy, float r, float width, float a0, float sweep, uint32_t color);
