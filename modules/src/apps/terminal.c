#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <socket.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <strawx.h>
#include <ui.h>

#define SHELL_PATH  "/bin/lsh.elf"
#define MAX_COLS    256
#define SCROLLBACK  2000
#define PAD_X       10
#define PAD_Y       8
#define EDIT_MAX    1024
#define HISTORY_MAX 100

#define COLOR_DEFAULT_FG 16
#define COLOR_DEFAULT_BG 17

#define ATTR_BOLD    0x1
#define ATTR_DIM     0x2
#define ATTR_REVERSE 0x4

typedef struct
{
    char    ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t attr;
}
Cell;

static uint32_t palette[18];

// The 16 ANSI colors come from the current theme, followed by the default foreground and background.
static void palette_load(void)
{
    memcpy(palette, sx_theme->ansi, sizeof(sx_theme->ansi));
    palette[COLOR_DEFAULT_FG] = sx_theme->text;
    palette[COLOR_DEFAULT_BG] = sx_darken(sx_theme->bg, 20);
}

static const SxFont *font = &sx_font_mono;

static struct
{
    SxWindow *win;
    int       sock;
    bool      alive;

    Cell     *lines;
    int64_t   total;
    int64_t   screen_top;
    int64_t   cur_line;
    int       cur_col;
    int       scroll;

    uint8_t   fg, bg, attr;

    int       esc_state;
    char      esc_buf[32];
    int       esc_len;

    char      edit[EDIT_MAX];
    int       edit_len;
    int       edit_pos;

    char     *history[HISTORY_MAX];
    int       history_len;
    int       history_pos;
    char      draft[EDIT_MAX];

    bool      cursor_on;
    uint64_t  blink_ms;
    bool      focused;
}
T;

static int char_w(void) { return sx_char_width(font, 'M'); }
static int line_h(void) { return sx_font_line_height(font) - 2; }

static int cols(void)
{
    int c = (sx_window_width(T.win) - 2 * PAD_X) / char_w();
    return c < 10 ? 10 : (c > MAX_COLS ? MAX_COLS : c);
}

static int rows(void)
{
    int r = (sx_window_height(T.win) - 2 * PAD_Y) / line_h();
    return r < 2 ? 2 : r;
}

static Cell *line_at(int64_t n)
{
    return T.lines + (size_t)(n % SCROLLBACK) * MAX_COLS;
}

static void clear_line(int64_t n)
{
    Cell *l = line_at(n);
    for (int i = 0; i < MAX_COLS; i++)
        l[i] = (Cell){ ' ', COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0 };
}

static void ensure_line(int64_t n)
{
    while (T.total <= n)
    {
        clear_line(T.total);
        T.total++;
    }
    if (T.screen_top < T.total - SCROLLBACK)
        T.screen_top = T.total - SCROLLBACK;
}

static void new_line(void)
{
    T.cur_line++;
    T.cur_col = 0;
    ensure_line(T.cur_line);
}

static void put_char(char c)
{
    if (T.cur_col >= cols())
        new_line();
    Cell *cell = &line_at(T.cur_line)[T.cur_col++];
    cell->ch = c;
    cell->fg = T.fg;
    cell->bg = T.bg;
    cell->attr = T.attr;
}

static void clear_screen(void)
{
    T.screen_top = T.total;
    T.cur_line = T.total;
    T.cur_col = 0;
    ensure_line(T.cur_line);
}

static void apply_sgr(void)
{
    int params[16], n = 0, v = -1;
    for (int i = 0; i <= T.esc_len; i++)
    {
        char c = i < T.esc_len ? T.esc_buf[i] : ';';
        if (c >= '0' && c <= '9')
        {
            v = (v < 0 ? 0 : v) * 10 + (c - '0');
        }
        else if (c == ';' && n < 16)
        {
            params[n++] = v < 0 ? 0 : v;
            v = -1;
        }
    }

    for (int i = 0; i < n; i++)
    {
        int p = params[i];
        if (p == 0)
        {
            T.fg = COLOR_DEFAULT_FG;
            T.bg = COLOR_DEFAULT_BG;
            T.attr = 0;
        }
        else if (p == 1) T.attr |= ATTR_BOLD;
        else if (p == 2) T.attr |= ATTR_DIM;
        else if (p == 7) T.attr |= ATTR_REVERSE;
        else if (p == 22) T.attr &= (uint8_t)~(ATTR_BOLD | ATTR_DIM);
        else if (p == 27) T.attr &= (uint8_t)~ATTR_REVERSE;
        else if (p >= 30 && p <= 37) T.fg = (uint8_t)(p - 30);
        else if (p == 39) T.fg = COLOR_DEFAULT_FG;
        else if (p >= 40 && p <= 47) T.bg = (uint8_t)(p - 40);
        else if (p == 49) T.bg = COLOR_DEFAULT_BG;
        else if (p >= 90 && p <= 97) T.fg = (uint8_t)(p - 90 + 8);
        else if (p >= 100 && p <= 107) T.bg = (uint8_t)(p - 100 + 8);
    }
}

static int esc_param(int fallback)
{
    int v = 0;
    bool any = false;
    for (int i = 0; i < T.esc_len && T.esc_buf[i] >= '0' && T.esc_buf[i] <= '9'; i++)
    {
        v = v * 10 + (T.esc_buf[i] - '0');
        any = true;
    }
    return any ? v : fallback;
}

static void handle_csi(char final)
{
    switch (final)
    {
        case 'm':
            apply_sgr();
            break;
        case 'J':
            if (esc_param(0) >= 2)
                clear_screen();
            break;
        case 'H':
            T.cur_line = T.screen_top;
            T.cur_col = 0;
            break;
        case 'K':
        {
            Cell *l = line_at(T.cur_line);
            for (int i = T.cur_col; i < MAX_COLS; i++)
                l[i] = (Cell){ ' ', COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0 };
            break;
        }
        case 'A':
            T.cur_line -= esc_param(1);
            if (T.cur_line < T.screen_top)
                T.cur_line = T.screen_top;
            break;
        case 'B':
            T.cur_line += esc_param(1);
            ensure_line(T.cur_line);
            break;
        case 'C':
            T.cur_col += esc_param(1);
            if (T.cur_col >= cols())
                T.cur_col = cols() - 1;
            break;
        case 'D':
            T.cur_col -= esc_param(1);
            if (T.cur_col < 0)
                T.cur_col = 0;
            break;
        case 'G':
            T.cur_col = esc_param(1) - 1;
            if (T.cur_col < 0)
                T.cur_col = 0;
            break;
        default:
            break;
    }
}

static void feed(const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = data[i];

        if (T.esc_state == 1)
        {
            if (c == '[')
            {
                T.esc_state = 2;
                T.esc_len = 0;
            }
            else
            {
                T.esc_state = 0;
            }
            continue;
        }
        if (T.esc_state == 2)
        {
            if ((c >= '0' && c <= '9') || c == ';' || c == '?')
            {
                if (T.esc_len < (int)sizeof(T.esc_buf))
                    T.esc_buf[T.esc_len++] = c;
            }
            else
            {
                handle_csi(c);
                T.esc_state = 0;
            }
            continue;
        }

        switch (c)
        {
            case 27:   T.esc_state = 1; break;
            case '\n': new_line(); break;
            case '\r': T.cur_col = 0; break;
            case '\b': if (T.cur_col > 0) T.cur_col--; break;
            case '\t':
                do
                    put_char(' ');
                while (T.cur_col % 8 && T.cur_col < cols());
                break;
            case '\a': break;
            default:
                if ((unsigned char)c >= 32)
                    put_char(c);
                break;
        }
    }
    T.scroll = 0;
}

static void feed_str(const char *s)
{
    feed(s, strlen(s));
}

static void send_bytes(const char *data, size_t len)
{
    if (!T.alive)
        return;
    size_t done = 0;
    while (done < len)
    {
        ssize_t n = write(T.sock, data + done, len - done);
        if (n == -EAGAIN)
        {
            wait(1);
            continue;
        }
        if (n <= 0)
            return;
        done += (size_t)n;
    }
}

static void history_add(const char *line)
{
    if (!line[0])
        return;
    if (T.history_len > 0 && strcmp(T.history[T.history_len - 1], line) == 0)
        return;
    if (T.history_len == HISTORY_MAX)
    {
        free(T.history[0]);
        memmove(T.history, T.history + 1, sizeof(char *) * (HISTORY_MAX - 1));
        T.history_len--;
    }
    T.history[T.history_len++] = strdup(line);
}

static void edit_set(const char *s)
{
    strncpy(T.edit, s, EDIT_MAX - 1);
    T.edit[EDIT_MAX - 1] = '\0';
    T.edit_len = (int)strlen(T.edit);
    T.edit_pos = T.edit_len;
}

static void submit_line(void)
{
    T.edit[T.edit_len] = '\0';
    feed(T.edit, (size_t)T.edit_len);
    feed_str("\n");
    send_bytes(T.edit, (size_t)T.edit_len);
    send_bytes("\n", 1);
    history_add(T.edit);
    T.history_pos = T.history_len;
    T.edit_len = T.edit_pos = 0;
}

static void handle_key(const SxEvent *ev)
{
    bool ctrl = ev->mods & SX_MOD_CTRL;
    bool shift = ev->mods & SX_MOD_SHIFT;

    if (ctrl && ev->key == 'c')
    {
        feed_str("^C\n");
        T.edit_len = T.edit_pos = 0;
        send_bytes("\x03", 1);
        return;
    }
    if (ctrl && ev->key == 'l')
    {
        Cell saved[MAX_COLS];
        memcpy(saved, line_at(T.cur_line), sizeof(saved));
        int col = T.cur_col;
        clear_screen();
        memcpy(line_at(T.cur_line), saved, sizeof(saved));
        T.cur_col = col;
        return;
    }
    if (ctrl && ev->key == 'd' && T.edit_len == 0)
    {
        edit_set("exit");
        submit_line();
        return;
    }
    if (ctrl && ev->key == 'a') { T.edit_pos = 0; return; }
    if (ctrl && ev->key == 'e') { T.edit_pos = T.edit_len; return; }
    if (ctrl && ev->key == 'u')
    {
        memmove(T.edit, T.edit + T.edit_pos, (size_t)(T.edit_len - T.edit_pos));
        T.edit_len -= T.edit_pos;
        T.edit_pos = 0;
        return;
    }

    switch (ev->key)
    {
        case SX_KEY_ENTER:
            submit_line();
            T.scroll = 0;
            return;
        case SX_KEY_BACKSPACE:
            if (T.edit_pos > 0)
            {
                memmove(T.edit + T.edit_pos - 1, T.edit + T.edit_pos, (size_t)(T.edit_len - T.edit_pos));
                T.edit_pos--;
                T.edit_len--;
            }
            return;
        case SX_KEY_DELETE:
            if (T.edit_pos < T.edit_len)
            {
                memmove(T.edit + T.edit_pos, T.edit + T.edit_pos + 1, (size_t)(T.edit_len - T.edit_pos - 1));
                T.edit_len--;
            }
            return;
        case SX_KEY_LEFT:  if (T.edit_pos > 0) T.edit_pos--; return;
        case SX_KEY_RIGHT: if (T.edit_pos < T.edit_len) T.edit_pos++; return;
        case SX_KEY_HOME:  T.edit_pos = 0; return;
        case SX_KEY_END:   T.edit_pos = T.edit_len; return;
        case SX_KEY_UP:
            if (T.history_pos > 0)
            {
                if (T.history_pos == T.history_len)
                {
                    T.edit[T.edit_len] = '\0';
                    strcpy(T.draft, T.edit);
                }
                edit_set(T.history[--T.history_pos]);
            }
            return;
        case SX_KEY_DOWN:
            if (T.history_pos < T.history_len)
            {
                T.history_pos++;
                edit_set(T.history_pos == T.history_len ? T.draft : T.history[T.history_pos]);
            }
            return;
        case SX_KEY_PAGE_UP:
            T.scroll += shift ? 1 : rows() / 2;
            return;
        case SX_KEY_PAGE_DOWN:
            T.scroll -= shift ? 1 : rows() / 2;
            if (T.scroll < 0)
                T.scroll = 0;
            return;
        default:
            break;
    }

    int ch = ev->key == SX_KEY_TAB ? ' ' : ev->ch;
    int count = ev->key == SX_KEY_TAB ? 4 : 1;
    if (ch >= 32 && ch < 127 && !ctrl)
    {
        for (int k = 0; k < count && T.edit_len < EDIT_MAX - 1; k++)
        {
            memmove(T.edit + T.edit_pos + 1, T.edit + T.edit_pos, (size_t)(T.edit_len - T.edit_pos));
            T.edit[T.edit_pos++] = (char)ch;
            T.edit_len++;
        }
        T.scroll = 0;
    }
}

static void draw_cell_run(SxCanvas *c, int x, int y, const char *text, int len, uint32_t color)
{
    sx_draw_text_n(c, font, x, y + (line_h() - sx_font_line_height(font)) / 2, text, len, color);
}

static void cell_colors(const Cell *cell, uint32_t *fg, uint32_t *bg)
{
    uint8_t f = cell->fg, b = cell->bg;
    if ((cell->attr & ATTR_BOLD) && f < 8)
        f = (uint8_t)(f + 8);
    *fg = palette[f];
    *bg = palette[b];
    if (cell->attr & ATTR_DIM)
        *fg = sx_mix(*fg, palette[COLOR_DEFAULT_BG], 110);
    if (cell->attr & ATTR_REVERSE)
    {
        uint32_t t = *fg;
        *fg = *bg;
        *bg = t;
    }
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(T.win);
    sx_clear(c, palette[COLOR_DEFAULT_BG]);

    int cw = char_w(), lh = line_h();
    int ncols = cols(), nrows = rows();

    int edit_lines = (T.cur_col + T.edit_len) / ncols + 1;
    int64_t content_end = T.cur_line + edit_lines - 1;
    if (content_end < T.total - 1)
        content_end = T.total - 1;

    int64_t oldest = T.total > SCROLLBACK ? T.total - SCROLLBACK : 0;
    int64_t max_scroll = content_end - nrows + 1 - oldest;
    if (max_scroll < 0)
        max_scroll = 0;
    if (T.scroll > max_scroll)
        T.scroll = (int)max_scroll;

    int64_t first = content_end - nrows + 1 - T.scroll;
    if (first < T.screen_top && T.scroll == 0)
        first = T.screen_top;
    if (first < oldest)
        first = oldest;

    char run[MAX_COLS];
    for (int r = 0; r < nrows; r++)
    {
        int64_t ln = first + r;
        if (ln >= T.total)
            break;
        Cell *line = line_at(ln);
        int y = PAD_Y + r * lh;

        for (int col = 0; col < ncols; col++)
        {
            uint32_t fg, bg;
            cell_colors(&line[col], &fg, &bg);
            if (bg != palette[COLOR_DEFAULT_BG])
                sx_fill_rect(c, PAD_X + col * cw, y, cw, lh, bg);
        }

        int col = 0;
        while (col < ncols)
        {
            uint32_t fg, bg;
            cell_colors(&line[col], &fg, &bg);
            int start = col, n = 0;
            while (col < ncols)
            {
                uint32_t f2, b2;
                cell_colors(&line[col], &f2, &b2);
                if (f2 != fg)
                    break;
                run[n++] = line[col].ch;
                col++;
            }
            while (n > 0 && run[n - 1] == ' ')
                n--;
            if (n > 0)
                draw_cell_run(c, PAD_X + start * cw, y, run, n, fg);
        }
    }

    int cursor_row = -1, cursor_col = 0;
    for (int i = 0; i <= T.edit_len; i++)
    {
        int64_t pos = T.cur_col + i;
        int64_t ln = T.cur_line + pos / ncols;
        int col = (int)(pos % ncols);
        int r = (int)(ln - first);
        if (r < 0 || r >= nrows)
            continue;
        if (i < T.edit_len)
        {
            char ch = T.edit[i];
            sx_fill_rect(c, PAD_X + col * cw, PAD_Y + r * lh, cw, lh, palette[COLOR_DEFAULT_BG]);
            draw_cell_run(c, PAD_X + col * cw, PAD_Y + r * lh, &ch, 1, palette[COLOR_DEFAULT_FG]);
        }
        if (i == T.edit_pos)
        {
            cursor_row = r;
            cursor_col = col;
        }
    }

    if (cursor_row >= 0 && T.alive)
    {
        int x = PAD_X + cursor_col * cw, y = PAD_Y + cursor_row * lh;
        if (!T.focused)
            sx_draw_rect(c, x, y + 1, cw, lh - 2, SX_THEME_ACCENT);
        else if (T.cursor_on)
            sx_fill_rect(c, x, y + 1, 2, lh - 2, SX_THEME_ACCENT);
    }

    if (T.scroll > 0)
    {
        char msg[48];
        snprintf(msg, sizeof(msg), " scrolled up %d lines ", T.scroll);
        int tw = sx_text_width(&sx_font_sans_small, msg);
        SxRect r = sx_rect(c->width - tw - 18, 8, tw + 10, 22);
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 6, SX_THEME_ACCENT_A(200));
        sx_draw_text_centered(c, &sx_font_sans_small, r, msg, SX_THEME_ACCENT_TEXT);
    }

    sx_window_commit(T.win);
}

static const char *run_script;

static bool start_shell(void)
{
    char name[48];
    snprintf(name, sizeof(name), "term.%llu.%llu", (unsigned long long)uptime_ms(),
             (unsigned long long)(uintptr_t)&T);

    int listener = socket(SOCK_NONBLOCK);
    if (listener < 0 || bind(listener, name) != 0 || listen(listener) != 0)
        return false;

    char columns[16];
    snprintf(columns, sizeof(columns), "%d", cols());
    const char *argv[] = { SHELL_PATH, "--terminal", name, columns, run_script ? "--run" : NULL, run_script, NULL };
    if (!spawn(SHELL_PATH, argv))
    {
        close(listener);
        return false;
    }

    uint64_t start = uptime_ms();
    while (uptime_ms() - start < 5000)
    {
        struct pollfd pfd = { listener, POLLIN, 0 };
        poll(&pfd, 1, 100);
        int fd = accept(listener);
        if (fd >= 0)
        {
            close(listener);
            T.sock = fd;
            T.alive = true;
            return true;
        }
    }
    close(listener);
    return false;
}

int main(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (strcmp(argv[i], "--run") == 0)
            run_script = argv[i + 1];
        else if (strcmp(argv[i], "--cwd") == 0)
            chdir(argv[i + 1]);
    }

    if (sx_connect() != 0)
    {
        printf("terminal: cannot connect to the display server\n");
        return 1;
    }

    palette_load();

    T.lines = malloc(sizeof(Cell) * MAX_COLS * SCROLLBACK);
    if (!T.lines)
        return 1;

    char title[96] = "Terminal";
    if (run_script)
    {
        const char *slash = strrchr(run_script, '/');
        snprintf(title, sizeof(title), "%s - Terminal", slash ? slash + 1 : run_script);
    }
    T.win = sx_window_create(title, 820, 500, SX_WINDOW_RESIZABLE);
    if (!T.win)
        return 1;

    T.fg = COLOR_DEFAULT_FG;
    T.bg = COLOR_DEFAULT_BG;
    T.sock = -1;
    ensure_line(0);

    if (!start_shell())
        feed_str("\033[31mCould not start the shell (" SHELL_PATH ").\033[0m\n");

    T.cursor_on = true;
    T.blink_ms = sx_time_ms();
    draw();

    char buf[4096];
    for (;;)
    {
        bool dirty = false;

        struct pollfd pfds[2] = { { sx_fd(), POLLIN, 0 }, { T.alive ? T.sock : -1, POLLIN, 0 } };
        uint64_t now = sx_time_ms();
        int timeout = sx_pending() ? 0 : (int)(530 - (now - T.blink_ms > 530 ? 530 : now - T.blink_ms));
        poll(pfds, 2, timeout);

        if (T.alive && (pfds[1].revents & (POLLIN | POLLHUP)))
        {
            for (int k = 0; k < 16; k++)
            {
                ssize_t n = read(T.sock, buf, sizeof(buf));
                if (n == -EAGAIN)
                    break;
                if (n <= 0)
                {
                    T.alive = false;
                    goto out;
                }
                feed(buf, (size_t)n);
                dirty = true;
            }
        }

        SxEvent ev;
        int rc;
        while ((rc = sx_next_event(&ev, 0)) > 0)
        {
            switch (ev.type)
            {
                case SX_EVENT_CLOSE:
                case SX_EVENT_DISCONNECTED:
                    goto out;
                case SX_EVENT_RESIZE:
                    palette_load();
                    dirty = true;
                    break;
                case SX_EVENT_FOCUS_IN:
                case SX_EVENT_FOCUS_OUT:
                    T.focused = ev.type == SX_EVENT_FOCUS_IN;
                    dirty = true;
                    break;
                case SX_EVENT_KEY_DOWN:
                    handle_key(&ev);
                    T.cursor_on = true;
                    T.blink_ms = sx_time_ms();
                    dirty = true;
                    break;
                case SX_EVENT_MOUSE_WHEEL:
                    T.scroll += ev.wheel * 3;
                    if (T.scroll < 0)
                        T.scroll = 0;
                    dirty = true;
                    break;
                default:
                    break;
            }
        }
        if (rc < 0)
            goto out;

        now = sx_time_ms();
        if (now - T.blink_ms >= 530)
        {
            T.cursor_on = !T.cursor_on;
            T.blink_ms = now;
            dirty = true;
        }

        if (dirty)
            draw();
    }

out:
    if (T.sock >= 0)
        close(T.sock);
    sx_window_destroy(T.win);
    sx_disconnect();
    return 0;
}
