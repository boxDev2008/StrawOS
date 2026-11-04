#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ioctl.h>
#include <socket.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <audio.h>

#include <strawx.h>
#include <ui.h>

#include "server.h"
#include "render.c"
#include "shell.c"
#include "anim.c"
#include "windows.c"
#include "workspace.c"
#include "input.c"
#include "clients.c"

static void broadcast_theme_changed(void)
{
    SxEvtMsg ev;
    memset(&ev, 0, sizeof(ev));
    ev.hdr.type = SX_EVT_THEME_CHANGED;
    ev.hdr.size = sizeof(ev);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (S.clients[i].used)
            send_raw(&S.clients[i], &ev, sizeof(ev));
}

static void audio_select(int fd, const struct audio_device_list *list, const char *key, bool input)
{
    char want[SX_SETTING_MAX];
    if (!sx_setting_get(key, want, sizeof(want)) || !want[0])
        return;
    for (uint32_t i = 0; i < list->count; i++)
    {
        const struct audio_device *d = &list->devices[i];
        if (d->input == input && strcmp(d->key, want) == 0)
        {
            uint32_t id = d->id;
            if (!d->selected)
                ioctl(fd, input ? AUDIO_SET_INPUT_DEVICE : AUDIO_SET_OUTPUT_DEVICE, &id);
            return;
        }
    }
}

static void audio_settings_load(void)
{
    int fd = open(AUDIO_DEVICE, O_WRONLY);
    if (fd < 0)
        return;
    uint32_t volume = (uint32_t)sx_setting_get_int("audio_volume", 100);
    uint32_t input = (uint32_t)sx_setting_get_int("audio_input_volume", 100);
    ioctl(fd, AUDIO_SET_VOLUME, &volume);
    ioctl(fd, AUDIO_SET_INPUT_VOLUME, &input);
    static struct audio_device_list list;
    if (ioctl(fd, AUDIO_LIST_DEVICES, &list) == 0)
    {
        audio_select(fd, &list, "audio_output", false);
        audio_select(fd, &list, "audio_input", true);
    }
    close(fd);
}

static void settings_load(void)
{
    const SxTheme *old_theme = sx_theme;
    sx_theme_load();
    if (sx_theme != old_theme)
        broadcast_theme_changed();

    S.clock_24h = sx_setting_get_int("clock_24h", 1) != 0;
    S.show_date = sx_setting_get_int("show_date", 1) != 0;
    S.desktop_icons = sx_setting_get_int("desktop_icons", 0) != 0;
    S.pointer_speed = sx_setting_get_int("mouse_speed", 100);
    S.animations = sx_setting_get_int("animations", 1) != 0;
    wallpaper_generate();
    audio_settings_load();
}

static bool display_init(void)
{
    S.fb_fd = open("/dev/framebuffer", O_RDWR);
    if (S.fb_fd < 0)
    {
        printf("strawx: cannot open /dev/framebuffer\n");
        return false;
    }

    if (ioctl(S.fb_fd, DEVFS_IOCTL_FB_GET_INFO, &S.fb) != 0 || (S.fb.bpp != 32 && S.fb.bpp != 24))
    {
        printf("strawx: unsupported framebuffer\n");
        return false;
    }

    S.width = (int)S.fb.width;
    S.height = (int)S.fb.height;
    S.fb_mem = (uint8_t *)S.fb.address;
    S.fb_is_xrgb = S.fb.bpp == 32 &&
                   S.fb.red_mask_shift == 16 && S.fb.green_mask_shift == 8 && S.fb.blue_mask_shift == 0 &&
                   S.fb.red_mask_size == 8 && S.fb.green_mask_size == 8 && S.fb.blue_mask_size == 8;

    size_t bytes = (size_t)S.width * (size_t)S.height * 4;
    S.back = malloc(bytes);
    S.wallpaper = malloc(bytes);
    // Scratch space for fading overlays; without it they just appear at full opacity.
    S.layer = malloc(bytes);
    if (!S.back || !S.wallpaper)
    {
        printf("strawx: out of memory for %dx%d buffers\n", S.width, S.height);
        return false;
    }

    sx_canvas_init(&S.canvas, S.back, S.width, S.height, S.width);

    if (ioctl(S.fb_fd, DEVFS_IOCTL_FB_ACQUIRE, NULL) != 0)
    {
        printf("strawx: the display is already in use\n");
        return false;
    }
    return true;
}

static bool input_init(void)
{
    S.kbd_fd = open("/dev/keyboard", O_RDONLY);
    S.mouse_fd = open("/dev/mouse", O_RDONLY);
    if (S.kbd_fd < 0 || S.mouse_fd < 0)
    {
        printf("strawx: cannot open input devices\n");
        return false;
    }

    uint8_t junk[256];
    while (read(S.kbd_fd, junk, sizeof(junk)) > 0)
        ;
    while (read(S.mouse_fd, junk, sizeof(junk)) > 0)
        ;
    return true;
}

static bool socket_init(void)
{
    S.listen_fd = socket(SOCK_NONBLOCK);
    if (S.listen_fd < 0)
        return false;
    if (bind(S.listen_fd, SX_SOCKET_NAME) != 0)
    {
        printf("strawx: socket name \"%s\" is taken; is another server running?\n", SX_SOCKET_NAME);
        return false;
    }
    return listen(S.listen_fd) == 0;
}

static void poll_input(void)
{
    MouseEvent events[64];
    for (;;)
    {
        ssize_t n = read(S.mouse_fd, events, sizeof(events));
        if (n <= 0)
            break;
        for (size_t i = 0; i < (size_t)n / sizeof(MouseEvent); i++)
            handle_mouse_event(&events[i]);
    }

    uint8_t codes[128];
    for (;;)
    {
        ssize_t n = read(S.kbd_fd, codes, sizeof(codes));
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; i++)
            handle_scancode(codes[i]);
    }
}

int main(void)
{
    memset(&S, 0, sizeof(S));
    S.bar_hover = BAR_NONE;
    S.dock_hover = -1;
    S.menu_hover = -1;
    S.desktop_hover = -1;
    S.desktop_selected = -1;
    S.last_minute = -1;

    if (!socket_init() || !display_init() || !input_init())
        return 1;

    printf("strawx: display server running at %dx%d\n", S.width, S.height);

    settings_load();
    for (int k = 0; k < CURSOR_COUNT; k++)
        cursor_build((CursorKind)k);

    S.mx = S.width / 2;
    S.my = S.height / 2;
    S.cursor = CURSOR_ARROW;
    S.cursor_rect = cursor_rect_at(S.mx, S.my, S.cursor);

    shell_load_apps();
    shell_tick();
    damage_all();
    compose();

    struct pollfd pfds[3 + MAX_CLIENTS];
    Client *pfd_client[3 + MAX_CLIENTS];

    for (;;)
    {
        int n = 0;
        pfds[n].fd = S.listen_fd; pfds[n].events = POLLIN; pfd_client[n++] = NULL;
        pfds[n].fd = S.kbd_fd;    pfds[n].events = POLLIN; pfd_client[n++] = NULL;
        pfds[n].fd = S.mouse_fd;  pfds[n].events = POLLIN; pfd_client[n++] = NULL;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!S.clients[i].used)
                continue;
            pfds[n].fd = S.clients[i].fd;
            pfds[n].events = POLLIN;
            pfd_client[n++] = &S.clients[i];
        }

        poll(pfds, (uint64_t)n, S.anim_busy ? ANIM_FRAME_MS : 1000);

        poll_input();

        if (pfds[0].revents & POLLIN)
            client_accept();

        for (int i = 3; i < n; i++)
        {
            Client *c = pfd_client[i];
            if (!c->used || !pfds[i].revents)
                continue;
            if (!client_read(c))
                client_disconnect(c);
        }

        if (S.settings_pending)
        {
            S.settings_pending = false;
            settings_load();
            S.desktop_selected = -1;
            S.desktop_hover = -1;
            damage_all();
        }

        shell_tick();
        anim_tick();
        pointer_grab_update();

        if (S.damage_count)
            compose();
    }

    return 0;
}
