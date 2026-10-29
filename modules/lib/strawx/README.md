# StrawX client library

StrawX is the StrawOS display server. This library is how programs talk to it: it connects
to the server, creates windows, hands you a pixel canvas to draw into, and delivers mouse and
keyboard events. It also has 2D drawing, text, image loading, a couple of UI widgets and the
system theme. All the built-in apps (`src/apps/`) are written against it.

## Building a StrawX program

### Inside StrawOS, with TinyCC

The headers are installed in `/usr/include/strawx` (on tcc's default include path) and the
library in `/usr/lib/libstrawx.a`:

```sh
tcc myapp.c -lstrawx -o myapp
./myapp
```

A complete example lives at `/examples/strawx_test.c`.

### As a built-in app

Put the source in `modules/src/apps/<name>.c` and add `<name>` to the `for app in ...` list in
`modules/build.sh`. It is linked with libc and StrawX into `/bin/<name>.elf`. To give it a menu
entry, add `modules/root/desktop/<name>.desktop`:

```ini
[Desktop Entry]
Name=My App
Comment=What it does
Icon=/share/icons/myapp.png
Exec=/bin/myapp.elf
```

## A minimal program

```c
#include <stdio.h>
#include <strawx.h>
#include <ui.h>

static void draw(SxWindow *win)
{
    SxCanvas *c = sx_window_canvas(win);
    sx_clear(c, SX_THEME_BG);
    sx_draw_text_centered(c, &sx_font_sans_large, sx_rect(0, 0, c->width, c->height),
                          "Hello, StrawX", SX_THEME_TEXT);
    sx_window_commit(win);
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("hello: cannot connect to the display server\n");
        return 1;
    }

    SxWindow *win = sx_window_create("Hello", 320, 200, SX_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    draw(win);

    for (;;)
    {
        SxEvent ev;
        if (sx_next_event(&ev, -1) < 0)
            break;
        if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
            break;
        if (ev.type == SX_EVENT_RESIZE)
            draw(win);
    }

    sx_window_destroy(win);
    sx_disconnect();
    return 0;
}
```

## Connecting

| Function | |
| --- | --- |
| `int sx_connect(void)` | Connects to the server (retries for about 5 seconds) and loads the theme. Returns 0 on success. |
| `void sx_disconnect(void)` | Destroys any remaining windows and closes the connection. |
| `int sx_screen_width(void)`, `int sx_screen_height(void)` | Screen size in pixels. |
| `int sx_fd(void)` | The connection's file descriptor, if you want to `poll()` it next to other fds. |
| `uint64_t sx_time_ms(void)` | Milliseconds since boot, handy for animation. |

`sx_connect()` closes the program's stdin, so a windowed program run from the terminal does not
read terminal input. `printf` output still goes to the terminal.

## Windows

```c
SxWindow *sx_window_create(const char *title, int width, int height, uint32_t flags);
SxWindow *sx_window_create_at(const char *title, int x, int y, int width, int height, uint32_t flags);
void      sx_window_destroy(SxWindow *win);
```

`width` and `height` are the size of the content area. The server draws the frame and title bar.
`sx_window_create` lets the server pick the position (`SX_POS_AUTO`). Flags:

- `SX_WINDOW_RESIZABLE`: the user can resize the window.
- `SX_WINDOW_NO_FRAME`: no title bar or border, for popups and panels.

A new window stays off screen until its first `sx_window_commit`, so draw something right away.

Other window calls: `sx_window_set_title`, `sx_window_set_visible`, `sx_window_move`,
`sx_window_resize`, `sx_window_width`, `sx_window_height`. You can store your own pointer in
`win->user`, and `win->focused` tracks keyboard focus.

### Drawing and presenting

```c
SxCanvas *sx_window_canvas(SxWindow *win);
void      sx_window_commit(SxWindow *win);
void      sx_window_damage(SxWindow *win, int x, int y, int w, int h);
```

Windows are double-buffered. `sx_window_canvas` returns the back buffer. Draw into it, then call
`sx_window_commit` to show the whole buffer, or `sx_window_damage` to show only the part that
changed. Fetch the canvas again after every commit, because it then points at the other buffer.

The new back buffer starts as a copy of the last frame you showed, so you can redraw just a
region and damage only that region. After a resize the buffer is reallocated at the new size,
and you must redraw everything.

`SxCanvas` exposes `pixels`, `width`, `height` and `stride` (in pixels). Pixels are 32-bit ARGB
(`0xAARRGGBB`). If you need to, you can write to them directly.

## Events

```c
int  sx_next_event(SxEvent *ev, int timeout_ms);
bool sx_pending(void);
```

`sx_next_event` waits up to `timeout_ms` for an event. `-1` waits forever and `0` only polls. It
returns 1 when it fills in `ev`, 0 on timeout and -1 when the connection to the server is lost.
For animation or periodic updates, pass a timeout and redraw when it returns 0 (see
`src/apps/clock.c`).

`ev.window` says which window the event belongs to. Which other fields are set depends on
`ev.type`:

| Type | Fields |
| --- | --- |
| `SX_EVENT_MOUSE_MOVE`, `_ENTER`, `_LEAVE` | `x`, `y` (relative to the content area), `buttons`, `mods` |
| `SX_EVENT_MOUSE_DOWN`, `_UP` | the above plus `button` (`SX_BUTTON_LEFT`, `_RIGHT`, `_MIDDLE`) |
| `SX_EVENT_MOUSE_WHEEL` | `wheel`: positive for scrolling up, negative for down |
| `SX_EVENT_KEY_DOWN`, `_KEY_UP` | `key` (an `SX_KEY_*` code), `ch` (the typed character, or 0), `mods` |
| `SX_EVENT_RESIZE` | `width`, `height`: redraw the window |
| `SX_EVENT_FOCUS_IN`, `_FOCUS_OUT` | |
| `SX_EVENT_CLOSE` | The user clicked the close button. Destroying the window is up to you. |
| `SX_EVENT_DISCONNECTED` | The server went away |

`buttons` is a bitmask of the buttons held down. `mods` is a bitmask of `SX_MOD_SHIFT`,
`SX_MOD_CTRL`, `SX_MOD_ALT`, `SX_MOD_SUPER` and `SX_MOD_CAPS`.

For text input use `ch`, which already accounts for Shift and Caps Lock. For everything else use
`key`. Printable keys and Backspace, Tab, Enter, Escape and Delete map to their ASCII codes.
Arrows, Home/End, Page Up/Down, Insert, F1–F12 and the modifier keys have their own `SX_KEY_*`
values from 256 up (see `protocol.h`).

When the user switches themes, every window gets an `SX_EVENT_RESIZE` at its current size. Any
program that redraws on resize therefore picks up the new colors automatically.

## Drawing (`gfx.h`)

Colors are built with `SX_RGB(r, g, b)` (opaque) or `SX_RGBA(r, g, b, a)`. Every drawing
function alpha-blends and respects the canvas clip rectangle.

```c
void sx_clear(SxCanvas *c, uint32_t color);
void sx_fill_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color);
void sx_draw_rect(SxCanvas *c, int x, int y, int w, int h, uint32_t color);
void sx_fill_gradient(SxCanvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void sx_fill_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color);
void sx_draw_rounded_rect(SxCanvas *c, int x, int y, int w, int h, int radius, uint32_t color);
void sx_fill_circle(SxCanvas *c, float cx, float cy, float r, uint32_t color);
void sx_draw_circle(SxCanvas *c, float cx, float cy, float r, float thickness, uint32_t color);
void sx_draw_line(SxCanvas *c, float x0, float y0, float x1, float y1, float thickness, uint32_t color);
void sx_blit(SxCanvas *c, int x, int y, const uint32_t *src, int w, int h, int src_stride);
```

Circles and lines are anti-aliased and take float coordinates.

**Clipping:** `sx_canvas_set_clip(c, rect)` limits drawing to a rectangle, and
`sx_canvas_reset_clip(c)` removes the limit. This is useful for scrolling lists and panels.

**Rectangles:** `sx_rect(x, y, w, h)` builds an `SxRect`. There are also `sx_rect_contains`
(hit testing), `sx_rect_intersect`, `sx_rect_union` and `sx_rect_empty`.

**Color helpers:** `sx_lighten(c, amount)`, `sx_darken(c, amount)`, `sx_mix(a, b, t)` (t from 0
to 255) and `sx_blend(dst, src, alpha)`.

### Text

```c
int  sx_draw_text(SxCanvas *c, const SxFont *font, int x, int y, const char *text, uint32_t color);
int  sx_draw_text_n(SxCanvas *c, const SxFont *font, int x, int y, const char *text, int len, uint32_t color);
void sx_draw_text_centered(SxCanvas *c, const SxFont *font, SxRect box, const char *text, uint32_t color);
int  sx_text_width(const SxFont *font, const char *text);
int  sx_char_width(const SxFont *font, char ch);
int  sx_font_line_height(const SxFont *font);
int  sx_font_cap_height(const SxFont *font);
int  sx_text_y_centered(const SxFont *font, SxRect box);
```

`(x, y)` is the top-left corner of the line, not the baseline. `sx_draw_text` returns the width
it drew. Only printable ASCII (32–126) is supported.

| Font | Face | Size |
| --- | --- | --- |
| `sx_font_sans` | Noto Sans | 14 |
| `sx_font_sans_small` | Noto Sans | 12 |
| `sx_font_sans_large` | Noto Sans | 24 |
| `sx_font_sans_huge` | Noto Sans Light | 44 |
| `sx_font_sans_bold` | Noto Sans Bold | 14 |
| `sx_font_sans_italic` | Noto Sans Italic | 14 |
| `sx_font_sans_h1` / `_h2` / `_h3` | Noto Sans Bold | 26 / 21 / 17 |
| `sx_font_mono` | Noto Sans Mono | 15 |
| `sx_font_mono_small` | Noto Sans Mono | 13 |

Fonts are rasterized the first time they are used, so pass them by address (`&sx_font_sans`).

## Images (`image.h`)

```c
SxImage *sx_image_load(const char *path);
SxImage *sx_image_decode(const uint8_t *data, size_t size);
SxImage *sx_image_create(int width, int height);
SxImage *sx_image_scale(const SxImage *img, int width, int height);
void     sx_image_free(SxImage *img);
void     sx_draw_image(SxCanvas *c, const SxImage *img, int x, int y);
void     sx_draw_image_scaled(SxCanvas *c, const SxImage *img, int x, int y, int w, int h);
```

Decoding is done by stb_image (PNG, JPEG, BMP, GIF, TGA and PPM/PGM). `sx_image_is_supported(path)`
checks a file's extension, and
`sx_image_error()` explains why a load failed. `SxImage` has `width`, `height` and `pixels` in
the same ARGB format as the canvas.

## Theme and widgets (`ui.h`, `theme.h`)

`ui.h` defines macros for the current theme's colors. Use them instead of hard-coded colors so
your app matches the desktop and follows theme changes:

`SX_THEME_BG`, `SX_THEME_SURFACE`, `SX_THEME_SURFACE_HI`, `SX_THEME_SUNKEN`, `SX_THEME_BORDER`,
`SX_THEME_TEXT`, `SX_THEME_TEXT_DIM`, `SX_THEME_DISABLED`, `SX_THEME_ACCENT`,
`SX_THEME_ACCENT_TEXT`, `SX_THEME_BLUE`, `SX_THEME_RED`, `SX_THEME_GREEN`,
plus `SX_THEME_ACCENT_A(alpha)` and `SX_THEME_RED_A(alpha)` for translucent variants.

The full theme is available as `sx_theme`, which also holds the 16 ANSI terminal colors.

Widgets:

```c
void sx_ui_button(SxCanvas *c, SxRect r, const char *label, const SxFont *font,
                  SxButtonState state, uint32_t base, uint32_t text);
void sx_ui_panel(SxCanvas *c, SxRect r, int radius, uint32_t fill);
```

`sx_ui_button` only draws the button. Tracking hover and press state with mouse events is up to
you, and `state` is one of `SX_BUTTON_NORMAL`, `SX_BUTTON_HOVER` or `SX_BUTTON_PRESSED`. See
`/examples/strawx_test.c` for the pattern.

## Settings and desktop entries

- `settings.h`: reads and writes `/config/settings.conf` with `sx_setting_get`,
  `sx_setting_get_int`, `sx_setting_set` and `sx_setting_set_int`.
- `desktop.h`: loads and launches `.desktop` files from `/desktop` with
  `sx_desktop_entry_load` and `sx_desktop_entry_launch`.

## Further reading

The apps in `modules/src/apps/` are the best reference. `clock.c` is a small animated window,
`calc.c` shows buttons and keyboard input, `paint.c` shows mouse drawing, and `notes.c` shows
text editing and scrolling.
