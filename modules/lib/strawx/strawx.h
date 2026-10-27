#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "protocol.h"
#include "gfx.h"
#include "image.h"
#include "desktop.h"
#include "settings.h"
#include "wallpaper.h"
#include "theme.h"

typedef struct SxWindow SxWindow;

typedef enum
{
    SX_EVENT_NONE = 0,
    SX_EVENT_MOUSE_MOVE,
    SX_EVENT_MOUSE_DOWN,
    SX_EVENT_MOUSE_UP,
    SX_EVENT_MOUSE_WHEEL,
    SX_EVENT_MOUSE_ENTER,
    SX_EVENT_MOUSE_LEAVE,
    SX_EVENT_KEY_DOWN,
    SX_EVENT_KEY_UP,
    SX_EVENT_FOCUS_IN,
    SX_EVENT_FOCUS_OUT,
    SX_EVENT_RESIZE,
    SX_EVENT_CLOSE,
    SX_EVENT_DISCONNECTED,
}
SxEventType;

typedef struct
{
    SxEventType type;
    SxWindow   *window;
    int         x, y;
    int         width, height;
    int         button;
    int         buttons;
    int         wheel;
    int         key;
    int         ch;
    int         mods;
}
SxEvent;

typedef struct
{
    uint32_t *pixels;
    int       w, h;
    bool      busy;
}
SxBuffer;

struct SxWindow
{
    uint32_t  id;
    uint32_t  flags;
    char      title[SX_TITLE_MAX];

    int       width, height;
    SxBuffer  buffers[2];
    int       back;
    int       front;
    SxCanvas  canvas;

    bool      focused;
    void     *user;

    SxWindow *next;
};

int  sx_connect(void);
void sx_disconnect(void);
int  sx_fd(void);
int  sx_screen_width(void);
int  sx_screen_height(void);

SxWindow *sx_window_create(const char *title, int width, int height, uint32_t flags);
SxWindow *sx_window_create_at(const char *title, int x, int y, int width, int height, uint32_t flags);
void      sx_window_destroy(SxWindow *win);
SxCanvas *sx_window_canvas(SxWindow *win);
int       sx_window_width(SxWindow *win);
int       sx_window_height(SxWindow *win);
void      sx_window_commit(SxWindow *win);
void      sx_window_damage(SxWindow *win, int x, int y, int w, int h);
void      sx_window_set_title(SxWindow *win, const char *title);
void      sx_window_set_visible(SxWindow *win, bool visible);
void      sx_window_move(SxWindow *win, int x, int y);
void      sx_window_resize(SxWindow *win, int width, int height);

int  sx_next_event(SxEvent *ev, int timeout_ms);
bool sx_pending(void);

uint64_t sx_time_ms(void);
