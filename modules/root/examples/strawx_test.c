// Minimal StrawX window for StrawOS: a button that counts clicks.
// Build and run inside the OS:  tcc strawx_test.c -lstrawx -o strawx_test && ./strawx_test

#include <stdio.h>
#include <strawx.h>
#include <ui.h>

static int clicks;
static bool hover, pressed;

static SxRect button_rect(SxCanvas *c)
{
    return sx_rect(c->width / 2 - 60, c->height / 2 + 4, 120, 34);
}

static void draw(SxWindow *win)
{
    SxCanvas *c = sx_window_canvas(win);

    sx_fill_rect(c, 0, 0, c->width, c->height, SX_THEME_BG);

    char buf[64];
    snprintf(buf, sizeof(buf), "Clicked %d time%s", clicks, clicks == 1 ? "" : "s");
    sx_draw_text_centered(c, &sx_font_sans_large, sx_rect(0, c->height / 2 - 40, c->width, 30), buf, SX_THEME_TEXT);

    SxButtonState state = pressed ? SX_BUTTON_PRESSED : hover ? SX_BUTTON_HOVER : SX_BUTTON_NORMAL;
    sx_ui_button(c, button_rect(c), "Click me", &sx_font_sans, state, SX_THEME_ACCENT, SX_THEME_ACCENT_TEXT);

    sx_window_commit(win);
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("strawx_test: cannot connect to the display server\n");
        return 1;
    }

    SxWindow *win = sx_window_create("StrawX Test", 300, 180, SX_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    draw(win);

    for (;;)
    {
        SxEvent ev;
        if (sx_next_event(&ev, -1) < 0)
            break;

        if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
            break;

        bool inside = sx_rect_contains(button_rect(sx_window_canvas(win)), ev.x, ev.y);
        switch (ev.type)
        {
        case SX_EVENT_MOUSE_MOVE:
            hover = inside;
            break;
        case SX_EVENT_MOUSE_LEAVE:
            hover = pressed = false;
            break;
        case SX_EVENT_MOUSE_DOWN:
            pressed = inside;
            break;
        case SX_EVENT_MOUSE_UP:
            if (pressed && inside)
                clicks++;
            pressed = false;
            break;
        case SX_EVENT_RESIZE:
            break;
        default:
            continue;
        }
        draw(win);
    }

    sx_window_destroy(win);
    sx_disconnect();
    return 0;
}
