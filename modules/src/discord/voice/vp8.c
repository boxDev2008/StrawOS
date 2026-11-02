#include <stdlib.h>
#include <string.h>

#include "vp8.h"
#include "vp8_tables.h"

enum
{
    DC_PRED,
    V_PRED,
    H_PRED,
    TM_PRED,
};

#define MB_INTER 0x10
#define MB_SKIP  0x20

typedef struct
{
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    uint32_t low;
    uint32_t range;
    int      count;
}
BoolEnc;

struct Vp8Enc
{
    int      w, h;
    int      mbw, mbh;
    int      ys, uvs;
    uint8_t *src[3];
    uint8_t *prev[3];
    uint8_t *rec[3];
    uint8_t *ref[3];
    uint8_t *still;
    uint8_t *still_q;
    uint8_t *modes;
    uint8_t (*above)[9];
    uint8_t  left[9];
    bool     have_ref;
    int      dq_y1ac, dq_y2dc, dq_y2ac, dq_uvdc, dq_uvac;
    uint8_t *tokens;
    size_t   tokens_cap;
};

static const uint8_t zigzag[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };
static const uint8_t bands[17] = { 0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0 };
static const uint8_t left_ctx_index[25] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
static const uint8_t above_ctx_index[25] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 4, 5, 6, 7, 6, 7, 8 };

static const uint8_t cat_probs[6][12] = {
    { 159, 0 },
    { 165, 145, 0 },
    { 173, 148, 140, 0 },
    { 176, 155, 140, 135, 0 },
    { 180, 157, 141, 134, 130, 0 },
    { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 },
};
static const int cat_base[6] = { 5, 7, 11, 19, 35, 67 };

static void be_init(BoolEnc *b, uint8_t *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->pos = 0;
    b->low = 0;
    b->range = 255;
    b->count = -24;
}

static void be_put(BoolEnc *b, int bit, int prob)
{
    uint32_t split = 1 + (((b->range - 1) * (uint32_t)prob) >> 8);
    uint32_t range = split;
    uint32_t low = b->low;
    if (bit)
    {
        low += split;
        range = b->range - split;
    }
    int shift = __builtin_clz(range) - 24;
    range <<= shift;
    int count = b->count + shift;
    if (count >= 0)
    {
        int offset = shift - count;
        if ((low << (offset - 1)) & 0x80000000u)
        {
            size_t x = b->pos;
            while (x > 0 && x <= b->cap && b->buf[x - 1] == 0xFF)
            {
                b->buf[x - 1] = 0;
                x--;
            }
            if (x > 0 && x <= b->cap)
                b->buf[x - 1]++;
        }
        if (b->pos < b->cap)
            b->buf[b->pos] = (uint8_t)(low >> (24 - offset));
        b->pos++;
        low <<= offset;
        shift = count;
        low &= 0xFFFFFF;
        count -= 8;
    }
    low <<= shift;
    b->count = count;
    b->low = low;
    b->range = range;
}

static void be_literal(BoolEnc *b, uint32_t v, int bits)
{
    while (bits-- > 0)
        be_put(b, (int)((v >> bits) & 1), 128);
}

static void be_flush(BoolEnc *b)
{
    for (int i = 0; i < 32; i++)
        be_put(b, 0, 128);
}

static bool be_ok(const BoolEnc *b)
{
    return b->pos <= b->cap;
}

Vp8Enc *vp8_new(int width, int height)
{
    if (width < 16 || height < 16 || width > 16383 || height > 16383)
        return NULL;
    Vp8Enc *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->w = width;
    e->h = height;
    e->mbw = (width + 15) / 16;
    e->mbh = (height + 15) / 16;
    e->ys = e->mbw * 16;
    e->uvs = e->mbw * 8;
    size_t ysize = (size_t)e->ys * (size_t)e->mbh * 16;
    size_t uvsize = (size_t)e->uvs * (size_t)e->mbh * 8;
    size_t mbs = (size_t)e->mbw * (size_t)e->mbh;
    bool ok = true;
    for (int p = 0; p < 3; p++)
    {
        size_t n = p ? uvsize : ysize;
        e->src[p] = calloc(1, n);
        e->prev[p] = calloc(1, n);
        e->rec[p] = calloc(1, n);
        e->ref[p] = calloc(1, n);
        ok = ok && e->src[p] && e->prev[p] && e->rec[p] && e->ref[p];
    }
    e->still = calloc(1, mbs);
    e->still_q = calloc(1, mbs);
    e->modes = calloc(1, mbs);
    e->above = calloc((size_t)e->mbw, sizeof(*e->above));
    e->tokens_cap = ysize * 2 + 4096;
    e->tokens = malloc(e->tokens_cap);
    if (!ok || !e->still || !e->still_q || !e->modes || !e->above || !e->tokens)
    {
        vp8_free(e);
        return NULL;
    }
    return e;
}

void vp8_free(Vp8Enc *e)
{
    if (!e)
        return;
    for (int p = 0; p < 3; p++)
    {
        free(e->src[p]);
        free(e->prev[p]);
        free(e->rec[p]);
        free(e->ref[p]);
    }
    free(e->still);
    free(e->still_q);
    free(e->modes);
    free(e->above);
    free(e->tokens);
    free(e);
}

Vp8Planes vp8_input(Vp8Enc *e)
{
    return (Vp8Planes){ e->src[0], e->src[1], e->src[2], e->ys, e->uvs };
}

Vp8Planes vp8_recon(Vp8Enc *e)
{
    return (Vp8Planes){ e->ref[0], e->ref[1], e->ref[2], e->ys, e->uvs };
}

static void pad_plane(uint8_t *p, int stride, int w, int h, int pw, int ph)
{
    if (w < pw)
        for (int y = 0; y < h; y++)
            memset(p + (size_t)y * stride + w, p[(size_t)y * stride + w - 1], (size_t)(pw - w));
    for (int y = h; y < ph; y++)
        memcpy(p + (size_t)y * stride, p + (size_t)(h - 1) * stride, (size_t)pw);
}

static void fdct4x4(const int16_t *in, int16_t *out)
{
    int t[16];
    for (int r = 0; r < 4; r++)
    {
        const int16_t *y = in + r * 4;
        int a = y[0] + y[3], b = y[1] + y[2], c = y[1] - y[2], d = y[0] - y[3];
        t[r * 4 + 0] = (a + b) * 8;
        t[r * 4 + 2] = (a - b) * 8;
        t[r * 4 + 1] = (d * 5352 + c * 2217) * 8 / 4096;
        t[r * 4 + 3] = (d * 2217 - c * 5352) * 8 / 4096;
    }
    for (int k = 0; k < 4; k++)
    {
        int y0 = t[k], y1 = t[4 + k], y2 = t[8 + k], y3 = t[12 + k];
        int a = y0 + y3, b = y1 + y2, c = y1 - y2, d = y0 - y3;
        out[k] = (int16_t)((a + b + 8) >> 4);
        out[8 + k] = (int16_t)((a - b + 8) >> 4);
        out[4 + k] = (int16_t)((d * 5352 + c * 2217 + (1 << 15)) >> 16);
        out[12 + k] = (int16_t)((d * 2217 - c * 5352 + (1 << 15)) >> 16);
    }
}

static void fwht4x4(const int16_t *in, int16_t *out)
{
    int t[16];
    for (int r = 0; r < 4; r++)
    {
        const int16_t *y = in + r * 4;
        t[r * 4 + 0] = y[0] + y[1] + y[2] + y[3];
        t[r * 4 + 1] = y[0] + y[1] - y[2] - y[3];
        t[r * 4 + 2] = y[0] - y[1] - y[2] + y[3];
        t[r * 4 + 3] = y[0] - y[1] + y[2] - y[3];
    }
    for (int k = 0; k < 4; k++)
    {
        int y0 = t[k], y1 = t[4 + k], y2 = t[8 + k], y3 = t[12 + k];
        int v[4] = { y0 + y1 + y2 + y3, y0 + y1 - y2 - y3, y0 - y1 - y2 + y3, y0 - y1 + y2 - y3 };
        for (int i = 0; i < 4; i++)
            out[i * 4 + k] = (int16_t)(v[i] >= 0 ? (v[i] + 1) >> 1 : -((-v[i] + 1) >> 1));
    }
}

static void iwht4x4(const int16_t *in, int16_t *out)
{
    int16_t tmp[16];
    for (int i = 0; i < 4; i++)
    {
        int a1 = in[i] + in[12 + i];
        int b1 = in[4 + i] + in[8 + i];
        int c1 = in[4 + i] - in[8 + i];
        int d1 = in[i] - in[12 + i];
        tmp[i] = (int16_t)(a1 + b1);
        tmp[4 + i] = (int16_t)(c1 + d1);
        tmp[8 + i] = (int16_t)(a1 - b1);
        tmp[12 + i] = (int16_t)(d1 - c1);
    }
    for (int i = 0; i < 4; i++)
    {
        const int16_t *ip = tmp + i * 4;
        int a1 = ip[0] + ip[3];
        int b1 = ip[1] + ip[2];
        int c1 = ip[1] - ip[2];
        int d1 = ip[0] - ip[3];
        out[i * 4 + 0] = (int16_t)((a1 + b1 + 3) >> 3);
        out[i * 4 + 1] = (int16_t)((c1 + d1 + 3) >> 3);
        out[i * 4 + 2] = (int16_t)((a1 - b1 + 3) >> 3);
        out[i * 4 + 3] = (int16_t)((d1 - c1 + 3) >> 3);
    }
}

static uint8_t clamp255(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void idct_add(const int16_t *in, uint8_t *dst, int stride)
{
    static const int c = 20091, s = 35468;
    int16_t tmp[16];
    for (int i = 0; i < 4; i++)
    {
        int a1 = in[i] + in[8 + i];
        int b1 = in[i] - in[8 + i];
        int t1 = (in[4 + i] * s) >> 16;
        int t2 = in[12 + i] + ((in[12 + i] * c) >> 16);
        int c1 = t1 - t2;
        t1 = in[4 + i] + ((in[4 + i] * c) >> 16);
        t2 = (in[12 + i] * s) >> 16;
        int d1 = t1 + t2;
        tmp[i] = (int16_t)(a1 + d1);
        tmp[12 + i] = (int16_t)(a1 - d1);
        tmp[4 + i] = (int16_t)(b1 + c1);
        tmp[8 + i] = (int16_t)(b1 - c1);
    }
    for (int i = 0; i < 4; i++)
    {
        const int16_t *ip = tmp + i * 4;
        int a1 = ip[0] + ip[2];
        int b1 = ip[0] - ip[2];
        int t1 = (ip[1] * s) >> 16;
        int t2 = ip[3] + ((ip[3] * c) >> 16);
        int c1 = t1 - t2;
        t1 = ip[1] + ((ip[1] * c) >> 16);
        t2 = (ip[3] * s) >> 16;
        int d1 = t1 + t2;
        int16_t o[4] = { (int16_t)((a1 + d1 + 4) >> 3), (int16_t)((b1 + c1 + 4) >> 3),
                         (int16_t)((b1 - c1 + 4) >> 3), (int16_t)((a1 - d1 + 4) >> 3) };
        uint8_t *d = dst + i * stride;
        for (int j = 0; j < 4; j++)
            d[j] = clamp255(d[j] + o[j]);
    }
}

static int16_t quantize(int v, int step, int bias16)
{
    int a = v < 0 ? -v : v;
    int q = (a + ((step * bias16) >> 4)) / step;
    if (q > 2048)
        q = 2048;
    return (int16_t)(v < 0 ? -q : q);
}

typedef struct
{
    uint8_t above[16];
    uint8_t left[16];
    uint8_t corner;
    bool    has_above;
    bool    has_left;
}
Edges;

static void get_edges(const uint8_t *p, int stride, int n, int mx, int my, Edges *e)
{
    e->has_above = my > 0;
    e->has_left = mx > 0;
    if (e->has_above)
        memcpy(e->above, p - stride, (size_t)n);
    else
        memset(e->above, 127, (size_t)n);
    for (int i = 0; i < n; i++)
        e->left[i] = e->has_left ? p[(size_t)i * stride - 1] : 129;
    e->corner = !e->has_above ? 127 : !e->has_left ? 129 : p[-stride - 1];
}

static bool mode_allowed(int mode, const Edges *e)
{
    switch (mode)
    {
        case V_PRED:
            return e->has_above;
        case H_PRED:
            return e->has_left;
        case TM_PRED:
            return e->has_above && e->has_left;
        default:
            return true;
    }
}

static void predict(int mode, int n, const Edges *e, uint8_t *out)
{
    switch (mode)
    {
        case DC_PRED:
        {
            int sum = 0, shift = n == 16 ? 3 : 2, v = 128;
            if (e->has_above)
                for (int i = 0; i < n; i++)
                    sum += e->above[i];
            if (e->has_left)
                for (int i = 0; i < n; i++)
                    sum += e->left[i];
            shift += e->has_above + e->has_left;
            if (e->has_above || e->has_left)
                v = (sum + (1 << (shift - 1))) >> shift;
            memset(out, v, (size_t)n * n);
            break;
        }
        case V_PRED:
            for (int r = 0; r < n; r++)
                memcpy(out + r * n, e->above, (size_t)n);
            break;
        case H_PRED:
            for (int r = 0; r < n; r++)
                memset(out + r * n, e->left[r], (size_t)n);
            break;
        default:
            for (int r = 0; r < n; r++)
                for (int c = 0; c < n; c++)
                    out[r * n + c] = clamp255(e->left[r] + e->above[c] - e->corner);
            break;
    }
}

static int sad(const uint8_t *a, int as, const uint8_t *b, int bs, int n)
{
    int s = 0;
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
        {
            int d = a[r * as + c] - b[r * bs + c];
            s += d < 0 ? -d : d;
        }
    return s;
}

static int best_mode(const uint8_t *src, int stride, int n, const Edges *e, uint8_t *pred)
{
    uint8_t tmp[256];
    int best = -1, best_sad = 0;
    for (int m = DC_PRED; m <= TM_PRED; m++)
    {
        if (!mode_allowed(m, e))
            continue;
        predict(m, n, e, tmp);
        int s = sad(src, stride, tmp, n, n);
        if (best < 0 || s < best_sad)
        {
            best = m;
            best_sad = s;
            memcpy(pred, tmp, (size_t)n * n);
        }
    }
    return best;
}

static void put_token(BoolEnc *b, const uint8_t *p, int v)
{
    if (v == 0)
    {
        be_put(b, 0, p[1]);
        return;
    }
    be_put(b, 1, p[1]);
    if (v == 1)
    {
        be_put(b, 0, p[2]);
        return;
    }
    be_put(b, 1, p[2]);
    if (v <= 4)
    {
        be_put(b, 0, p[3]);
        if (v == 2)
            be_put(b, 0, p[4]);
        else
        {
            be_put(b, 1, p[4]);
            be_put(b, v == 4, p[5]);
        }
        return;
    }
    be_put(b, 1, p[3]);
    int cat;
    if (v <= 10)
    {
        be_put(b, 0, p[6]);
        cat = v <= 6 ? 0 : 1;
        be_put(b, cat, p[7]);
    }
    else
    {
        be_put(b, 1, p[6]);
        if (v <= 34)
        {
            be_put(b, 0, p[8]);
            cat = v <= 18 ? 2 : 3;
            be_put(b, cat == 3, p[9]);
        }
        else
        {
            be_put(b, 1, p[8]);
            cat = v <= 66 ? 4 : 5;
            be_put(b, cat == 5, p[10]);
        }
    }
    const uint8_t *cp = cat_probs[cat];
    int bits = 0;
    while (cp[bits])
        bits++;
    int extra = v - cat_base[cat];
    for (int i = 0; i < bits; i++)
        be_put(b, (extra >> (bits - 1 - i)) & 1, cp[i]);
}

static int put_block(BoolEnc *b, int type, const int16_t *q, int first, int ctx)
{
    const uint8_t (*probs)[3][11] = vp8_default_coef_probs[type];
    int eob = first;
    for (int i = first; i < 16; i++)
        if (q[zigzag[i]])
            eob = i + 1;
    bool prev_zero = false;
    for (int i = first; i < eob; i++)
    {
        const uint8_t *p = probs[bands[i]][ctx];
        int v = q[zigzag[i]];
        int a = v < 0 ? -v : v;
        if (!prev_zero)
            be_put(b, 1, p[0]);
        put_token(b, p, a > 2114 ? 2114 : a);
        if (a)
            be_put(b, v < 0, 128);
        prev_zero = a == 0;
        ctx = a == 0 ? 0 : a == 1 ? 1 : 2;
    }
    if (eob < 16)
        be_put(b, 0, probs[bands[eob]][ctx][0]);
    return eob > first;
}

static bool mb_unchanged(Vp8Enc *e, int mx, int my)
{
    for (int p = 0; p < 3; p++)
    {
        int n = p ? 8 : 16, stride = p ? e->uvs : e->ys;
        size_t off = (size_t)my * n * stride + (size_t)mx * n;
        for (int r = 0; r < n; r++)
            if (memcmp(e->src[p] + off + (size_t)r * stride, e->prev[p] + off + (size_t)r * stride, (size_t)n))
                return false;
    }
    return true;
}

static void copy_block(uint8_t *dst, int ds, const uint8_t *src, int ss, int n)
{
    for (int r = 0; r < n; r++)
        memcpy(dst + (size_t)r * ds, src + (size_t)r * ss, (size_t)n);
}

static void encode_mb(Vp8Enc *e, int mx, int my, bool key, int q, BoolEnc *tok)
{
    int idx = my * e->mbw + mx;
    size_t yoff = (size_t)my * 16 * e->ys + (size_t)mx * 16;
    size_t uvoff = (size_t)my * 8 * e->uvs + (size_t)mx * 8;
    const uint8_t *src[3] = { e->src[0] + yoff, e->src[1] + uvoff, e->src[2] + uvoff };
    uint8_t *rec[3] = { e->rec[0] + yoff, e->rec[1] + uvoff, e->rec[2] + uvoff };
    const uint8_t *ref[3] = { e->ref[0] + yoff, e->ref[1] + uvoff, e->ref[2] + uvoff };
    uint8_t *above = e->above[mx];

    if (!key && e->still[idx] && q >= e->still_q[idx] && mb_unchanged(e, mx, my))
    {
        copy_block(rec[0], e->ys, ref[0], e->ys, 16);
        copy_block(rec[1], e->uvs, ref[1], e->uvs, 8);
        copy_block(rec[2], e->uvs, ref[2], e->uvs, 8);
        e->modes[idx] = MB_INTER | MB_SKIP;
        memset(above, 0, 9);
        memset(e->left, 0, 9);
        return;
    }

    uint8_t py[256], pu[64], pv[64];
    Edges ey, eu, ev;
    get_edges(rec[0], e->ys, 16, mx, my, &ey);
    get_edges(rec[1], e->uvs, 8, mx, my, &eu);
    get_edges(rec[2], e->uvs, 8, mx, my, &ev);
    int ymode = best_mode(src[0], e->ys, 16, &ey, py);
    int intra_cost = sad(src[0], e->ys, py, 16, 16);

    uint8_t tu[64], tv[64];
    int uvmode = -1, uv_cost = 0;
    for (int m = DC_PRED; m <= TM_PRED; m++)
    {
        if (!mode_allowed(m, &eu))
            continue;
        predict(m, 8, &eu, tu);
        predict(m, 8, &ev, tv);
        int s = sad(src[1], e->uvs, tu, 8, 8) + sad(src[2], e->uvs, tv, 8, 8);
        if (uvmode < 0 || s < uv_cost)
        {
            uvmode = m;
            uv_cost = s;
            memcpy(pu, tu, 64);
            memcpy(pv, tv, 64);
        }
    }
    intra_cost += uv_cost;

    bool inter = false;
    if (!key)
    {
        int inter_cost = sad(src[0], e->ys, ref[0], e->ys, 16) + sad(src[1], e->uvs, ref[1], e->uvs, 8) +
                         sad(src[2], e->uvs, ref[2], e->uvs, 8);
        if (inter_cost <= intra_cost + 384)
        {
            inter = true;
            copy_block(py, 16, ref[0], e->ys, 16);
            copy_block(pu, 8, ref[1], e->uvs, 8);
            copy_block(pv, 8, ref[2], e->uvs, 8);
        }
    }

    int16_t coef[25][16], qc[25][16];
    int16_t resid[16];
    for (int b = 0; b < 16; b++)
    {
        int bx = (b & 3) * 4, by = (b >> 2) * 4;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                resid[r * 4 + c] = (int16_t)(src[0][(size_t)(by + r) * e->ys + bx + c] - py[(by + r) * 16 + bx + c]);
        fdct4x4(resid, coef[b]);
    }
    for (int b = 0; b < 8; b++)
    {
        const uint8_t *s = b < 4 ? src[1] : src[2];
        const uint8_t *p = b < 4 ? pu : pv;
        int bx = (b & 1) * 4, by = ((b >> 1) & 1) * 4;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                resid[r * 4 + c] = (int16_t)(s[(size_t)(by + r) * e->uvs + bx + c] - p[(by + r) * 8 + bx + c]);
        fdct4x4(resid, coef[16 + b]);
    }
    int16_t dcs[16];
    for (int b = 0; b < 16; b++)
        dcs[b] = coef[b][0];
    fwht4x4(dcs, coef[24]);

    bool any = false;
    for (int i = 0; i < 16; i++)
    {
        qc[24][i] = quantize(coef[24][i], i ? e->dq_y2ac : e->dq_y2dc, 8);
        any |= qc[24][i] != 0;
    }
    for (int b = 0; b < 16; b++)
    {
        qc[b][0] = 0;
        for (int i = 1; i < 16; i++)
        {
            qc[b][i] = quantize(coef[b][i], e->dq_y1ac, 6);
            any |= qc[b][i] != 0;
        }
    }
    for (int b = 16; b < 24; b++)
        for (int i = 0; i < 16; i++)
        {
            qc[b][i] = quantize(coef[b][i], i ? e->dq_uvac : e->dq_uvdc, i ? 6 : 8);
            any |= qc[b][i] != 0;
        }

    copy_block(rec[0], e->ys, py, 16, 16);
    copy_block(rec[1], e->uvs, pu, 8, 8);
    copy_block(rec[2], e->uvs, pv, 8, 8);

    if (any)
    {
        int16_t dq[16], ydc[16];
        for (int i = 0; i < 16; i++)
            dq[i] = (int16_t)(qc[24][i] * (i ? e->dq_y2ac : e->dq_y2dc));
        iwht4x4(dq, ydc);
        for (int b = 0; b < 16; b++)
        {
            dq[0] = ydc[b];
            for (int i = 1; i < 16; i++)
                dq[i] = (int16_t)(qc[b][i] * e->dq_y1ac);
            idct_add(dq, rec[0] + (size_t)((b >> 2) * 4) * e->ys + (b & 3) * 4, e->ys);
        }
        for (int b = 16; b < 24; b++)
        {
            for (int i = 0; i < 16; i++)
                dq[i] = (int16_t)(qc[b][i] * (i ? e->dq_uvac : e->dq_uvdc));
            int k = b - 16;
            uint8_t *base = k < 4 ? rec[1] : rec[2];
            idct_add(dq, base + (size_t)(((k >> 1) & 1) * 4) * e->uvs + (k & 1) * 4, e->uvs);
        }

        int ctx = above[8] + e->left[8];
        above[8] = e->left[8] = (uint8_t)put_block(tok, 1, qc[24], 0, ctx);
        for (int b = 0; b < 16; b++)
        {
            uint8_t *a = &above[above_ctx_index[b]], *l = &e->left[left_ctx_index[b]];
            *a = *l = (uint8_t)put_block(tok, 0, qc[b], 1, *a + *l);
        }
        for (int b = 16; b < 24; b++)
        {
            uint8_t *a = &above[above_ctx_index[b]], *l = &e->left[left_ctx_index[b]];
            *a = *l = (uint8_t)put_block(tok, 2, qc[b], 0, *a + *l);
        }
    }
    else
    {
        memset(above, 0, 9);
        memset(e->left, 0, 9);
    }

    e->modes[idx] = (uint8_t)(ymode | uvmode << 2 | (inter ? MB_INTER : 0) | (any ? 0 : MB_SKIP));
    if (inter)
        e->modes[idx] = (uint8_t)(MB_INTER | (any ? 0 : MB_SKIP));
    e->still[idx] = !any;
    e->still_q[idx] = (uint8_t)q;
}

static void put_ymode(BoolEnc *b, bool key, int mode)
{
    if (key)
    {
        be_put(b, 1, 145);
        if (mode == DC_PRED || mode == V_PRED)
        {
            be_put(b, 0, 156);
            be_put(b, mode == V_PRED, 163);
        }
        else
        {
            be_put(b, 1, 156);
            be_put(b, mode == TM_PRED, 128);
        }
        return;
    }
    if (mode == DC_PRED)
    {
        be_put(b, 0, 112);
        return;
    }
    be_put(b, 1, 112);
    if (mode == V_PRED || mode == H_PRED)
    {
        be_put(b, 0, 86);
        be_put(b, mode == H_PRED, 140);
    }
    else
    {
        be_put(b, 1, 86);
        be_put(b, 0, 37);
    }
}

static void put_uvmode(BoolEnc *b, bool key, int mode)
{
    const uint8_t *p = key ? (const uint8_t[]){ 142, 114, 183 } : (const uint8_t[]){ 162, 101, 204 };
    be_put(b, mode != DC_PRED, p[0]);
    if (mode == DC_PRED)
        return;
    be_put(b, mode != V_PRED, p[1]);
    if (mode == V_PRED)
        return;
    be_put(b, mode == TM_PRED, p[2]);
}

static int prob_of(int zeros, int total)
{
    if (total == 0)
        return 128;
    int p = (zeros * 256 + total / 2) / total;
    return p < 1 ? 1 : p > 255 ? 255 : p;
}

static bool is_inter_at(const Vp8Enc *e, int mx, int my)
{
    if (mx < 0 || my < 0)
        return false;
    return (e->modes[my * e->mbw + mx] & MB_INTER) != 0;
}

static void set_quant(Vp8Enc *e, int q)
{
    e->dq_y1ac = vp8_ac_q[q];
    e->dq_y2dc = vp8_dc_q[q] * 2;
    e->dq_y2ac = vp8_ac_q[q] * 155 / 100;
    if (e->dq_y2ac < 8)
        e->dq_y2ac = 8;
    e->dq_uvdc = vp8_dc_q[q] > 132 ? 132 : vp8_dc_q[q];
    e->dq_uvac = vp8_ac_q[q];
}

size_t vp8_encode(Vp8Enc *e, bool key, int q, uint8_t *out, size_t cap)
{
    if (q < 0)
        q = 0;
    if (q > 127)
        q = 127;
    if (!e->have_ref)
        key = true;
    size_t head = key ? 10 : 3;
    if (cap < head + 64)
        return 0;

    pad_plane(e->src[0], e->ys, e->w, e->h, e->ys, e->mbh * 16);
    pad_plane(e->src[1], e->uvs, (e->w + 1) / 2, (e->h + 1) / 2, e->uvs, e->mbh * 8);
    pad_plane(e->src[2], e->uvs, (e->w + 1) / 2, (e->h + 1) / 2, e->uvs, e->mbh * 8);
    set_quant(e, q);

    BoolEnc tok;
    be_init(&tok, e->tokens, e->tokens_cap);
    memset(e->above, 0, sizeof(*e->above) * (size_t)e->mbw);
    for (int my = 0; my < e->mbh; my++)
    {
        memset(e->left, 0, sizeof(e->left));
        for (int mx = 0; mx < e->mbw; mx++)
            encode_mb(e, mx, my, key, q, &tok);
    }
    be_flush(&tok);

    int mbs = e->mbw * e->mbh, skips = 0, inters = 0;
    for (int i = 0; i < mbs; i++)
    {
        skips += (e->modes[i] & MB_SKIP) != 0;
        inters += (e->modes[i] & MB_INTER) != 0;
    }
    int prob_skip = prob_of(mbs - skips, mbs);
    int prob_intra = prob_of(mbs - inters, mbs);

    BoolEnc h;
    be_init(&h, out + head, cap - head);
    if (key)
    {
        be_literal(&h, 0, 1);
        be_literal(&h, 0, 1);
    }
    be_literal(&h, 0, 1);
    be_literal(&h, 0, 1);
    be_literal(&h, 0, 6);
    be_literal(&h, 0, 3);
    be_literal(&h, 0, 1);
    be_literal(&h, 0, 2);
    be_literal(&h, (uint32_t)q, 7);
    for (int i = 0; i < 5; i++)
        be_literal(&h, 0, 1);
    if (key)
        be_literal(&h, 1, 1);
    else
    {
        be_literal(&h, 0, 1);
        be_literal(&h, 0, 1);
        be_literal(&h, 0, 2);
        be_literal(&h, 0, 2);
        be_literal(&h, 0, 1);
        be_literal(&h, 0, 1);
        be_literal(&h, 1, 1);
        be_literal(&h, 1, 1);
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            for (int k = 0; k < 3; k++)
                for (int l = 0; l < 11; l++)
                    be_put(&h, 0, vp8_coef_update_probs[i][j][k][l]);
    be_literal(&h, 1, 1);
    be_literal(&h, (uint32_t)prob_skip, 8);
    if (!key)
    {
        be_literal(&h, (uint32_t)prob_intra, 8);
        be_literal(&h, 255, 8);
        be_literal(&h, 128, 8);
        be_literal(&h, 0, 1);
        be_literal(&h, 0, 1);
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 19; j++)
                be_put(&h, 0, vp8_mv_update_probs[i][j]);
    }

    for (int my = 0; my < e->mbh; my++)
        for (int mx = 0; mx < e->mbw; mx++)
        {
            uint8_t m = e->modes[my * e->mbw + mx];
            be_put(&h, (m & MB_SKIP) != 0, prob_skip);
            if (!key)
                be_put(&h, (m & MB_INTER) != 0, prob_intra);
            if (m & MB_INTER)
            {
                be_put(&h, 0, 255);
                int cnt = (is_inter_at(e, mx, my - 1) ? 2 : 0) + (is_inter_at(e, mx - 1, my) ? 2 : 0) +
                          (is_inter_at(e, mx - 1, my - 1) ? 1 : 0);
                be_put(&h, 0, vp8_mode_contexts[cnt][0]);
            }
            else
            {
                put_ymode(&h, key, m & 3);
                put_uvmode(&h, key, (m >> 2) & 3);
            }
        }
    be_flush(&h);

    if (!be_ok(&h) || !be_ok(&tok) || h.pos >= (1u << 19) || head + h.pos + tok.pos > cap)
        return 0;

    uint32_t tag = (key ? 0u : 1u) | (1u << 4) | ((uint32_t)h.pos << 5);
    out[0] = (uint8_t)tag;
    out[1] = (uint8_t)(tag >> 8);
    out[2] = (uint8_t)(tag >> 16);
    if (key)
    {
        out[3] = 0x9D;
        out[4] = 0x01;
        out[5] = 0x2A;
        out[6] = (uint8_t)e->w;
        out[7] = (uint8_t)(e->w >> 8);
        out[8] = (uint8_t)e->h;
        out[9] = (uint8_t)(e->h >> 8);
    }
    memcpy(out + head + h.pos, e->tokens, tok.pos);

    for (int p = 0; p < 3; p++)
    {
        uint8_t *t = e->ref[p];
        e->ref[p] = e->rec[p];
        e->rec[p] = t;
        memcpy(e->prev[p], e->src[p], p ? (size_t)e->uvs * e->mbh * 8 : (size_t)e->ys * e->mbh * 16);
    }
    e->have_ref = true;
    return head + h.pos + tok.pos;
}
