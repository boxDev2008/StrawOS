#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <strawx.h>
#include <ui.h>

#include "utils/errors.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"

struct browser_window;
struct gui_layout_table;
struct gui_bitmap_table;
struct gui_window_table;
struct gui_misc_table;
struct gui_fetch_table;
struct gui_clipboard_table;
struct gui_download_table;
struct core_window_table;

typedef struct NsxBitmap
{
    int       width, height;
    uint32_t *pixels;
    bool      opaque;
    uint32_t *scaled;
    int       scaled_w, scaled_h;
}
NsxBitmap;

extern struct gui_layout_table    *nsx_layout_table;
extern struct gui_bitmap_table    *nsx_bitmap_table;
extern struct gui_window_table    *nsx_window_table;
extern struct gui_misc_table      *nsx_misc_table;
extern struct gui_fetch_table     *nsx_fetch_table;
extern struct gui_clipboard_table *nsx_clipboard_table;
extern struct gui_download_table  *nsx_download_table;
extern struct core_window_table   *nsx_core_window_table;
extern const struct plotter_table  nsx_plotters;

int  nsx_font_draw(SxCanvas *c, const plot_font_style_t *fs, int x, int y, const char *text, size_t length,
                   uint32_t color);
void nsx_font_metrics(const plot_font_style_t *fs, int *ascent, int *descent);

uint32_t nsx_colour(uint32_t ns);

nserror nsx_fetch_http_register(void);

int  nsx_schedule_run(void);
void nsx_schedule_clear(void);

void nsx_windows_init(void);
bool nsx_windows_open(void);
void nsx_windows_handle_event(const SxEvent *ev);
void nsx_windows_redraw(void);
bool nsx_windows_need_redraw(void);
void nsx_windows_disconnect(void);

extern bool nsx_quit;
