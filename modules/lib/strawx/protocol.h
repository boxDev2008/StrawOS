#pragma once

#include <stdint.h>

#define SX_SOCKET_NAME      "strawx"
#define SX_PROTOCOL_VERSION 1
#define SX_TITLE_MAX        64
#define SX_MSG_MAX          256

#define SX_POS_AUTO         (-100000)

#define SX_WINDOW_RESIZABLE 0x1
#define SX_WINDOW_NO_FRAME  0x2

typedef struct
{
    uint16_t type;
    uint16_t size;
    uint32_t window;
}
SxMsgHeader;

enum
{
    SX_REQ_HELLO = 1,
    SX_REQ_CREATE_WINDOW,
    SX_REQ_DESTROY_WINDOW,
    SX_REQ_SET_TITLE,
    SX_REQ_ATTACH_BUFFER,
    SX_REQ_DAMAGE,
    SX_REQ_SET_VISIBLE,
    SX_REQ_MOVE,
    SX_REQ_RESIZE,
    SX_REQ_RELOAD_SETTINGS,
};

typedef struct
{
    SxMsgHeader hdr;
    uint32_t    version;
}
SxReqHello;

typedef struct
{
    SxMsgHeader hdr;
    uint32_t    serial;
    int32_t     x, y;
    int32_t     w, h;
    uint32_t    flags;
    char        title[SX_TITLE_MAX];
}
SxReqCreateWindow;

typedef struct
{
    SxMsgHeader hdr;
    char        title[SX_TITLE_MAX];
}
SxReqSetTitle;

typedef struct
{
    SxMsgHeader hdr;
    uint64_t    pixels;
    int32_t     w, h;
    int32_t     stride;
}
SxReqAttachBuffer;

typedef struct
{
    SxMsgHeader hdr;
    int32_t     x, y, w, h;
}
SxReqRect;

typedef struct
{
    SxMsgHeader hdr;
    uint32_t    visible;
}
SxReqSetVisible;

enum
{
    SX_EVT_WELCOME = 100,
    SX_EVT_WINDOW_CREATED,
    SX_EVT_CONFIGURE,
    SX_EVT_MOUSE_MOVE,
    SX_EVT_MOUSE_DOWN,
    SX_EVT_MOUSE_UP,
    SX_EVT_MOUSE_WHEEL,
    SX_EVT_MOUSE_ENTER,
    SX_EVT_MOUSE_LEAVE,
    SX_EVT_KEY_DOWN,
    SX_EVT_KEY_UP,
    SX_EVT_FOCUS_IN,
    SX_EVT_FOCUS_OUT,
    SX_EVT_CLOSE,
    SX_EVT_BUFFER_RELEASE,
    SX_EVT_THEME_CHANGED,
};

typedef struct
{
    SxMsgHeader hdr;
    int32_t     x, y;
    int32_t     w, h;
    uint32_t    button;
    uint32_t    buttons;
    int32_t     wheel;
    uint32_t    key;
    uint32_t    ch;
    uint32_t    mods;
    uint32_t    serial;
    uint64_t    buffer;
}
SxEvtMsg;

#define SX_BUTTON_LEFT   0x1
#define SX_BUTTON_RIGHT  0x2
#define SX_BUTTON_MIDDLE 0x4

#define SX_MOD_SHIFT 0x01
#define SX_MOD_CTRL  0x02
#define SX_MOD_ALT   0x04
#define SX_MOD_SUPER 0x08
#define SX_MOD_CAPS  0x10

enum
{
    SX_KEY_NONE      = 0,
    SX_KEY_BACKSPACE = 8,
    SX_KEY_TAB       = 9,
    SX_KEY_ENTER     = 13,
    SX_KEY_ESCAPE    = 27,
    SX_KEY_SPACE     = 32,
    SX_KEY_DELETE    = 127,

    SX_KEY_UP = 256,
    SX_KEY_DOWN,
    SX_KEY_LEFT,
    SX_KEY_RIGHT,
    SX_KEY_HOME,
    SX_KEY_END,
    SX_KEY_PAGE_UP,
    SX_KEY_PAGE_DOWN,
    SX_KEY_INSERT,
    SX_KEY_F1, SX_KEY_F2, SX_KEY_F3, SX_KEY_F4, SX_KEY_F5, SX_KEY_F6,
    SX_KEY_F7, SX_KEY_F8, SX_KEY_F9, SX_KEY_F10, SX_KEY_F11, SX_KEY_F12,
    SX_KEY_LSHIFT, SX_KEY_RSHIFT,
    SX_KEY_LCTRL, SX_KEY_RCTRL,
    SX_KEY_LALT, SX_KEY_RALT,
    SX_KEY_LSUPER, SX_KEY_RSUPER,
    SX_KEY_CAPS_LOCK,
    SX_KEY_MENU,
};
