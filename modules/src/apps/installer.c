#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ioctl.h>
#include <disk.h>
#include <strawx.h>
#include <ui.h>
#include <limine-bios-hdd.h>

#define WIN_W      600
#define WIN_H      440
#define PAD        28
#define ROW_H      58
#define ROW_GAP    10
#define LIST_TOP   112
#define BTN_H      38
#define MAX_DISKS  16
#define COPY_CHUNK (256 * 1024)
#define TARGET     "/mnt"

typedef enum
{
    STAGE_PICK,
    STAGE_CONFIRM,
    STAGE_COPY,
    STAGE_DONE,
    STAGE_FAILED,
}
Stage;

typedef enum
{
    HIT_NONE,
    HIT_PRIMARY,
    HIT_SECONDARY,
    HIT_DISK,
}
Hit;

typedef struct
{
    char name[16];
    struct disk_info info;
}
Disk;

typedef struct
{
    char     path[256];
    bool     dir;
    uint64_t size;
}
Entry;

static struct
{
    SxWindow *win;
    Stage     stage;

    Disk      disks[MAX_DISKS];
    int       disk_count;
    int       selected;

    Hit       hover;
    int       hover_disk;
    Hit       pressed;

    Entry    *entries;
    int       entry_count;
    int       entry_cap;
    int       current;
    int       src;
    int       dst;
    uint64_t  total;
    uint64_t  copied;
    uint8_t  *buf;

    char      error[160];
}
I;

static const char *kind_name(uint8_t kind)
{
    switch (kind)
    {
        case DISK_KIND_NVME: return "NVMe";
        case DISK_KIND_AHCI: return "SATA";
        default:             return "IDE";
    }
}

static void format_size(char *out, size_t size, uint64_t bytes)
{
    const char *units[] = { "MB", "GB", "TB" };
    uint64_t scale = 1000ULL * 1000;
    int u = 0;
    while (u < 2 && bytes >= scale * 1000)
    {
        scale *= 1000;
        u++;
    }
    uint64_t tenths = bytes * 10 / scale;
    snprintf(out, size, "%llu.%llu %s", (unsigned long long)(tenths / 10),
             (unsigned long long)(tenths % 10), units[u]);
}

static void scan_disks(void)
{
    I.disk_count = 0;
    int fd = open("/dev", O_RDONLY);
    if (fd >= 0)
    {
        struct dirent de;
        while (readdir(fd, &de) == 1 && I.disk_count < MAX_DISKS)
        {
            if (strncmp(de.name, "disk", 4) != 0 || strchr(de.name, 'p'))
                continue;

            char path[96];
            snprintf(path, sizeof(path), "/dev/%s", de.name);
            int d = open(path, O_RDONLY);
            if (d < 0)
                continue;

            Disk *k = &I.disks[I.disk_count];
            if (ioctl(d, DISK_GET_INFO, &k->info) == 0 && !k->info.mounted)
            {
                snprintf(k->name, sizeof(k->name), "%s", de.name);
                I.disk_count++;
            }
            close(d);
        }
        close(fd);
    }

    for (int i = 1; i < I.disk_count; i++)
    {
        Disk d = I.disks[i];
        int j = i - 1;
        while (j >= 0 && strcmp(I.disks[j].name, d.name) > 0)
        {
            I.disks[j + 1] = I.disks[j];
            j--;
        }
        I.disks[j + 1] = d;
    }

    if (I.selected >= I.disk_count)
        I.selected = -1;
    if (I.selected < 0 && I.disk_count > 0)
        I.selected = 0;
}

static SxRect primary_rect(void)
{
    return sx_rect(WIN_W - PAD - 170, WIN_H - PAD - BTN_H, 170, BTN_H);
}

static SxRect secondary_rect(void)
{
    SxRect p = primary_rect();
    return sx_rect(p.x - 12 - 120, p.y, 120, BTN_H);
}

static SxRect disk_rect(int i)
{
    return sx_rect(PAD, LIST_TOP + i * (ROW_H + ROW_GAP), WIN_W - 2 * PAD, ROW_H);
}

static const char *primary_label(void)
{
    switch (I.stage)
    {
        case STAGE_PICK:    return "Continue";
        case STAGE_CONFIRM: return "Erase and Install";
        case STAGE_DONE:    return "Close";
        default:            return NULL;
    }
}

static const char *secondary_label(void)
{
    switch (I.stage)
    {
        case STAGE_PICK:    return "Rescan";
        case STAGE_CONFIRM: return "Back";
        case STAGE_FAILED:  return "Back";
        default:            return NULL;
    }
}

static bool primary_enabled(void)
{
    return I.stage != STAGE_PICK || I.selected >= 0;
}

static SxButtonState state_for(Hit hit)
{
    if (I.pressed == hit && I.hover == hit)
        return SX_BUTTON_PRESSED;
    return I.hover == hit ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL;
}

static void draw_header(SxCanvas *c, const char *title, const char *subtitle)
{
    sx_draw_text(c, &sx_font_sans_large, PAD, 24, title, SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans, PAD, 58, subtitle, SX_THEME_TEXT_DIM);
}

static void draw_pick(SxCanvas *c)
{
    draw_header(c, "Install StrawOS", "Choose the disk StrawOS should be installed on.");

    if (I.disk_count == 0)
    {
        sx_draw_text_centered(c, &sx_font_sans, sx_rect(0, LIST_TOP, WIN_W, 120),
                              "No disks found", SX_THEME_TEXT_DIM);
        return;
    }

    for (int i = 0; i < I.disk_count; i++)
    {
        SxRect r = disk_rect(i);
        if (r.y + r.h > WIN_H - PAD - BTN_H - 12)
            break;

        Disk *d = &I.disks[i];
        bool sel = i == I.selected;
        bool hov = I.hover == HIT_DISK && I.hover_disk == i;
        uint32_t fill = sel ? SX_THEME_ACCENT_A(50) : hov ? SX_THEME_SURFACE_HI : SX_THEME_SURFACE;
        sx_fill_rounded_rect(c, r.x, r.y, r.w, r.h, 8, fill);
        sx_draw_rounded_rect(c, r.x, r.y, r.w, r.h, 8, sel ? SX_THEME_ACCENT : SX_RGBA(255, 255, 255, 18));

        char size[32], line[128];
        format_size(size, sizeof(size), d->info.sectors * d->info.sector_size);
        snprintf(line, sizeof(line), "%s  -  %s  -  %s", kind_name(d->info.kind), size, d->name);

        sx_draw_text(c, &sx_font_sans_bold, r.x + 16, r.y + 10, d->info.model[0] ? d->info.model : "Unknown disk", SX_THEME_TEXT);
        sx_draw_text(c, &sx_font_sans_small, r.x + 16, r.y + 33, line, SX_THEME_TEXT_DIM);
    }
}

static void draw_confirm(SxCanvas *c)
{
    Disk *d = &I.disks[I.selected];
    draw_header(c, "Erase disk?", "This cannot be undone.");

    char size[32], line[160];
    format_size(size, sizeof(size), d->info.sectors * d->info.sector_size);
    snprintf(line, sizeof(line), "%s (%s)", d->info.model[0] ? d->info.model : d->name, size);

    SxRect r = sx_rect(PAD, LIST_TOP, WIN_W - 2 * PAD, 120);
    sx_ui_panel(c, r, 10, SX_THEME_RED_A(40));
    sx_draw_text(c, &sx_font_sans_bold, r.x + 18, r.y + 18, line, SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans, r.x + 18, r.y + 48, "Everything on this disk will be erased.", SX_THEME_TEXT);
    sx_draw_text(c, &sx_font_sans_small, r.x + 18, r.y + 78,
                 "A new FAT32 partition is created and StrawOS is copied onto it.", SX_THEME_TEXT_DIM);
}

static void draw_copy(SxCanvas *c)
{
    draw_header(c, "Installing", "Copying StrawOS to the disk. Do not turn off the computer.");

    SxRect bar = sx_rect(PAD, LIST_TOP + 30, WIN_W - 2 * PAD, 12);
    sx_fill_rounded_rect(c, bar.x, bar.y, bar.w, bar.h, 6, SX_THEME_SUNKEN);
    int fill = I.total ? (int)((uint64_t)bar.w * I.copied / I.total) : 0;
    if (fill > 0)
        sx_fill_rounded_rect(c, bar.x, bar.y, fill < 12 ? 12 : fill, bar.h, 6, SX_THEME_ACCENT);

    char done[32], total[32], line[96];
    format_size(done, sizeof(done), I.copied);
    format_size(total, sizeof(total), I.total);
    snprintf(line, sizeof(line), "%s of %s", done, total);
    sx_draw_text(c, &sx_font_sans, PAD, bar.y + 26, line, SX_THEME_TEXT);

    if (I.current < I.entry_count)
        sx_draw_text(c, &sx_font_sans_small, PAD, bar.y + 52, I.entries[I.current].path, SX_THEME_TEXT_DIM);
}

static void draw_done(SxCanvas *c)
{
    draw_header(c, "StrawOS is installed", "Remove the USB drive and restart to boot from the disk.");
}

static void draw_failed(SxCanvas *c)
{
    draw_header(c, "Installation failed", "Nothing else was changed after this step.");
    SxRect r = sx_rect(PAD, LIST_TOP, WIN_W - 2 * PAD, 64);
    sx_ui_panel(c, r, 10, SX_THEME_RED_A(40));
    sx_draw_text(c, &sx_font_sans, r.x + 18, r.y + 22, I.error, SX_THEME_TEXT);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(I.win);
    sx_clear(c, SX_THEME_BG);

    switch (I.stage)
    {
        case STAGE_PICK:    draw_pick(c);    break;
        case STAGE_CONFIRM: draw_confirm(c); break;
        case STAGE_COPY:    draw_copy(c);    break;
        case STAGE_DONE:    draw_done(c);    break;
        case STAGE_FAILED:  draw_failed(c);  break;
    }

    const char *p = primary_label();
    if (p)
    {
        bool enabled = primary_enabled();
        uint32_t base = I.stage == STAGE_CONFIRM ? SX_THEME_RED : SX_THEME_ACCENT;
        sx_ui_button(c, primary_rect(), p, &sx_font_sans_bold,
                     enabled ? state_for(HIT_PRIMARY) : SX_BUTTON_NORMAL,
                     enabled ? base : SX_THEME_SURFACE_HI,
                     enabled ? SX_THEME_ACCENT_TEXT : SX_THEME_DISABLED);
    }

    const char *s = secondary_label();
    if (s)
        sx_ui_button(c, I.stage == STAGE_FAILED ? primary_rect() : secondary_rect(), s, &sx_font_sans,
                     state_for(HIT_SECONDARY), SX_THEME_SURFACE_HI, SX_THEME_TEXT);

    sx_window_commit(I.win);
}

static void fail(const char *msg)
{
    if (I.src >= 0)
        close(I.src);
    if (I.dst >= 0)
        close(I.dst);
    I.src = I.dst = -1;
    umount(TARGET);
    snprintf(I.error, sizeof(I.error), "%s", msg);
    I.stage = STAGE_FAILED;
}

static bool add_entry(const char *path, bool dir, uint64_t size)
{
    if (I.entry_count == I.entry_cap)
    {
        int cap = I.entry_cap ? I.entry_cap * 2 : 256;
        Entry *n = realloc(I.entries, (size_t)cap * sizeof(Entry));
        if (!n)
            return false;
        I.entries = n;
        I.entry_cap = cap;
    }
    Entry *e = &I.entries[I.entry_count++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->dir = dir;
    e->size = size;
    I.total += size;
    return true;
}

static bool collect(const char *dir)
{
    int fd = open(dir, O_RDONLY);
    if (fd < 0)
        return false;

    bool ok = true;
    struct dirent de;
    while (ok && readdir(fd, &de) == 1)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", strcmp(dir, "/") == 0 ? "" : dir, de.name);
        if (strcmp(path, "/dev") == 0 || strcmp(path, TARGET) == 0)
            continue;

        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        if (st.type == STAT_DIR)
            ok = add_entry(path, true, 0) && collect(path);
        else if (st.type == STAT_FILE)
            ok = add_entry(path, false, st.size);
    }
    close(fd);
    return ok;
}

static bool disk_ioctl(const char *path, uint64_t request)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    int rc = ioctl(fd, request, NULL);
    close(fd);
    return rc == 0;
}

static void start_install(void)
{
    Disk *d = &I.disks[I.selected];
    char disk[64], part[64];
    snprintf(disk, sizeof(disk), "/dev/%s", d->name);
    snprintf(part, sizeof(part), "/dev/%sp1", d->name);

    I.stage = STAGE_COPY;
    I.entry_count = 0;
    I.current = 0;
    I.total = 0;
    I.copied = 0;
    I.src = I.dst = -1;
    draw();

    umount(TARGET);
    if (!disk_ioctl(disk, DISK_PARTITION))
        return fail("Could not write the partition table.");
    if (!disk_ioctl(part, DISK_FORMAT))
        return fail("Could not format the partition.");

    mkdir(TARGET, 0755);
    if (mount(part, TARGET) != 0)
        return fail("Could not mount the new partition.");
    if (!collect("/"))
        return fail("Could not read the live system.");
}

static bool disk_rw(int fd, uint64_t lba, uint32_t count, void *buf, bool write)
{
    struct disk_io io = { .lba = lba, .count = count, .buf = buf };
    return ioctl(fd, write ? DISK_WRITE : DISK_READ, &io) == 0;
}

static bool install_bios_boot(void)
{
    Disk *d = &I.disks[I.selected];
    char disk[64], part[64];
    snprintf(disk, sizeof(disk), "/dev/%s", d->name);
    snprintf(part, sizeof(part), "/dev/%sp2", d->name);

    const uint8_t *img = binary_limine_hdd_bin_data;
    size_t stage2_bytes = sizeof(binary_limine_hdd_bin_data) - 512;
    uint32_t stage2_sectors = (uint32_t)((stage2_bytes + 511) / 512);

    bool ok = false;
    uint8_t *stage2 = calloc(stage2_sectors, 512);
    int pf = open(part, O_RDONLY);
    int df = open(disk, O_RDONLY);
    struct disk_info info;
    uint8_t mbr[512], boot[512];

    if (!stage2 || pf < 0 || df < 0 || ioctl(pf, DISK_GET_INFO, &info) != 0 ||
        info.sector_size != 512 || info.sectors < stage2_sectors)
        goto out;

    memcpy(stage2, img + 512, stage2_bytes);
    if (!disk_rw(pf, 0, stage2_sectors, stage2, true))
        goto out;
    if (!disk_rw(df, 0, 1, mbr, false))
        goto out;

    uint64_t stage2_loc = info.start * 512;
    memcpy(boot, img, 512);
    memcpy(boot + 0x1A4, &stage2_loc, sizeof(stage2_loc));
    memcpy(boot + 218, mbr + 218, 6);
    memcpy(boot + 440, mbr + 440, 70);
    ok = disk_rw(df, 0, 1, boot, true);

out:
    if (pf >= 0)
        close(pf);
    if (df >= 0)
        close(df);
    free(stage2);
    return ok;
}

static void copy_step(void)
{
    uint64_t start = sx_time_ms();
    while (sx_time_ms() - start < 40)
    {
        if (I.current >= I.entry_count)
        {
            if (umount(TARGET) != 0)
                return fail("Could not finish writing to the disk.");
            if (!install_bios_boot())
                return fail("Could not install the BIOS boot loader.");
            I.stage = STAGE_DONE;
            return;
        }

        Entry *e = &I.entries[I.current];
        char dst[300];
        snprintf(dst, sizeof(dst), "%s%s", TARGET, e->path);

        if (e->dir)
        {
            struct stat st;
            if (mkdir(dst, 0755) != 0 && stat(dst, &st) != 0)
                return fail("Could not create a folder on the disk.");
            I.current++;
            continue;
        }

        if (I.src < 0)
        {
            I.src = open(e->path, O_RDONLY);
            I.dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
            if (I.src < 0 || I.dst < 0)
                return fail("Could not copy a file to the disk.");
        }

        ssize_t n = read(I.src, I.buf, COPY_CHUNK);
        if (n < 0)
            return fail("Could not read a file from the live system.");
        if (n == 0)
        {
            close(I.src);
            close(I.dst);
            I.src = I.dst = -1;
            I.current++;
            continue;
        }
        if (write(I.dst, I.buf, (size_t)n) != n)
            return fail("Could not write to the disk. Is it full?");
        I.copied += (uint64_t)n;
    }
}

static Hit hit_test(int x, int y, int *disk)
{
    if (I.stage == STAGE_FAILED)
        return sx_rect_contains(primary_rect(), x, y) ? HIT_SECONDARY : HIT_NONE;
    if (primary_label() && sx_rect_contains(primary_rect(), x, y))
        return HIT_PRIMARY;
    if (secondary_label() && sx_rect_contains(secondary_rect(), x, y))
        return HIT_SECONDARY;
    if (I.stage == STAGE_PICK)
    {
        for (int i = 0; i < I.disk_count; i++)
        {
            if (sx_rect_contains(disk_rect(i), x, y))
            {
                *disk = i;
                return HIT_DISK;
            }
        }
    }
    return HIT_NONE;
}

static bool activate(Hit hit, int disk)
{
    switch (hit)
    {
        case HIT_DISK:
            I.selected = disk;
            break;
        case HIT_PRIMARY:
            if (I.stage == STAGE_PICK && I.selected >= 0)
                I.stage = STAGE_CONFIRM;
            else if (I.stage == STAGE_CONFIRM)
                start_install();
            else if (I.stage == STAGE_DONE)
                return false;
            break;
        case HIT_SECONDARY:
            if (I.stage == STAGE_PICK)
                scan_disks();
            else
            {
                I.stage = STAGE_PICK;
                scan_disks();
            }
            break;
        default:
            break;
    }
    return true;
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("installer: cannot connect to the display server\n");
        return 1;
    }

    I.buf = malloc(COPY_CHUNK);
    I.win = sx_window_create("Install StrawOS", WIN_W, WIN_H, 0);
    if (!I.buf || !I.win)
        return 1;

    I.selected = -1;
    I.src = I.dst = -1;
    umount(TARGET);
    scan_disks();
    draw();

    bool running = true;
    while (running)
    {
        SxEvent ev;
        int rc = sx_next_event(&ev, I.stage == STAGE_COPY ? 0 : -1);
        if (rc < 0)
            break;

        if (rc > 0)
        {
            int disk = -1;
            switch (ev.type)
            {
                case SX_EVENT_CLOSE:
                case SX_EVENT_DISCONNECTED:
                    if (I.stage != STAGE_COPY)
                        running = false;
                    break;
                case SX_EVENT_MOUSE_MOVE:
                {
                    Hit h = hit_test(ev.x, ev.y, &disk);
                    if (h != I.hover || disk != I.hover_disk)
                    {
                        I.hover = h;
                        I.hover_disk = disk;
                        draw();
                    }
                    break;
                }
                case SX_EVENT_MOUSE_LEAVE:
                    I.hover = HIT_NONE;
                    I.pressed = HIT_NONE;
                    draw();
                    break;
                case SX_EVENT_MOUSE_DOWN:
                    I.pressed = hit_test(ev.x, ev.y, &disk);
                    if (I.pressed == HIT_DISK)
                        activate(HIT_DISK, disk);
                    draw();
                    break;
                case SX_EVENT_MOUSE_UP:
                {
                    Hit h = hit_test(ev.x, ev.y, &disk);
                    Hit p = I.pressed;
                    I.pressed = HIT_NONE;
                    if (h == p && (h == HIT_SECONDARY || (h == HIT_PRIMARY && primary_enabled())))
                        running = activate(h, disk);
                    draw();
                    break;
                }
                case SX_EVENT_KEY_DOWN:
                    if (I.stage == STAGE_PICK && I.disk_count > 0)
                    {
                        if (ev.key == SX_KEY_UP && I.selected > 0)
                            I.selected--;
                        else if (ev.key == SX_KEY_DOWN && I.selected < I.disk_count - 1)
                            I.selected++;
                    }
                    if (ev.key == SX_KEY_ENTER && primary_label() && primary_enabled())
                        running = activate(HIT_PRIMARY, -1);
                    else if (ev.key == SX_KEY_ESCAPE && I.stage == STAGE_CONFIRM)
                        activate(HIT_SECONDARY, -1);
                    draw();
                    break;
                default:
                    break;
            }
            continue;
        }

        if (I.stage == STAGE_COPY)
        {
            copy_step();
            draw();
        }
    }

    sx_window_destroy(I.win);
    sx_disconnect();
    return 0;
}
