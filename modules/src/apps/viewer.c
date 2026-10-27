#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strawx.h>
#include <ui.h>

#define BAR_H       36
#define MAX_SIBLINGS 256
#define PATH_LEN    256
#define ZOOM_MIN    0.02f
#define ZOOM_MAX    32.0f

static struct
{
    SxWindow *win;

    char     path[PATH_LEN];
    char     dir[PATH_LEN];
    SxImage *img;
    SxImage *cache;
    char     error[96];

    float    zoom;
    float    ox, oy;
    bool     fit;

    bool     dragging;
    int      drag_x, drag_y;

    char     siblings[MAX_SIBLINGS][NAME_MAX];
    int      sibling_count;
    int      sibling_index;
}
V;

static int view_w(void) { return sx_window_width(V.win); }
static int view_h(void) { int h = sx_window_height(V.win) - BAR_H; return h > 1 ? h : 1; }

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int name_compare(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void scan_siblings(void)
{
    V.sibling_count = 0;
    V.sibling_index = -1;

    int fd = open(V.dir, O_RDONLY);
    if (fd < 0)
        return;

    struct dirent de;
    while (V.sibling_count < MAX_SIBLINGS && readdir(fd, &de) == 1)
    {
        if (de.type == STAT_FILE && sx_image_is_supported(de.name))
        {
            memcpy(V.siblings[V.sibling_count], de.name, NAME_MAX);
            V.siblings[V.sibling_count][NAME_MAX - 1] = '\0';
            V.sibling_count++;
        }
    }
    close(fd);

    for (int i = 1; i < V.sibling_count; i++)
    {
        char key[NAME_MAX];
        memcpy(key, V.siblings[i], NAME_MAX);
        int j = i - 1;
        while (j >= 0 && name_compare(V.siblings[j], key) > 0)
        {
            memcpy(V.siblings[j + 1], V.siblings[j], NAME_MAX);
            j--;
        }
        memcpy(V.siblings[j + 1], key, NAME_MAX);
    }

    for (int i = 0; i < V.sibling_count; i++)
        if (strcmp(V.siblings[i], base_name(V.path)) == 0)
            V.sibling_index = i;
}

static void fit_view(void)
{
    if (!V.img)
        return;
    float zx = (float)(view_w() - 32) / V.img->width;
    float zy = (float)(view_h() - 32) / V.img->height;
    V.zoom = zx < zy ? zx : zy;
    if (V.zoom > 1.0f)
        V.zoom = 1.0f;
    if (V.zoom < ZOOM_MIN)
        V.zoom = ZOOM_MIN;
    V.ox = (view_w() - V.img->width * V.zoom) * 0.5f;
    V.oy = (view_h() - V.img->height * V.zoom) * 0.5f;
    V.fit = true;
}

static void set_zoom(float zoom, float cx, float cy)
{
    if (!V.img)
        return;
    if (zoom < ZOOM_MIN)
        zoom = ZOOM_MIN;
    if (zoom > ZOOM_MAX)
        zoom = ZOOM_MAX;

    float ix = (cx - V.ox) / V.zoom;
    float iy = (cy - V.oy) / V.zoom;
    V.zoom = zoom;
    V.ox = cx - ix * zoom;
    V.oy = cy - iy * zoom;
    V.fit = false;
}

static void open_image(const char *path)
{
    sx_image_free(V.img);
    sx_image_free(V.cache);
    V.img = NULL;
    V.cache = NULL;
    V.error[0] = '\0';

    strncpy(V.path, path, PATH_LEN - 1);
    V.path[PATH_LEN - 1] = '\0';
    strcpy(V.dir, V.path);
    char *slash = strrchr(V.dir, '/');
    if (slash == V.dir)
        V.dir[1] = '\0';
    else if (slash)
        *slash = '\0';

    V.img = sx_image_load(V.path);
    if (!V.img)
        snprintf(V.error, sizeof(V.error), "Can't open this image: %s", sx_image_error());

    char title[PATH_LEN + 32];
    snprintf(title, sizeof(title), "%s - Image Viewer", base_name(V.path));
    sx_window_set_title(V.win, title);

    scan_siblings();
    fit_view();
}

static void step_image(int delta)
{
    if (V.sibling_count < 2 || V.sibling_index < 0)
        return;
    int i = (V.sibling_index + delta + V.sibling_count) % V.sibling_count;
    char path[PATH_LEN];
    if (strcmp(V.dir, "/") == 0)
        snprintf(path, sizeof(path), "/%s", V.siblings[i]);
    else
        snprintf(path, sizeof(path), "%s/%s", V.dir, V.siblings[i]);
    open_image(path);
}

static void draw_checker(SxCanvas *c, SxRect r)
{
    r = sx_rect_intersect(r, c->clip);
    for (int y = r.y; y < r.y + r.h; y++)
    {
        uint32_t *row = c->pixels + (size_t)y * c->stride;
        for (int x = r.x; x < r.x + r.w; x++)
            row[x] = ((x >> 3) + (y >> 3)) & 1 ? SX_RGB(0x3A, 0x3D, 0x46) : SX_RGB(0x2C, 0x2F, 0x37);
    }
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(V.win);
    int w = c->width, h = c->height;
    sx_clear(c, SX_THEME_SUNKEN);

    SxRect view = sx_rect(0, 0, w, view_h());
    sx_canvas_set_clip(c, view);

    if (V.img)
    {
        int dw = (int)(V.img->width * V.zoom + 0.5f);
        int dh = (int)(V.img->height * V.zoom + 0.5f);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        int dx = (int)(V.ox + 0.5f), dy = (int)(V.oy + 0.5f);

        draw_checker(c, sx_rect(dx, dy, dw, dh));

        if (V.zoom < 1.0f)
        {
            if (!V.cache || V.cache->width != dw || V.cache->height != dh)
            {
                sx_image_free(V.cache);
                V.cache = sx_image_scale(V.img, dw, dh);
            }
            if (V.cache)
                sx_draw_image(c, V.cache, dx, dy);
        }
        else if (dw == V.img->width && dh == V.img->height)
        {
            sx_draw_image(c, V.img, dx, dy);
        }
        else
        {
            sx_draw_image_scaled(c, V.img, dx, dy, dw, dh);
        }
    }
    else
    {
        const char *msg = V.error[0] ? V.error : "Open an image from Files to view it here";
        sx_draw_text_centered(c, &sx_font_sans, view, msg, V.error[0] ? SX_RGB(0xFF, 0x9A, 0x9C) : SX_THEME_TEXT_DIM);
    }

    sx_canvas_reset_clip(c);

    SxRect bar = sx_rect(0, h - BAR_H, w, BAR_H);
    sx_fill_rect(c, bar.x, bar.y, bar.w, bar.h, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, bar.y, w, 1, SX_RGBA(0, 0, 0, 90));

    if (V.path[0])
    {
        const SxFont *bold = &sx_font_sans_bold, *small = &sx_font_sans_small;
        int x = 14;
        x += sx_draw_text(c, bold, x, sx_text_y_centered(bold, bar), base_name(V.path), SX_THEME_TEXT) + 14;

        char info[96];
        if (V.img)
        {
            snprintf(info, sizeof(info), "%d x %d", V.img->width, V.img->height);
            x += sx_draw_text(c, small, x, sx_text_y_centered(small, bar), info, SX_THEME_TEXT_DIM) + 14;
            if (V.sibling_count > 1 && V.sibling_index >= 0)
            {
                snprintf(info, sizeof(info), "%d of %d", V.sibling_index + 1, V.sibling_count);
                sx_draw_text(c, small, x, sx_text_y_centered(small, bar), info, SX_THEME_TEXT_DIM);
            }

            snprintf(info, sizeof(info), "%d%%", (int)(V.zoom * 100.0f + 0.5f));
            int zw = sx_text_width(bold, info);
            sx_draw_text(c, bold, w - 14 - zw, sx_text_y_centered(bold, bar), info, SX_THEME_ACCENT);

            const char *hint = "wheel zoom  -  drag pan  -  0 fit  -  1 actual size  -  arrows browse";
            int hw = sx_text_width(small, hint);
            if (w - 14 - zw - 24 - hw > x + 80)
                sx_draw_text(c, small, w - 14 - zw - 24 - hw, sx_text_y_centered(small, bar), hint, SX_THEME_TEXT_DIM);
        }
    }

    sx_window_commit(V.win);
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("viewer: cannot connect to the display server\n");
        return 1;
    }

    V.win = sx_window_create("Image Viewer", 900, 620, SX_WINDOW_RESIZABLE);
    if (!V.win)
        return 1;

    V.zoom = 1.0f;
    if (argc > 1 && argv[1][0] == '/')
        open_image(argv[1]);
    draw();

    bool dirty = false;
    SxEvent ev;
    while (sx_next_event(&ev, -1) > 0)
    {
        bool redraw = false;
        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                if (V.fit)
                    fit_view();
                redraw = true;
                break;

            case SX_EVENT_MOUSE_WHEEL:
                if (V.img && ev.y < view_h())
                {
                    float f = ev.wheel > 0 ? 1.25f : 0.8f;
                    set_zoom(V.zoom * f, (float)ev.x, (float)ev.y);
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.button == SX_BUTTON_LEFT && ev.y < view_h())
                {
                    V.dragging = true;
                    V.drag_x = ev.x;
                    V.drag_y = ev.y;
                }
                break;

            case SX_EVENT_MOUSE_MOVE:
                if (V.dragging && V.img)
                {
                    V.ox += ev.x - V.drag_x;
                    V.oy += ev.y - V.drag_y;
                    V.drag_x = ev.x;
                    V.drag_y = ev.y;
                    V.fit = false;
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_UP:
                if (ev.button == SX_BUTTON_LEFT)
                    V.dragging = false;
                break;

            case SX_EVENT_KEY_DOWN:
                redraw = true;
                if (ev.ch == '+' || ev.ch == '=')
                    set_zoom(V.zoom * 1.25f, view_w() * 0.5f, view_h() * 0.5f);
                else if (ev.ch == '-')
                    set_zoom(V.zoom * 0.8f, view_w() * 0.5f, view_h() * 0.5f);
                else if (ev.ch == '0')
                    fit_view();
                else if (ev.ch == '1')
                    set_zoom(1.0f, view_w() * 0.5f, view_h() * 0.5f);
                else if (ev.key == SX_KEY_RIGHT || ev.key == SX_KEY_SPACE)
                    step_image(1);
                else if (ev.key == SX_KEY_LEFT || ev.key == SX_KEY_BACKSPACE)
                    step_image(-1);
                else
                    redraw = false;
                break;

            default:
                break;
        }

        dirty |= redraw;
        if (dirty && !sx_pending())
        {
            draw();
            dirty = false;
        }
    }

out:
    sx_window_destroy(V.win);
    sx_disconnect();
    return 0;
}
