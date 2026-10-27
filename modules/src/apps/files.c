#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strawx.h>
#include <ui.h>

#define MAX_ENTRIES  512
#define PATH_MAX_LEN 256
#define HISTORY_MAX  32

#define TOOLBAR_H    48
#define PROMPT_H     44
#define STATUS_H     26
#define SIDEBAR_W    170
#define HEADER_H     28
#define ROW_H        30
#define SIZE_COL_W   90
#define TYPE_COL_W   130
#define SCROLLBAR_W  8

#define NOTES_PATH   "/bin/notes.elf"
#define VIEWER_PATH  "/bin/viewer.elf"
#define TERMINAL_PATH "/bin/terminal.elf"
#define ICON_SIZE    20

typedef struct
{
    char           name[NAME_MAX];
    enum stat_type type;
    uint64_t       size;
    SxImage       *icon;
}
Entry;

typedef enum
{
    PROMPT_NONE,
    PROMPT_NEW_FOLDER,
    PROMPT_NEW_FILE,
    PROMPT_RENAME,
    PROMPT_DELETE,
}
PromptKind;

typedef enum
{
    ACT_NONE,
    ACT_OPEN,
    ACT_EDIT,
    ACT_CUT,
    ACT_COPY,
    ACT_PASTE,
    ACT_RENAME,
    ACT_DELETE,
    ACT_WALLPAPER,
    ACT_TERMINAL,
    ACT_PROPERTIES,
    ACT_NEW_FILE,
    ACT_NEW_FOLDER,
    ACT_REFRESH,
    ACT_SEPARATOR,
}
Action;

typedef struct
{
    const char *label;
    const char *shortcut;
    Action      action;
    bool        enabled;
}
MenuItem;

#define MENU_MAX       16
#define MENU_W         236
#define MENU_ITEM_H    30
#define MENU_SEP_H     9
#define PROPS_W        400

typedef enum
{
    HIT_NONE,
    HIT_BACK,
    HIT_UP,
    HIT_NEW_FOLDER,
    HIT_DELETE,
    HIT_PLACE,
    HIT_CRUMB,
    HIT_ROW,
    HIT_PROMPT_OK,
    HIT_PROMPT_CANCEL,
    HIT_SCROLLBAR,
    HIT_MENU_ITEM,
    HIT_PROPS_CLOSE,
}
HitKind;

typedef struct
{
    HitKind kind;
    int     index;
}
Hit;

typedef struct
{
    const char *label;
    const char *path;
}
Place;

static const Place places[] = {
    { "Root",    "/"         },
    { "Desktop", "/desktop"  },
    { "Pictures", "/pictures" },
    { "Scripts",  "/scripts"  },
    { "Programs", "/bin"     },
    { "Devices", "/dev"      },
};
#define PLACE_N ((int)(sizeof(places) / sizeof(places[0])))

#define CRUMB_MAX 32

static struct
{
    SxWindow *win;

    char  cwd[PATH_MAX_LEN];
    Entry entries[MAX_ENTRIES];
    int   count;

    int   selected;
    int   scroll;
    Hit   hover;
    Hit   pressed;

    char  history[HISTORY_MAX][PATH_MAX_LEN];
    int   history_len;

    PromptKind prompt;
    char  input[NAME_MAX];
    bool  input_fresh;

    char  status[160];
    bool  status_error;

    uint64_t last_click_ms;
    int      last_click_row;

    SxRect crumb_rects[CRUMB_MAX];
    int    crumb_ends[CRUMB_MAX];
    int    crumb_count;

    bool     menu_open;
    int      menu_x, menu_y;
    int      menu_target;
    MenuItem menu[MENU_MAX];
    int      menu_count;

    char     clip_path[PATH_MAX_LEN];
    bool     clip_cut;

    bool     props_open;
    Entry    props_entry;
    char     props_lines[8][2][160];
    int      props_line_count;
    SxImage *props_icon;
}
F;

static int width(void)  { return sx_window_width(F.win); }
static int height(void) { return sx_window_height(F.win); }

static int prompt_h(void) { return F.prompt == PROMPT_NONE ? 0 : PROMPT_H; }

static SxRect back_rect(void)       { return sx_rect(12, 9, 32, 30); }
static SxRect up_rect(void)         { return sx_rect(48, 9, 32, 30); }
static SxRect delete_rect(void)     { return sx_rect(width() - 12 - 76, 9, 76, 30); }
static SxRect new_folder_rect(void) { return sx_rect(delete_rect().x - 8 - 106, 9, 106, 30); }

static SxRect sidebar_rect(void)
{
    int top = TOOLBAR_H + prompt_h();
    return sx_rect(0, top, SIDEBAR_W, height() - top - STATUS_H);
}

static SxRect place_rect(int i)
{
    SxRect s = sidebar_rect();
    return sx_rect(8, s.y + 34 + i * 34, SIDEBAR_W - 16, 30);
}

static SxRect list_rect(void)
{
    int top = TOOLBAR_H + prompt_h();
    return sx_rect(SIDEBAR_W, top, width() - SIDEBAR_W, height() - top - STATUS_H);
}

static SxRect rows_rect(void)
{
    SxRect l = list_rect();
    return sx_rect(l.x, l.y + HEADER_H, l.w, l.h - HEADER_H);
}

static int visible_rows(void)
{
    int n = rows_rect().h / ROW_H;
    return n > 1 ? n : 1;
}

static SxRect prompt_field_rect(void)
{
    return sx_rect(SIDEBAR_W + 12 + 150, TOOLBAR_H + 7, width() - SIDEBAR_W - 12 - 150 - 200, 30);
}

static SxRect prompt_ok_rect(void)
{
    return sx_rect(width() - 12 - 84 - 8 - 84, TOOLBAR_H + 7, 84, 30);
}

static SxRect prompt_cancel_rect(void)
{
    return sx_rect(width() - 12 - 84, TOOLBAR_H + 7, 84, 30);
}

static void set_status(const char *msg, bool error)
{
    strncpy(F.status, msg, sizeof(F.status) - 1);
    F.status[sizeof(F.status) - 1] = '\0';
    F.status_error = error;
}

static void join_path(char *out, const char *dir, const char *name)
{
    if (strcmp(dir, "/") == 0)
        snprintf(out, PATH_MAX_LEN, "/%s", name);
    else
        snprintf(out, PATH_MAX_LEN, "%s/%s", dir, name);
}

static const char *extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && dot != name ? dot + 1 : "";
}

static bool ext_is(const char *name, const char *ext)
{
    const char *e = extension(name);
    if (strlen(e) != strlen(ext))
        return false;
    for (; *e; e++, ext++)
        if (tolower((unsigned char)*e) != *ext)
            return false;
    return true;
}

static const char *type_label(const Entry *e)
{
    switch (e->type)
    {
        case STAT_DIR:    return "Folder";
        case STAT_DEVICE: return "Device";
        case STAT_SOCKET: return "Socket";
        default: break;
    }
    if (ext_is(e->name, "elf"))
        return "Application";
    if (ext_is(e->name, "txt"))
        return "Text document";
    if (ext_is(e->name, "c") || ext_is(e->name, "h"))
        return "C source";
    if (ext_is(e->name, "desktop"))
        return "Shortcut";
    if (ext_is(e->name, "lua"))
        return "Lua script";
    if (ext_is(e->name, "png"))
        return "PNG image";
    if (ext_is(e->name, "bmp"))
        return "BMP image";
    if (ext_is(e->name, "jpg") || ext_is(e->name, "jpeg"))
        return "JPEG image";
    if (ext_is(e->name, "gif"))
        return "GIF image";
    if (ext_is(e->name, "tga"))
        return "TGA image";
    if (ext_is(e->name, "ppm") || ext_is(e->name, "pgm") || ext_is(e->name, "pnm"))
        return "PNM image";
    return "File";
}

static void format_size(uint64_t size, char *out, size_t n)
{
    if (size < 1024)
        snprintf(out, n, "%llu B", (unsigned long long)size);
    else if (size < 1024 * 1024)
        snprintf(out, n, "%llu.%llu KB", (unsigned long long)(size / 1024),
                 (unsigned long long)(size % 1024 * 10 / 1024));
    else
        snprintf(out, n, "%llu.%llu MB", (unsigned long long)(size / (1024 * 1024)),
                 (unsigned long long)(size % (1024 * 1024) * 10 / (1024 * 1024)));
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

static bool entry_before(const Entry *a, const Entry *b)
{
    bool da = a->type == STAT_DIR, db = b->type == STAT_DIR;
    if (da != db)
        return da;
    return name_compare(a->name, b->name) < 0;
}

static void sort_entries(void)
{
    for (int i = 1; i < F.count; i++)
    {
        Entry key = F.entries[i];
        int j = i - 1;
        while (j >= 0 && entry_before(&key, &F.entries[j]))
        {
            F.entries[j + 1] = F.entries[j];
            j--;
        }
        F.entries[j + 1] = key;
    }
}

static void ensure_visible(void)
{
    if (F.selected < 0)
        return;
    if (F.selected < F.scroll)
        F.scroll = F.selected;
    if (F.selected >= F.scroll + visible_rows())
        F.scroll = F.selected - visible_rows() + 1;
}

static void clamp_scroll(void)
{
    int max = F.count - visible_rows();
    if (F.scroll > max)
        F.scroll = max;
    if (F.scroll < 0)
        F.scroll = 0;
}

static bool load_dir(const char *path, const char *select_name)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        char msg[300];
        snprintf(msg, sizeof(msg), "Can't open %s", path);
        set_status(msg, true);
        return false;
    }

    strncpy(F.cwd, path, PATH_MAX_LEN - 1);
    F.cwd[PATH_MAX_LEN - 1] = '\0';
    for (int i = 0; i < F.count; i++)
        sx_image_free(F.entries[i].icon);
    F.count = 0;

    struct dirent de;
    while (F.count < MAX_ENTRIES && readdir(fd, &de) == 1)
    {
        Entry *e = &F.entries[F.count++];
        memcpy(e->name, de.name, NAME_MAX);
        e->name[NAME_MAX - 1] = '\0';
        e->type = de.type;
        e->size = 0;
        e->icon = NULL;

        char full[PATH_MAX_LEN];
        join_path(full, F.cwd, e->name);
        struct stat st;
        if (stat(full, &st) == 0)
        {
            e->type = st.type;
            e->size = st.size;
        }

        SxDesktopEntry de_entry;
        if (e->type == STAT_FILE && sx_is_desktop_file(e->name) && sx_desktop_entry_load(full, &de_entry) &&
            de_entry.icon[0])
        {
            SxImage *img = sx_image_load(de_entry.icon);
            if (img)
            {
                e->icon = sx_image_scale(img, ICON_SIZE, ICON_SIZE);
                sx_image_free(img);
            }
        }
    }
    close(fd);

    sort_entries();

    F.selected = F.count > 0 ? 0 : -1;
    if (select_name)
        for (int i = 0; i < F.count; i++)
            if (strcmp(F.entries[i].name, select_name) == 0)
                F.selected = i;
    F.scroll = 0;
    ensure_visible();
    clamp_scroll();

    char title[PATH_MAX_LEN + 16];
    const char *base = strrchr(F.cwd, '/');
    if (strcmp(F.cwd, "/") == 0)
        snprintf(title, sizeof(title), "Files");
    else
        snprintf(title, sizeof(title), "%s - Files", base + 1);
    sx_window_set_title(F.win, title);
    return true;
}

static void refresh(void)
{
    char keep[NAME_MAX] = "";
    if (F.selected >= 0 && F.selected < F.count)
        strcpy(keep, F.entries[F.selected].name);
    int scroll = F.scroll;
    char path[PATH_MAX_LEN];
    strcpy(path, F.cwd);
    load_dir(path, keep[0] ? keep : NULL);
    F.scroll = scroll;
    ensure_visible();
    clamp_scroll();
}

static void navigate(const char *path, const char *select_name)
{
    if (strcmp(path, F.cwd) == 0)
        return;

    char old[PATH_MAX_LEN];
    strcpy(old, F.cwd);
    if (!load_dir(path, select_name))
        return;

    if (F.history_len == HISTORY_MAX)
    {
        memmove(F.history[0], F.history[1], sizeof(F.history[0]) * (HISTORY_MAX - 1));
        F.history_len--;
    }
    strcpy(F.history[F.history_len++], old);
    set_status("", false);
}

static void go_back(void)
{
    if (F.history_len == 0)
        return;
    char path[PATH_MAX_LEN];
    strcpy(path, F.history[--F.history_len]);
    load_dir(path, NULL);
}

static void go_up(void)
{
    if (strcmp(F.cwd, "/") == 0)
        return;

    char parent[PATH_MAX_LEN], child[NAME_MAX];
    strcpy(parent, F.cwd);
    char *slash = strrchr(parent, '/');
    strncpy(child, slash + 1, NAME_MAX - 1);
    child[NAME_MAX - 1] = '\0';
    if (slash == parent)
        parent[1] = '\0';
    else
        *slash = '\0';
    navigate(parent, child);
}

static void open_entry(int index)
{
    if (index < 0 || index >= F.count)
        return;

    Entry *e = &F.entries[index];
    char full[PATH_MAX_LEN];
    join_path(full, F.cwd, e->name);

    char msg[300];
    switch (e->type)
    {
        case STAT_DIR:
            navigate(full, NULL);
            return;
        case STAT_DEVICE:
        case STAT_SOCKET:
            set_status("Devices can't be opened here", true);
            return;
        default:
            break;
    }

    if (sx_is_desktop_file(e->name))
    {
        SxDesktopEntry entry;
        if (sx_desktop_entry_load(full, &entry) && sx_desktop_entry_launch(&entry))
            snprintf(msg, sizeof(msg), "Started %s", entry.name);
        else
            snprintf(msg, sizeof(msg), "Couldn't start %s", e->name);
        set_status(msg, false);
        return;
    }

    if (ext_is(e->name, "lua"))
    {
        const char *argv[] = { TERMINAL_PATH, "--run", full, NULL };
        if (spawn(TERMINAL_PATH, argv))
            snprintf(msg, sizeof(msg), "Running %s", e->name);
        else
            snprintf(msg, sizeof(msg), "Couldn't run %s", e->name);
        set_status(msg, false);
        return;
    }

    if (sx_image_is_supported(e->name))
    {
        const char *argv[] = { VIEWER_PATH, full, NULL };
        if (spawn(VIEWER_PATH, argv))
            snprintf(msg, sizeof(msg), "Opened %s", e->name);
        else
            snprintf(msg, sizeof(msg), "Couldn't open %s", e->name);
        set_status(msg, false);
        return;
    }

    if (ext_is(e->name, "elf"))
    {
        if (spawn(full, NULL))
            snprintf(msg, sizeof(msg), "Started %s", e->name);
        else
            snprintf(msg, sizeof(msg), "Couldn't start %s", e->name);
        set_status(msg, false);
        return;
    }

    const char *argv[] = { NOTES_PATH, full, NULL };
    if (spawn(NOTES_PATH, argv))
        snprintf(msg, sizeof(msg), "Opened %s in Notes", e->name);
    else
        snprintf(msg, sizeof(msg), "Couldn't open %s", e->name);
    set_status(msg, false);
}

static bool has_selection(void)
{
    return F.selected >= 0 && F.selected < F.count;
}

static void start_prompt(PromptKind kind)
{
    if ((kind == PROMPT_DELETE || kind == PROMPT_RENAME) && !has_selection())
        return;
    F.prompt = kind;
    F.input[0] = '\0';
    F.input_fresh = kind != PROMPT_DELETE;
    if (kind == PROMPT_NEW_FOLDER)
        strcpy(F.input, "New Folder");
    else if (kind == PROMPT_NEW_FILE)
        strcpy(F.input, "New File.txt");
    else if (kind == PROMPT_RENAME)
        strcpy(F.input, F.entries[F.selected].name);
    ensure_visible();
    clamp_scroll();
}

static bool valid_name(const char *name)
{
    if (!name[0] || strchr(name, '/') || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        set_status("Names can't be empty or contain '/'", true);
        return false;
    }
    return true;
}

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool create_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT);
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

static bool copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return false;
    int out = open(dst, O_WRONLY | O_CREAT);
    if (out < 0)
    {
        close(in);
        return false;
    }

    bool ok = true;
    char buf[2048];
    for (;;)
    {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0)
            ok = false;
        if (n <= 0)
            break;
        if (write(out, buf, (size_t)n) != n)
        {
            ok = false;
            break;
        }
    }
    close(in);
    close(out);
    return ok;
}

static bool copy_path(const char *src, const char *dst, int depth)
{
    struct stat st;
    if (depth > 16 || stat(src, &st) != 0)
        return false;
    if (st.type != STAT_DIR)
        return st.type == STAT_FILE && copy_file(src, dst);

    if (mkdir(dst, 0) != 0)
        return false;

    int fd = open(src, O_RDONLY);
    if (fd < 0)
        return false;

    bool ok = true;
    struct dirent de;
    while (readdir(fd, &de) == 1)
    {
        char a[PATH_MAX_LEN], b[PATH_MAX_LEN];
        join_path(a, src, de.name);
        join_path(b, dst, de.name);
        if (!copy_path(a, b, depth + 1))
            ok = false;
    }
    close(fd);
    return ok;
}

static void unique_name(const char *dir, const char *name, char *out)
{
    char full[PATH_MAX_LEN];
    join_path(full, dir, name);
    if (!path_exists(full))
    {
        strcpy(out, name);
        return;
    }

    char stem[NAME_MAX], ext[NAME_MAX] = "";
    strcpy(stem, name);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem)
    {
        strcpy(ext, dot);
        *dot = '\0';
    }

    for (int i = 1; i < 1000; i++)
    {
        if (i == 1)
            snprintf(out, NAME_MAX, "%s copy%s", stem, ext);
        else
            snprintf(out, NAME_MAX, "%s copy %d%s", stem, i, ext);
        join_path(full, dir, out);
        if (!path_exists(full))
            return;
    }
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void clip_set(bool cut)
{
    if (!has_selection())
        return;
    join_path(F.clip_path, F.cwd, F.entries[F.selected].name);
    F.clip_cut = cut;

    char msg[300];
    snprintf(msg, sizeof(msg), "%s %s", cut ? "Cut" : "Copied", F.entries[F.selected].name);
    set_status(msg, false);
}

static void paste(void)
{
    if (!F.clip_path[0])
        return;

    char msg[300];
    if (!path_exists(F.clip_path))
    {
        snprintf(msg, sizeof(msg), "%s no longer exists", base_name(F.clip_path));
        set_status(msg, true);
        F.clip_path[0] = '\0';
        return;
    }

    size_t src_len = strlen(F.clip_path);
    if (strncmp(F.cwd, F.clip_path, src_len) == 0 && (F.cwd[src_len] == '/' || F.cwd[src_len] == '\0'))
    {
        set_status("Can't paste a folder into itself", true);
        return;
    }

    char name[NAME_MAX], dst[PATH_MAX_LEN];
    unique_name(F.cwd, base_name(F.clip_path), name);
    join_path(dst, F.cwd, name);

    bool ok;
    if (F.clip_cut)
    {
        ok = strcmp(dst, F.clip_path) == 0 || rename(F.clip_path, dst) == 0;
        if (ok)
            F.clip_path[0] = '\0';
    }
    else
    {
        ok = copy_path(F.clip_path, dst, 0);
    }

    load_dir(F.cwd, name);
    snprintf(msg, sizeof(msg), ok ? "Pasted %s" : "Couldn't paste %s", name);
    set_status(msg, !ok);
}

static void confirm_prompt(void)
{
    char full[PATH_MAX_LEN], msg[300];
    char name[NAME_MAX];

    if (F.prompt == PROMPT_NEW_FOLDER || F.prompt == PROMPT_NEW_FILE)
    {
        bool folder = F.prompt == PROMPT_NEW_FOLDER;
        if (!valid_name(F.input))
            return;
        join_path(full, F.cwd, F.input);
        if (path_exists(full))
        {
            set_status("Something with that name already exists", true);
            return;
        }
        if (folder ? mkdir(full, 0) != 0 : !create_file(full))
        {
            set_status(folder ? "Couldn't create the folder here" : "Couldn't create the file here", true);
            return;
        }
        F.prompt = PROMPT_NONE;
        strcpy(name, F.input);
        load_dir(F.cwd, name);
        snprintf(msg, sizeof(msg), "Created %s", name);
        set_status(msg, false);
    }
    else if (F.prompt == PROMPT_RENAME)
    {
        Entry *e = &F.entries[F.selected];
        if (!valid_name(F.input))
            return;
        if (strcmp(F.input, e->name) == 0)
        {
            F.prompt = PROMPT_NONE;
            return;
        }
        char from[PATH_MAX_LEN];
        join_path(from, F.cwd, e->name);
        join_path(full, F.cwd, F.input);
        if (path_exists(full))
        {
            set_status("Something with that name already exists", true);
            return;
        }
        if (rename(from, full) != 0)
        {
            set_status("Couldn't rename that", true);
            return;
        }
        F.prompt = PROMPT_NONE;
        strcpy(name, F.input);
        load_dir(F.cwd, name);
        snprintf(msg, sizeof(msg), "Renamed to %s", name);
        set_status(msg, false);
    }
    else if (F.prompt == PROMPT_DELETE)
    {
        Entry *e = &F.entries[F.selected];
        join_path(full, F.cwd, e->name);
        F.prompt = PROMPT_NONE;
        if (unlink(full) != 0)
        {
            snprintf(msg, sizeof(msg), e->type == STAT_DIR ? "Couldn't delete %s (is it empty?)"
                                                           : "Couldn't delete %s", e->name);
            set_status(msg, true);
            return;
        }
        snprintf(msg, sizeof(msg), "Deleted %s", e->name);
        int keep = F.selected;
        load_dir(F.cwd, NULL);
        F.selected = keep < F.count ? keep : F.count - 1;
        ensure_visible();
        clamp_scroll();
        set_status(msg, false);
    }
}

static void open_in_notes(int index)
{
    if (index < 0 || index >= F.count)
        return;
    char full[PATH_MAX_LEN], msg[300];
    join_path(full, F.cwd, F.entries[index].name);
    const char *argv[] = { NOTES_PATH, full, NULL };
    snprintf(msg, sizeof(msg), spawn(NOTES_PATH, argv) ? "Opened %s in Notes" : "Couldn't open %s",
             F.entries[index].name);
    set_status(msg, false);
}

static void open_in_terminal(int index)
{
    char dir[PATH_MAX_LEN], msg[300];
    if (index >= 0 && index < F.count && F.entries[index].type == STAT_DIR)
        join_path(dir, F.cwd, F.entries[index].name);
    else
        strcpy(dir, F.cwd);
    const char *argv[] = { TERMINAL_PATH, "--cwd", dir, NULL };
    bool ok = spawn(TERMINAL_PATH, argv);
    snprintf(msg, sizeof(msg), ok ? "Opened terminal in %s" : "Couldn't open terminal in %s", dir);
    set_status(msg, !ok);
}

static void set_wallpaper(int index)
{
    if (index < 0 || index >= F.count)
        return;
    char full[PATH_MAX_LEN], msg[300];
    join_path(full, F.cwd, F.entries[index].name);
    if (sx_setting_set("wallpaper", full))
    {
        sx_settings_apply();
        snprintf(msg, sizeof(msg), "%s is now the wallpaper", F.entries[index].name);
        set_status(msg, false);
    }
    else
    {
        set_status("Couldn't save the wallpaper setting", true);
    }
}

static void dir_stats(const char *path, int depth, uint64_t *bytes, int *files, int *folders)
{
    if (depth > 16)
        return;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return;

    struct dirent de;
    while (readdir(fd, &de) == 1)
    {
        char child[PATH_MAX_LEN];
        join_path(child, path, de.name);
        struct stat st;
        if (stat(child, &st) != 0)
            continue;
        if (st.type == STAT_DIR)
        {
            (*folders)++;
            dir_stats(child, depth + 1, bytes, files, folders);
        }
        else
        {
            (*files)++;
            *bytes += st.size;
        }
    }
    close(fd);
}

static void props_add(const char *label, const char *value)
{
    if (F.props_line_count >= 8)
        return;
    strncpy(F.props_lines[F.props_line_count][0], label, 159);
    strncpy(F.props_lines[F.props_line_count][1], value, 159);
    F.props_lines[F.props_line_count][0][159] = '\0';
    F.props_lines[F.props_line_count][1][159] = '\0';
    F.props_line_count++;
}

static void open_properties(int index)
{
    if (index < 0 || index >= F.count)
        return;

    Entry *e = &F.entries[index];
    F.props_entry = *e;
    F.props_entry.icon = NULL;
    F.props_line_count = 0;
    sx_image_free(F.props_icon);
    F.props_icon = NULL;

    char full[PATH_MAX_LEN], buf[160], size[32];
    join_path(full, F.cwd, e->name);

    props_add("Type", type_label(e));
    props_add("Location", F.cwd);

    if (e->type == STAT_DIR)
    {
        uint64_t bytes = 0;
        int files = 0, folders = 0;
        dir_stats(full, 0, &bytes, &files, &folders);
        format_size(bytes, size, sizeof(size));
        props_add("Size", size);
        snprintf(buf, sizeof(buf), "%d file%s, %d folder%s", files, files == 1 ? "" : "s", folders,
                 folders == 1 ? "" : "s");
        props_add("Contains", buf);
    }
    else if (e->type == STAT_FILE)
    {
        format_size(e->size, size, sizeof(size));
        snprintf(buf, sizeof(buf), "%s (%llu bytes)", size, (unsigned long long)e->size);
        props_add("Size", buf);
    }

    if (sx_image_is_supported(e->name))
    {
        SxImage *img = sx_image_load(full);
        if (img)
        {
            snprintf(buf, sizeof(buf), "%d x %d pixels", img->width, img->height);
            props_add("Dimensions", buf);
            int s = img->width > img->height ? img->width : img->height;
            F.props_icon = sx_image_scale(img, img->width * 48 / s > 0 ? img->width * 48 / s : 1,
                                          img->height * 48 / s > 0 ? img->height * 48 / s : 1);
            sx_image_free(img);
        }
        else
        {
            props_add("Dimensions", sx_image_error());
        }
    }

    SxDesktopEntry de;
    if (sx_is_desktop_file(e->name) && sx_desktop_entry_load(full, &de))
    {
        props_add("Shortcut to", de.exec);
        if (de.comment[0])
            props_add("Comment", de.comment);
        SxImage *img = de.icon[0] ? sx_image_load(de.icon) : NULL;
        if (img)
        {
            F.props_icon = sx_image_scale(img, 48, 48);
            sx_image_free(img);
        }
    }

    F.props_open = true;
}

static void draw_folder_icon(SxCanvas *c, int x, int y, int s)
{
    uint32_t base = SX_THEME_ACCENT;
    sx_fill_rounded_rect(c, x + s / 10, y + s * 2 / 10, s * 4 / 10, s * 2 / 10, s / 10, sx_darken(base, 50));
    sx_fill_rounded_rect(c, x + s / 10, y + s * 3 / 10, s * 8 / 10, s * 6 / 10, s / 8, sx_darken(base, 25));
    sx_fill_rounded_rect(c, x + s / 10, y + s * 4 / 10, s * 8 / 10, s * 5 / 10, s / 8, base);
}

static void draw_file_icon(SxCanvas *c, const Entry *e, int x, int y, int s)
{
    int px = x + s * 2 / 10, pw = s * 6 / 10, py = y + s / 10, ph = s * 8 / 10;

    if (e->type == STAT_DEVICE || e->type == STAT_SOCKET)
    {
        uint32_t col = e->type == STAT_DEVICE ? SX_RGB(0x8A, 0x92, 0xA3) : SX_RGB(0xC0, 0x5C, 0xF0);
        sx_fill_rounded_rect(c, x + s * 2 / 10, y + s * 2 / 10, s * 6 / 10, s * 6 / 10, s / 8, col);
        for (int i = 0; i < 3; i++)
        {
            int py2 = y + s * 3 / 10 + i * s * 2 / 10;
            sx_fill_rect(c, x + s / 10, py2, s / 10, s / 14 + 1, col);
            sx_fill_rect(c, x + s * 8 / 10, py2, s / 10, s / 14 + 1, col);
        }
        sx_fill_rounded_rect(c, x + s * 35 / 100, y + s * 35 / 100, s * 3 / 10, s * 3 / 10, 2, SX_RGBA(0, 0, 0, 70));
        return;
    }

    if (e->icon)
    {
        sx_draw_image(c, e->icon, x, y);
        return;
    }

    if (sx_image_is_supported(e->name))
    {
        sx_fill_rounded_rect(c, x + s / 10, y + s * 2 / 10, s * 8 / 10, s * 6 / 10, s / 8, SX_THEME_TEXT);
        sx_fill_circle(c, x + s * 0.68f, y + s * 0.38f, s * 0.08f, SX_THEME_ACCENT);
        float bx = x + s * 0.18f, by = y + s * 0.74f;
        sx_draw_line(c, bx, by, x + s * 0.42f, y + s * 0.44f, s / 9.0f, SX_THEME_BLUE);
        sx_draw_line(c, x + s * 0.42f, y + s * 0.44f, x + s * 0.62f, by, s / 9.0f, SX_THEME_BLUE);
        sx_draw_line(c, x + s * 0.55f, y + s * 0.6f, x + s * 0.82f, by, s / 9.0f, SX_THEME_BLUE);
        return;
    }

    if (ext_is(e->name, "lua"))
    {
        float cx = x + s * 0.5f, cy = y + s * 0.5f;
        sx_fill_circle(c, cx, cy, s * 0.42f, SX_RGB(0x2C, 0x3E, 0x9E));
        sx_fill_circle(c, cx + s * 0.08f, cy - s * 0.08f, s * 0.16f, SX_RGB(255, 255, 255));
        sx_fill_circle(c, cx + s * 0.32f, cy - s * 0.32f, s * 0.09f, SX_RGB(0x2C, 0x3E, 0x9E));
        return;
    }

    if (ext_is(e->name, "elf"))
    {
        sx_fill_rounded_rect(c, px, py, pw, ph, s / 8, SX_THEME_BLUE);
        float cx = x + s * 0.5f, cy = y + s * 0.5f, r = s * 0.16f;
        sx_draw_line(c, cx - r * 0.6f, cy - r, cx + r, cy, s / 10.0f, SX_RGB(255, 255, 255));
        sx_draw_line(c, cx + r, cy, cx - r * 0.6f, cy + r, s / 10.0f, SX_RGB(255, 255, 255));
        sx_draw_line(c, cx - r * 0.6f, cy - r, cx - r * 0.6f, cy + r, s / 10.0f, SX_RGB(255, 255, 255));
        return;
    }

    sx_fill_rounded_rect(c, px, py, pw, ph, s / 10, SX_THEME_TEXT);
    int fold = s / 4;
    sx_fill_rect(c, px + pw - fold, py, fold, fold, SX_RGB(0xB8, 0xBD, 0xC8));
    uint32_t line = ext_is(e->name, "txt") ? SX_THEME_ACCENT : SX_THEME_TEXT_DIM;
    for (int i = 0; i < 3; i++)
        sx_fill_rect(c, px + pw / 6, py + ph * (4 + i * 2) / 10, pw * (i == 2 ? 3 : 4) / 6, s / 14 + 1, line);
}

static void draw_entry_icon(SxCanvas *c, const Entry *e, int x, int y, int s)
{
    if (e->type == STAT_DIR)
        draw_folder_icon(c, x, y, s);
    else
        draw_file_icon(c, e, x, y, s);
}

static bool hovering(HitKind kind, int index)
{
    return F.hover.kind == kind && F.hover.index == index;
}

static bool pressing(HitKind kind, int index)
{
    return F.pressed.kind == kind && F.pressed.index == index && hovering(kind, index);
}

static SxButtonState button_state(HitKind kind, int index)
{
    if (pressing(kind, index))
        return SX_BUTTON_PRESSED;
    return hovering(kind, index) ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL;
}

static void draw_nav_button(SxCanvas *c, SxRect r, HitKind kind, bool enabled)
{
    SxButtonState st = enabled ? button_state(kind, 0) : SX_BUTTON_NORMAL;
    sx_ui_button(c, r, "", &sx_font_sans, st, SX_THEME_SURFACE_HI, SX_THEME_TEXT);

    uint32_t col = enabled ? SX_THEME_TEXT : SX_THEME_DISABLED;
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    if (kind == HIT_BACK)
    {
        sx_draw_line(c, cx + 2, cy - 5, cx - 3, cy, 1.8f, col);
        sx_draw_line(c, cx - 3, cy, cx + 2, cy + 5, 1.8f, col);
    }
    else
    {
        sx_draw_line(c, cx - 5, cy + 2, cx, cy - 3, 1.8f, col);
        sx_draw_line(c, cx, cy - 3, cx + 5, cy + 2, 1.8f, col);
    }
}

static int ellipsize(const SxFont *font, const char *text, int max_w, char *out, int out_size)
{
    int len = (int)strlen(text);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, text, (size_t)len);
    out[len] = '\0';
    if (sx_text_width(font, out) <= max_w)
        return len;
    int dots = sx_text_width(font, "...");
    while (len > 0 && sx_text_width_n(font, out, len) + dots > max_w)
        len--;
    if (len + 4 <= out_size)
        memcpy(out + len, "...", 4);
    return len;
}

static void draw_breadcrumbs(SxCanvas *c, int x, int max_x)
{
    F.crumb_count = 0;
    const SxFont *font = &sx_font_sans;

    int ends[CRUMB_MAX];
    int n = 0;
    ends[n++] = 1;
    for (int i = 1; F.cwd[i] && n < CRUMB_MAX; i++)
        if (F.cwd[i + 1] == '/' || F.cwd[i + 1] == '\0')
            ends[n++] = i + 1;
    if (strcmp(F.cwd, "/") == 0)
        n = 1;

    int first = 0;
    for (;;)
    {
        int total = 0;
        for (int i = first; i < n; i++)
        {
            int start = i <= 1 ? i : ends[i - 1] + 1;
            char seg[NAME_MAX];
            int len = i == 0 ? 0 : ends[i] - start;
            memcpy(seg, F.cwd + start, (size_t)len);
            seg[len] = '\0';
            total += sx_text_width(font, i == 0 ? "Root" : seg) + 20 + 14;
        }
        if (x + total <= max_x || first >= n - 1)
            break;
        first++;
    }

    SxRect bar = sx_rect(x - 4, 9, max_x - x + 4, 30);
    sx_fill_rounded_rect(c, bar.x, bar.y, bar.w, bar.h, 8, SX_THEME_SUNKEN);

    for (int i = first; i < n; i++)
    {
        int start = i <= 1 ? i : ends[i - 1] + 1;
        char seg[NAME_MAX];
        int len = i == 0 ? 0 : ends[i] - start;
        memcpy(seg, F.cwd + start, (size_t)len);
        seg[len] = '\0';
        const char *label = i == 0 ? "Root" : seg;

        int tw = sx_text_width(font, label);
        SxRect r = sx_rect(x, 12, tw + 20, 24);
        bool last = i == n - 1;
        int idx = F.crumb_count;
        if (hovering(HIT_CRUMB, idx) && !last)
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, SX_RGBA(255, 255, 255, 20));
        sx_draw_text(c, font, r.x + 10, sx_text_y_centered(font, r), label,
                     last ? SX_THEME_TEXT : SX_THEME_TEXT_DIM);

        F.crumb_rects[idx] = r;
        F.crumb_ends[idx] = ends[i];
        F.crumb_count++;
        x += r.w;

        if (!last)
        {
            float cx = x + 7.0f, cy = 24.0f;
            sx_draw_line(c, cx - 2, cy - 4, cx + 2, cy, 1.3f, SX_THEME_TEXT_DIM);
            sx_draw_line(c, cx + 2, cy, cx - 2, cy + 4, 1.3f, SX_THEME_TEXT_DIM);
            x += 14;
        }
    }
}

static void draw_toolbar(SxCanvas *c)
{
    int w = c->width;
    sx_fill_rect(c, 0, 0, w, TOOLBAR_H, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, TOOLBAR_H - 1, w, 1, SX_RGBA(0, 0, 0, 90));

    draw_nav_button(c, back_rect(), HIT_BACK, F.history_len > 0);
    draw_nav_button(c, up_rect(), HIT_UP, strcmp(F.cwd, "/") != 0);

    sx_ui_button(c, new_folder_rect(), "New Folder", &sx_font_sans, button_state(HIT_NEW_FOLDER, 0),
                 SX_THEME_SURFACE_HI, SX_THEME_TEXT);
    bool can_delete = F.selected >= 0 && F.selected < F.count;
    sx_ui_button(c, delete_rect(), "Delete", &sx_font_sans,
                 can_delete ? button_state(HIT_DELETE, 0) : SX_BUTTON_NORMAL,
                 can_delete ? SX_RGB(0x4A, 0x2A, 0x2E) : SX_THEME_SURFACE_HI,
                 can_delete ? SX_RGB(0xFF, 0x9A, 0x9C) : SX_THEME_DISABLED);

    draw_breadcrumbs(c, up_rect().x + up_rect().w + 14, new_folder_rect().x - 12);
}

static void draw_prompt(SxCanvas *c)
{
    if (F.prompt == PROMPT_NONE)
        return;

    int w = c->width;
    SxRect bar = sx_rect(0, TOOLBAR_H, w, PROMPT_H);
    sx_fill_rect(c, bar.x, bar.y, bar.w, bar.h, SX_RGB(0x2A, 0x26, 0x1C));
    sx_fill_rect(c, 0, bar.y + bar.h - 1, w, 1, SX_RGBA(0, 0, 0, 90));

    const SxFont *font = &sx_font_sans;
    SxRect field = prompt_field_rect();

    if (F.prompt != PROMPT_DELETE)
    {
        const char *label = F.prompt == PROMPT_NEW_FOLDER ? "New folder name"
                          : F.prompt == PROMPT_NEW_FILE   ? "New file name"
                                                          : "Rename to";
        sx_draw_text(c, &sx_font_sans_bold, SIDEBAR_W + 12, sx_text_y_centered(font, bar), label, SX_THEME_ACCENT);
        sx_fill_rounded_rect(c, field.x, field.y, field.w, field.h, 7, SX_THEME_SUNKEN);
        sx_draw_rounded_rect(c, field.x, field.y, field.w, field.h, 7, SX_THEME_ACCENT);
        SxRect old = c->clip;
        sx_canvas_set_clip(c, sx_rect_intersect(old, sx_rect(field.x + 4, field.y, field.w - 8, field.h)));
        if (F.input_fresh)
            sx_fill_rounded_rect(c, field.x + 7, field.y + 6, sx_text_width(font, F.input) + 6, field.h - 12, 4,
                                 SX_THEME_ACCENT_A(70));
        int tw = sx_draw_text(c, font, field.x + 10, sx_text_y_centered(font, field), F.input, SX_THEME_TEXT);
        sx_fill_rect(c, field.x + 11 + tw, field.y + 7, 2, field.h - 14, SX_THEME_ACCENT);
        c->clip = old;
        sx_ui_button(c, prompt_ok_rect(), F.prompt == PROMPT_RENAME ? "Rename" : "Create", &sx_font_sans_bold,
                     button_state(HIT_PROMPT_OK, 0), SX_THEME_ACCENT, SX_THEME_ACCENT_TEXT);
    }
    else
    {
        Entry *e = &F.entries[F.selected];
        char text[NAME_MAX + 32], name[NAME_MAX + 4];
        ellipsize(font, e->name, prompt_ok_rect().x - SIDEBAR_W - 200, name, sizeof(name));
        snprintf(text, sizeof(text), "Delete \"%s\"?", name);
        sx_draw_text(c, &sx_font_sans_bold, SIDEBAR_W + 12, sx_text_y_centered(font, bar), text, SX_RGB(0xFF, 0x9A, 0x9C));
        const char *hint = "Only empty folders can be deleted";
        int hint_x = SIDEBAR_W + 24 + sx_text_width(&sx_font_sans_bold, text);
        if (e->type == STAT_DIR && hint_x + sx_text_width(&sx_font_sans_small, hint) < prompt_ok_rect().x - 12)
            sx_draw_text(c, &sx_font_sans_small, hint_x, sx_text_y_centered(&sx_font_sans_small, bar), hint,
                         SX_THEME_TEXT_DIM);
        sx_ui_button(c, prompt_ok_rect(), "Delete", &sx_font_sans_bold, button_state(HIT_PROMPT_OK, 0),
                     SX_THEME_RED, SX_RGB(255, 255, 255));
    }

    sx_ui_button(c, prompt_cancel_rect(), "Cancel", &sx_font_sans, button_state(HIT_PROMPT_CANCEL, 0),
                 SX_THEME_SURFACE_HI, SX_THEME_TEXT);
}

static void draw_sidebar(SxCanvas *c)
{
    SxRect s = sidebar_rect();
    sx_fill_rect(c, s.x, s.y, s.w, s.h, SX_THEME_SUNKEN);
    sx_fill_rect(c, s.x + s.w - 1, s.y, 1, s.h, SX_RGBA(0, 0, 0, 90));
    sx_draw_text(c, &sx_font_sans_small, 18, s.y + 10, "PLACES", SX_THEME_TEXT_DIM);

    for (int i = 0; i < PLACE_N; i++)
    {
        SxRect r = place_rect(i);
        bool current = strcmp(F.cwd, places[i].path) == 0;
        if (current)
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8, SX_THEME_ACCENT_A(40));
        else if (hovering(HIT_PLACE, i))
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8, SX_RGBA(255, 255, 255, 14));

        Entry fake;
        memset(&fake, 0, sizeof(fake));
        fake.type = strcmp(places[i].path, "/dev") == 0 ? STAT_DEVICE : STAT_DIR;
        draw_entry_icon(c, &fake, r.x + 8, r.y + 5, 20);
        sx_draw_text(c, &sx_font_sans, r.x + 36, sx_text_y_centered(&sx_font_sans, r), places[i].label,
                     current ? SX_THEME_TEXT : SX_THEME_TEXT_DIM);
    }
}

static void draw_list(SxCanvas *c)
{
    SxRect l = list_rect();
    sx_fill_rect(c, l.x, l.y, l.w, l.h, SX_THEME_BG);

    const SxFont *font = &sx_font_sans;
    const SxFont *small = &sx_font_sans_small;
    int right = l.x + l.w - 16 - SCROLLBAR_W;
    int size_x = right - SIZE_COL_W;
    int type_x = size_x - TYPE_COL_W;
    int name_x = l.x + 44;

    SxRect head = sx_rect(l.x, l.y, l.w, HEADER_H);
    sx_fill_rect(c, head.x, head.y, head.w, head.h, (sx_theme->title_inactive));
    sx_fill_rect(c, head.x, head.y + head.h - 1, head.w, 1, SX_RGBA(0, 0, 0, 80));
    int hy = sx_text_y_centered(small, head);
    sx_draw_text(c, small, name_x, hy, "Name", SX_THEME_TEXT_DIM);
    sx_draw_text(c, small, type_x, hy, "Type", SX_THEME_TEXT_DIM);
    sx_draw_text(c, small, right - sx_text_width(small, "Size"), hy, "Size", SX_THEME_TEXT_DIM);

    SxRect rows = rows_rect();
    SxRect old = c->clip;
    sx_canvas_set_clip(c, sx_rect_intersect(old, rows));

    if (F.count == 0)
        sx_draw_text_centered(c, font, sx_rect(rows.x, rows.y, rows.w, 80), "This folder is empty", SX_THEME_TEXT_DIM);

    for (int i = F.scroll; i < F.count && i < F.scroll + visible_rows() + 1; i++)
    {
        Entry *e = &F.entries[i];
        SxRect r = sx_rect(rows.x + 6, rows.y + (i - F.scroll) * ROW_H, rows.w - 12 - SCROLLBAR_W, ROW_H);

        if (i == F.selected)
            sx_fill_rounded_rect(c, r.x, r.y + 1, r.w, r.h - 2, 7, SX_THEME_ACCENT_A(48));
        else if (hovering(HIT_ROW, i))
            sx_fill_rounded_rect(c, r.x, r.y + 1, r.w, r.h - 2, 7, SX_RGBA(255, 255, 255, 12));

        draw_entry_icon(c, e, r.x + 10, r.y + 5, 20);

        char name[NAME_MAX + 4];
        ellipsize(font, e->name, type_x - name_x - 12, name, sizeof(name));
        sx_draw_text(c, font, name_x, sx_text_y_centered(font, r), name, SX_THEME_TEXT);
        sx_draw_text(c, small, type_x, sx_text_y_centered(small, r), type_label(e), SX_THEME_TEXT_DIM);

        if (e->type == STAT_FILE)
        {
            char size[32];
            format_size(e->size, size, sizeof(size));
            sx_draw_text(c, small, right - sx_text_width(small, size), sx_text_y_centered(small, r), size,
                         SX_THEME_TEXT_DIM);
        }
    }

    if (F.count > visible_rows())
    {
        SxRect track = sx_rect(l.x + l.w - SCROLLBAR_W - 4, rows.y + 4, SCROLLBAR_W, rows.h - 8);
        int thumb_h = track.h * visible_rows() / F.count;
        if (thumb_h < 24)
            thumb_h = 24;
        int max_scroll = F.count - visible_rows();
        int thumb_y = track.y + (track.h - thumb_h) * F.scroll / (max_scroll > 0 ? max_scroll : 1);
        sx_fill_rounded_rect(c, track.x, track.y, track.w, track.h, 4, SX_RGBA(255, 255, 255, 10));
        sx_fill_rounded_rect(c, track.x, thumb_y, track.w, thumb_h, 4,
                             SX_RGBA(255, 255, 255, hovering(HIT_SCROLLBAR, 0) ? 90 : 50));
    }

    c->clip = old;
}

static void draw_status(SxCanvas *c)
{
    int w = c->width, y = c->height - STATUS_H;
    SxRect bar = sx_rect(0, y, w, STATUS_H);
    sx_fill_rect(c, bar.x, bar.y, bar.w, bar.h, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, y, w, 1, SX_RGBA(0, 0, 0, 90));

    const SxFont *small = &sx_font_sans_small;
    char left[96];
    int folders = 0;
    for (int i = 0; i < F.count; i++)
        if (F.entries[i].type == STAT_DIR)
            folders++;
    snprintf(left, sizeof(left), "%d item%s  |  %d folder%s, %d file%s", F.count, F.count == 1 ? "" : "s",
             folders, folders == 1 ? "" : "s", F.count - folders, F.count - folders == 1 ? "" : "s");
    sx_draw_text(c, small, 12, sx_text_y_centered(small, bar), left, SX_THEME_TEXT_DIM);

    if (F.status[0])
    {
        int tw = sx_text_width(small, F.status);
        sx_draw_text(c, small, w - 12 - tw, sx_text_y_centered(small, bar), F.status,
                     F.status_error ? SX_RGB(0xFF, 0x9A, 0x9C) : SX_THEME_ACCENT);
    }
}

static void menu_add(const char *label, const char *shortcut, Action action, bool enabled)
{
    if (F.menu_count >= MENU_MAX)
        return;
    MenuItem *m = &F.menu[F.menu_count++];
    m->label = label;
    m->shortcut = shortcut;
    m->action = action;
    m->enabled = enabled;
}

static int menu_height(void)
{
    int h = 12;
    for (int i = 0; i < F.menu_count; i++)
        h += F.menu[i].action == ACT_SEPARATOR ? MENU_SEP_H : MENU_ITEM_H;
    return h;
}

static SxRect menu_item_rect(int i)
{
    int y = F.menu_y + 6;
    for (int k = 0; k < i; k++)
        y += F.menu[k].action == ACT_SEPARATOR ? MENU_SEP_H : MENU_ITEM_H;
    int h = F.menu[i].action == ACT_SEPARATOR ? MENU_SEP_H : MENU_ITEM_H;
    return sx_rect(F.menu_x + 6, y, MENU_W - 12, h);
}

static void open_menu(int x, int y, int target)
{
    F.menu_count = 0;
    F.menu_target = target;
    F.prompt = PROMPT_NONE;

    if (target >= 0)
    {
        Entry *e = &F.entries[target];
        bool file = e->type == STAT_FILE;
        bool removable = e->type == STAT_DIR || file;

        menu_add(e->type == STAT_DIR ? "Open" : (ext_is(e->name, "elf") || ext_is(e->name, "lua") || sx_is_desktop_file(e->name) ? "Run" : "Open"),
                 "Enter", ACT_OPEN, e->type != STAT_DEVICE && e->type != STAT_SOCKET);
        if (file)
            menu_add("Edit in Notes", "", ACT_EDIT, true);
        if (file && sx_image_is_supported(e->name))
            menu_add("Set as Wallpaper", "", ACT_WALLPAPER, true);
        if (e->type == STAT_DIR)
            menu_add("Open in Terminal", "", ACT_TERMINAL, true);
        menu_add("", "", ACT_SEPARATOR, true);
        menu_add("Cut", "Ctrl+X", ACT_CUT, removable);
        menu_add("Copy", "Ctrl+C", ACT_COPY, removable);
        menu_add("Paste", "Ctrl+V", ACT_PASTE, F.clip_path[0] != 0);
        menu_add("", "", ACT_SEPARATOR, true);
        menu_add("Rename", "F2", ACT_RENAME, removable);
        menu_add("Delete", "Del", ACT_DELETE, removable);
        menu_add("", "", ACT_SEPARATOR, true);
        menu_add("Properties", "Alt+Enter", ACT_PROPERTIES, true);
    }
    else
    {
        menu_add("New File", "Ctrl+N", ACT_NEW_FILE, true);
        menu_add("New Folder", "Ctrl+Shift+N", ACT_NEW_FOLDER, true);
        menu_add("", "", ACT_SEPARATOR, true);
        menu_add("Paste", "Ctrl+V", ACT_PASTE, F.clip_path[0] != 0);
        menu_add("Refresh", "F5", ACT_REFRESH, true);
        menu_add("", "", ACT_SEPARATOR, true);
        menu_add("Open in Terminal", "", ACT_TERMINAL, true);
    }

    int h = menu_height();
    if (x + MENU_W > width() - 4)
        x = width() - 4 - MENU_W;
    if (y + h > height() - 4)
        y = height() - 4 - h;
    F.menu_x = x < 4 ? 4 : x;
    F.menu_y = y < 4 ? 4 : y;
    F.menu_open = true;
}

static void run_action(Action action, int target)
{
    F.menu_open = false;
    if (target >= 0 && target < F.count)
        F.selected = target;

    switch (action)
    {
        case ACT_OPEN:       open_entry(target); break;
        case ACT_EDIT:       open_in_notes(target); break;
        case ACT_CUT:        clip_set(true); break;
        case ACT_COPY:       clip_set(false); break;
        case ACT_PASTE:      paste(); break;
        case ACT_RENAME:     start_prompt(PROMPT_RENAME); break;
        case ACT_DELETE:     start_prompt(PROMPT_DELETE); break;
        case ACT_WALLPAPER:  set_wallpaper(target); break;
        case ACT_TERMINAL:   open_in_terminal(target); break;
        case ACT_PROPERTIES: open_properties(target); break;
        case ACT_NEW_FILE:   start_prompt(PROMPT_NEW_FILE); break;
        case ACT_NEW_FOLDER: start_prompt(PROMPT_NEW_FOLDER); break;
        case ACT_REFRESH:    refresh(); break;
        default: break;
    }
}

static void draw_popup_shadow(SxCanvas *c, SxRect r, int radius)
{
    for (int i = 1; i <= 6; i++)
        sx_fill_rounded_rect(c, r.x - i, r.y - i + 3, r.w + 2 * i, r.h + 2 * i, radius + i, SX_RGBA(0, 0, 0, 10));
}

static void draw_menu(SxCanvas *c)
{
    if (!F.menu_open)
        return;

    SxRect m = sx_rect(F.menu_x, F.menu_y, MENU_W, menu_height());
    draw_popup_shadow(c, m, 10);
    sx_fill_rounded_rect(c, m.x, m.y, m.w, m.h, 10, SX_THEME_SURFACE);
    sx_draw_rounded_rect(c, m.x, m.y, m.w, m.h, 10, SX_RGBA(255, 255, 255, 26));

    const SxFont *font = &sx_font_sans;
    const SxFont *small = &sx_font_sans_small;
    for (int i = 0; i < F.menu_count; i++)
    {
        MenuItem *it = &F.menu[i];
        SxRect r = menu_item_rect(i);
        if (it->action == ACT_SEPARATOR)
        {
            sx_fill_rect(c, r.x + 6, r.y + r.h / 2, r.w - 12, 1, SX_RGBA(255, 255, 255, 18));
            continue;
        }

        bool hot = it->enabled && hovering(HIT_MENU_ITEM, i);
        bool danger = it->action == ACT_DELETE;
        if (hot)
            sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, danger ? SX_THEME_RED_A(200) : SX_THEME_ACCENT_A(60));

        uint32_t col = !it->enabled ? SX_THEME_DISABLED : (danger && !hot ? SX_RGB(0xFF, 0x9A, 0x9C) : SX_THEME_TEXT);
        sx_draw_text(c, font, r.x + 12, sx_text_y_centered(font, r), it->label, col);
        if (it->shortcut[0])
        {
            int sw = sx_text_width(small, it->shortcut);
            sx_draw_text(c, small, r.x + r.w - 12 - sw, sx_text_y_centered(small, r), it->shortcut,
                         it->enabled ? SX_THEME_TEXT_DIM : SX_THEME_DISABLED);
        }
    }
}

static SxRect props_rect(void)
{
    int h = 112 + F.props_line_count * 26 + 56;
    return sx_rect((width() - PROPS_W) / 2, (height() - h) / 2, PROPS_W, h);
}

static SxRect props_close_rect(void)
{
    SxRect p = props_rect();
    return sx_rect(p.x + p.w - 16 - 90, p.y + p.h - 16 - 32, 90, 32);
}

static void draw_properties(SxCanvas *c)
{
    if (!F.props_open)
        return;

    sx_fill_rect(c, 0, 0, c->width, c->height, SX_RGBA(0, 0, 0, 110));

    SxRect p = props_rect();
    draw_popup_shadow(c, p, 14);
    sx_fill_rounded_rect(c, p.x, p.y, p.w, p.h, 14, SX_THEME_SURFACE);
    sx_draw_rounded_rect(c, p.x, p.y, p.w, p.h, 14, SX_RGBA(255, 255, 255, 26));

    if (F.props_icon)
        sx_draw_image(c, F.props_icon, p.x + 20 + (48 - F.props_icon->width) / 2, p.y + 20 + (48 - F.props_icon->height) / 2);
    else
        draw_entry_icon(c, &F.props_entry, p.x + 20, p.y + 20, 48);

    char name[NAME_MAX + 4];
    ellipsize(&sx_font_sans_large, F.props_entry.name, p.w - 104, name, sizeof(name));
    sx_draw_text(c, &sx_font_sans_large, p.x + 84, p.y + 18, name, SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans_small, p.x + 86, p.y + 52, "Properties", SX_THEME_TEXT_DIM);
    sx_fill_rect(c, p.x + 20, p.y + 92, p.w - 40, 1, SX_RGBA(255, 255, 255, 18));

    const SxFont *font = &sx_font_sans;
    for (int i = 0; i < F.props_line_count; i++)
    {
        int y = p.y + 104 + i * 26;
        sx_draw_text(c, font, p.x + 20, y, F.props_lines[i][0], SX_THEME_TEXT_DIM);
        char value[164];
        ellipsize(font, F.props_lines[i][1], p.w - 150, value, sizeof(value));
        sx_draw_text(c, font, p.x + 130, y, value, SX_THEME_TEXT);
    }

    sx_ui_button(c, props_close_rect(), "Close", &sx_font_sans_bold, button_state(HIT_PROPS_CLOSE, 0),
                 SX_THEME_ACCENT, SX_THEME_ACCENT_TEXT);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(F.win);
    sx_clear(c, SX_THEME_BG);
    draw_list(c);
    draw_sidebar(c);
    draw_toolbar(c);
    draw_prompt(c);
    draw_status(c);
    draw_menu(c);
    draw_properties(c);
    sx_window_commit(F.win);
}

static Hit hit_test(int x, int y)
{
    Hit h = { HIT_NONE, 0 };

    if (F.props_open)
    {
        if (sx_rect_contains(props_close_rect(), x, y))
            h.kind = HIT_PROPS_CLOSE;
        return h;
    }

    if (F.menu_open)
    {
        for (int i = 0; i < F.menu_count; i++)
        {
            if (F.menu[i].action != ACT_SEPARATOR && sx_rect_contains(menu_item_rect(i), x, y))
            {
                h.kind = HIT_MENU_ITEM;
                h.index = i;
                return h;
            }
        }
        return h;
    }

    if (sx_rect_contains(back_rect(), x, y))        { h.kind = HIT_BACK; return h; }
    if (sx_rect_contains(up_rect(), x, y))          { h.kind = HIT_UP; return h; }
    if (sx_rect_contains(new_folder_rect(), x, y))  { h.kind = HIT_NEW_FOLDER; return h; }
    if (sx_rect_contains(delete_rect(), x, y))      { h.kind = HIT_DELETE; return h; }

    for (int i = 0; i < F.crumb_count; i++)
    {
        if (sx_rect_contains(F.crumb_rects[i], x, y))
        {
            h.kind = HIT_CRUMB;
            h.index = i;
            return h;
        }
    }

    if (F.prompt != PROMPT_NONE)
    {
        if (sx_rect_contains(prompt_ok_rect(), x, y))     { h.kind = HIT_PROMPT_OK; return h; }
        if (sx_rect_contains(prompt_cancel_rect(), x, y)) { h.kind = HIT_PROMPT_CANCEL; return h; }
    }

    for (int i = 0; i < PLACE_N; i++)
    {
        if (sx_rect_contains(place_rect(i), x, y))
        {
            h.kind = HIT_PLACE;
            h.index = i;
            return h;
        }
    }

    SxRect rows = rows_rect();
    if (F.count > visible_rows() && x >= rows.x + rows.w - SCROLLBAR_W - 8 && sx_rect_contains(rows, x, y))
    {
        h.kind = HIT_SCROLLBAR;
        return h;
    }

    if (sx_rect_contains(rows, x, y))
    {
        int i = F.scroll + (y - rows.y) / ROW_H;
        if (i < F.count)
        {
            h.kind = HIT_ROW;
            h.index = i;
        }
    }
    return h;
}

static void scroll_to_pointer(int y)
{
    SxRect rows = rows_rect();
    int max_scroll = F.count - visible_rows();
    if (max_scroll <= 0)
        return;
    F.scroll = (y - rows.y) * max_scroll / (rows.h > 1 ? rows.h : 1);
    clamp_scroll();
}

static void activate(Hit h)
{
    switch (h.kind)
    {
        case HIT_BACK:          go_back(); break;
        case HIT_UP:            go_up(); break;
        case HIT_NEW_FOLDER:    start_prompt(PROMPT_NEW_FOLDER); break;
        case HIT_DELETE:        start_prompt(PROMPT_DELETE); break;
        case HIT_PROMPT_OK:     confirm_prompt(); break;
        case HIT_PROMPT_CANCEL: F.prompt = PROMPT_NONE; break;
        case HIT_PLACE:         navigate(places[h.index].path, NULL); break;
        case HIT_PROPS_CLOSE:   F.props_open = false; break;
        case HIT_MENU_ITEM:
            if (F.menu[h.index].enabled)
                run_action(F.menu[h.index].action, F.menu_target);
            break;
        case HIT_CRUMB:
        {
            char path[PATH_MAX_LEN];
            int end = F.crumb_ends[h.index];
            memcpy(path, F.cwd, (size_t)end);
            path[end] = '\0';
            navigate(path, NULL);
            break;
        }
        default:
            break;
    }
}

static void handle_prompt_key(const SxEvent *ev)
{
    if (ev->key == SX_KEY_ESCAPE)
    {
        F.prompt = PROMPT_NONE;
        return;
    }
    if (ev->key == SX_KEY_ENTER)
    {
        confirm_prompt();
        return;
    }
    if (F.prompt == PROMPT_DELETE)
        return;

    if (ev->mods & (SX_MOD_CTRL | SX_MOD_ALT))
        return;

    bool printable = ev->ch >= 32 && ev->ch < 127 && ev->ch != '/';
    if (!printable && ev->key != SX_KEY_BACKSPACE)
        return;
    if (F.input_fresh && printable)
        F.input[0] = '\0';
    F.input_fresh = false;

    size_t len = strlen(F.input);
    if (ev->key == SX_KEY_BACKSPACE)
    {
        if (len > 0)
            F.input[len - 1] = '\0';
    }
    else if (printable && len < NAME_MAX - 1)
    {
        F.input[len] = (char)ev->ch;
        F.input[len + 1] = '\0';
    }
}

static void handle_key(const SxEvent *ev)
{
    if (F.props_open)
    {
        if (ev->key == SX_KEY_ESCAPE || ev->key == SX_KEY_ENTER)
            F.props_open = false;
        return;
    }

    if (F.menu_open)
    {
        if (ev->key == SX_KEY_ESCAPE)
            F.menu_open = false;
        return;
    }

    if (F.prompt != PROMPT_NONE)
    {
        handle_prompt_key(ev);
        return;
    }

    bool ctrl = ev->mods & SX_MOD_CTRL;
    bool alt = ev->mods & SX_MOD_ALT;

    switch (ev->key)
    {
        case SX_KEY_UP:
            if (alt)
                go_up();
            else if (F.selected > 0)
                F.selected--;
            break;
        case SX_KEY_DOWN:
            if (F.selected < F.count - 1)
                F.selected++;
            break;
        case SX_KEY_LEFT:
            if (alt)
                go_back();
            break;
        case SX_KEY_HOME:
            F.selected = F.count ? 0 : -1;
            break;
        case SX_KEY_END:
            F.selected = F.count - 1;
            break;
        case SX_KEY_PAGE_UP:
            F.selected = F.selected - visible_rows() > 0 ? F.selected - visible_rows() : (F.count ? 0 : -1);
            break;
        case SX_KEY_PAGE_DOWN:
            F.selected = F.selected + visible_rows() < F.count ? F.selected + visible_rows() : F.count - 1;
            break;
        case SX_KEY_ENTER:
            if (alt)
                open_properties(F.selected);
            else
                open_entry(F.selected);
            break;
        case SX_KEY_F2:
            start_prompt(PROMPT_RENAME);
            break;
        case SX_KEY_MENU:
            if (has_selection())
            {
                SxRect rows = rows_rect();
                open_menu(rows.x + 60, rows.y + (F.selected - F.scroll + 1) * ROW_H, F.selected);
            }
            break;
        case SX_KEY_BACKSPACE:
            go_up();
            break;
        case SX_KEY_DELETE:
            start_prompt(PROMPT_DELETE);
            break;
        case SX_KEY_F5:
            refresh();
            break;
        default:
            if (ctrl && ev->key == 'n')
                start_prompt(ev->mods & SX_MOD_SHIFT ? PROMPT_NEW_FOLDER : PROMPT_NEW_FILE);
            else if (ctrl && ev->key == 'r')
                refresh();
            else if (ctrl && ev->key == 'c')
                clip_set(false);
            else if (ctrl && ev->key == 'x')
                clip_set(true);
            else if (ctrl && ev->key == 'v')
                paste();
            break;
    }
    ensure_visible();
    clamp_scroll();
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("files: cannot connect to the display server\n");
        return 1;
    }

    F.win = sx_window_create("Files", 780, 500, SX_WINDOW_RESIZABLE);
    if (!F.win)
        return 1;

    const char *start = argc > 1 && argv[1][0] == '/' ? argv[1] : "/";
    if (!load_dir(start, NULL))
        load_dir("/", NULL);
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
                clamp_scroll();
                redraw = true;
                break;

            case SX_EVENT_FOCUS_IN:
                refresh();
                redraw = true;
                break;

            case SX_EVENT_MOUSE_MOVE:
            {
                if (F.pressed.kind == HIT_SCROLLBAR && (ev.buttons & SX_BUTTON_LEFT))
                {
                    scroll_to_pointer(ev.y);
                    redraw = true;
                    break;
                }
                Hit h = hit_test(ev.x, ev.y);
                if (h.kind != F.hover.kind || h.index != F.hover.index)
                {
                    F.hover = h;
                    redraw = true;
                }
                break;
            }

            case SX_EVENT_MOUSE_LEAVE:
                F.hover.kind = HIT_NONE;
                redraw = true;
                break;

            case SX_EVENT_MOUSE_DOWN:
            {
                if (F.menu_open && hit_test(ev.x, ev.y).kind != HIT_MENU_ITEM)
                {
                    F.menu_open = false;
                    F.pressed.kind = HIT_NONE;
                    redraw = true;
                    break;
                }

                if (ev.button == SX_BUTTON_RIGHT && !F.props_open)
                {
                    F.menu_open = false;
                    Hit h = hit_test(ev.x, ev.y);
                    if (h.kind == HIT_ROW)
                    {
                        F.selected = h.index;
                        open_menu(ev.x, ev.y, h.index);
                    }
                    else if (sx_rect_contains(rows_rect(), ev.x, ev.y))
                    {
                        open_menu(ev.x, ev.y, -1);
                    }
                    F.hover = hit_test(ev.x, ev.y);
                    redraw = true;
                    break;
                }

                if (ev.button != SX_BUTTON_LEFT)
                    break;
                Hit h = hit_test(ev.x, ev.y);
                F.pressed = h;
                F.hover = h;
                if (h.kind == HIT_ROW)
                {
                    uint64_t now = sx_time_ms();
                    bool dbl = now - F.last_click_ms < 400 && F.last_click_row == h.index;
                    F.last_click_ms = dbl ? 0 : now;
                    F.last_click_row = h.index;
                    F.selected = h.index;
                    if (F.prompt == PROMPT_DELETE)
                        F.prompt = PROMPT_NONE;
                    if (dbl)
                        open_entry(h.index);
                }
                else if (h.kind == HIT_SCROLLBAR)
                {
                    scroll_to_pointer(ev.y);
                }
                redraw = true;
                break;
            }

            case SX_EVENT_MOUSE_UP:
            {
                if (ev.button != SX_BUTTON_LEFT)
                    break;
                Hit h = hit_test(ev.x, ev.y);
                if (h.kind == F.pressed.kind && h.index == F.pressed.index &&
                    h.kind != HIT_ROW && h.kind != HIT_SCROLLBAR)
                    activate(h);
                F.pressed.kind = HIT_NONE;
                F.hover = hit_test(ev.x, ev.y);
                redraw = true;
                break;
            }

            case SX_EVENT_MOUSE_WHEEL:
                F.scroll -= ev.wheel * 3;
                clamp_scroll();
                redraw = true;
                break;

            case SX_EVENT_KEY_DOWN:
                handle_key(&ev);
                redraw = true;
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
    sx_window_destroy(F.win);
    sx_disconnect();
    return 0;
}
