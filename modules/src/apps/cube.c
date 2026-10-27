#include <stdio.h>
#include <math.h>
#include <strawx.h>
#include <ui.h>

static const float verts[8][3] = {
    { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
    { -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 },
};

static const int faces[6][4] = {
    { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
    { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 4, 7, 3 },
};

static const uint32_t face_colors[6] = {
    SX_RGB(0xF0, 0xB4, 0x3C), SX_RGB(0x5B, 0x8C, 0xFF), SX_RGB(0x46, 0xC2, 0x7A),
    SX_RGB(0xE5, 0x48, 0x4D), SX_RGB(0xC0, 0x5C, 0xF0), SX_RGB(0x2E, 0xB8, 0xA6),
};

static struct
{
    SxWindow *win;
    float ax, ay;
    float vx, vy;
    bool  paused;
    bool  dragging;
    int   last_x, last_y;
    uint64_t frames, fps_ms;
    int   fps;
}
Q;

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void fill_triangle(SxCanvas *c, const float *a, const float *b, const float *d, uint32_t color)
{
    float area = edge(a[0], a[1], b[0], b[1], d[0], d[1]);
    if (area == 0)
        return;
    if (area < 0)
    {
        const float *t = b;
        b = d;
        d = t;
    }

    int x0 = (int)fminf(a[0], fminf(b[0], d[0])), x1 = (int)fmaxf(a[0], fmaxf(b[0], d[0])) + 1;
    int y0 = (int)fminf(a[1], fminf(b[1], d[1])), y1 = (int)fmaxf(a[1], fmaxf(b[1], d[1])) + 1;
    SxRect r = sx_rect_intersect(sx_rect(x0, y0, x1 - x0, y1 - y0), c->clip);

    for (int y = r.y; y < r.y + r.h; y++)
    {
        float py = y + 0.5f;
        uint32_t *row = c->pixels + y * c->stride;
        for (int x = r.x; x < r.x + r.w; x++)
        {
            float px = x + 0.5f;
            if (edge(a[0], a[1], b[0], b[1], px, py) >= 0 &&
                edge(b[0], b[1], d[0], d[1], px, py) >= 0 &&
                edge(d[0], d[1], a[0], a[1], px, py) >= 0)
                row[x] = color;
        }
    }
}

static uint32_t shade(uint32_t color, float light)
{
    int r = (int)(((color >> 16) & 0xFF) * light);
    int g = (int)(((color >> 8) & 0xFF) * light);
    int b = (int)((color & 0xFF) * light);
    return SX_RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

static void draw(void)
{
    SxCanvas *c = sx_window_canvas(Q.win);
    int w = c->width, h = c->height;

    sx_fill_gradient(c, 0, 0, w, h, SX_RGB(0x22, 0x26, 0x33), SX_RGB(0x12, 0x13, 0x18));

    float sx = sinf(Q.ax), cx = cosf(Q.ax);
    float sy = sinf(Q.ay), cy = cosf(Q.ay);

    float rot[8][3], proj[8][2];
    float scale = (w < h ? w : h) * 0.32f;

    for (int i = 0; i < 8; i++)
    {
        float x = verts[i][0], y = verts[i][1], z = verts[i][2];
        float y1 = y * cx - z * sx, z1 = y * sx + z * cx;
        float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
        rot[i][0] = x2;
        rot[i][1] = y1;
        rot[i][2] = z2;

        float persp = 3.2f / (z2 + 5.0f);
        proj[i][0] = w * 0.5f + x2 * scale * persp;
        proj[i][1] = h * 0.5f + y1 * scale * persp;
    }

    sx_fill_circle(c, w * 0.5f, h * 0.5f + scale * 0.95f, scale * 0.55f, SX_RGBA(0, 0, 0, 40));

    const float light[3] = { -0.4f, -0.7f, -0.6f };

    for (int f = 0; f < 6; f++)
    {
        const int *q = faces[f];
        float *p0 = proj[q[0]], *p1 = proj[q[1]], *p2 = proj[q[2]], *p3 = proj[q[3]];

        if (edge(p0[0], p0[1], p1[0], p1[1], p2[0], p2[1]) >= 0)
            continue;

        float ux = rot[q[1]][0] - rot[q[0]][0], uy = rot[q[1]][1] - rot[q[0]][1], uz = rot[q[1]][2] - rot[q[0]][2];
        float vx = rot[q[3]][0] - rot[q[0]][0], vy = rot[q[3]][1] - rot[q[0]][1], vz = rot[q[3]][2] - rot[q[0]][2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        float lambert = (nx * light[0] + ny * light[1] + nz * light[2]) / len;
        float k = 0.45f + 0.65f * (lambert > 0 ? lambert : 0);

        uint32_t col = shade(face_colors[f], k);
        fill_triangle(c, p0, p1, p2, col);
        fill_triangle(c, p0, p2, p3, col);

        for (int e = 0; e < 4; e++)
        {
            float *a = proj[q[e]], *b = proj[q[(e + 1) % 4]];
            sx_draw_line(c, a[0], a[1], b[0], b[1], 1.6f, sx_lighten(col, 40));
        }
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%d fps%s", Q.fps, Q.paused ? "  (paused)" : "");
    sx_draw_text(c, &sx_font_sans_small, 12, h - 24, buf, SX_THEME_TEXT_DIM);
    const char *hint = "drag to spin  -  space to pause";
    sx_draw_text(c, &sx_font_sans_small, w - 12 - sx_text_width(&sx_font_sans_small, hint), h - 24, hint,
                 SX_THEME_TEXT_DIM);

    sx_window_commit(Q.win);
}

int main(void)
{
    if (sx_connect() != 0)
    {
        printf("cube: cannot connect to the display server\n");
        return 1;
    }

    Q.win = sx_window_create("Cube", 420, 420, SX_WINDOW_RESIZABLE);
    if (!Q.win)
        return 1;

    Q.ax = 0.55f;
    Q.ay = 0.65f;
    Q.vx = 0.011f;
    Q.vy = 0.017f;
    Q.fps_ms = sx_time_ms();

    uint64_t next_frame = sx_time_ms();

    for (;;)
    {
        uint64_t now = sx_time_ms();
        int wait_ms = next_frame > now ? (int)(next_frame - now) : 0;

        SxEvent ev;
        int rc = sx_next_event(&ev, wait_ms);
        if (rc < 0)
            break;

        if (rc > 0)
        {
            switch (ev.type)
            {
                case SX_EVENT_CLOSE:
                case SX_EVENT_DISCONNECTED:
                    goto out;
                case SX_EVENT_MOUSE_DOWN:
                    Q.dragging = true;
                    Q.last_x = ev.x;
                    Q.last_y = ev.y;
                    break;
                case SX_EVENT_MOUSE_UP:
                    Q.dragging = false;
                    break;
                case SX_EVENT_MOUSE_MOVE:
                    if (Q.dragging)
                    {
                        Q.vy = (ev.x - Q.last_x) * 0.01f;
                        Q.vx = (ev.y - Q.last_y) * 0.01f;
                        Q.ay += Q.vy;
                        Q.ax += Q.vx;
                        Q.last_x = ev.x;
                        Q.last_y = ev.y;
                    }
                    break;
                case SX_EVENT_KEY_DOWN:
                    if (ev.key == SX_KEY_SPACE)
                        Q.paused = !Q.paused;
                    break;
                default:
                    break;
            }
            continue;
        }

        next_frame = sx_time_ms() + 16;
        if (!Q.paused && !Q.dragging)
        {
            Q.ax += Q.vx;
            Q.ay += Q.vy;
        }
        draw();

        Q.frames++;
        now = sx_time_ms();
        if (now - Q.fps_ms >= 1000)
        {
            Q.fps = (int)(Q.frames * 1000 / (now - Q.fps_ms));
            Q.frames = 0;
            Q.fps_ms = now;
        }
    }

out:
    sx_window_destroy(Q.win);
    sx_disconnect();
    return 0;
}
