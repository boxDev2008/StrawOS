#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <strawx.h>
#include <ui.h>

#define HISTORY   120
#define SAMPLE_MS 500
#define MAX_PROCS 1024

#define TAB_BAR_H 52
#define ROW_H     30
#define HEADER_H  32
#define FOOTER_H  52

typedef enum
{
    TAB_PERFORMANCE,
    TAB_PROCESSES,
    TAB_COUNT
}
Tab;

typedef enum
{
    COL_NAME,
    COL_PID,
    COL_STATUS,
    COL_CPU,
    COL_MEM,
    COL_COUNT
}
Column;

typedef enum
{
    HIT_NONE,
    HIT_TAB,
    HIT_HEADER,
    HIT_ROW,
    HIT_KILL,
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
    struct taskinfo info;
    char            label[TASKINFO_NAME_MAX];
    float           cpu;
}
Proc;

typedef struct
{
    float history[HISTORY];
    int   count;
}
History;

static struct
{
    SxWindow *win;
    History   mem_hist;
    History   cpu_hist;
    History   core_hist[CPUINFO_MAX_CPUS];
    struct meminfo mem;
    struct cpuinfo cpu;
    bool      have_cpu;
    uint64_t  cpu_delta;

    Tab       tab;
    Hit       hover;
    Proc      procs[MAX_PROCS];
    int       proc_count;
    uint64_t  selected;
    Column    sort_col;
    bool      sort_desc;
    int       scroll;
    char      status[96];
    uint64_t  status_until;
}
M;

static const char *tab_names[TAB_COUNT] = { "Performance", "Processes" };
static const char *col_names[COL_COUNT] = { "Name", "PID", "Status", "CPU", "Memory" };
static const int   col_widths[COL_COUNT] = { 0, 52, 76, 60, 86 };

static void history_push(History *h, float v)
{
    if (h->count < HISTORY)
    {
        h->history[h->count++] = v;
    }
    else
    {
        memmove(h->history, h->history + 1, sizeof(float) * (HISTORY - 1));
        h->history[HISTORY - 1] = v;
    }
}

static void sample(void)
{
    meminfo(&M.mem);
    float used = M.mem.total_pages
        ? (float)(M.mem.total_pages - M.mem.free_pages) / (float)M.mem.total_pages
        : 0.0f;
    history_push(&M.mem_hist, used);

    struct cpuinfo prev = M.cpu;
    if (cpuinfo(&M.cpu) == 0)
    {
        if (!M.have_cpu)
            prev = (struct cpuinfo){ 0 };
        uint64_t total = M.cpu.total_ticks - prev.total_ticks;
        uint64_t busy = M.cpu.busy_ticks - prev.busy_ticks;
        M.cpu_delta = total;
        history_push(&M.cpu_hist, total ? (float)busy / (float)total : 0.0f);

        for (uint64_t i = 0; i < M.cpu.cpus; i++)
        {
            uint64_t ct = M.cpu.cpu_total_ticks[i] - prev.cpu_total_ticks[i];
            uint64_t cb = M.cpu.cpu_busy_ticks[i] - prev.cpu_busy_ticks[i];
            history_push(&M.core_hist[i], ct ? (float)cb / (float)ct : 0.0f);
        }
        M.have_cpu = true;
    }
}

static void proc_label(const struct taskinfo *t, char *out)
{
    const char *slash = strrchr(t->name, '/');
    strcpy(out, slash && slash[1] ? slash + 1 : t->name);
    size_t len = strlen(out);
    if (len > 4 && strcmp(out + len - 4, ".elf") == 0)
        out[len - 4] = '\0';
}

static int proc_compare(const void *a, const void *b)
{
    const Proc *pa = a, *pb = b;
    int r = 0;
    switch (M.sort_col)
    {
        case COL_PID:
            r = pa->info.pid < pb->info.pid ? -1 : pa->info.pid > pb->info.pid;
            break;
        case COL_STATUS:
            r = (int)pa->info.state - (int)pb->info.state;
            break;
        case COL_CPU:
            r = pa->cpu < pb->cpu ? -1 : pa->cpu > pb->cpu;
            break;
        case COL_MEM:
            r = pa->info.mem_kb < pb->info.mem_kb ? -1 : pa->info.mem_kb > pb->info.mem_kb;
            break;
        default:
            r = strcmp(pa->label, pb->label);
            break;
    }
    if (r == 0)
        r = pa->info.pid < pb->info.pid ? -1 : pa->info.pid > pb->info.pid;
    return M.sort_desc ? -r : r;
}

static void sort_procs(void)
{
    qsort(M.procs, M.proc_count, sizeof(Proc), proc_compare);
}

static void sample_procs(void)
{
    static struct taskinfo list[MAX_PROCS];
    static uint64_t prev_pid[MAX_PROCS], prev_ticks[MAX_PROCS];
    int prev_count = M.proc_count;
    for (int i = 0; i < prev_count; i++)
    {
        prev_pid[i] = M.procs[i].info.pid;
        prev_ticks[i] = M.procs[i].info.cpu_ticks;
    }

    int n = tasklist(list, MAX_PROCS);
    if (n < 0)
        n = 0;

    for (int i = 0; i < n; i++)
    {
        M.procs[i].info = list[i];
        proc_label(&list[i], M.procs[i].label);
        M.procs[i].cpu = 0.0f;
        for (int k = 0; k < prev_count; k++)
        {
            if (prev_pid[k] != list[i].pid)
                continue;
            if (M.cpu_delta && list[i].cpu_ticks >= prev_ticks[k])
                M.procs[i].cpu = (float)(list[i].cpu_ticks - prev_ticks[k]) / (float)M.cpu_delta;
            break;
        }
    }
    M.proc_count = n;
    sort_procs();
}

static int find_selected(void)
{
    for (int i = 0; i < M.proc_count; i++)
        if (M.procs[i].info.pid == M.selected)
            return i;
    return -1;
}

static bool can_kill(int index)
{
    return index >= 0 && !M.procs[index].info.kernel;
}

static void set_status(const char *msg)
{
    snprintf(M.status, sizeof(M.status), "%s", msg);
    M.status_until = sx_time_ms() + 4000;
}

static void kill_selected(void)
{
    int i = find_selected();
    if (!can_kill(i))
        return;

    char msg[96];
    const char *name = M.procs[i].label;
    if (kill((pid_t)M.selected) == 0)
    {
        snprintf(msg, sizeof(msg), "Ended %s (PID %llu)", name, (unsigned long long)M.selected);
        memmove(&M.procs[i], &M.procs[i + 1], sizeof(Proc) * (M.proc_count - i - 1));
        M.proc_count--;
        M.selected = 0;
    }
    else
    {
        snprintf(msg, sizeof(msg), "Could not end %s", name);
    }
    set_status(msg);
}

static void format_mib(uint64_t pages, char *out, size_t size)
{
    uint64_t kib = pages * M.mem.page_size / 1024;
    uint64_t tenths = kib * 10 / 1024;
    snprintf(out, size, "%llu.%llu MiB", (unsigned long long)(tenths / 10), (unsigned long long)(tenths % 10));
}

static void info_row(SxCanvas *c, int x, int y, int w, const char *label, const char *value)
{
    sx_draw_text(c, &sx_font_sans, x, y, label, SX_THEME_TEXT_DIM);
    int vw = sx_text_width(&sx_font_sans, value);
    sx_draw_text(c, &sx_font_sans, x + w - vw, y, value, SX_THEME_TEXT);
}

static float history_last(const History *h)
{
    return h->count ? h->history[h->count - 1] : 0.0f;
}

static void plot(SxCanvas *c, SxRect g, const History *h, uint32_t color, uint32_t fill, bool large)
{
    sx_fill_rounded_rect(c, g.x, g.y, g.w, g.h, large ? 8 : 5, SX_THEME_BG);
    if (large)
        for (int i = 1; i < 4; i++)
            sx_fill_rect(c, g.x + 1, g.y + g.h * i / 4, g.w - 2, 1, SX_RGBA(255, 255, 255, 12));

    if (h->count < 2)
        return;

    float step = (float)(g.w - 2) / (HISTORY - 1);
    float x0 = g.x + 1 + (HISTORY - h->count) * step;
    float prev_x = 0, prev_y = 0;
    for (int i = 0; i < h->count; i++)
    {
        float x = x0 + i * step;
        float y = g.y + g.h - 2 - h->history[i] * (g.h - 4);
        int ix = (int)x;
        sx_fill_rect(c, ix, (int)y, (int)step + 1, g.y + g.h - 1 - (int)y, fill);
        if (i > 0)
            sx_draw_line(c, prev_x, prev_y, x, y, large ? 2.0f : 1.5f, color);
        prev_x = x;
        prev_y = y;
    }
    if (large)
        sx_fill_circle(c, prev_x, prev_y, 3.5f, color);
}

static void graph_card(SxCanvas *c, SxRect card, const char *title, const char *detail,
                       const History *h, uint32_t color, uint32_t fill)
{
    char buf[16];
    sx_ui_panel(c, card, 12, SX_THEME_SURFACE);

    sx_draw_text(c, &sx_font_sans_bold, card.x + 16, card.y + 12, title, SX_THEME_TEXT);
    int lw = sx_text_width(&sx_font_sans, detail);
    sx_draw_text(c, &sx_font_sans, card.x + card.w - 16 - lw, card.y + 12, detail, SX_THEME_TEXT_DIM);

    snprintf(buf, sizeof(buf), "%d%%", (int)(history_last(h) * 100.0f + 0.5f));
    sx_draw_text(c, &sx_font_sans_huge, card.x + 14, card.y + 30, buf, color);

    SxRect g = sx_rect(card.x + 16, card.y + 96, card.w - 32, card.h - 112);
    if (g.h > 20)
        plot(c, g, h, color, fill, true);
}

#define CORE_CELL_H 78
#define CORE_GAP    8

static int core_columns(int cores)
{
    return cores <= 4 ? cores : cores <= 16 ? 4 : 8;
}

static int core_grid_height(void)
{
    int cores = M.have_cpu ? (int)M.cpu.cpus : 0;
    if (cores < 2)
        return 0;
    int cols = core_columns(cores);
    int rows = (cores + cols - 1) / cols;
    return rows * CORE_CELL_H + (rows - 1) * CORE_GAP;
}

static void core_grid(SxCanvas *c, int x, int y, int w)
{
    int cores = (int)M.cpu.cpus;
    int cols = core_columns(cores);
    int cell_w = (w - (cols - 1) * CORE_GAP) / cols;
    char label[16], pct[16];

    for (int i = 0; i < cores; i++)
    {
        SxRect cell = sx_rect(x + (i % cols) * (cell_w + CORE_GAP), y + (i / cols) * (CORE_CELL_H + CORE_GAP),
                              cell_w, CORE_CELL_H);
        sx_ui_panel(c, cell, 10, SX_THEME_SURFACE);

        const History *h = &M.core_hist[i];
        snprintf(label, sizeof(label), "Core %d", i);
        snprintf(pct, sizeof(pct), "%d%%", (int)(history_last(h) * 100.0f + 0.5f));
        sx_draw_text(c, &sx_font_sans_small, cell.x + 10, cell.y + 8, label, SX_THEME_TEXT_DIM);
        int pw = sx_text_width(&sx_font_sans_small, pct);
        sx_draw_text(c, &sx_font_sans_small, cell.x + cell.w - 10 - pw, cell.y + 8, pct, SX_THEME_TEXT);

        plot(c, sx_rect(cell.x + 8, cell.y + 28, cell.w - 16, cell.h - 36), h,
             SX_THEME_BLUE, SX_RGBA(0x5B, 0x8C, 0xFF, 40), false);
    }
}

static void draw_performance(SxCanvas *c, int top)
{
    int w = c->width, h = c->height;
    int pad = 14;
    char buf[64], buf2[64];
    char line[96];

    int grid_h = core_grid_height();
    int cards_h = h - 150 - top - (grid_h ? grid_h + pad : 0);
    int card_h = cards_h / 2 > 150 ? cards_h / 2 : 150;

    if (M.have_cpu)
        snprintf(line, sizeof(line), "%llu core%s", (unsigned long long)M.cpu.cpus, M.cpu.cpus == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "unavailable");
    SxRect cpu_card = sx_rect(pad, top, w - 2 * pad, card_h);
    graph_card(c, cpu_card, "CPU", line, &M.cpu_hist, SX_THEME_BLUE, SX_RGBA(0x5B, 0x8C, 0xFF, 40));

    int below_cpu = cpu_card.y + cpu_card.h + pad;
    if (grid_h)
    {
        core_grid(c, pad, below_cpu, w - 2 * pad);
        below_cpu += grid_h + pad;
    }

    uint64_t used = M.mem.total_pages - M.mem.free_pages;
    format_mib(used, buf, sizeof(buf));
    format_mib(M.mem.total_pages, buf2, sizeof(buf2));
    snprintf(line, sizeof(line), "%s of %s", buf, buf2);
    SxRect card = sx_rect(pad, below_cpu, w - 2 * pad, card_h);
    graph_card(c, card, "Memory", line, &M.mem_hist, SX_THEME_GREEN, SX_RGBA(0x46, 0xC2, 0x7A, 40));

    int y = card.y + card.h + 16;
    int rw = w - 2 * pad - 8;
    uint64_t up = uptime_ms() / 1000;
    snprintf(buf, sizeof(buf), "%llu:%02llu:%02llu", (unsigned long long)(up / 3600),
             (unsigned long long)(up / 60 % 60), (unsigned long long)(up % 60));
    info_row(c, pad + 4, y, rw, "Uptime", buf);
    y += 26;

    snprintf(buf, sizeof(buf), "%d x %d", sx_screen_width(), sx_screen_height());
    info_row(c, pad + 4, y, rw, "Display", buf);
    y += 26;

    snprintf(buf, sizeof(buf), "StrawX, protocol v%d", SX_PROTOCOL_VERSION);
    info_row(c, pad + 4, y, rw, "Display server", buf);
    y += 26;

    snprintf(buf, sizeof(buf), "%llu free pages of %llu KiB", (unsigned long long)M.mem.free_pages,
             (unsigned long long)(M.mem.page_size / 1024));
    info_row(c, pad + 4, y, rw, "Allocator", buf);
}

#define PAD 14

static SxRect tab_rect(int i)
{
    int w = sx_window_width(M.win);
    int seg_w = 130;
    int x = (w - seg_w * TAB_COUNT) / 2;
    return sx_rect(x + i * seg_w, PAD, seg_w, TAB_BAR_H - PAD - 4);
}

static SxRect list_card(void)
{
    int w = sx_window_width(M.win), h = sx_window_height(M.win);
    return sx_rect(PAD, TAB_BAR_H, w - 2 * PAD, h - TAB_BAR_H - FOOTER_H);
}

static SxRect list_body(void)
{
    SxRect card = list_card();
    return sx_rect(card.x + 1, card.y + HEADER_H, card.w - 2, card.h - HEADER_H - 6);
}

static SxRect kill_rect(void)
{
    int w = sx_window_width(M.win), h = sx_window_height(M.win);
    return sx_rect(w - PAD - 110, h - FOOTER_H + 10, 110, FOOTER_H - 20);
}

static SxRect col_rect(SxRect row, Column col)
{
    int right = row.x + row.w - 12;
    for (int i = COL_COUNT - 1; i > col; i--)
        right -= col_widths[i];
    if (col == COL_NAME)
        return sx_rect(row.x + 14, row.y, right - (row.x + 14), row.h);
    return sx_rect(right - col_widths[col], row.y, col_widths[col], row.h);
}

static SxRect row_rect(int index)
{
    SxRect body = list_body();
    return sx_rect(body.x, body.y + index * ROW_H - M.scroll, body.w, ROW_H);
}

static int max_scroll(void)
{
    int max = M.proc_count * ROW_H - list_body().h;
    return max > 0 ? max : 0;
}

static void clamp_scroll(void)
{
    int max = max_scroll();
    if (M.scroll > max)
        M.scroll = max;
    if (M.scroll < 0)
        M.scroll = 0;
}

static void scroll_to_selected(void)
{
    int i = find_selected();
    if (i < 0)
        return;
    int body_h = list_body().h;
    if (i * ROW_H < M.scroll)
        M.scroll = i * ROW_H;
    else if ((i + 1) * ROW_H > M.scroll + body_h)
        M.scroll = (i + 1) * ROW_H - body_h;
    clamp_scroll();
}

static bool hovering(HitKind kind, int index)
{
    return M.hover.kind == kind && M.hover.index == index;
}

static void draw_text_fit(SxCanvas *c, const SxFont *font, int x, int y, int max_w, const char *text, uint32_t color)
{
    int len = (int)strlen(text);
    if (sx_text_width(font, text) <= max_w)
    {
        sx_draw_text(c, font, x, y, text, color);
        return;
    }
    int dots = sx_text_width(font, "...");
    while (len > 0 && sx_text_width_n(font, text, len) + dots > max_w)
        len--;
    int end = sx_draw_text_n(c, font, x, y, text, len, color);
    sx_draw_text(c, font, end, y, "...", color);
}

static void draw_cell(SxCanvas *c, SxRect cell, const SxFont *font, const char *text, uint32_t color, bool right)
{
    int y = sx_text_y_centered(font, cell);
    if (right)
        sx_draw_text(c, font, cell.x + cell.w - sx_text_width(font, text), y, text, color);
    else
        draw_text_fit(c, font, cell.x, y, cell.w - 8, text, color);
}

static void format_kib(uint64_t kib, char *out, size_t size)
{
    if (kib < 1024)
    {
        snprintf(out, size, "%llu KiB", (unsigned long long)kib);
        return;
    }
    uint64_t tenths = kib * 10 / 1024;
    snprintf(out, size, "%llu.%llu MiB", (unsigned long long)(tenths / 10), (unsigned long long)(tenths % 10));
}

static void format_pct(float v, char *out, size_t size)
{
    int tenths = (int)(v * 1000.0f + 0.5f);
    if (tenths == 0)
        snprintf(out, size, "0%%");
    else
        snprintf(out, size, "%d.%d%%", tenths / 10, tenths % 10);
}

static const char *state_name(uint8_t state)
{
    switch (state)
    {
        case TASK_STATE_RUNNING:  return "Running";
        case TASK_STATE_READY:    return "Ready";
        case TASK_STATE_SLEEPING: return "Sleeping";
        default:                  return "Unknown";
    }
}

static void draw_processes(SxCanvas *c)
{
    SxRect card = list_card();
    sx_ui_panel(c, card, 12, SX_THEME_SURFACE);

    SxRect header = sx_rect(card.x, card.y, card.w, HEADER_H);
    for (int col = 0; col < COL_COUNT; col++)
    {
        SxRect cell = col_rect(header, (Column)col);
        bool right = col != COL_NAME;
        bool sorted = (int)M.sort_col == col;
        char label[32];
        snprintf(label, sizeof(label), "%s%s", col_names[col], sorted ? (M.sort_desc ? " v" : " ^") : "");
        uint32_t color = sorted || hovering(HIT_HEADER, col) ? SX_THEME_TEXT : SX_THEME_TEXT_DIM;
        draw_cell(c, cell, &sx_font_sans_bold, label, color, right);
    }
    sx_fill_rect(c, card.x + 1, card.y + HEADER_H - 1, card.w - 2, 1, SX_RGBA(255, 255, 255, 18));

    SxRect body = list_body();
    sx_canvas_set_clip(c, body);
    char pid[24], cpu[16], mem[24];
    for (int i = 0; i < M.proc_count; i++)
    {
        SxRect row = row_rect(i);
        if (row.y + row.h <= body.y)
            continue;
        if (row.y >= body.y + body.h)
            break;

        const Proc *p = &M.procs[i];
        bool selected = p->info.pid == M.selected;
        SxRect hl = sx_rect(row.x + 5, row.y + 1, row.w - 10, row.h - 2);
        if (selected)
            sx_fill_rounded_rect(c, hl.x, hl.y, hl.w, hl.h, 6, SX_THEME_ACCENT_A(60));
        else if (hovering(HIT_ROW, i))
            sx_fill_rounded_rect(c, hl.x, hl.y, hl.w, hl.h, 6, SX_RGBA(255, 255, 255, 12));

        uint32_t text = p->info.kernel ? SX_THEME_TEXT_DIM : SX_THEME_TEXT;
        snprintf(pid, sizeof(pid), "%llu", (unsigned long long)p->info.pid);
        format_pct(p->cpu, cpu, sizeof(cpu));
        format_kib(p->info.mem_kb, mem, sizeof(mem));

        SxRect name = col_rect(row, COL_NAME);
        if (p->info.kernel)
        {
            const char *tag = "kernel";
            int tw = sx_text_width(&sx_font_sans_small, tag);
            SxRect badge = sx_rect(name.x + name.w - tw - 18, row.y + 7, tw + 12, row.h - 14);
            sx_fill_rounded_rect(c, badge.x, badge.y, badge.w, badge.h, 4, SX_RGBA(255, 255, 255, 14));
            sx_draw_text_centered(c, &sx_font_sans_small, badge, tag, SX_THEME_TEXT_DIM);
            name.w -= badge.w + 10;
        }
        draw_cell(c, name, &sx_font_sans, p->label, text, false);
        draw_cell(c, col_rect(row, COL_PID), &sx_font_sans, pid, SX_THEME_TEXT_DIM, true);
        draw_cell(c, col_rect(row, COL_STATUS), &sx_font_sans, state_name(p->info.state), SX_THEME_TEXT_DIM, true);
        draw_cell(c, col_rect(row, COL_CPU), &sx_font_sans, cpu, p->cpu >= 0.005f ? SX_THEME_BLUE : SX_THEME_TEXT_DIM, true);
        draw_cell(c, col_rect(row, COL_MEM), &sx_font_sans, mem, text, true);
    }
    sx_canvas_reset_clip(c);

    int max = max_scroll();
    if (max > 0)
    {
        int track_h = body.h - 8;
        int thumb_h = track_h * body.h / (body.h + max);
        if (thumb_h < 24)
            thumb_h = 24;
        int thumb_y = body.y + 4 + (track_h - thumb_h) * M.scroll / max;
        sx_fill_rounded_rect(c, body.x + body.w - 7, thumb_y, 4, thumb_h, 2, SX_RGBA(255, 255, 255, 50));
    }

    int h = sx_window_height(M.win);
    SxRect footer = sx_rect(PAD + 4, h - FOOTER_H, card.w, FOOTER_H);
    char line[96];
    uint32_t line_color = SX_THEME_TEXT_DIM;
    if (M.status[0] && sx_time_ms() < M.status_until)
    {
        snprintf(line, sizeof(line), "%s", M.status);
        line_color = SX_THEME_TEXT;
    }
    else
    {
        snprintf(line, sizeof(line), "%d process%s", M.proc_count, M.proc_count == 1 ? "" : "es");
    }
    sx_draw_text(c, &sx_font_sans, footer.x, sx_text_y_centered(&sx_font_sans, footer), line, line_color);

    SxRect kr = kill_rect();
    if (can_kill(find_selected()))
        sx_ui_button(c, kr, "End task", &sx_font_sans_bold,
                     hovering(HIT_KILL, 0) ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL, SX_THEME_RED, SX_RGB(255, 255, 255));
    else
        sx_ui_button(c, kr, "End task", &sx_font_sans_bold, SX_BUTTON_NORMAL, SX_THEME_SURFACE, SX_THEME_DISABLED);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(M.win);
    sx_clear(c, SX_THEME_BG);

    SxRect bar = sx_rect(tab_rect(0).x, tab_rect(0).y, tab_rect(0).w * TAB_COUNT, tab_rect(0).h);
    sx_fill_rounded_rect(c, bar.x, bar.y, bar.w, bar.h, 8, SX_THEME_SUNKEN);
    for (int i = 0; i < TAB_COUNT; i++)
    {
        SxRect r = tab_rect(i);
        SxRect inner = sx_rect(r.x + 3, r.y + 3, r.w - 6, r.h - 6);
        bool current = (int)M.tab == i;
        if (current)
            sx_fill_rounded_rect(c, inner.x, inner.y, inner.w, inner.h, 6, SX_THEME_SURFACE_HI);
        else if (hovering(HIT_TAB, i))
            sx_fill_rounded_rect(c, inner.x, inner.y, inner.w, inner.h, 6, SX_RGBA(255, 255, 255, 10));
        sx_draw_text_centered(c, current ? &sx_font_sans_bold : &sx_font_sans, r, tab_names[i],
                              current ? SX_THEME_TEXT : SX_THEME_TEXT_DIM);
    }

    if (M.tab == TAB_PROCESSES)
        draw_processes(c);
    else
        draw_performance(c, TAB_BAR_H);

    sx_window_commit(M.win);
}

static Hit hit_test(int x, int y)
{
    for (int i = 0; i < TAB_COUNT; i++)
        if (sx_rect_contains(tab_rect(i), x, y))
            return (Hit){ HIT_TAB, i };

    if (M.tab != TAB_PROCESSES)
        return (Hit){ HIT_NONE, 0 };

    if (sx_rect_contains(kill_rect(), x, y))
        return (Hit){ HIT_KILL, 0 };

    SxRect card = list_card();
    SxRect header = sx_rect(card.x, card.y, card.w, HEADER_H);
    for (int col = 0; col < COL_COUNT; col++)
        if (sx_rect_contains(col_rect(header, (Column)col), x, y))
            return (Hit){ HIT_HEADER, col };

    SxRect body = list_body();
    if (sx_rect_contains(body, x, y))
    {
        int i = (y - body.y + M.scroll) / ROW_H;
        if (i >= 0 && i < M.proc_count)
            return (Hit){ HIT_ROW, i };
    }
    return (Hit){ HIT_NONE, 0 };
}

static void activate(Hit h)
{
    switch (h.kind)
    {
        case HIT_TAB:
            M.tab = (Tab)h.index;
            break;
        case HIT_HEADER:
            if ((int)M.sort_col == h.index)
                M.sort_desc = !M.sort_desc;
            else
            {
                M.sort_col = (Column)h.index;
                // Numbers read best biggest-first; names alphabetically.
                M.sort_desc = h.index == COL_CPU || h.index == COL_MEM;
            }
            sort_procs();
            break;
        case HIT_ROW:
            M.selected = M.procs[h.index].info.pid;
            break;
        case HIT_KILL:
            kill_selected();
            clamp_scroll();
            break;
        default:
            break;
    }
}

static void move_selection(int delta)
{
    if (M.proc_count == 0)
        return;
    int i = find_selected();
    i = i < 0 ? 0 : i + delta;
    if (i < 0)
        i = 0;
    if (i >= M.proc_count)
        i = M.proc_count - 1;
    M.selected = M.procs[i].info.pid;
    scroll_to_selected();
}

static bool handle_key(const SxEvent *ev)
{
    if (ev->key == SX_KEY_TAB)
    {
        M.tab = (Tab)((M.tab + 1) % TAB_COUNT);
        return true;
    }
    if (M.tab != TAB_PROCESSES)
        return false;

    switch (ev->key)
    {
        case SX_KEY_UP:        move_selection(-1); return true;
        case SX_KEY_DOWN:      move_selection(1); return true;
        case SX_KEY_PAGE_UP:   move_selection(-list_body().h / ROW_H); return true;
        case SX_KEY_PAGE_DOWN: move_selection(list_body().h / ROW_H); return true;
        case SX_KEY_HOME:      move_selection(-MAX_PROCS); return true;
        case SX_KEY_END:       move_selection(MAX_PROCS); return true;
        case SX_KEY_DELETE:
            kill_selected();
            clamp_scroll();
            return true;
        default:
            return false;
    }
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("sysmon: cannot connect to the display server\n");
        return 1;
    }

    M.win = sx_window_create("System Monitor", 520, 720, SX_WINDOW_RESIZABLE);
    if (!M.win)
        return 1;

    M.sort_col = COL_CPU;
    M.sort_desc = true;
    sample();
    sample_procs();
    draw();
    uint64_t last = sx_time_ms();
    bool dirty = false;

    for (;;)
    {
        SxEvent ev;
        uint64_t now = sx_time_ms();
        int wait_ms = now - last >= SAMPLE_MS ? 0 : (int)(SAMPLE_MS - (now - last));
        int rc = sx_next_event(&ev, wait_ms);
        if (rc < 0)
            break;

        if (rc == 0)
        {
            last = sx_time_ms();
            sample();
            sample_procs();
            clamp_scroll();
            draw();
            continue;
        }

        switch (ev.type)
        {
            case SX_EVENT_CLOSE:
            case SX_EVENT_DISCONNECTED:
                goto out;

            case SX_EVENT_RESIZE:
                clamp_scroll();
                dirty = true;
                break;

            case SX_EVENT_MOUSE_MOVE:
            {
                Hit h = hit_test(ev.x, ev.y);
                if (h.kind != M.hover.kind || h.index != M.hover.index)
                {
                    M.hover = h;
                    dirty = true;
                }
                break;
            }

            case SX_EVENT_MOUSE_LEAVE:
                M.hover.kind = HIT_NONE;
                dirty = true;
                break;

            case SX_EVENT_MOUSE_DOWN:
                if (ev.button == SX_BUTTON_LEFT)
                {
                    activate(hit_test(ev.x, ev.y));
                    M.hover = hit_test(ev.x, ev.y);
                    dirty = true;
                }
                break;

            case SX_EVENT_MOUSE_WHEEL:
                if (M.tab == TAB_PROCESSES)
                {
                    M.scroll -= ev.wheel * ROW_H * 3;
                    clamp_scroll();
                    M.hover = hit_test(ev.x, ev.y);
                    dirty = true;
                }
                break;

            case SX_EVENT_KEY_DOWN:
                dirty |= handle_key(&ev);
                break;

            default:
                break;
        }

        if (dirty && !sx_pending())
        {
            draw();
            dirty = false;
        }
    }

out:
    sx_window_destroy(M.win);
    sx_disconnect();
    return 0;
}
