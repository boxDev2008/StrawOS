#pragma once

/* The subset of SDL3 that lite-xl uses, implemented over StrawX in lib/lite-xl-port/sdl.c */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDLCALL

#define SDL_malloc  malloc
#define SDL_calloc  calloc
#define SDL_realloc realloc
#define SDL_free    free
#define SDL_strcmp  strcmp

/* init, hints, errors */

#define SDL_INIT_VIDEO  0x20u
#define SDL_INIT_EVENTS 0x4000u

#define SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR"
#define SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH           "SDL_MOUSE_FOCUS_CLICKTHROUGH"
#define SDL_HINT_IME_IMPLEMENTED_UI                 "SDL_IME_IMPLEMENTED_UI"
#define SDL_HINT_RENDER_DRIVER                      "SDL_RENDER_DRIVER"
#define SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS          "SDL_MOUSE_DOUBLE_CLICK_RADIUS"

bool        SDL_Init(uint32_t flags);
void        SDL_Quit(void);
bool        SDL_SetAppMetadata(const char *name, const char *version, const char *id);
bool        SDL_SetHint(const char *name, const char *value);
bool        SDL_EnableScreenSaver(void);
const char *SDL_GetPlatform(void);

bool        SDL_SetError(const char *fmt, ...);
const char *SDL_GetError(void);
bool        SDL_ClearError(void);

/* geometry */

typedef struct { int x, y; } SDL_Point;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { uint8_t r, g, b, a; } SDL_Color;

bool SDL_GetRectIntersection(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result);

/* pixel formats and surfaces */

typedef enum
{
    SDL_PIXELFORMAT_UNKNOWN,
    SDL_PIXELFORMAT_INDEX8,
    SDL_PIXELFORMAT_RGB24,
    SDL_PIXELFORMAT_RGBA32,
    SDL_PIXELFORMAT_BGRA32,
}
SDL_PixelFormat;

typedef struct
{
    SDL_PixelFormat format;
    uint8_t  bits_per_pixel;
    uint8_t  bytes_per_pixel;
    uint8_t  padding[2];
    uint32_t Rmask, Gmask, Bmask, Amask;
    uint8_t  Rbits, Gbits, Bbits, Abits;
    uint8_t  Rshift, Gshift, Bshift, Ashift;
}
SDL_PixelFormatDetails;

typedef struct SDL_Properties *SDL_PropertiesID;

typedef enum { SDL_SCALEMODE_NEAREST, SDL_SCALEMODE_LINEAR } SDL_ScaleMode;

typedef struct SDL_Surface
{
    uint32_t        flags;
    SDL_PixelFormat format;
    int             w, h;
    int             pitch;
    void           *pixels;

    /* private to the shim */
    SDL_Rect        clip;
    bool            owns_pixels;
    struct SDL_Properties *props;
}
SDL_Surface;

const SDL_PixelFormatDetails *SDL_GetPixelFormatDetails(SDL_PixelFormat format);
SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, uint32_t r, uint32_t g, uint32_t b, uint32_t a);

SDL_Surface     *SDL_CreateSurface(int w, int h, SDL_PixelFormat format);
SDL_Surface     *SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat format, void *pixels, int pitch);
void             SDL_DestroySurface(SDL_Surface *surface);
bool             SDL_SetSurfaceClipRect(SDL_Surface *surface, const SDL_Rect *rect);
bool             SDL_GetSurfaceClipRect(SDL_Surface *surface, SDL_Rect *rect);
uint32_t         SDL_MapSurfaceRGB(SDL_Surface *surface, uint8_t r, uint8_t g, uint8_t b);
uint32_t         SDL_MapSurfaceRGBA(SDL_Surface *surface, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
bool             SDL_FillSurfaceRect(SDL_Surface *dst, const SDL_Rect *rect, uint32_t color);
bool             SDL_BlitSurfaceScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst,
                                       const SDL_Rect *dstrect, SDL_ScaleMode mode);
SDL_PropertiesID SDL_GetSurfaceProperties(SDL_Surface *surface);

bool  SDL_HasProperty(SDL_PropertiesID props, const char *name);
void *SDL_GetPointerProperty(SDL_PropertiesID props, const char *name, void *default_value);
bool  SDL_SetPointerProperty(SDL_PropertiesID props, const char *name, void *value);

/* file I/O */

typedef struct SDL_IOStream SDL_IOStream;
typedef enum { SDL_IO_SEEK_SET, SDL_IO_SEEK_CUR, SDL_IO_SEEK_END } SDL_IOWhence;

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode);
size_t        SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size);
int64_t       SDL_SeekIO(SDL_IOStream *io, int64_t offset, SDL_IOWhence whence);
int64_t       SDL_GetIOSize(SDL_IOStream *io);
bool          SDL_CloseIO(SDL_IOStream *io);

/* filesystem */

typedef enum { SDL_ENUM_CONTINUE, SDL_ENUM_SUCCESS, SDL_ENUM_FAILURE } SDL_EnumerationResult;
typedef SDL_EnumerationResult (*SDL_EnumerateDirectoryCallback)(void *userdata, const char *dirname,
                                                                const char *fname);

bool SDL_EnumerateDirectory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata);
bool SDL_RemovePath(const char *path);

/* shared objects: StrawOS has no dynamic loading, so these always fail */

typedef struct SDL_SharedObject SDL_SharedObject;
typedef void (*SDL_FunctionPointer)(void);

SDL_SharedObject   *SDL_LoadObject(const char *sofile);
SDL_FunctionPointer SDL_LoadFunction(SDL_SharedObject *handle, const char *name);
void                SDL_UnloadObject(SDL_SharedObject *handle);

/* threads: StrawOS processes are single-threaded, so thread creation fails */

typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Mutex SDL_Mutex;
typedef struct SDL_Condition SDL_Condition;
typedef int (*SDL_ThreadFunction)(void *data);

SDL_Thread    *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void           SDL_WaitThread(SDL_Thread *thread, int *status);
SDL_Mutex     *SDL_CreateMutex(void);
void           SDL_DestroyMutex(SDL_Mutex *mutex);
void           SDL_LockMutex(SDL_Mutex *mutex);
void           SDL_UnlockMutex(SDL_Mutex *mutex);
SDL_Condition *SDL_CreateCondition(void);
void           SDL_DestroyCondition(SDL_Condition *cond);
void           SDL_SignalCondition(SDL_Condition *cond);
void           SDL_WaitCondition(SDL_Condition *cond, SDL_Mutex *mutex);

/* time */

uint64_t SDL_GetTicks(void);
uint64_t SDL_GetPerformanceCounter(void);
uint64_t SDL_GetPerformanceFrequency(void);
void     SDL_Delay(uint32_t ms);

/* displays and windows */

typedef uint32_t SDL_DisplayID;
typedef uint64_t SDL_WindowFlags;

typedef struct
{
    SDL_DisplayID   displayID;
    SDL_PixelFormat format;
    int             w, h;
    float           pixel_density;
    float           refresh_rate;
}
SDL_DisplayMode;

#define SDL_WINDOW_FULLSCREEN         0x0000000000000001ull
#define SDL_WINDOW_HIDDEN             0x0000000000000008ull
#define SDL_WINDOW_RESIZABLE          0x0000000000000020ull
#define SDL_WINDOW_MINIMIZED          0x0000000000000040ull
#define SDL_WINDOW_MAXIMIZED          0x0000000000000080ull
#define SDL_WINDOW_INPUT_FOCUS        0x0000000000000200ull
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x0000000000002000ull

typedef struct SDL_Window SDL_Window;

typedef enum
{
    SDL_HITTEST_NORMAL,
    SDL_HITTEST_DRAGGABLE,
    SDL_HITTEST_RESIZE_TOPLEFT,
    SDL_HITTEST_RESIZE_TOP,
    SDL_HITTEST_RESIZE_TOPRIGHT,
    SDL_HITTEST_RESIZE_RIGHT,
    SDL_HITTEST_RESIZE_BOTTOMRIGHT,
    SDL_HITTEST_RESIZE_BOTTOM,
    SDL_HITTEST_RESIZE_BOTTOMLEFT,
    SDL_HITTEST_RESIZE_LEFT,
}
SDL_HitTestResult;

typedef SDL_HitTestResult (*SDL_HitTest)(SDL_Window *win, const SDL_Point *area, void *data);

SDL_DisplayID          SDL_GetPrimaryDisplay(void);
const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID id);

SDL_Window     *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags);
void            SDL_DestroyWindow(SDL_Window *window);
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window);
SDL_PixelFormat SDL_GetWindowPixelFormat(SDL_Window *window);
SDL_Surface    *SDL_GetWindowSurface(SDL_Window *window);
bool            SDL_UpdateWindowSurface(SDL_Window *window);
bool            SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *rects, int count);
bool            SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
bool            SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h);
bool            SDL_SetWindowSize(SDL_Window *window, int w, int h);
bool            SDL_GetWindowPosition(SDL_Window *window, int *x, int *y);
bool            SDL_SetWindowPosition(SDL_Window *window, int x, int y);
bool            SDL_SetWindowTitle(SDL_Window *window, const char *title);
bool            SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon);
bool            SDL_ShowWindow(SDL_Window *window);
bool            SDL_RaiseWindow(SDL_Window *window);
bool            SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen);
bool            SDL_MaximizeWindow(SDL_Window *window);
bool            SDL_MinimizeWindow(SDL_Window *window);
bool            SDL_RestoreWindow(SDL_Window *window);
bool            SDL_SetWindowBordered(SDL_Window *window, bool bordered);
bool            SDL_SetWindowOpacity(SDL_Window *window, float opacity);
bool            SDL_SetWindowHitTest(SDL_Window *window, SDL_HitTest callback, void *data);

/* text input */

bool SDL_StartTextInput(SDL_Window *window);
bool SDL_SetTextInputArea(SDL_Window *window, const SDL_Rect *rect, int cursor);
bool SDL_ClearComposition(SDL_Window *window);

/* keyboard */

typedef uint32_t SDL_Keycode;
typedef uint16_t SDL_Keymod;

#define SDLK_SCANCODE_MASK (1u << 30)
#define SDLK_W             0x77u

#define SDL_KMOD_NUM 0x1000u
#define SDL_KMOD_GUI 0x0c00u

typedef enum
{
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_KP_1 = 89,
    SDL_SCANCODE_KP_9 = 97,
    SDL_SCANCODE_KP_0 = 98,
    SDL_SCANCODE_KP_PERIOD = 99,
}
SDL_Scancode;

const char *SDL_GetKeyName(SDL_Keycode key);
const char *SDL_GetScancodeName(SDL_Scancode scancode);

/* mouse */

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5

typedef enum
{
    SDL_SYSTEM_CURSOR_DEFAULT,
    SDL_SYSTEM_CURSOR_TEXT,
    SDL_SYSTEM_CURSOR_WAIT,
    SDL_SYSTEM_CURSOR_CROSSHAIR,
    SDL_SYSTEM_CURSOR_PROGRESS,
    SDL_SYSTEM_CURSOR_NWSE_RESIZE,
    SDL_SYSTEM_CURSOR_NESW_RESIZE,
    SDL_SYSTEM_CURSOR_EW_RESIZE,
    SDL_SYSTEM_CURSOR_NS_RESIZE,
    SDL_SYSTEM_CURSOR_MOVE,
    SDL_SYSTEM_CURSOR_NOT_ALLOWED,
    SDL_SYSTEM_CURSOR_POINTER,
}
SDL_SystemCursor;

typedef struct SDL_Cursor SDL_Cursor;

SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor id);
bool        SDL_SetCursor(SDL_Cursor *cursor);
bool        SDL_CaptureMouse(bool enabled);
uint32_t    SDL_GetMouseState(float *x, float *y);

/* events */

typedef enum
{
    SDL_EVENT_FIRST = 0,
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_WILL_ENTER_BACKGROUND = 0x103,
    SDL_EVENT_DID_ENTER_BACKGROUND,
    SDL_EVENT_WILL_ENTER_FOREGROUND,
    SDL_EVENT_DID_ENTER_FOREGROUND,
    SDL_EVENT_WINDOW_EXPOSED = 0x204,
    SDL_EVENT_WINDOW_RESIZED = 0x206,
    SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
    SDL_EVENT_WINDOW_MINIMIZED = 0x209,
    SDL_EVENT_WINDOW_MAXIMIZED,
    SDL_EVENT_WINDOW_RESTORED,
    SDL_EVENT_WINDOW_MOUSE_LEAVE = 0x20d,
    SDL_EVENT_WINDOW_FOCUS_GAINED,
    SDL_EVENT_WINDOW_FOCUS_LOST,
    SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED = 0x213,
    SDL_EVENT_KEY_DOWN = 0x300,
    SDL_EVENT_KEY_UP,
    SDL_EVENT_TEXT_EDITING,
    SDL_EVENT_TEXT_INPUT,
    SDL_EVENT_MOUSE_MOTION = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN,
    SDL_EVENT_MOUSE_BUTTON_UP,
    SDL_EVENT_MOUSE_WHEEL,
    SDL_EVENT_FINGER_DOWN = 0x700,
    SDL_EVENT_FINGER_UP,
    SDL_EVENT_FINGER_MOTION,
    SDL_EVENT_DROP_FILE = 0x1000,
    SDL_EVENT_USER = 0x8000,
    SDL_EVENT_LAST = 0xFFFF,
}
SDL_EventType;

typedef union SDL_Event
{
    uint32_t type;
    struct { uint32_t type; int32_t data1, data2; } window;
    struct { uint32_t type; SDL_Scancode scancode; SDL_Keycode key; SDL_Keymod mod; bool down; } key;
    struct { uint32_t type; const char *text; } text;
    struct { uint32_t type; const char *text; int32_t start, length; } edit;
    struct { uint32_t type; uint8_t button; bool down; uint8_t clicks; float x, y; } button;
    struct { uint32_t type; float x, y, xrel, yrel; } motion;
    struct { uint32_t type; float x, y; } wheel;
    struct { uint32_t type; uint64_t fingerID; float x, y, dx, dy; } tfinger;
    struct { uint32_t type; float x, y; const char *data; } drop;
    uint8_t padding[128];
}
SDL_Event;

typedef enum { SDL_ADDEVENT, SDL_PEEKEVENT, SDL_GETEVENT } SDL_EventAction;

bool     SDL_PollEvent(SDL_Event *event);
bool     SDL_WaitEvent(SDL_Event *event);
bool     SDL_WaitEventTimeout(SDL_Event *event, int32_t timeout_ms);
void     SDL_PumpEvents(void);
int      SDL_PeepEvents(SDL_Event *events, int numevents, SDL_EventAction action,
                        uint32_t min_type, uint32_t max_type);
bool     SDL_PushEvent(SDL_Event *event);
void     SDL_FlushEvent(uint32_t type);
uint32_t SDL_RegisterEvents(int numevents);
void     SDL_SetEventEnabled(uint32_t type, bool enabled);

/* clipboard and message boxes */

char *SDL_GetClipboardText(void);
bool  SDL_SetClipboardText(const char *text);

#define SDL_MESSAGEBOX_ERROR 0x10u

bool SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window);
