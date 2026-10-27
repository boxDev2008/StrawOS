#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strawx.h>
#include <ui.h>

#define DEFAULT_PATH "/notes.txt"
#define HEADER_H   40
#define STATUS_H   26
#define GUTTER_W   52
#define PAD_X      10
#define TAB_WIDTH  4

static const SxFont *font = &sx_font_mono;

static char file_path[256] = DEFAULT_PATH;
static const char *file_name = &DEFAULT_PATH[1];
static bool lua_mode;

#define HL_KEYWORD  SX_RGB(0xC6, 0x7C, 0xF0)
#define HL_CONSTANT SX_RGB(0xFF, 0x9E, 0x64)
#define HL_BUILTIN  SX_RGB(0x6C, 0xA8, 0xFF)
#define HL_FUNCNAME SX_RGB(0xF2, 0xD0, 0x7A)
#define HL_STRING   SX_RGB(0x8E, 0xD4, 0x8A)
#define HL_NUMBER   SX_THEME_ACCENT
#define HL_COMMENT  SX_RGB(0x6A, 0x72, 0x82)
#define HL_MAX_LINE 2048

typedef enum
{
    HL_NORMAL,
    HL_LONG_STRING,
    HL_LONG_COMMENT,
}
HlMode;

typedef struct
{
    HlMode mode;
    int    level;
}
HlState;

static const char *lua_keywords[] = {
    "and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if", "in",
    "local", "not", "or", "repeat", "return", "then", "until", "while", NULL,
};

static const char *lua_constants[] = { "true", "false", "nil", NULL };

static const char *lua_builtins[] = {
    "print", "pairs", "ipairs", "require", "type", "tostring", "tonumber", "select", "error", "assert",
    "pcall", "xpcall", "setmetatable", "getmetatable", "rawget", "rawset", "rawequal", "rawlen", "next",
    "load", "loadfile", "dofile", "collectgarbage", "string", "table", "math", "io", "os", "coroutine",
    "utf8", "debug", "package", "sys", "sh", "shell", "arg", "_G", "_ENV", "_VERSION", "self", NULL,
};

static bool word_in(const char *s, int len, const char **list)
{
    for (int i = 0; list[i]; i++)
        if ((int)strlen(list[i]) == len && strncmp(s, list[i], (size_t)len) == 0)
            return true;
    return false;
}

static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static int long_bracket_open(const char *s, int len, int i)
{
    if (i >= len || s[i] != '[')
        return -1;
    int level = 0;
    int j = i + 1;
    while (j < len && s[j] == '=')
    {
        level++;
        j++;
    }
    return j < len && s[j] == '[' ? level : -1;
}

static int long_bracket_close(const char *s, int len, int i, int level)
{
    for (; i < len; i++)
    {
        if (s[i] != ']')
            continue;
        int j = i + 1, n = 0;
        while (j < len && s[j] == '=')
        {
            n++;
            j++;
        }
        if (n == level && j < len && s[j] == ']')
            return j + 1;
    }
    return -1;
}

static void hl_fill(uint32_t *colors, int from, int to, uint32_t color)
{
    for (int i = from; i < to && i < HL_MAX_LINE; i++)
        if (i >= 0)
            colors[i] = color;
}

static void hl_line(const char *s, int len, HlState *st, uint32_t *colors)
{
    hl_fill(colors, 0, len, SX_THEME_TEXT);
    int i = 0;
    bool after_function = false;

    if (st->mode != HL_NORMAL)
    {
        uint32_t color = st->mode == HL_LONG_STRING ? HL_STRING : HL_COMMENT;
        int end = long_bracket_close(s, len, 0, st->level);
        if (end < 0)
        {
            hl_fill(colors, 0, len, color);
            return;
        }
        hl_fill(colors, 0, end, color);
        st->mode = HL_NORMAL;
        i = end;
    }

    while (i < len)
    {
        char c = s[i];

        if (c == '-' && i + 1 < len && s[i + 1] == '-')
        {
            int level = long_bracket_open(s, len, i + 2);
            if (level >= 0)
            {
                int end = long_bracket_close(s, len, i + 4 + level, level);
                if (end < 0)
                {
                    hl_fill(colors, i, len, HL_COMMENT);
                    st->mode = HL_LONG_COMMENT;
                    st->level = level;
                    return;
                }
                hl_fill(colors, i, end, HL_COMMENT);
                i = end;
                continue;
            }
            hl_fill(colors, i, len, HL_COMMENT);
            return;
        }

        if (c == '"' || c == '\'')
        {
            int j = i + 1;
            while (j < len && s[j] != c)
                j += s[j] == '\\' ? 2 : 1;
            if (j > len)
                j = len;
            hl_fill(colors, i, j + 1, HL_STRING);
            i = j + 1;
            continue;
        }

        if (c == '[')
        {
            int level = long_bracket_open(s, len, i);
            if (level >= 0)
            {
                int end = long_bracket_close(s, len, i + 2 + level, level);
                if (end < 0)
                {
                    hl_fill(colors, i, len, HL_STRING);
                    st->mode = HL_LONG_STRING;
                    st->level = level;
                    return;
                }
                hl_fill(colors, i, end, HL_STRING);
                i = end;
                continue;
            }
        }

        if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < len && s[i + 1] >= '0' && s[i + 1] <= '9'))
        {
            int j = i + 1;
            while (j < len)
            {
                char d = s[j];
                if (is_ident_char(d) || d == '.')
                    j++;
                else if ((d == '+' || d == '-') && (s[j - 1] == 'e' || s[j - 1] == 'E' || s[j - 1] == 'p' || s[j - 1] == 'P'))
                    j++;
                else
                    break;
            }
            hl_fill(colors, i, j, HL_NUMBER);
            i = j;
            continue;
        }

        if (is_ident_start(c))
        {
            int j = i + 1;
            while (j < len && is_ident_char(s[j]))
                j++;
            int n = j - i;

            if (after_function)
            {
                int k = j;
                while (k < len && (s[k] == '.' || s[k] == ':' || is_ident_char(s[k])))
                    k++;
                hl_fill(colors, i, k, HL_FUNCNAME);
                after_function = false;
                i = k;
                continue;
            }

            if (word_in(s + i, n, lua_keywords))
            {
                hl_fill(colors, i, j, HL_KEYWORD);
                after_function = n == 8 && strncmp(s + i, "function", 8) == 0;
            }
            else if (word_in(s + i, n, lua_constants))
            {
                hl_fill(colors, i, j, HL_CONSTANT);
            }
            else if (word_in(s + i, n, lua_builtins) && !(i > 0 && (s[i - 1] == '.' || s[i - 1] == ':')))
            {
                hl_fill(colors, i, j, HL_BUILTIN);
            }
            i = j;
            continue;
        }

        if (c != ' ' && c != '\t')
            after_function = false;
        i++;
    }
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static struct
{
    SxWindow *win;

    char *text;
    int   len, cap;

    int   cursor;
    int   anchor;
    int   want_col;

    int   scroll_line;
    int   scroll_x;

    bool  modified;
    bool  focused;
    bool  cursor_on;
    uint64_t blink_ms;

    bool  selecting;
    bool  save_hover, save_down;
    bool  run_hover, run_down;
    char  status[128];
    uint64_t status_ms;

    char *clipboard;
    int   clip_len;
}
E;

static void reserve(int need)
{
    if (need <= E.cap)
        return;
    int cap = E.cap ? E.cap : 4096;
    while (cap < need)
        cap *= 2;
    char *n = malloc((size_t)cap);
    if (!n)
        return;
    if (E.text)
    {
        memcpy(n, E.text, (size_t)E.len);
        free(E.text);
    }
    E.text = n;
    E.cap = cap;
}

static bool has_selection(void)
{
    return E.anchor >= 0 && E.anchor != E.cursor;
}

static int sel_start(void) { return E.anchor < E.cursor ? E.anchor : E.cursor; }
static int sel_end(void)   { return E.anchor < E.cursor ? E.cursor : E.anchor; }

static void delete_range(int a, int b)
{
    if (b <= a)
        return;
    memmove(E.text + a, E.text + b, (size_t)(E.len - b));
    E.len -= b - a;
    if (E.cursor > b)
        E.cursor -= b - a;
    else if (E.cursor > a)
        E.cursor = a;
    E.modified = true;
}

static void delete_selection(void)
{
    if (!has_selection())
        return;
    int a = sel_start(), b = sel_end();
    delete_range(a, b);
    E.cursor = a;
    E.anchor = -1;
}

static void insert_text(const char *s, int n)
{
    delete_selection();
    E.anchor = -1;
    reserve(E.len + n + 1);
    if (E.len + n + 1 > E.cap)
        return;
    memmove(E.text + E.cursor + n, E.text + E.cursor, (size_t)(E.len - E.cursor));
    memcpy(E.text + E.cursor, s, (size_t)n);
    E.len += n;
    E.cursor += n;
    E.modified = true;
}

static int line_start(int pos)
{
    while (pos > 0 && E.text[pos - 1] != '\n')
        pos--;
    return pos;
}

static int line_end(int pos)
{
    while (pos < E.len && E.text[pos] != '\n')
        pos++;
    return pos;
}

static int line_of(int pos)
{
    int line = 0;
    for (int i = 0; i < pos; i++)
        if (E.text[i] == '\n')
            line++;
    return line;
}

static int line_count(void)
{
    return line_of(E.len) + 1;
}

static int pos_of_line(int line)
{
    int pos = 0;
    while (line > 0 && pos < E.len)
    {
        if (E.text[pos] == '\n')
            line--;
        pos++;
    }
    return pos;
}

static int col_of(int pos)
{
    return pos - line_start(pos);
}

static int pos_at(int line, int col)
{
    int start = pos_of_line(line);
    int end = line_end(start);
    return start + col < end ? start + col : end;
}

static void set_status(const char *msg)
{
    strncpy(E.status, msg, sizeof(E.status) - 1);
    E.status_ms = sx_time_ms();
}

static void load(void)
{
    char msg[300];
    int fd = open(file_path, O_RDONLY);
    if (fd >= 0)
    {
        char buf[1024];
        for (;;)
        {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0)
                break;
            reserve(E.len + (int)n + 1);
            memcpy(E.text + E.len, buf, (size_t)n);
            E.len += (int)n;
        }
        close(fd);
        snprintf(msg, sizeof(msg), "Opened %s", file_path);
        set_status(msg);
        return;
    }

    if (strcmp(file_path, DEFAULT_PATH) != 0)
        return;

    const char *welcome =
        "Welcome to Notes!\n"
        "\n"
        "This window is a client of StrawX, the StrawOS display server.\n"
        "It draws into memory the server reads directly, and gets\n"
        "keyboard and mouse events over a local socket.\n"
        "\n"
        "Try it out:\n"
        "  - Type anywhere, use the arrow keys, Home/End and Page Up/Down\n"
        "  - Shift+arrows or drag with the mouse to select\n"
        "  - Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V\n"
        "  - Ctrl+S saves to " DEFAULT_PATH "\n";
    insert_text(welcome, (int)strlen(welcome));
    E.cursor = 0;
    E.modified = false;
}

static void save(void)
{
    char msg[300];
    unlink(file_path);
    int fd = open(file_path, O_WRONLY | O_CREAT);
    if (fd < 0)
    {
        snprintf(msg, sizeof(msg), "Could not save %s", file_path);
        set_status(msg);
        return;
    }
    int off = 0;
    while (off < E.len)
    {
        ssize_t n = write(fd, E.text + off, (size_t)(E.len - off));
        if (n <= 0)
            break;
        off += (int)n;
    }
    close(fd);
    E.modified = false;
    snprintf(msg, sizeof(msg), "Saved to %s", file_path);
    set_status(msg);
}

static int char_w(void)  { return sx_char_width(font, 'M'); }
static int line_h(void)  { return sx_font_line_height(font); }

static SxRect text_rect(void)
{
    int w = sx_window_width(E.win), h = sx_window_height(E.win);
    return sx_rect(GUTTER_W, HEADER_H, w - GUTTER_W, h - HEADER_H - STATUS_H);
}

static SxRect save_button_rect(void)
{
    return sx_rect(sx_window_width(E.win) - 84, 7, 74, HEADER_H - 14);
}

static SxRect run_button_rect(void)
{
    SxRect s = save_button_rect();
    return sx_rect(s.x - 8 - 74, s.y, 74, s.h);
}

static void run_script(void)
{
    if (!lua_mode)
        return;
    save();
    const char *argv[] = { "/bin/terminal.elf", "--run", file_path, NULL };
    set_status(spawn("/bin/terminal.elf", argv) ? "Running in a new Terminal" : "Couldn't start the Terminal");
}

static int visible_lines(void)
{
    int n = text_rect().h / line_h();
    return n > 1 ? n : 1;
}

static void ensure_cursor_visible(void)
{
    int line = line_of(E.cursor);
    if (line < E.scroll_line)
        E.scroll_line = line;
    if (line >= E.scroll_line + visible_lines())
        E.scroll_line = line - visible_lines() + 1;

    int x = col_of(E.cursor) * char_w();
    int view_w = text_rect().w - 2 * PAD_X;
    if (x < E.scroll_x)
        E.scroll_x = x - view_w / 3 > 0 ? x - view_w / 3 : 0;
    if (x > E.scroll_x + view_w - char_w())
        E.scroll_x = x - view_w + char_w() * 4;
}

static int pos_from_point(int x, int y)
{
    SxRect tr = text_rect();
    int line = E.scroll_line + (y - tr.y) / line_h();
    if (y < tr.y)
        line = E.scroll_line - 1;
    if (line < 0)
        line = 0;
    if (line >= line_count())
        line = line_count() - 1;
    int col = (x - tr.x - PAD_X + E.scroll_x + char_w() / 2) / char_w();
    if (col < 0)
        col = 0;
    return pos_at(line, col);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(E.win);
    int w = c->width, h = c->height;
    int cw = char_w(), lh = line_h();

    sx_clear(c, SX_THEME_BG);

    sx_fill_rect(c, 0, 0, w, HEADER_H, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, HEADER_H - 1, w, 1, SX_RGBA(0, 0, 0, 80));
    sx_draw_text(c, &sx_font_sans_bold, 14, sx_text_y_centered(&sx_font_sans_bold, sx_rect(0, 0, w, HEADER_H)),
                 file_name, SX_THEME_TEXT);
    if (E.modified)
    {
        int tw = sx_text_width(&sx_font_sans_bold, file_name);
        sx_fill_circle(c, 14 + tw + 10.0f, HEADER_H * 0.5f, 3.5f, SX_THEME_ACCENT);
    }
    if (lua_mode)
        sx_ui_button(c, run_button_rect(), "Run", &sx_font_sans_bold,
                     E.run_down ? SX_BUTTON_PRESSED : (E.run_hover ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL),
                     SX_RGB(0x2C, 0x3E, 0x9E), SX_RGB(255, 255, 255));
    sx_ui_button(c, save_button_rect(), "Save", &sx_font_sans_bold,
                 E.save_down ? SX_BUTTON_PRESSED : (E.save_hover ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL),
                 SX_THEME_ACCENT, SX_THEME_ACCENT_TEXT);

    SxRect tr = text_rect();
    sx_fill_rect(c, 0, tr.y, GUTTER_W, tr.h, SX_THEME_SUNKEN);

    int cur_line = line_of(E.cursor);
    int a = has_selection() ? sel_start() : -1, b = has_selection() ? sel_end() : -1;

    int pos = pos_of_line(E.scroll_line);
    int lines = visible_lines() + 1;

    static uint32_t colors[HL_MAX_LINE];
    HlState hl = { HL_NORMAL, 0 };
    if (lua_mode)
    {
        int p = 0;
        while (p < pos)
        {
            int e = line_end(p);
            hl_line(E.text + p, e - p, &hl, colors);
            p = e + 1;
        }
    }
    for (int i = 0; i < lines && pos <= E.len; i++)
    {
        int line = E.scroll_line + i;
        int y = tr.y + i * lh;
        int end = line_end(pos);

        if (line == cur_line && E.focused)
            sx_fill_rect(c, tr.x, y, tr.w, lh, SX_RGBA(255, 255, 255, 7));

        char num[16];
        snprintf(num, sizeof(num), "%d", line + 1);
        int nw = sx_text_width(font, num);
        sx_draw_text(c, font, GUTTER_W - 12 - nw, y, num,
                     line == cur_line ? SX_THEME_TEXT_DIM : SX_RGB(0x55, 0x5A, 0x66));

        SxRect old = c->clip;
        sx_canvas_set_clip(c, sx_rect_intersect(old, tr));

        int x0 = tr.x + PAD_X - E.scroll_x;

        if (a >= 0 && b > pos && a <= end)
        {
            int s0 = a > pos ? a - pos : 0;
            int s1 = (b < end ? b : end) - pos;
            int extra = b > end ? cw / 2 : 0;
            sx_fill_rect(c, x0 + s0 * cw, y, (s1 - s0) * cw + extra, lh, SX_THEME_ACCENT_A(70));
        }

        if (lua_mode)
        {
            int n = end - pos;
            hl_line(E.text + pos, n, &hl, colors);
            int k = 0;
            while (k < n)
            {
                uint32_t color = k < HL_MAX_LINE ? colors[k] : SX_THEME_TEXT;
                int start = k;
                while (k < n && (k < HL_MAX_LINE ? colors[k] : SX_THEME_TEXT) == color)
                    k++;
                sx_draw_text_n(c, font, x0 + start * cw, y, E.text + pos + start, k - start, color);
            }
        }
        else
        {
            sx_draw_text_n(c, font, x0, y, E.text + pos, end - pos, SX_THEME_TEXT);
        }

        if (line == cur_line && E.focused && E.cursor_on)
            sx_fill_rect(c, x0 + col_of(E.cursor) * cw, y + 2, 2, lh - 4, SX_THEME_ACCENT);

        c->clip = old;
        pos = end + 1;
    }

    int sy = h - STATUS_H;
    sx_fill_rect(c, 0, sy, w, STATUS_H, SX_THEME_SURFACE);
    sx_fill_rect(c, 0, sy, w, 1, SX_RGBA(0, 0, 0, 80));
    char status[96];
    snprintf(status, sizeof(status), "Ln %d, Col %d    |    %d characters    |    %s", cur_line + 1,
             col_of(E.cursor) + 1, E.len, lua_mode ? "Lua" : "Plain text");
    SxRect sr = sx_rect(0, sy, w, STATUS_H);
    sx_draw_text(c, &sx_font_sans_small, 12, sx_text_y_centered(&sx_font_sans_small, sr), status, SX_THEME_TEXT_DIM);

    if (E.status[0] && sx_time_ms() - E.status_ms < 4000)
    {
        int tw = sx_text_width(&sx_font_sans_small, E.status);
        sx_draw_text(c, &sx_font_sans_small, w - 12 - tw, sx_text_y_centered(&sx_font_sans_small, sr),
                     E.status, SX_THEME_ACCENT);
    }

    sx_window_commit(E.win);
}

static void move_to(int pos, bool extend)
{
    if (extend)
    {
        if (E.anchor < 0)
            E.anchor = E.cursor;
    }
    else
    {
        E.anchor = -1;
    }
    if (pos < 0)
        pos = 0;
    if (pos > E.len)
        pos = E.len;
    E.cursor = pos;
}

static void copy_selection(void)
{
    if (!has_selection())
        return;
    int n = sel_end() - sel_start();
    free(E.clipboard);
    E.clipboard = malloc((size_t)n + 1);
    if (!E.clipboard)
        return;
    memcpy(E.clipboard, E.text + sel_start(), (size_t)n);
    E.clip_len = n;
}

static void handle_key(const SxEvent *ev)
{
    bool shift = ev->mods & SX_MOD_SHIFT;
    bool ctrl = ev->mods & SX_MOD_CTRL;
    int line = line_of(E.cursor);
    bool vertical = false;

    if (ctrl)
    {
        switch (ev->key)
        {
            case 's': save(); return;
            case 'r': run_script(); return;
            case 'a': E.anchor = 0; E.cursor = E.len; return;
            case 'c': copy_selection(); return;
            case 'x': copy_selection(); delete_selection(); return;
            case 'v': if (E.clipboard) insert_text(E.clipboard, E.clip_len); return;
            case SX_KEY_HOME: move_to(0, shift); return;
            case SX_KEY_END:  move_to(E.len, shift); return;
            default: break;
        }
    }

    switch (ev->key)
    {
        case SX_KEY_LEFT:
            if (has_selection() && !shift)
                move_to(sel_start(), false);
            else
                move_to(E.cursor - 1, shift);
            break;
        case SX_KEY_RIGHT:
            if (has_selection() && !shift)
                move_to(sel_end(), false);
            else
                move_to(E.cursor + 1, shift);
            break;
        case SX_KEY_UP:
            vertical = true;
            if (line > 0)
                move_to(pos_at(line - 1, E.want_col), shift);
            else
                move_to(0, shift);
            break;
        case SX_KEY_DOWN:
            vertical = true;
            if (line < line_count() - 1)
                move_to(pos_at(line + 1, E.want_col), shift);
            else
                move_to(E.len, shift);
            break;
        case SX_KEY_PAGE_UP:
            vertical = true;
            move_to(pos_at(line - visible_lines() > 0 ? line - visible_lines() : 0, E.want_col), shift);
            break;
        case SX_KEY_PAGE_DOWN:
        {
            vertical = true;
            int target = line + visible_lines();
            if (target >= line_count())
                target = line_count() - 1;
            move_to(pos_at(target, E.want_col), shift);
            break;
        }
        case SX_KEY_HOME:
            move_to(line_start(E.cursor), shift);
            break;
        case SX_KEY_END:
            move_to(line_end(E.cursor), shift);
            break;
        case SX_KEY_BACKSPACE:
            if (has_selection())
                delete_selection();
            else if (E.cursor > 0)
                delete_range(E.cursor - 1, E.cursor);
            break;
        case SX_KEY_DELETE:
            if (has_selection())
                delete_selection();
            else if (E.cursor < E.len)
                delete_range(E.cursor, E.cursor + 1);
            break;
        case SX_KEY_F5:
            run_script();
            return;
        case SX_KEY_TAB:
        {
            int spaces = TAB_WIDTH - col_of(E.cursor) % TAB_WIDTH;
            insert_text("    ", spaces);
            break;
        }
        default:
            if (ev->ch == '\n' || (ev->ch >= 32 && ev->ch < 127))
            {
                char ch = (char)ev->ch;
                insert_text(&ch, 1);
            }
            else
            {
                return;
            }
            break;
    }

    if (!vertical)
        E.want_col = col_of(E.cursor);
}

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("notes: cannot connect to the display server\n");
        return 1;
    }

    E.anchor = -1;
    if (argc > 1 && argv[1][0] == '/')
    {
        strncpy(file_path, argv[1], sizeof(file_path) - 1);
        const char *slash = strrchr(file_path, '/');
        file_name = slash ? slash + 1 : file_path;
    }
    lua_mode = ends_with(file_path, ".lua");

    char title[96];
    snprintf(title, sizeof(title), "%s - Notes", file_name);
    E.win = sx_window_create(title, 660, 460, SX_WINDOW_RESIZABLE);
    if (!E.win)
        return 1;

    load();
    E.cursor_on = true;
    E.blink_ms = sx_time_ms();
    draw();

    bool dirty = false;

    for (;;)
    {
        SxEvent ev;
        int rc = sx_next_event(&ev, 100);
        if (rc < 0)
            break;

        bool redraw = false;
        uint64_t now = sx_time_ms();

        if (rc == 0)
        {
            if (E.focused && now - E.blink_ms >= 530)
            {
                E.cursor_on = !E.cursor_on;
                E.blink_ms = now;
                redraw = true;
            }
            if (E.status[0] && now - E.status_ms >= 4000 && now - E.status_ms < 4200)
                redraw = true;
            if (redraw || dirty)
            {
                draw();
                dirty = false;
            }
            continue;
        }

        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                redraw = true;
                break;

            case SX_EVENT_FOCUS_IN:
            case SX_EVENT_FOCUS_OUT:
                E.focused = ev.type == SX_EVENT_FOCUS_IN;
                redraw = true;
                break;

            case SX_EVENT_KEY_DOWN:
                handle_key(&ev);
                ensure_cursor_visible();
                E.cursor_on = true;
                E.blink_ms = now;
                redraw = true;
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.button != SX_BUTTON_LEFT)
                    break;
                if (sx_rect_contains(save_button_rect(), ev.x, ev.y))
                {
                    E.save_down = true;
                }
                else if (lua_mode && sx_rect_contains(run_button_rect(), ev.x, ev.y))
                {
                    E.run_down = true;
                }
                else if (ev.y >= HEADER_H)
                {
                    move_to(pos_from_point(ev.x, ev.y), (ev.mods & SX_MOD_SHIFT) != 0);
                    E.want_col = col_of(E.cursor);
                    E.selecting = true;
                    E.cursor_on = true;
                    E.blink_ms = now;
                }
                redraw = true;
                break;

            case SX_EVENT_MOUSE_MOVE:
            {
                bool hover = sx_rect_contains(save_button_rect(), ev.x, ev.y);
                bool run_hover = lua_mode && sx_rect_contains(run_button_rect(), ev.x, ev.y);
                if (hover != E.save_hover || run_hover != E.run_hover)
                {
                    E.save_hover = hover;
                    E.run_hover = run_hover;
                    redraw = true;
                }
                if (E.selecting)
                {
                    move_to(pos_from_point(ev.x, ev.y), true);
                    ensure_cursor_visible();
                    redraw = true;
                }
                break;
            }

            case SX_EVENT_MOUSE_UP:
                if (E.save_down && sx_rect_contains(save_button_rect(), ev.x, ev.y))
                    save();
                if (E.run_down && sx_rect_contains(run_button_rect(), ev.x, ev.y))
                    run_script();
                E.save_down = false;
                E.run_down = false;
                E.selecting = false;
                redraw = true;
                break;

            case SX_EVENT_MOUSE_LEAVE:
                if (E.save_hover || E.run_hover)
                {
                    E.save_hover = false;
                    E.run_hover = false;
                    redraw = true;
                }
                break;

            case SX_EVENT_MOUSE_WHEEL:
            {
                E.scroll_line -= ev.wheel * 3;
                int max = line_count() - visible_lines();
                if (E.scroll_line > max)
                    E.scroll_line = max;
                if (E.scroll_line < 0)
                    E.scroll_line = 0;
                redraw = true;
                break;
            }

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
    sx_window_destroy(E.win);
    sx_disconnect();
    return 0;
}
