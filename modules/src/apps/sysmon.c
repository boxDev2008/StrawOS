#include <stdio.h>
#include <string.h>
#include <time.h>
#include <strawx.h>
#include <ui.h>

#define HISTORY   120
#define SAMPLE_MS 500

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
}
M;

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
        // First sample has no previous reading, so it shows the average since boot.
        if (!M.have_cpu)
            prev = (struct cpuinfo){ 0 };
        uint64_t total = M.cpu.total_ticks - prev.total_ticks;
        uint64_t busy = M.cpu.busy_ticks - prev.busy_ticks;
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

// Height of the per-core grid; zero on single-core machines, where it would repeat the CPU card.
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

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(M.win);
    int w = c->width, h = c->height;
    sx_clear(c, SX_THEME_BG);

    int pad = 14;
    char buf[64], buf2[64];
    char line[96];

    int grid_h = core_grid_height();
    int cards_h = h - 150 - pad - (grid_h ? grid_h + pad : 0);
    int card_h = cards_h / 2 > 150 ? cards_h / 2 : 150;

    if (M.have_cpu)
        snprintf(line, sizeof(line), "%llu core%s", (unsigned long long)M.cpu.cpus, M.cpu.cpus == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "unavailable");
    SxRect cpu_card = sx_rect(pad, pad, w - 2 * pad, card_h);
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

    sx_window_commit(M.win);
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("sysmon: cannot connect to the display server\n");
        return 1;
    }

    M.win = sx_window_create("System Monitor", 520, 760, SX_WINDOW_RESIZABLE);
    if (!M.win)
        return 1;

    sample();
    draw();
    uint64_t last = sx_time_ms();

    for (;;)
    {
        SxEvent ev;
        uint64_t now = sx_time_ms();
        int wait_ms = now - last >= SAMPLE_MS ? 0 : (int)(SAMPLE_MS - (now - last));
        int rc = sx_next_event(&ev, wait_ms);
        if (rc < 0)
            break;

        if (rc > 0)
        {
            if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
                break;
            if (ev.type == SX_EVENT_RESIZE)
                draw();
            continue;
        }

        last = sx_time_ms();
        sample();
        draw();
    }

    sx_window_destroy(M.win);
    sx_disconnect();
    return 0;
}
