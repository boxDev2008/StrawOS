#include <stdio.h>
#include <math.h>
#include <time.h>
#include <strawx.h>
#include <ui.h>

#define PI 3.14159265358979f

static const char *weekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                "August", "September", "October", "November", "December" };

static void hand(SxCanvas *c, float cx, float cy, float angle, float len, float tail, float width, uint32_t color)
{
    float sx = sinf(angle), cy_ = -cosf(angle);
    sx_draw_line(c, cx - sx * tail, cy - cy_ * tail, cx + sx * len, cy + cy_ * len, width, color);
}

static void draw(SxWindow *win)
{
    SxCanvas *c = sx_window_canvas(win);
    int w = c->width, h = c->height;

    sx_fill_gradient(c, 0, 0, w, h, SX_RGB(0x24, 0x27, 0x30), SX_THEME_SUNKEN);

    struct rtc_time t;
    rtc_time(&t);

    int text_h = 56;
    float cx = w * 0.5f;
    float cy = (h - text_h) * 0.5f + 6;
    float r = (w < h - text_h ? w : h - text_h) * 0.5f - 14;
    if (r < 20)
        r = 20;

    sx_fill_circle(c, cx, cy + 3, r + 2, SX_RGBA(0, 0, 0, 90));
    sx_fill_circle(c, cx, cy, r, SX_RGB(0x2E, 0x32, 0x3D));
    sx_draw_circle(c, cx, cy, r - 0.5f, 1.2f, SX_RGBA(255, 255, 255, 30));

    for (int i = 0; i < 60; i++)
    {
        float a = i * PI / 30.0f;
        bool major = i % 5 == 0;
        float r0 = r - (major ? r * 0.13f : r * 0.06f);
        float r1 = r - r * 0.04f;
        sx_draw_line(c, cx + sinf(a) * r0, cy - cosf(a) * r0, cx + sinf(a) * r1, cy - cosf(a) * r1,
                     major ? 2.4f : 1.0f, major ? SX_THEME_TEXT : SX_RGBA(255, 255, 255, 90));
    }

    float sec = (float)t.second;
    float min = t.minute + sec / 60.0f;
    float hour = (t.hour % 12) + min / 60.0f;

    hand(c, cx, cy, hour * PI / 6.0f, r * 0.50f, r * 0.08f, r * 0.055f + 1.5f, SX_THEME_TEXT);
    hand(c, cx, cy, min * PI / 30.0f, r * 0.75f, r * 0.08f, r * 0.035f + 1.2f, SX_THEME_TEXT);
    hand(c, cx, cy, sec * PI / 30.0f, r * 0.82f, r * 0.16f, 1.5f, SX_THEME_ACCENT);
    sx_fill_circle(c, cx, cy, r * 0.04f + 2, SX_THEME_ACCENT);
    sx_fill_circle(c, cx, cy, r * 0.015f + 1, SX_RGB(0x2E, 0x32, 0x3D));

    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
    sx_draw_text_centered(c, &sx_font_sans_large, sx_rect(0, h - text_h - 4, w, 30), buf, SX_THEME_TEXT);
    snprintf(buf, sizeof(buf), "%s, %d %s %d", weekdays[t.weekday % 7], t.day, months[(t.month + 11) % 12], t.year);
    sx_draw_text_centered(c, &sx_font_sans_small, sx_rect(0, h - 30, w, 20), buf, SX_THEME_TEXT_DIM);

    sx_window_commit(win);
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("clock: cannot connect to the display server\n");
        return 1;
    }

    SxWindow *win = sx_window_create("Clock", 280, 330, SX_WINDOW_RESIZABLE);
    if (!win)
        return 1;

    draw(win);
    int last_second = -1;

    for (;;)
    {
        SxEvent ev;
        int rc = sx_next_event(&ev, 200);
        if (rc < 0)
            break;

        if (rc > 0)
        {
            if (ev.type == SX_EVENT_CLOSE || ev.type == SX_EVENT_DISCONNECTED)
                break;
            if (ev.type == SX_EVENT_RESIZE)
                draw(win);
            continue;
        }

        struct rtc_time t;
        rtc_time(&t);
        if (t.second != last_second)
        {
            last_second = t.second;
            draw(win);
        }
    }

    sx_window_destroy(win);
    sx_disconnect();
    return 0;
}
