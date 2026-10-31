#define MAX_CLIENTS   32
#define MAX_WINDOWS   64
#define MAX_DAMAGE    32
#define CLIENT_RX_MAX 8192

#define TITLE_H       36
#define FRAME_RADIUS  12
#define SHADOW_SIZE   28
#define SHADOW_OFF_Y  8
#define RESIZE_GRIP   6
#define MIN_WIN_W     160
#define MIN_WIN_H     60

#define BAR_H         32
#define WORKSPACES    4
#define SNAP_GAP      10
#define SNAP_EDGE     4
#define DRAG_THRESHOLD 6

#define DOCK_ICON     44
#define DOCK_PAD      8
#define DOCK_GAP      6
#define DOCK_MARGIN   10
#define DOCK_H        (DOCK_ICON + DOCK_PAD * 2 + 6)
#define DOCK_REVEAL   3

#define DEVFS_IOCTL_FB_GET_INFO 0x1000
#define DEVFS_IOCTL_FB_ACQUIRE  0x1001

typedef struct
{
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
}
FbDeviceInfo;

typedef struct
{
    int16_t  dx;
    int16_t  dy;
    int8_t   wheel;
    uint8_t  buttons;
    uint16_t reserved;
}
MouseEvent;

typedef struct Client Client;
typedef struct Window Window;

// A window rendered offscreen for animating; alpha holds the rounded-corner coverage.
typedef struct
{
    uint32_t *px;
    int       w, h;
    SxRect    frame;
}
AnimSnap;

typedef enum
{
    FADE_TITLE,
    FADE_BAR,
    FADE_DOCK,
    FADE_MENU,
    FADE_DESK,
    FADE_KINDS,
}
FadeKind;

struct Client
{
    bool     used;
    int      fd;
    uint32_t id;
    uint8_t  rx[CLIENT_RX_MAX];
    size_t   rx_len;
    uint64_t pid;
    char     exe[TASKINFO_NAME_MAX];
};

struct Window
{
    bool      used;
    uint32_t  id;
    Client   *client;
    char      title[SX_TITLE_MAX];
    uint32_t  flags;

    int       x, y;
    int       w, h;

    uint32_t *buf;
    int       buf_w, buf_h, buf_stride;

    bool      mapped;
    bool      hidden;
    bool      minimized;
    bool      maximized;
    SxRect    restore;

    bool      size_driven;
    bool      cfg_pending;

    int       workspace;
    bool      snapped;

    AnimSnap  close_snap;
    uint64_t  close_snap_ms;

    uint64_t  created_order;
};

typedef enum
{
    HIT_NONE,
    HIT_CLIENT,
    HIT_TITLE,
    HIT_CLOSE,
    HIT_MAXIMIZE,
    HIT_MINIMIZE,
    // Resize zones are HIT_RESIZE combined with the EDGE_* bits being dragged.
    HIT_RESIZE    = 0x10,
    HIT_RESIZE_L  = HIT_RESIZE | 0x1,
    HIT_RESIZE_R  = HIT_RESIZE | 0x2,
    HIT_RESIZE_T  = HIT_RESIZE | 0x4,
    HIT_RESIZE_B  = HIT_RESIZE | 0x8,
    HIT_RESIZE_TL = HIT_RESIZE_T | HIT_RESIZE_L,
    HIT_RESIZE_TR = HIT_RESIZE_T | HIT_RESIZE_R,
    HIT_RESIZE_BL = HIT_RESIZE_B | HIT_RESIZE_L,
    HIT_RESIZE_BR = HIT_RESIZE_B | HIT_RESIZE_R,
}
HitZone;

#define EDGE_L 0x1
#define EDGE_R 0x2
#define EDGE_T 0x4
#define EDGE_B 0x8

#define RESIZE_CORNER 14

typedef enum
{
    CURSOR_ARROW,
    CURSOR_RESIZE_H,
    CURSOR_RESIZE_V,
    CURSOR_RESIZE_D,
    CURSOR_RESIZE_D2,
    CURSOR_COUNT,
}
CursorKind;

typedef enum
{
    DRAG_NONE,
    DRAG_MOVE,
    DRAG_RESIZE,
    DRAG_CLIENT,
    DRAG_TITLE_BUTTON,
}
DragMode;

static struct
{
    int           fb_fd;
    FbDeviceInfo  fb;
    uint8_t      *fb_mem;
    bool          fb_is_xrgb;
    int           width, height;

    uint32_t     *back;
    SxCanvas      canvas;
    uint32_t     *wallpaper;
    uint32_t     *layer;

    SxRect        damage[MAX_DAMAGE];
    int           damage_count;

    int           listen_fd;
    int           kbd_fd;
    int           mouse_fd;
    Client        clients[MAX_CLIENTS];
    Window        windows[MAX_WINDOWS];
    Window       *zorder[MAX_WINDOWS];
    int           zcount;
    uint32_t      next_window_id;
    uint32_t      next_client_id;
    uint64_t      order_counter;
    int           cascade;

    Window       *focused;

    int           mx, my;
    uint8_t       buttons;
    CursorKind    cursor;
    SxRect        cursor_rect;
    Window       *hover_win;
    HitZone       hover_zone;
    Window       *hover_frame_win;

    DragMode      drag;
    Window       *drag_win;
    HitZone       drag_zone;
    int           drag_start_mx, drag_start_my;
    SxRect        drag_start_rect;
    SxRect        drag_orig_rect;
    bool          drag_detach;

    bool          snap_active;
    SxRect        snap_rect;
    int           snap_kind;

    int           workspace;
    Window       *ws_focus[WORKSPACES];

    uint64_t      last_click_ms;
    int           last_click_x, last_click_y;
    Window       *last_click_win;

    bool          kbd_e0;
    uint32_t      mods;
    bool          caps_lock;

    bool          menu_open;
    int           menu_hover;
    int           menu_sel;
    int           menu_scroll;
    char          menu_query[32];
    int           bar_hover;
    int           dock_hover;
    float         dock_level;
    int           desktop_selected;
    int           desktop_hover;
    int           last_minute;

    bool          animations;
    bool          anim_busy;
    float         menu_level;

    bool          clock_24h;
    bool          show_date;
    bool          desktop_icons;
    int           pointer_speed;
    int           mouse_frac_x, mouse_frac_y;
}
S;

static void damage_add(SxRect r);
static void damage_all(void);
static SxRect window_frame_rect(Window *w);
static SxRect window_outer_rect(Window *w);
static SxRect window_client_rect(Window *w);
static void window_focus(Window *w);
static void window_raise(Window *w);
static void window_destroy(Window *w);
static void window_set_minimized(Window *w, bool minimized);
static void window_toggle_maximize(Window *w);
static void window_request_close(Window *w);
static bool window_visible(Window *w);
static void send_event(Window *w, uint16_t type, const SxEvtMsg *proto);
static void shell_launch(int app_index);
static SxRect bar_rect(void);
static SxRect dock_rect(void);
static SxRect dock_outer_rect(void);
static SxRect menu_rect(void);
static SxRect work_area(void);
static void damage_chrome(void);
static void update_hover(void);
static void settings_load(void);
static void damage_window(Window *w);
static SxRect frame_outer_rect(SxRect f);
static SxRect menu_outer_rect(void);
static int menu_slide_offset(void);
static float fade_level(FadeKind kind, int64_t key);
static bool anim_draw_window(SxCanvas *c, Window *w);
static void anim_draw_ghosts(SxCanvas *c);
static void draw_faded(SxCanvas *c, SxRect bounds, void (*fn)(SxCanvas *), float alpha);
