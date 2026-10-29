#include <SDL3/SDL.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strawx.h>
#include <ui.h>

/* lite-xl draws into a private framebuffer (the window surface). Presenting copies the dirty
   rects into the StrawX back buffer, which already holds the last frame, and damages them. */

/* errors and misc */

static char error_buf[256];

bool SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
    va_end(ap);
    return false;
}

const char *SDL_GetError(void)
{
    return error_buf;
}

bool SDL_ClearError(void)
{
    error_buf[0] = '\0';
    return true;
}

bool SDL_Init(uint32_t flags)
{
    (void)flags;
    return sx_connect() == 0 || SDL_SetError("cannot connect to the display server");
}

void SDL_Quit(void)
{
}

bool SDL_SetAppMetadata(const char *name, const char *version, const char *id)
{
    (void)name;
    (void)version;
    (void)id;
    return true;
}

bool SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return true;
}

bool SDL_EnableScreenSaver(void)
{
    return true;
}

const char *SDL_GetPlatform(void)
{
    return "StrawOS";
}

bool SDL_GetRectIntersection(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result)
{
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    *result = (SDL_Rect){ x1, y1, x2 > x1 ? x2 - x1 : 0, y2 > y1 ? y2 - y1 : 0 };
    return x2 > x1 && y2 > y1;
}

/* pixel formats: byte-order names, so on x86 BGRA32 is the 0xAARRGGBB of StrawX canvases */

static const SDL_PixelFormatDetails formats[] = {
    [SDL_PIXELFORMAT_INDEX8] = { SDL_PIXELFORMAT_INDEX8, 8, 1 },
    [SDL_PIXELFORMAT_RGB24]  = { SDL_PIXELFORMAT_RGB24, 24, 3 },
    [SDL_PIXELFORMAT_RGBA32] = { SDL_PIXELFORMAT_RGBA32, 32, 4, { 0 },
                                 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000,
                                 8, 8, 8, 8, 0, 8, 16, 24 },
    [SDL_PIXELFORMAT_BGRA32] = { SDL_PIXELFORMAT_BGRA32, 32, 4, { 0 },
                                 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000,
                                 8, 8, 8, 8, 16, 8, 0, 24 },
};

const SDL_PixelFormatDetails *SDL_GetPixelFormatDetails(SDL_PixelFormat format)
{
    if (format <= SDL_PIXELFORMAT_UNKNOWN || format > SDL_PIXELFORMAT_BGRA32)
        return NULL;
    return &formats[format];
}

SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    for (int f = SDL_PIXELFORMAT_RGBA32; f <= SDL_PIXELFORMAT_BGRA32; f++)
        if (bpp == formats[f].bits_per_pixel && r == formats[f].Rmask && g == formats[f].Gmask &&
            b == formats[f].Bmask && a == formats[f].Amask)
            return (SDL_PixelFormat)f;
    return SDL_PIXELFORMAT_UNKNOWN;
}

/* properties: lite-xl keeps one pointer per glyph atlas surface, so a short list is plenty */

#define PROPS_MAX 4

struct SDL_Properties
{
    int count;
    struct { char name[24]; void *value; } items[PROPS_MAX];
};

static int prop_index(SDL_PropertiesID props, const char *name)
{
    for (int i = 0; i < props->count; i++)
        if (strcmp(props->items[i].name, name) == 0)
            return i;
    return -1;
}

bool SDL_HasProperty(SDL_PropertiesID props, const char *name)
{
    return props && prop_index(props, name) >= 0;
}

void *SDL_GetPointerProperty(SDL_PropertiesID props, const char *name, void *default_value)
{
    int i = props ? prop_index(props, name) : -1;
    return i >= 0 ? props->items[i].value : default_value;
}

bool SDL_SetPointerProperty(SDL_PropertiesID props, const char *name, void *value)
{
    if (!props)
        return false;
    int i = prop_index(props, name);
    if (!value)
    {
        /* as in SDL, a NULL value deletes the property */
        if (i >= 0)
            props->items[i] = props->items[--props->count];
        return true;
    }
    if (i < 0)
    {
        if (props->count == PROPS_MAX || strlen(name) >= sizeof(props->items[0].name))
            return SDL_SetError("too many properties");
        i = props->count++;
        strcpy(props->items[i].name, name);
    }
    props->items[i].value = value;
    return true;
}

/* surfaces */

static SDL_Surface *surface_wrap(int w, int h, SDL_PixelFormat format, void *pixels, int pitch)
{
    if (!SDL_GetPixelFormatDetails(format))
    {
        SDL_SetError("unsupported pixel format");
        return NULL;
    }
    SDL_Surface *s = calloc(1, sizeof(SDL_Surface));
    if (!s)
        return NULL;
    s->format = format;
    s->w = w;
    s->h = h;
    s->pitch = pitch;
    s->pixels = pixels;
    s->clip = (SDL_Rect){ 0, 0, w, h };
    return s;
}

SDL_Surface *SDL_CreateSurface(int w, int h, SDL_PixelFormat format)
{
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails(format);
    if (!d)
    {
        SDL_SetError("unsupported pixel format");
        return NULL;
    }
    int pitch = (w * d->bytes_per_pixel + 3) & ~3;
    void *pixels = calloc((size_t)pitch * (size_t)(h > 0 ? h : 1), 1);
    if (!pixels)
        return NULL;
    SDL_Surface *s = surface_wrap(w, h, format, pixels, pitch);
    if (!s)
    {
        free(pixels);
        return NULL;
    }
    s->owns_pixels = true;
    return s;
}

SDL_Surface *SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat format, void *pixels, int pitch)
{
    return surface_wrap(w, h, format, pixels, pitch);
}

void SDL_DestroySurface(SDL_Surface *surface)
{
    if (!surface)
        return;
    if (surface->owns_pixels)
        free(surface->pixels);
    free(surface->props);
    free(surface);
}

SDL_PropertiesID SDL_GetSurfaceProperties(SDL_Surface *surface)
{
    if (!surface->props)
        surface->props = calloc(1, sizeof(struct SDL_Properties));
    return surface->props;
}

bool SDL_SetSurfaceClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    SDL_Rect full = { 0, 0, surface->w, surface->h };
    if (!rect)
    {
        surface->clip = full;
        return true;
    }
    return SDL_GetRectIntersection(rect, &full, &surface->clip);
}

bool SDL_GetSurfaceClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
    *rect = surface->clip;
    return true;
}

uint32_t SDL_MapSurfaceRGBA(SDL_Surface *surface, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails(surface->format);
    if (!d || d->bytes_per_pixel != 4)
        return 0;
    return (uint32_t)r << d->Rshift | (uint32_t)g << d->Gshift | (uint32_t)b << d->Bshift |
           (uint32_t)a << d->Ashift;
}

uint32_t SDL_MapSurfaceRGB(SDL_Surface *surface, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapSurfaceRGBA(surface, r, g, b, 0xff);
}

bool SDL_FillSurfaceRect(SDL_Surface *dst, const SDL_Rect *rect, uint32_t color)
{
    SDL_Rect r = dst->clip;
    if (rect && !SDL_GetRectIntersection(rect, &dst->clip, &r))
        return true;
    if (SDL_GetPixelFormatDetails(dst->format)->bytes_per_pixel != 4)
        return SDL_SetError("fill needs a 32-bit surface");
    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint32_t *p = (uint32_t *)((uint8_t *)dst->pixels + (size_t)y * dst->pitch) + r.x;
        for (int x = 0; x < r.w; x++)
            p[x] = color;
    }
    return true;
}

/* lite-xl only blits a 1x1 RGBA surface to draw translucent rectangles: blend its colour */
bool SDL_BlitSurfaceScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst,
                           const SDL_Rect *dstrect, SDL_ScaleMode mode)
{
    (void)srcrect;
    (void)mode;
    const SDL_PixelFormatDetails *sd = SDL_GetPixelFormatDetails(src->format);
    const SDL_PixelFormatDetails *dd = SDL_GetPixelFormatDetails(dst->format);
    if (src->w != 1 || src->h != 1 || sd->bytes_per_pixel != 4 || dd->bytes_per_pixel != 4)
        return SDL_SetError("only 1x1 32-bit blits are supported");

    SDL_Rect r = dst->clip;
    if (dstrect && !SDL_GetRectIntersection(dstrect, &dst->clip, &r))
        return true;

    uint32_t c = *(uint32_t *)src->pixels;
    uint32_t sr = (c & sd->Rmask) >> sd->Rshift, sg = (c & sd->Gmask) >> sd->Gshift;
    uint32_t sb = (c & sd->Bmask) >> sd->Bshift, sa = (c & sd->Amask) >> sd->Ashift;
    uint32_t ia = 255 - sa;

    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint32_t *p = (uint32_t *)((uint8_t *)dst->pixels + (size_t)y * dst->pitch) + r.x;
        for (int x = 0; x < r.w; x++)
        {
            uint32_t d = p[x];
            uint32_t dr = (d & dd->Rmask) >> dd->Rshift, dg = (d & dd->Gmask) >> dd->Gshift;
            uint32_t db = (d & dd->Bmask) >> dd->Bshift, da = (d & dd->Amask) >> dd->Ashift;
            dr = (sr * sa + dr * ia) / 255;
            dg = (sg * sa + dg * ia) / 255;
            db = (sb * sa + db * ia) / 255;
            da = sa + da * ia / 255;
            p[x] = dr << dd->Rshift | dg << dd->Gshift | db << dd->Bshift | da << dd->Ashift;
        }
    }
    return true;
}

/* file I/O */

struct SDL_IOStream
{
    FILE *fp;
};

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
    FILE *fp = fopen(file, mode);
    if (!fp)
    {
        SDL_SetError("cannot open %s", file);
        return NULL;
    }
    SDL_IOStream *io = malloc(sizeof(SDL_IOStream));
    if (!io)
    {
        fclose(fp);
        return NULL;
    }
    io->fp = fp;
    return io;
}

size_t SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size)
{
    return fread(ptr, 1, size, io->fp);
}

int64_t SDL_SeekIO(SDL_IOStream *io, int64_t offset, SDL_IOWhence whence)
{
    static const int whences[] = { SEEK_SET, SEEK_CUR, SEEK_END };
    if (fseek(io->fp, (long)offset, whences[whence]) != 0)
        return -1;
    return ftell(io->fp);
}

int64_t SDL_GetIOSize(SDL_IOStream *io)
{
    long pos = ftell(io->fp);
    fseek(io->fp, 0, SEEK_END);
    long size = ftell(io->fp);
    fseek(io->fp, pos, SEEK_SET);
    return size;
}

bool SDL_CloseIO(SDL_IOStream *io)
{
    if (!io)
        return true;
    int r = fclose(io->fp);
    free(io);
    return r == 0;
}

/* filesystem */

bool SDL_EnumerateDirectory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.type != STAT_DIR)
        return SDL_SetError("%s is not a directory", path);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return SDL_SetError("cannot open %s", path);

    /* SDL passes the directory with a trailing separator */
    char dirname[512];
    size_t len = strlen(path);
    snprintf(dirname, sizeof(dirname), "%s%s", path, len && path[len - 1] == '/' ? "" : "/");

    SDL_EnumerationResult result = SDL_ENUM_CONTINUE;
    struct dirent ent;
    while (result == SDL_ENUM_CONTINUE && readdir(fd, &ent) == 1)
    {
        char name[NAME_MAX + 1];
        memcpy(name, ent.name, NAME_MAX);
        name[NAME_MAX] = '\0';
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        result = callback(userdata, dirname, name);
    }
    close(fd);
    return result != SDL_ENUM_FAILURE;
}

bool SDL_RemovePath(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return SDL_SetError("%s does not exist", path);
    if ((st.type == STAT_DIR ? rmdir(path) : remove(path)) != 0)
        return SDL_SetError("cannot remove %s", path);
    return true;
}

SDL_SharedObject *SDL_LoadObject(const char *sofile)
{
    SDL_SetError("cannot load %s: StrawOS has no dynamic loading", sofile);
    return NULL;
}

SDL_FunctionPointer SDL_LoadFunction(SDL_SharedObject *handle, const char *name)
{
    (void)handle;
    (void)name;
    return NULL;
}

void SDL_UnloadObject(SDL_SharedObject *handle)
{
    (void)handle;
}

/* threads */

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)fn;
    (void)data;
    SDL_SetError("cannot create thread %s: StrawOS has no threads", name);
    return NULL;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    (void)thread;
    if (status)
        *status = 0;
}

/* with a single thread there is nothing to lock; hand out distinct non-NULL handles */
static char sync_handle;

SDL_Mutex *SDL_CreateMutex(void)
{
    return (SDL_Mutex *)&sync_handle;
}

void SDL_DestroyMutex(SDL_Mutex *mutex)
{
    (void)mutex;
}

void SDL_LockMutex(SDL_Mutex *mutex)
{
    (void)mutex;
}

void SDL_UnlockMutex(SDL_Mutex *mutex)
{
    (void)mutex;
}

SDL_Condition *SDL_CreateCondition(void)
{
    return (SDL_Condition *)&sync_handle;
}

void SDL_DestroyCondition(SDL_Condition *cond)
{
    (void)cond;
}

void SDL_SignalCondition(SDL_Condition *cond)
{
    (void)cond;
}

void SDL_WaitCondition(SDL_Condition *cond, SDL_Mutex *mutex)
{
    (void)cond;
    (void)mutex;
}

/* event queue */

#define QUEUE_MAX       256
#define TEXT_MAX        32
#define DOUBLE_CLICK_MS 400

typedef struct
{
    SDL_Event ev;
    char      text[TEXT_MAX];
}
Queued;

struct SDL_Window
{
    SxWindow   *sx;
    SDL_Surface surface;
};

static struct
{
    Queued     queue[QUEUE_MAX];
    int        q_head, q_count;
    char       text[TEXT_MAX];  /* backs the text of the event last handed out */

    SDL_Window *window;
    float       mouse_x, mouse_y;
    int         click_button, clicks;
    int         click_x, click_y;
    uint64_t    click_time;
    uint32_t    next_user_event;

    char       *clipboard;
}
S = { .next_user_event = SDL_EVENT_USER };

static void push_text(const SDL_Event *e, const char *text)
{
    if (S.q_count == QUEUE_MAX)
        return;
    Queued *q = &S.queue[(S.q_head + S.q_count) % QUEUE_MAX];
    q->ev = *e;
    snprintf(q->text, TEXT_MAX, "%s", text ? text : "");
    S.q_count++;
}

static void push(const SDL_Event *e)
{
    push_text(e, NULL);
}

/* removes the i-th queued event, copying it out */
static void take(int i, SDL_Event *out)
{
    Queued *q = &S.queue[(S.q_head + i) % QUEUE_MAX];
    if (out)
    {
        *out = q->ev;
        memcpy(S.text, q->text, TEXT_MAX);
        if (out->type == SDL_EVENT_TEXT_INPUT)
            out->text.text = S.text;
    }
    for (int j = i; j > 0; j--)
        S.queue[(S.q_head + j) % QUEUE_MAX] = S.queue[(S.q_head + j - 1) % QUEUE_MAX];
    S.q_head = (S.q_head + 1) % QUEUE_MAX;
    S.q_count--;
}

static int sdl_button(int button)
{
    switch (button)
    {
        case SX_BUTTON_LEFT:   return SDL_BUTTON_LEFT;
        case SX_BUTTON_MIDDLE: return SDL_BUTTON_MIDDLE;
        case SX_BUTTON_RIGHT:  return SDL_BUTTON_RIGHT;
        default:               return 0;
    }
}

/* special StrawX keys travel as SDL "scancode" keycodes, plain ASCII as itself */
static SDL_Keycode sdl_key(int key)
{
    return key < 128 ? (SDL_Keycode)key : SDLK_SCANCODE_MASK | (SDL_Keycode)key;
}

/* lite-xl tracks modifiers from key events alone, so a release that happened while another
   window had focus would leave ctrl or shift stuck. Releasing them all on focus changes avoids that. */
static void release_modifiers(void)
{
    static const int mods[] = { SX_KEY_LSHIFT, SX_KEY_RSHIFT, SX_KEY_LCTRL, SX_KEY_RCTRL,
                                SX_KEY_LALT, SX_KEY_RALT };
    for (int i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); i++)
    {
        SDL_Event e = { .key = { SDL_EVENT_KEY_UP, SDL_SCANCODE_UNKNOWN, sdl_key(mods[i]) } };
        push(&e);
    }
}

static void translate(const SxEvent *ev)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));

    switch (ev->type)
    {
        case SX_EVENT_CLOSE:
            e.type = SDL_EVENT_QUIT;
            push(&e);
            break;

        case SX_EVENT_DISCONNECTED:
            exit(0);

        case SX_EVENT_RESIZE:
            e.type = SDL_EVENT_WINDOW_RESIZED;
            e.window.data1 = ev->width;
            e.window.data2 = ev->height;
            push(&e);
            /* also sent on theme changes; repaint everything to be safe */
            e.type = SDL_EVENT_WINDOW_EXPOSED;
            push(&e);
            break;

        case SX_EVENT_FOCUS_IN:
        case SX_EVENT_FOCUS_OUT:
            release_modifiers();
            e.type = ev->type == SX_EVENT_FOCUS_IN ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
            push(&e);
            break;

        case SX_EVENT_MOUSE_LEAVE:
            e.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
            push(&e);
            break;

        case SX_EVENT_KEY_DOWN:
        case SX_EVENT_KEY_UP:
            e.type = ev->type == SX_EVENT_KEY_DOWN ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = sdl_key(ev->key);
            e.key.mod = SDL_KMOD_NUM;
            e.key.down = ev->type == SX_EVENT_KEY_DOWN;
            push(&e);
            if (ev->type == SX_EVENT_KEY_DOWN && ev->ch >= 32 && ev->ch < 127 &&
                !(ev->mods & (SX_MOD_CTRL | SX_MOD_ALT)))
            {
                char text[2] = { (char)ev->ch, '\0' };
                memset(&e, 0, sizeof(e));
                e.type = SDL_EVENT_TEXT_INPUT;
                push_text(&e, text);
            }
            break;

        case SX_EVENT_MOUSE_DOWN:
        {
            int b = sdl_button(ev->button);
            uint64_t now = sx_time_ms();
            if (b == S.click_button && now - S.click_time < DOUBLE_CLICK_MS &&
                abs(ev->x - S.click_x) < 4 && abs(ev->y - S.click_y) < 4)
                S.clicks++;
            else
                S.clicks = 1;
            S.click_button = b;
            S.click_time = now;
            S.click_x = ev->x;
            S.click_y = ev->y;

            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = (uint8_t)b;
            e.button.down = true;
            e.button.clicks = (uint8_t)S.clicks;
            e.button.x = (float)ev->x;
            e.button.y = (float)ev->y;
            push(&e);
            break;
        }

        case SX_EVENT_MOUSE_UP:
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = (uint8_t)sdl_button(ev->button);
            e.button.clicks = (uint8_t)S.clicks;
            e.button.x = (float)ev->x;
            e.button.y = (float)ev->y;
            push(&e);
            break;

        case SX_EVENT_MOUSE_MOVE:
        case SX_EVENT_MOUSE_ENTER:
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = (float)ev->x;
            e.motion.y = (float)ev->y;
            e.motion.xrel = (float)ev->x - S.mouse_x;
            e.motion.yrel = (float)ev->y - S.mouse_y;
            S.mouse_x = (float)ev->x;
            S.mouse_y = (float)ev->y;
            push(&e);
            break;

        case SX_EVENT_MOUSE_WHEEL:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.y = (float)ev->wheel;
            push(&e);
            break;

        default:
            break;
    }
}

/* waits up to timeout_ms (-1 forever) for StrawX events; true if any were queued */
static bool pump(int timeout_ms)
{
    SxEvent ev;
    int r = sx_next_event(&ev, timeout_ms);
    if (r < 0)
        exit(0);
    while (r > 0)
    {
        translate(&ev);
        r = sx_next_event(&ev, 0);
    }
    return S.q_count > 0;
}

bool SDL_PollEvent(SDL_Event *event)
{
    if (S.q_count == 0)
        pump(0);
    if (S.q_count == 0)
        return false;
    take(0, event);
    return true;
}

bool SDL_WaitEventTimeout(SDL_Event *event, int32_t timeout_ms)
{
    if (S.q_count == 0)
        pump(timeout_ms);
    if (S.q_count == 0)
        return false;
    if (event)
        take(0, event);
    return true;
}

bool SDL_WaitEvent(SDL_Event *event)
{
    return SDL_WaitEventTimeout(event, -1);
}

void SDL_PumpEvents(void)
{
    pump(0);
}

int SDL_PeepEvents(SDL_Event *events, int numevents, SDL_EventAction action,
                   uint32_t min_type, uint32_t max_type)
{
    int n = 0;
    for (int i = 0; i < S.q_count && n < numevents; )
    {
        SDL_Event *e = &S.queue[(S.q_head + i) % QUEUE_MAX].ev;
        if (e->type < min_type || e->type > max_type)
        {
            i++;
            continue;
        }
        if (action == SDL_GETEVENT)
            take(i, &events[n++]);
        else
        {
            if (events)
                events[n] = *e;
            n++;
            i++;
        }
    }
    return n;
}

bool SDL_PushEvent(SDL_Event *event)
{
    push(event);
    return true;
}

void SDL_FlushEvent(uint32_t type)
{
    for (int i = 0; i < S.q_count; )
    {
        if (S.queue[(S.q_head + i) % QUEUE_MAX].ev.type == type)
            take(i, NULL);
        else
            i++;
    }
}

uint32_t SDL_RegisterEvents(int numevents)
{
    uint32_t first = S.next_user_event;
    S.next_user_event += (uint32_t)numevents;
    return first;
}

void SDL_SetEventEnabled(uint32_t type, bool enabled)
{
    (void)type;
    (void)enabled;
}

/* time */

uint64_t SDL_GetTicks(void)
{
    return sx_time_ms();
}

uint64_t SDL_GetPerformanceCounter(void)
{
    return sx_time_ms();
}

uint64_t SDL_GetPerformanceFrequency(void)
{
    return 1000;
}

void SDL_Delay(uint32_t ms)
{
    /* sleep on the event socket so input wakes lite-xl up straight away */
    if (S.q_count == 0)
        pump((int)ms);
}

/* displays and windows */

SDL_DisplayID SDL_GetPrimaryDisplay(void)
{
    return 1;
}

const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID id)
{
    static SDL_DisplayMode mode;
    mode = (SDL_DisplayMode){ id, SDL_PIXELFORMAT_BGRA32, sx_screen_width(), sx_screen_height(), 1.0f, 60.0f };
    return &mode;
}

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{
    SDL_Window *win = calloc(1, sizeof(SDL_Window));
    if (!win)
        return NULL;
    win->sx = sx_window_create(title, w, h, flags & SDL_WINDOW_RESIZABLE ? SX_WINDOW_RESIZABLE : 0);
    if (!win->sx)
    {
        free(win);
        SDL_SetError("cannot create a window");
        return NULL;
    }
    win->surface.format = SDL_PIXELFORMAT_BGRA32;
    S.window = win;
    return win;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (!window)
        return;
    sx_window_destroy(window->sx);
    free(window->surface.pixels);
    free(window->surface.props);
    if (S.window == window)
        S.window = NULL;
    free(window);
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
    return SDL_WINDOW_RESIZABLE | (window->sx->focused ? SDL_WINDOW_INPUT_FOCUS : 0);
}

SDL_PixelFormat SDL_GetWindowPixelFormat(SDL_Window *window)
{
    (void)window;
    return SDL_PIXELFORMAT_BGRA32;
}

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    SDL_Surface *s = &window->surface;
    int w = sx_window_width(window->sx);
    int h = sx_window_height(window->sx);
    if (!s->pixels || s->w != w || s->h != h)
    {
        void *pixels = calloc((size_t)w * (size_t)h, 4);
        if (pixels)
        {
            free(s->pixels);
            s->pixels = pixels;
            s->w = w;
            s->h = h;
            s->pitch = w * 4;
            s->clip = (SDL_Rect){ 0, 0, w, h };
        }
    }
    return s;
}

bool SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *rects, int count)
{
    SDL_Surface *s = &window->surface;
    SxCanvas *c = sx_window_canvas(window->sx);
    int max_w = s->w < c->width ? s->w : c->width;
    int max_h = s->h < c->height ? s->h : c->height;

    int x1 = max_w, y1 = max_h, x2 = 0, y2 = 0;
    for (int i = 0; i < count; i++)
    {
        int rx1 = rects[i].x < 0 ? 0 : rects[i].x;
        int ry1 = rects[i].y < 0 ? 0 : rects[i].y;
        int rx2 = rects[i].x + rects[i].w > max_w ? max_w : rects[i].x + rects[i].w;
        int ry2 = rects[i].y + rects[i].h > max_h ? max_h : rects[i].y + rects[i].h;
        if (rx2 <= rx1 || ry2 <= ry1)
            continue;

        for (int y = ry1; y < ry2; y++)
            memcpy(c->pixels + (size_t)y * c->stride + rx1,
                   (uint32_t *)s->pixels + (size_t)y * s->w + rx1,
                   (size_t)(rx2 - rx1) * 4);

        if (rx1 < x1) x1 = rx1;
        if (ry1 < y1) y1 = ry1;
        if (rx2 > x2) x2 = rx2;
        if (ry2 > y2) y2 = ry2;
    }

    if (x2 > x1 && y2 > y1)
        sx_window_damage(window->sx, x1, y1, x2 - x1, y2 - y1);
    return true;
}

bool SDL_UpdateWindowSurface(SDL_Window *window)
{
    SDL_Surface *s = SDL_GetWindowSurface(window);
    SDL_Rect all = { 0, 0, s->w, s->h };
    return SDL_UpdateWindowSurfaceRects(window, &all, 1);
}

bool SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    if (w)
        *w = sx_window_width(window->sx);
    if (h)
        *h = sx_window_height(window->sx);
    return true;
}

bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{
    return SDL_GetWindowSize(window, w, h);
}

bool SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    sx_window_resize(window->sx, w, h);
    return true;
}

/* StrawX does not report where a window is, so positions are not saved or restored */
bool SDL_GetWindowPosition(SDL_Window *window, int *x, int *y)
{
    (void)window;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    return true;
}

bool SDL_SetWindowPosition(SDL_Window *window, int x, int y)
{
    (void)window;
    (void)x;
    (void)y;
    return true;
}

bool SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    sx_window_set_title(window->sx, title);
    return true;
}

bool SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon)
{
    (void)window;
    (void)icon;
    return true;
}

bool SDL_ShowWindow(SDL_Window *window)
{
    /* a StrawX window appears on its first commit */
    (void)window;
    return true;
}

bool SDL_RaiseWindow(SDL_Window *window)
{
    (void)window;
    return true;
}

bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
    (void)window;
    (void)fullscreen;
    return SDL_SetError("fullscreen is not supported");
}

bool SDL_MaximizeWindow(SDL_Window *window)
{
    (void)window;
    return true;
}

bool SDL_MinimizeWindow(SDL_Window *window)
{
    (void)window;
    return true;
}

bool SDL_RestoreWindow(SDL_Window *window)
{
    (void)window;
    return true;
}

bool SDL_SetWindowBordered(SDL_Window *window, bool bordered)
{
    (void)window;
    (void)bordered;
    return true;
}

bool SDL_SetWindowOpacity(SDL_Window *window, float opacity)
{
    (void)window;
    (void)opacity;
    return SDL_SetError("window opacity is not supported");
}

bool SDL_SetWindowHitTest(SDL_Window *window, SDL_HitTest callback, void *data)
{
    (void)window;
    (void)callback;
    (void)data;
    return SDL_SetError("custom window frames are not supported");
}

bool SDL_StartTextInput(SDL_Window *window)
{
    (void)window;
    return true;
}

bool SDL_SetTextInputArea(SDL_Window *window, const SDL_Rect *rect, int cursor)
{
    (void)window;
    (void)rect;
    (void)cursor;
    return true;
}

bool SDL_ClearComposition(SDL_Window *window)
{
    (void)window;
    return true;
}

/* keyboard */

const char *SDL_GetKeyName(SDL_Keycode key)
{
    static char ascii[2];
    static const char *names[] = {
        [SX_KEY_UP - SX_KEY_UP]        = "Up",
        [SX_KEY_DOWN - SX_KEY_UP]      = "Down",
        [SX_KEY_LEFT - SX_KEY_UP]      = "Left",
        [SX_KEY_RIGHT - SX_KEY_UP]     = "Right",
        [SX_KEY_HOME - SX_KEY_UP]      = "Home",
        [SX_KEY_END - SX_KEY_UP]       = "End",
        [SX_KEY_PAGE_UP - SX_KEY_UP]   = "PageUp",
        [SX_KEY_PAGE_DOWN - SX_KEY_UP] = "PageDown",
        [SX_KEY_INSERT - SX_KEY_UP]    = "Insert",
        [SX_KEY_F1 - SX_KEY_UP]  = "F1",  [SX_KEY_F2 - SX_KEY_UP]  = "F2",
        [SX_KEY_F3 - SX_KEY_UP]  = "F3",  [SX_KEY_F4 - SX_KEY_UP]  = "F4",
        [SX_KEY_F5 - SX_KEY_UP]  = "F5",  [SX_KEY_F6 - SX_KEY_UP]  = "F6",
        [SX_KEY_F7 - SX_KEY_UP]  = "F7",  [SX_KEY_F8 - SX_KEY_UP]  = "F8",
        [SX_KEY_F9 - SX_KEY_UP]  = "F9",  [SX_KEY_F10 - SX_KEY_UP] = "F10",
        [SX_KEY_F11 - SX_KEY_UP] = "F11", [SX_KEY_F12 - SX_KEY_UP] = "F12",
        [SX_KEY_LSHIFT - SX_KEY_UP]    = "Left Shift",
        [SX_KEY_RSHIFT - SX_KEY_UP]    = "Right Shift",
        [SX_KEY_LCTRL - SX_KEY_UP]     = "Left Ctrl",
        [SX_KEY_RCTRL - SX_KEY_UP]     = "Right Ctrl",
        [SX_KEY_LALT - SX_KEY_UP]      = "Left Alt",
        [SX_KEY_RALT - SX_KEY_UP]      = "Right Alt",
        [SX_KEY_LSUPER - SX_KEY_UP]    = "Left GUI",
        [SX_KEY_RSUPER - SX_KEY_UP]    = "Right GUI",
        [SX_KEY_CAPS_LOCK - SX_KEY_UP] = "CapsLock",
        [SX_KEY_MENU - SX_KEY_UP]      = "Menu",
    };

    switch (key)
    {
        case SX_KEY_BACKSPACE: return "Backspace";
        case SX_KEY_TAB:       return "Tab";
        case SX_KEY_ENTER:     return "Return";
        case SX_KEY_ESCAPE:    return "Escape";
        case SX_KEY_SPACE:     return "Space";
        case SX_KEY_DELETE:    return "Delete";
    }
    if (key > 32 && key < 127)
    {
        ascii[0] = (char)key;
        return ascii;
    }
    int sx = (int)(key & ~SDLK_SCANCODE_MASK);
    if (sx >= SX_KEY_UP && sx - SX_KEY_UP < (int)(sizeof(names) / sizeof(names[0])) &&
        names[sx - SX_KEY_UP])
        return names[sx - SX_KEY_UP];
    return "";
}

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
    (void)scancode;
    return "";
}

/* mouse */

SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor id)
{
    /* StrawX has a single cursor shape; hand out distinct non-NULL handles */
    static char cursors[SDL_SYSTEM_CURSOR_POINTER + 1];
    return (SDL_Cursor *)&cursors[id];
}

bool SDL_SetCursor(SDL_Cursor *cursor)
{
    (void)cursor;
    return true;
}

bool SDL_CaptureMouse(bool enabled)
{
    (void)enabled;
    return true;
}

uint32_t SDL_GetMouseState(float *x, float *y)
{
    if (x)
        *x = S.mouse_x;
    if (y)
        *y = S.mouse_y;
    return 0;
}

/* StrawOS has no system clipboard yet, so copy and paste work within lite-xl */

char *SDL_GetClipboardText(void)
{
    return strdup(S.clipboard ? S.clipboard : "");
}

bool SDL_SetClipboardText(const char *text)
{
    char *copy = strdup(text);
    if (!copy)
        return false;
    free(S.clipboard);
    S.clipboard = copy;
    return true;
}

/* message box: a small modal window with the message and an OK button */

#define BOX_W     460
#define BOX_PAD   20
#define BUTTON_W  88
#define BUTTON_H  30
#define BOX_LINES 10

static int wrap_message(const char *p, char lines[][128], int max_lines)
{
    int count = 0;
    int max_w = BOX_W - 2 * BOX_PAD;
    while (*p && count < max_lines)
    {
        char *line = lines[count++];
        int len = 0, fit = 0;
        while (p[len] && p[len] != '\n' && len < 127)
        {
            len++;
            memcpy(line, p, len);
            line[len] = '\0';
            if (sx_text_width(&sx_font_sans, line) > max_w)
            {
                len--;
                break;
            }
            if (p[len] == ' ' || p[len] == '\0' || p[len] == '\n')
                fit = len;
        }
        if (fit == 0)
            fit = len;
        memcpy(line, p, fit);
        line[fit] = '\0';
        p += fit;
        if (*p == '\n')
            p++;
        else
            while (*p == ' ')
                p++;
    }
    return count;
}

static void draw_box(SxWindow *w, char lines[][128], int count, SxRect button, SxButtonState state)
{
    SxCanvas *c = sx_window_canvas(w);
    sx_clear(c, SX_THEME_BG);
    int lh = sx_font_line_height(&sx_font_sans);
    for (int i = 0; i < count; i++)
        sx_draw_text(c, &sx_font_sans, BOX_PAD, BOX_PAD + i * lh, lines[i], SX_THEME_TEXT);
    sx_ui_button(c, button, "OK", &sx_font_sans, state, SX_THEME_ACCENT, SX_THEME_ACCENT_TEXT);
    sx_window_commit(w);
}

bool SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window)
{
    (void)flags;
    (void)window;
    printf("%s: %s\n", title, message);
    if (sx_connect() != 0)
        return false;

    char lines[BOX_LINES][128];
    int count = wrap_message(message, lines, BOX_LINES);
    int h = BOX_PAD * 3 + count * sx_font_line_height(&sx_font_sans) + BUTTON_H;
    SxRect button = sx_rect(BOX_W - BOX_PAD - BUTTON_W, h - BOX_PAD - BUTTON_H, BUTTON_W, BUTTON_H);

    SxWindow *w = sx_window_create_at(title, (sx_screen_width() - BOX_W) / 2,
                                      (sx_screen_height() - h) / 2, BOX_W, h, 0);
    if (!w)
        return false;
    SxButtonState state = SX_BUTTON_NORMAL;
    draw_box(w, lines, count, button, state);

    for (bool done = false; !done; )
    {
        SxEvent ev;
        if (sx_next_event(&ev, -1) < 0)
            break;
        if (ev.window != w)
            continue;   /* the editor window is modal-blocked */

        SxButtonState next = state;
        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
                done = true;
                break;
            case SX_EVENT_KEY_DOWN:
                done = ev.key == SX_KEY_ENTER || ev.key == SX_KEY_ESCAPE;
                break;
            case SX_EVENT_MOUSE_MOVE:
                if (state != SX_BUTTON_PRESSED)
                    next = sx_rect_contains(button, ev.x, ev.y) ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL;
                break;
            case SX_EVENT_MOUSE_DOWN:
                if (ev.button == SX_BUTTON_LEFT && sx_rect_contains(button, ev.x, ev.y))
                    next = SX_BUTTON_PRESSED;
                break;
            case SX_EVENT_MOUSE_UP:
                if (state == SX_BUTTON_PRESSED)
                {
                    done = sx_rect_contains(button, ev.x, ev.y);
                    next = SX_BUTTON_NORMAL;
                }
                break;
            case SX_EVENT_RESIZE:
                draw_box(w, lines, count, button, state);
                break;
            default:
                break;
        }
        if (next != state)
        {
            state = next;
            draw_box(w, lines, count, button, state);
        }
    }

    sx_window_destroy(w);
    return true;
}
