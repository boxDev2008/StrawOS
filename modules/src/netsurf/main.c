#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "utils/messages.h"
#include "utils/file.h"
#include "utils/utils.h"
#include "netsurf/netsurf.h"
#include "netsurf/misc.h"
#include "netsurf/fetch.h"
#include "netsurf/utf8.h"
#include "netsurf/clipboard.h"
#include "netsurf/download.h"
#include "netsurf/core_window.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser.h"
#include "netsurf/browser_window.h"
#include "netsurf/cookie_db.h"
#include "desktop/download.h"
#include "content/fetch.h"

#include "nsx.h"

#define RES_DIR      "/share/netsurf/"
#define CONFIG_DIR   "/config/netsurf"
#define DOWNLOAD_DIR "/downloads"

bool nsx_quit;

typedef struct ScheduleEntry ScheduleEntry;
struct ScheduleEntry
{
    uint64_t       when;
    void         (*callback)(void *p);
    void          *p;
    ScheduleEntry *next;
};

static ScheduleEntry *schedule_list;

static nserror misc_schedule(int t, void (*callback)(void *p), void *p)
{
    for (ScheduleEntry **e = &schedule_list; *e; e = &(*e)->next)
    {
        if ((*e)->callback == callback && (*e)->p == p)
        {
            ScheduleEntry *dead = *e;
            *e = dead->next;
            free(dead);
            break;
        }
    }
    if (t < 0)
        return NSERROR_OK;

    ScheduleEntry *n = malloc(sizeof(ScheduleEntry));
    if (!n)
        return NSERROR_NOMEM;
    n->when = sx_time_ms() + (uint64_t)t;
    n->callback = callback;
    n->p = p;

    ScheduleEntry **pos = &schedule_list;
    while (*pos && (*pos)->when <= n->when)
        pos = &(*pos)->next;
    n->next = *pos;
    *pos = n;
    return NSERROR_OK;
}

int nsx_schedule_run(void)
{
    uint64_t now = sx_time_ms();
    int budget = 64;
    while (schedule_list && schedule_list->when <= now && budget-- > 0)
    {
        ScheduleEntry *e = schedule_list;
        schedule_list = e->next;
        void (*cb)(void *) = e->callback;
        void *p = e->p;
        free(e);
        cb(p);
    }
    if (!schedule_list)
        return -1;
    now = sx_time_ms();
    return schedule_list->when <= now ? 0 : (int)(schedule_list->when - now);
}

void nsx_schedule_clear(void)
{
    while (schedule_list)
    {
        ScheduleEntry *e = schedule_list;
        schedule_list = e->next;
        free(e);
    }
}

static void misc_quit(void)
{
}

static nserror misc_launch_url(struct nsurl *url)
{
    (void)url;
    return NSERROR_NO_FETCH_HANDLER;
}

static struct gui_misc_table misc_table = {
    .schedule = misc_schedule,
    .quit = misc_quit,
    .launch_url = misc_launch_url,
};

static const char *fetch_filetype(const char *path)
{
    static const struct { const char *ext; const char *type; } types[] = {
        { ".html", "text/html" }, { ".htm", "text/html" }, { ".xhtml", "application/xhtml+xml" },
        { ".css", "text/css" }, { ".js", "application/javascript" }, { ".json", "application/json" },
        { ".txt", "text/plain" }, { ".lua", "text/plain" }, { ".c", "text/plain" }, { ".h", "text/plain" },
        { ".conf", "text/plain" }, { ".md", "text/plain" }, { ".desktop", "text/plain" },
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" }, { ".bmp", "image/bmp" }, { ".ico", "image/x-icon" },
        { ".svg", "image/svg+xml" }, { ".webp", "image/webp" },
    };
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot && (!slash || dot > slash))
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
            if (strcasecmp(dot, types[i].ext) == 0)
                return types[i].type;
    return "text/plain";
}

static struct nsurl *fetch_get_resource_url(const char *path)
{
    char buf[256];
    if (strcmp(path, "favicon.ico") == 0 || strcmp(path, "default.ico") == 0)
        path = "favicon.png";
    snprintf(buf, sizeof(buf), "file://" RES_DIR "%s", path);
    nsurl *url = NULL;
    if (nsurl_create(buf, &url) != NSERROR_OK)
        return NULL;
    return url;
}

static char *fetch_mimetype(const char *path)
{
    return strdup(fetch_filetype(path));
}

static struct gui_fetch_table fetch_table = {
    .filetype = fetch_filetype,
    .get_resource_url = fetch_get_resource_url,
    .mimetype = fetch_mimetype,
};

static nserror utf8_passthrough(const char *string, size_t len, char **result)
{
    if (len == 0)
        len = strlen(string);
    *result = strndup(string, len);
    return *result ? NSERROR_OK : NSERROR_NOMEM;
}

static struct gui_utf8_table utf8_table = {
    .utf8_to_local = utf8_passthrough,
    .local_to_utf8 = utf8_passthrough,
};

static char *clipboard_text;
static size_t clipboard_len;

static void clipboard_get(char **buffer, size_t *length)
{
    *buffer = clipboard_text ? strndup(clipboard_text, clipboard_len) : NULL;
    *length = *buffer ? clipboard_len : 0;
}

static void clipboard_set(const char *buffer, size_t length, nsclipboard_styles styles[], int n_styles)
{
    (void)styles;
    (void)n_styles;
    free(clipboard_text);
    clipboard_text = strndup(buffer, length);
    clipboard_len = clipboard_text ? length : 0;
}

static struct gui_clipboard_table clipboard_table = {
    .get = clipboard_get,
    .set = clipboard_set,
};

struct gui_download_window
{
    FILE *fp;
    char  path[256];
};

static struct gui_download_window *download_create(struct download_context *ctx, struct gui_window *parent)
{
    (void)parent;
    struct gui_download_window *dw = calloc(1, sizeof(*dw));
    if (!dw)
        return NULL;
    mkdir(DOWNLOAD_DIR, 0755);
    const char *name = download_context_get_filename(ctx);
    snprintf(dw->path, sizeof(dw->path), DOWNLOAD_DIR "/%s", name && *name ? name : "download");
    dw->fp = fopen(dw->path, "wb");
    if (!dw->fp)
    {
        free(dw);
        return NULL;
    }
    return dw;
}

static nserror download_data(struct gui_download_window *dw, const char *data, unsigned int size)
{
    return fwrite(data, 1, size, dw->fp) == size ? NSERROR_OK : NSERROR_SAVE_FAILED;
}

static void download_error(struct gui_download_window *dw, const char *msg)
{
    (void)msg;
    fclose(dw->fp);
    remove(dw->path);
    free(dw);
}

static void download_done(struct gui_download_window *dw)
{
    fclose(dw->fp);
    free(dw);
}

static struct gui_download_table download_table = {
    .create = download_create,
    .data = download_data,
    .error = download_error,
    .done = download_done,
};

static nserror cw_invalidate(struct core_window *cw, const struct rect *r)
{
    (void)cw;
    (void)r;
    return NSERROR_OK;
}

static nserror cw_set_extent(struct core_window *cw, int w, int h)
{
    (void)cw;
    (void)w;
    (void)h;
    return NSERROR_OK;
}

static nserror cw_set_scroll(struct core_window *cw, int x, int y)
{
    (void)cw;
    (void)x;
    (void)y;
    return NSERROR_OK;
}

static nserror cw_get_scroll(const struct core_window *cw, int *x, int *y)
{
    (void)cw;
    *x = *y = 0;
    return NSERROR_OK;
}

static nserror cw_get_dimensions(const struct core_window *cw, int *w, int *h)
{
    (void)cw;
    *w = *h = 0;
    return NSERROR_OK;
}

static nserror cw_drag_status(struct core_window *cw, core_window_drag_status ds)
{
    (void)cw;
    (void)ds;
    return NSERROR_OK;
}

static struct core_window_table core_window_table = {
    .invalidate = cw_invalidate,
    .set_extent = cw_set_extent,
    .set_scroll = cw_set_scroll,
    .get_scroll = cw_get_scroll,
    .get_dimensions = cw_get_dimensions,
    .drag_status = cw_drag_status,
};

static nserror set_defaults(struct nsoption_s *defaults)
{
    (void)defaults;
    nsoption_setnull_charp(cookie_file, strdup(CONFIG_DIR "/Cookies"));
    nsoption_setnull_charp(cookie_jar, strdup(CONFIG_DIR "/Cookies"));
    nsoption_set_int(font_size, 120);
    nsoption_set_int(font_min_size, 90);
    nsoption_set_int(max_fetchers, 12);
    nsoption_set_int(max_fetchers_per_host, 4);
    nsoption_set_int(max_cached_fetch_handles, 0);
    nsoption_set_int(memory_cache_size, 24 * 1024 * 1024);
    nsoption_set_bool(core_select_menu, true);
    nsoption_set_bool(enable_javascript, false);
    nsoption_set_bool(foreground_images, true);
    nsoption_set_bool(background_images, true);
    nsoption_set_bool(animate_images, true);
    nsoption_set_charp(homepage_url, strdup("https://html.duckduckgo.com/html/"));
    return NSERROR_OK;
}

static void run(void)
{
    while (!nsx_quit)
    {
        int timeout = nsx_schedule_run();
        if (nsx_quit)
            break;

        if (nsx_windows_need_redraw() && !sx_pending())
            nsx_windows_redraw();

        if (timeout < 0 || timeout > 60)
            timeout = 60;

        SxEvent ev;
        int rc = sx_next_event(&ev, timeout);
        if (rc < 0)
            break;
        while (rc > 0)
        {
            if (ev.type == SX_EVENT_DISCONNECTED)
            {
                nsx_quit = true;
                break;
            }
            nsx_windows_handle_event(&ev);
            if (nsx_quit)
                break;
            rc = sx_next_event(&ev, 0);
        }
    }
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("netsurf: cannot connect to the display server\n");
        return 1;
    }

    struct netsurf_table table = {
        .misc = &misc_table,
        .window = nsx_window_table,
        .corewindow = &core_window_table,
        .clipboard = &clipboard_table,
        .download = &download_table,
        .fetch = &fetch_table,
        .utf8 = &utf8_table,
        .bitmap = nsx_bitmap_table,
        .layout = nsx_layout_table,
    };
    if (netsurf_register(&table) != NSERROR_OK)
    {
        printf("netsurf: failed to register the frontend\n");
        return 1;
    }

    bitmap_fmt_t fmt = { .layout = BITMAP_LAYOUT_ARGB8888, .pma = false };
    bitmap_set_format(&fmt);

    if (nsoption_init(set_defaults, &nsoptions, &nsoptions_default) != NSERROR_OK)
    {
        printf("netsurf: options failed to initialise\n");
        return 1;
    }
    mkdir("/config", 0755);
    mkdir(CONFIG_DIR, 0755);
    nsoption_read(CONFIG_DIR "/Choices", nsoptions);

    if (messages_add_from_file(RES_DIR "Messages") != NSERROR_OK)
        printf("netsurf: messages failed to load\n");

    if (netsurf_init(NULL) != NSERROR_OK)
    {
        printf("netsurf: core failed to initialise\n");
        return 1;
    }
    nsx_fetch_http_register();
    browser_set_dpi(96);

    urldb_load_cookies(nsoption_charp(cookie_file));

    nsx_windows_init();

    char start[1024];
    if (argc > 1 && argv[1][0] == '/')
        snprintf(start, sizeof(start), "file://%s", argv[1]);
    else if (argc > 1)
        snprintf(start, sizeof(start), "%s", argv[1]);
    else
        snprintf(start, sizeof(start), "%s", nsoption_charp(homepage_url));

    nsurl *url;
    struct browser_window *bw = NULL;
    if (nsurl_create(start, &url) == NSERROR_OK)
    {
        browser_window_create(BW_CREATE_HISTORY | BW_CREATE_FOREGROUND, url, NULL, NULL, &bw);
        nsurl_unref(url);
    }

    if (bw)
        run();

    urldb_save_cookies(nsoption_charp(cookie_jar));
    nsx_windows_disconnect();
    netsurf_exit();
    nsoption_finalise(nsoptions, nsoptions_default);
    sx_disconnect();
    return 0;
}
