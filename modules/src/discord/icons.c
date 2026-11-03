#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "icons.h"

typedef enum
{
    SH_SEG,
    SH_CIRCLE,
    SH_RING,
    SH_ARC,
    SH_RRECT,
    SH_BOX,
    SH_POLY,
}
ShapeKind;

typedef struct
{
    uint8_t kind;
    bool    cut;
    int     n;
    float   v[20];
}
Shape;

typedef struct
{
    Shape s[20];
    int   n;
    bool  cut;
    float rot_c, rot_s;
    bool  rotated;
}
Ib;

static Shape *push(Ib *b, ShapeKind kind)
{
    Shape *s = &b->s[b->n < 19 ? b->n++ : 19];
    memset(s, 0, sizeof(*s));
    s->kind = (uint8_t)kind;
    s->cut = b->cut;
    return s;
}

static void seg(Ib *b, float x0, float y0, float x1, float y1, float w)
{
    Shape *s = push(b, SH_SEG);
    s->v[0] = x0, s->v[1] = y0, s->v[2] = x1, s->v[3] = y1, s->v[4] = w * 0.5f;
}

static void circle(Ib *b, float cx, float cy, float r)
{
    Shape *s = push(b, SH_CIRCLE);
    s->v[0] = cx, s->v[1] = cy, s->v[2] = r;
}

static void ring(Ib *b, float cx, float cy, float r, float w)
{
    Shape *s = push(b, SH_RING);
    s->v[0] = cx, s->v[1] = cy, s->v[2] = r, s->v[3] = w * 0.5f;
}

static void arc(Ib *b, float cx, float cy, float r, float w, float a0, float sweep)
{
    Shape *s = push(b, SH_ARC);
    float d2r = 3.14159265f / 180.0f;
    s->v[0] = cx, s->v[1] = cy, s->v[2] = r, s->v[3] = w * 0.5f;
    s->v[4] = a0 * d2r, s->v[5] = sweep * d2r;
    s->v[6] = cx + r * cosf(a0 * d2r), s->v[7] = cy + r * sinf(a0 * d2r);
    s->v[8] = cx + r * cosf((a0 + sweep) * d2r), s->v[9] = cy + r * sinf((a0 + sweep) * d2r);
}

static void rrect(Ib *b, float x, float y, float w, float h, float r)
{
    Shape *s = push(b, SH_RRECT);
    s->v[0] = x + w * 0.5f, s->v[1] = y + h * 0.5f, s->v[2] = w * 0.5f, s->v[3] = h * 0.5f, s->v[4] = r;
}

static void box(Ib *b, float x, float y, float w, float h, float r, float lw)
{
    Shape *s = push(b, SH_BOX);
    s->v[0] = x + w * 0.5f, s->v[1] = y + h * 0.5f, s->v[2] = w * 0.5f, s->v[3] = h * 0.5f, s->v[4] = r;
    s->v[5] = lw * 0.5f;
}

static void poly(Ib *b, float round, int n, const float *pts)
{
    Shape *s = push(b, SH_POLY);
    if (n > 9)
        n = 9;
    s->n = n;
    memcpy(s->v, pts, sizeof(float) * 2 * (size_t)n);
    s->v[18] = round;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline float len2(float x, float y)
{
    return sqrtf(x * x + y * y);
}

static float sd_seg(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float l = vx * vx + vy * vy;
    float t = l > 0 ? clampf((wx * vx + wy * vy) / l, 0, 1) : 0;
    return len2(wx - vx * t, wy - vy * t);
}

static float sd_rrect(float px, float py, const float *v)
{
    float r = v[4];
    float qx = fabsf(px - v[0]) - (v[2] - r);
    float qy = fabsf(py - v[1]) - (v[3] - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return len2(ox, oy) + (in < 0 ? in : 0) - r;
}

static float sd_poly(float px, float py, const Shape *s)
{
    const float *v = s->v;
    int n = s->n;
    float d = (px - v[0]) * (px - v[0]) + (py - v[1]) * (py - v[1]);
    float sign = 1;
    for (int i = 0, j = n - 1; i < n; j = i, i++)
    {
        float ex = v[j * 2] - v[i * 2], ey = v[j * 2 + 1] - v[i * 2 + 1];
        float wx = px - v[i * 2], wy = py - v[i * 2 + 1];
        float t = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1);
        float bx = wx - ex * t, by = wy - ey * t;
        float dd = bx * bx + by * by;
        if (dd < d)
            d = dd;
        bool c1 = py >= v[i * 2 + 1], c2 = py < v[j * 2 + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3))
            sign = -sign;
    }
    return sign * sqrtf(d) - v[18];
}

static float sd_shape(const Shape *s, float px, float py)
{
    const float *v = s->v;
    switch (s->kind)
    {
        case SH_SEG:
            return sd_seg(px, py, v[0], v[1], v[2], v[3]) - v[4];
        case SH_CIRCLE:
            return len2(px - v[0], py - v[1]) - v[2];
        case SH_RING:
            return fabsf(len2(px - v[0], py - v[1]) - v[2]) - v[3];
        case SH_ARC:
        {
            float dx = px - v[0], dy = py - v[1];
            float a = atan2f(dy, dx) - v[4];
            const float tau = 6.2831853f;
            while (a < 0)
                a += tau;
            while (a >= tau)
                a -= tau;
            if (a <= v[5])
                return fabsf(len2(dx, dy) - v[2]) - v[3];
            float d0 = len2(px - v[6], py - v[7]);
            float d1 = len2(px - v[8], py - v[9]);
            return (d0 < d1 ? d0 : d1) - v[3];
        }
        case SH_RRECT:
            return sd_rrect(px, py, v);
        case SH_BOX:
            return fabsf(sd_rrect(px, py, v)) - v[5];
        case SH_POLY:
            return sd_poly(px, py, s);
    }
    return 1e9f;
}

static void render(SxCanvas *c, const Ib *b, int x, int y, int size, uint32_t color)
{
    float scale = size / 24.0f;
    float inv = 1.0f / scale;
    SxRect r = sx_rect_intersect(sx_rect(x, y, size, size), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        for (int xx = r.x; xx < r.x + r.w; xx++)
        {
            float px = (xx + 0.5f - x) * inv;
            float py = (yy + 0.5f - y) * inv;
            if (b->rotated)
            {
                float dx = px - 12, dy = py - 12;
                px = 12 + dx * b->rot_c - dy * b->rot_s;
                py = 12 + dx * b->rot_s + dy * b->rot_c;
            }
            float d = 1e9f;
            for (int i = 0; i < b->n; i++)
            {
                float ds = sd_shape(&b->s[i], px, py);
                if (b->s[i].cut)
                    d = d > -ds ? d : -ds;
                else
                    d = d < ds ? d : ds;
            }
            float cov = clampf(0.5f - d * scale, 0, 1);
            if (cov > 0)
                sx_blend_pixel(c, xx, yy, color, (uint32_t)(cov * 255.0f + 0.5f));
        }
    }
}

static void rotate(Ib *b, float deg)
{
    float a = deg * 3.14159265f / 180.0f;
    b->rotated = true;
    b->rot_c = cosf(a);
    b->rot_s = sinf(a);
}

static void slash(Ib *b)
{
    b->cut = true;
    seg(b, 2.5f, 2.5f, 21.5f, 21.5f, 5.0f);
    b->cut = false;
    seg(b, 3.5f, 3.5f, 20.5f, 20.5f, 2.0f);
}

static void handset(Ib *b)
{
    arc(b, 20, 4, 14.2f, 4.2f, 90, 90);
    rrect(b, 3, 3, 5.6f, 7.4f, 1.6f);
    rrect(b, 14, 15.4f, 7.4f, 5.6f, 1.6f);
}

void icon_draw(SxCanvas *c, IconId id, int x, int y, int size, uint32_t color)
{
    Ib b;
    memset(&b, 0, sizeof(b));
    switch (id)
    {
        case ICON_HASH:
            seg(&b, 10, 3.5f, 8, 20.5f, 2);
            seg(&b, 16, 3.5f, 14, 20.5f, 2);
            seg(&b, 4.5f, 9, 20, 9, 2);
            seg(&b, 4, 15, 19.5f, 15, 2);
            break;
        case ICON_SPEAKER:
        {
            const float body[] = { 3, 9, 7, 9, 12.5f, 4.5f, 12.5f, 19.5f, 7, 15, 3, 15 };
            poly(&b, 0.8f, 6, body);
            arc(&b, 12.5f, 12, 3.8f, 2, -55, 110);
            arc(&b, 12.5f, 12, 7.8f, 2, -55, 110);
            break;
        }
        case ICON_MIC:
        case ICON_MIC_OFF:
            rrect(&b, 8.5f, 2, 7, 12.5f, 3.5f);
            arc(&b, 12, 10.5f, 7, 2, 0, 180);
            seg(&b, 12, 17.5f, 12, 21.5f, 2);
            seg(&b, 8.5f, 21.5f, 15.5f, 21.5f, 2);
            if (id == ICON_MIC_OFF)
                slash(&b);
            break;
        case ICON_HEADPHONES:
        case ICON_HEADPHONES_OFF:
            arc(&b, 12, 13, 8.5f, 2, 180, 180);
            seg(&b, 3.5f, 13, 3.5f, 16, 2);
            seg(&b, 20.5f, 13, 20.5f, 16, 2);
            rrect(&b, 2.5f, 13, 6, 8.5f, 2.5f);
            rrect(&b, 15.5f, 13, 6, 8.5f, 2.5f);
            if (id == ICON_HEADPHONES_OFF)
                slash(&b);
            break;
        case ICON_PHONE:
            handset(&b);
            break;
        case ICON_HANGUP:
            rotate(&b, -135);
            handset(&b);
            break;
        case ICON_SCREEN:
        case ICON_SCREEN_STOP:
            box(&b, 2.5f, 3.5f, 19, 13.5f, 2.5f, 2);
            seg(&b, 12, 17, 12, 20.5f, 2);
            seg(&b, 8, 20.5f, 16, 20.5f, 2);
            if (id == ICON_SCREEN)
            {
                seg(&b, 12, 13.5f, 12, 7.5f, 2);
                seg(&b, 9, 10.5f, 12, 7.5f, 2);
                seg(&b, 15, 10.5f, 12, 7.5f, 2);
            }
            else
            {
                seg(&b, 9.5f, 7.75f, 14.5f, 12.75f, 2);
                seg(&b, 14.5f, 7.75f, 9.5f, 12.75f, 2);
            }
            break;
        case ICON_LOGOUT:
            seg(&b, 5.5f, 3.5f, 10, 3.5f, 2);
            seg(&b, 3.5f, 5.5f, 3.5f, 18.5f, 2);
            seg(&b, 5.5f, 20.5f, 10, 20.5f, 2);
            arc(&b, 5.5f, 5.5f, 2, 2, 180, 90);
            arc(&b, 5.5f, 18.5f, 2, 2, 90, 90);
            seg(&b, 9.5f, 12, 20.5f, 12, 2);
            seg(&b, 16, 7.5f, 20.5f, 12, 2);
            seg(&b, 16, 16.5f, 20.5f, 12, 2);
            break;
        case ICON_HOME:
        {
            rrect(&b, 2, 3, 20, 14.5f, 5);
            const float tail[] = { 6, 15, 12, 15, 6.5f, 21.5f };
            poly(&b, 0.6f, 3, tail);
            b.cut = true;
            circle(&b, 7.5f, 10.25f, 1.6f);
            circle(&b, 12, 10.25f, 1.6f);
            circle(&b, 16.5f, 10.25f, 1.6f);
            b.cut = false;
            break;
        }
        case ICON_FILE:
        {
            const float page[] = { 4.5f, 2, 14, 2, 19.5f, 7.5f, 19.5f, 22, 4.5f, 22 };
            poly(&b, 0.8f, 5, page);
            b.cut = true;
            const float fold[] = { 13.5f, 1, 13.5f, 8, 20.5f, 8 };
            poly(&b, 0, 3, fold);
            seg(&b, 8, 13, 16, 13, 1.6f);
            seg(&b, 8, 17, 14, 17, 1.6f);
            b.cut = false;
            seg(&b, 14.5f, 3, 14.5f, 7, 1.2f);
            seg(&b, 14.5f, 7, 18.5f, 7, 1.2f);
            break;
        }
        case ICON_SIGNAL:
            rrect(&b, 3, 14, 4, 7, 1.5f);
            rrect(&b, 10, 9.5f, 4, 11.5f, 1.5f);
            rrect(&b, 17, 4, 4, 17, 1.5f);
            break;
        case ICON_ARROW:
            seg(&b, 4, 12, 19.5f, 12, 2.2f);
            seg(&b, 13.5f, 6, 19.5f, 12, 2.2f);
            seg(&b, 13.5f, 18, 19.5f, 12, 2.2f);
            break;
        case ICON_AT:
            ring(&b, 12, 12, 4, 2);
            seg(&b, 16, 8, 16, 13, 2);
            arc(&b, 19, 13, 3, 2, 0, 180);
            seg(&b, 22, 12, 22, 13, 2);
            arc(&b, 12, 12, 10, 2, 53, 307);
            break;
        case ICON_LOCK:
            rrect(&b, 4, 10, 16, 12, 3);
            arc(&b, 12, 9.5f, 4.5f, 2.2f, 180, 180);
            seg(&b, 7.5f, 9.5f, 7.5f, 11, 2.2f);
            seg(&b, 16.5f, 9.5f, 16.5f, 11, 2.2f);
            b.cut = true;
            circle(&b, 12, 15.5f, 1.8f);
            b.cut = false;
            break;
        case ICON_CLOSE:
            seg(&b, 6, 6, 18, 18, 2.2f);
            seg(&b, 18, 6, 6, 18, 2.2f);
            break;
        case ICON_EYE:
        {
            arc(&b, 12, 22, 14, 2, 225, 90);
            arc(&b, 12, 2, 14, 2, 45, 90);
            circle(&b, 12, 12, 3.4f);
            break;
        }
        case ICON_DOWNLOAD:
            seg(&b, 12, 3, 12, 15, 2);
            seg(&b, 7, 10, 12, 15, 2);
            seg(&b, 17, 10, 12, 15, 2);
            seg(&b, 3, 15, 3, 19, 2);
            arc(&b, 5, 19, 2, 2, 90, 90);
            seg(&b, 5, 21, 19, 21, 2);
            arc(&b, 19, 19, 2, 2, 0, 90);
            seg(&b, 21, 19, 21, 15, 2);
            break;
        case ICON_EXTERNAL:
            seg(&b, 18, 13, 18, 19, 2);
            arc(&b, 16, 19, 2, 2, 0, 90);
            seg(&b, 16, 21, 5, 21, 2);
            arc(&b, 5, 19, 2, 2, 90, 90);
            seg(&b, 3, 19, 3, 8, 2);
            arc(&b, 5, 8, 2, 2, 180, 90);
            seg(&b, 5, 6, 11, 6, 2);
            seg(&b, 15, 3, 21, 3, 2);
            seg(&b, 21, 3, 21, 9, 2);
            seg(&b, 10, 14, 21, 3, 2);
            break;
        case ICON_CHECK:
            seg(&b, 5, 12.5f, 10, 17.5f, 2.2f);
            seg(&b, 10, 17.5f, 19.5f, 7, 2.2f);
            break;
        default:
            return;
    }
    render(c, &b, x, y, size, color);
}

void icon_spinner(SxCanvas *c, int cx, int cy, int radius, float width, float phase, uint32_t color)
{
    Ib b;
    memset(&b, 0, sizeof(b));
    float a0 = phase * 360.0f;
    float sweep = 80.0f + 160.0f * (0.5f + 0.5f * sinf(phase * 6.2831853f * 0.5f));
    float s = 24.0f / (radius * 2 + width + 4);
    arc(&b, 12, 12, radius * s, width * s, a0, sweep);
    int size = radius * 2 + (int)width + 4;
    render(c, &b, cx - size / 2, cy - size / 2, size, color);
}

void icon_arc(SxCanvas *c, float cx, float cy, float r, float width, float a0, float sweep, uint32_t color)
{
    Ib b;
    memset(&b, 0, sizeof(b));
    int size = (int)(2 * (r + width) + 4);
    float s = 24.0f / size;
    int x = (int)(cx - size / 2.0f), y = (int)(cy - size / 2.0f);
    arc(&b, (cx - x) * s, (cy - y) * s, r * s, width * s, a0, sweep);
    render(c, &b, x, y, size, color);
}
