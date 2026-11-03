#include <stdlib.h>
#include <string.h>

#include "vp8dec.h"
#include "vp8_tables.h"

enum
{
    DC_PRED,
    V_PRED,
    H_PRED,
    TM_PRED,
    B_PRED,
    NEARESTMV,
    NEARMV,
    ZEROMV,
    NEWMV,
    SPLITMV,
};

enum
{
    B_DC_PRED,
    B_TM_PRED,
    B_VE_PRED,
    B_HE_PRED,
    B_LD_PRED,
    B_RD_PRED,
    B_VR_PRED,
    B_VL_PRED,
    B_HD_PRED,
    B_HU_PRED,
    LEFT4X4,
    ABOVE4X4,
    ZERO4X4,
    NEW4X4,
};

enum
{
    CURRENT,
    LAST,
    GOLDEN,
    ALTREF,
};

enum
{
    Q_Y1,
    Q_Y2,
    Q_UV,
};

typedef union
{
    struct
    {
        int16_t x, y;
    } d;
    uint32_t raw;
}
Mv;

typedef struct
{
    uint8_t y_mode;
    uint8_t uv_mode;
    uint8_t ref;
    uint8_t segment;
    uint8_t skip;
    uint8_t coeffs;
    Mv      mv;
    Mv      mvs[16];
    uint8_t bmodes[16];
}
Mb;

typedef struct
{
    uint8_t *p[3];
    int      refs;
}
Img;

typedef struct
{
    const uint8_t *p, *end;
    uint32_t       value;
    uint32_t       range;
    int            count;
}
Bd;

typedef struct
{
    uint8_t coef[4][8][3][11];
    uint8_t mv[2][19];
    uint8_t ymode[4];
    uint8_t uvmode[3];
}
Entropy;

struct Vp8Dec
{
    int      w, h;
    int      mbw, mbh;
    int      ys, uvs;
    Img      img[4];
    int      ref[4];
    Mb      *mb_store;
    Mb      *mbs;
    int      mb_stride;
    uint8_t (*above_ctx)[9];
    Entropy  ent, saved;

    bool     seg_enabled, seg_update_map, seg_abs;
    int8_t   seg_quant[4], seg_lf[4];
    uint8_t  seg_probs[3];

    bool     lf_simple;
    int      lf_level, lf_sharp;
    bool     lf_delta;
    int8_t   ref_delta[4], mode_delta[4];

    int      dq[4][3][2];
    int      sign_bias[4];
    bool     have_key;
    bool     keyframe;
    int      version;
    bool     skip_enabled;
    uint8_t  skip_prob, prob_intra, prob_last, prob_gf;
};

static const int8_t kf_ymode_tree[8] = { -B_PRED, 2, 4, 6, -DC_PRED, -V_PRED, -H_PRED, -TM_PRED };
static const int8_t ymode_tree[8] = { -DC_PRED, 2, 4, 6, -V_PRED, -H_PRED, -TM_PRED, -B_PRED };
static const int8_t uv_mode_tree[6] = { -DC_PRED, 2, -V_PRED, 4, -H_PRED, -TM_PRED };
static const int8_t bmode_tree[18] = {
    -B_DC_PRED, 2, -B_TM_PRED, 4, -B_VE_PRED, 6, 8, 12, -B_HE_PRED, 10, -B_RD_PRED, -B_VR_PRED,
    -B_LD_PRED, 14, -B_VL_PRED, 16, -B_HD_PRED, -B_HU_PRED,
};
static const int8_t small_mv_tree[14] = { 2, 8, 4, 6, -0, -1, -2, -3, 10, 12, -4, -5, -6, -7 };
static const int8_t mv_ref_tree[8] = { -ZEROMV, 2, -NEARESTMV, 4, -NEARMV, 6, -NEWMV, -SPLITMV };
static const int8_t submv_ref_tree[6] = { -LEFT4X4, 2, -ABOVE4X4, 4, -ZERO4X4, -NEW4X4 };
static const int8_t split_mv_tree[6] = { -3, 2, -2, 4, -0, -1 };

static const uint8_t kf_ymode_probs[4] = { 145, 156, 163, 128 };
static const uint8_t kf_uv_mode_probs[3] = { 142, 114, 183 };
static const uint8_t default_ymode_probs[4] = { 112, 86, 140, 37 };
static const uint8_t default_uv_mode_probs[3] = { 162, 101, 204 };
static const uint8_t default_bmode_probs[9] = { 120, 90, 79, 133, 87, 85, 80, 111, 151 };
static const uint8_t split_mv_probs[3] = { 110, 111, 150 };
static const uint8_t submv_ref_probs[5][3] = {
    { 147, 136, 18 }, { 106, 145, 1 }, { 179, 121, 1 }, { 223, 1, 34 }, { 208, 1, 1 },
};
static const uint8_t mv_partitions[4][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
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

static const int16_t sixtap_filters[8][6] = {
    { 0, 0, 128, 0, 0, 0 },     { 0, -6, 123, 12, -1, 0 }, { 2, -11, 108, 36, -8, 1 }, { 0, -9, 93, 50, -6, 0 },
    { 3, -16, 77, 77, -16, 3 }, { 0, -6, 50, 93, -9, 0 },  { 1, -8, 36, 108, -11, 2 }, { 0, -1, 12, 123, -6, 0 },
};
static const int16_t bilinear_filters[8][6] = {
    { 0, 0, 128, 0, 0, 0 }, { 0, 0, 112, 16, 0, 0 }, { 0, 0, 96, 32, 0, 0 }, { 0, 0, 80, 48, 0, 0 },
    { 0, 0, 64, 64, 0, 0 }, { 0, 0, 48, 80, 0, 0 },  { 0, 0, 32, 96, 0, 0 }, { 0, 0, 16, 112, 0, 0 },
};

static void bd_init(Bd *b, const uint8_t *p, size_t n)
{
    b->p = p;
    b->end = p + n;
    b->value = 0;
    b->count = 0;
    b->range = 255;
    for (int i = 0; i < 2; i++)
        b->value = (b->value << 8) | (b->p < b->end ? *b->p++ : 0);
}

static int bd_read(Bd *b, int prob)
{
    uint32_t split = 1 + (((b->range - 1) * (uint32_t)prob) >> 8);
    uint32_t big = split << 8;
    int bit;
    if (b->value >= big)
    {
        bit = 1;
        b->range -= split;
        b->value -= big;
    }
    else
    {
        bit = 0;
        b->range = split;
    }
    if (b->range < 128)
    {
        int shift = __builtin_clz(b->range) - 24;
        b->range <<= shift;
        b->value <<= shift;
        b->count += shift;
        if (b->count >= 8)
        {
            b->count -= 8;
            b->value |= (uint32_t)(b->p < b->end ? *b->p++ : 0) << b->count;
        }
    }
    return bit;
}

static int bd_literal(Bd *b, int bits)
{
    int v = 0;
    while (bits-- > 0)
        v = (v << 1) | bd_read(b, 128);
    return v;
}

static int bd_maybe_signed(Bd *b, int bits)
{
    if (!bd_read(b, 128))
        return 0;
    int v = bd_literal(b, bits);
    return bd_read(b, 128) ? -v : v;
}

static int bd_tree(Bd *b, const int8_t *tree, const uint8_t *probs)
{
    int i = 0;
    while ((i = tree[i + bd_read(b, probs[i >> 1])]) > 0)
        ;
    return -i;
}

static void free_images(Vp8Dec *d)
{
    for (int i = 0; i < 4; i++)
    {
        for (int p = 0; p < 3; p++)
            free(d->img[i].p[p]);
        memset(&d->img[i], 0, sizeof(d->img[i]));
    }
}

static bool resize(Vp8Dec *d, int w, int h)
{
    if (w == d->w && h == d->h && d->mbs)
        return true;
    if (w < 1 || h < 1 || w > 4096 || h > 4096)
        return false;
    free_images(d);
    free(d->mb_store);
    free(d->above_ctx);
    d->mb_store = NULL;
    d->above_ctx = NULL;
    d->mbs = NULL;
    d->w = w;
    d->h = h;
    d->mbw = (w + 15) / 16;
    d->mbh = (h + 15) / 16;
    d->ys = d->mbw * 16;
    d->uvs = d->mbw * 8;
    size_t ysize = (size_t)d->ys * (size_t)d->mbh * 16, uvsize = (size_t)d->uvs * (size_t)d->mbh * 8;
    for (int i = 0; i < 4; i++)
        for (int p = 0; p < 3; p++)
            if (!(d->img[i].p[p] = calloc(1, p ? uvsize : ysize)))
                return false;
    d->mb_stride = d->mbw + 1;
    d->mb_store = calloc((size_t)d->mb_stride * (size_t)(d->mbh + 1), sizeof(Mb));
    d->above_ctx = calloc((size_t)d->mbw, sizeof(*d->above_ctx));
    if (!d->mb_store || !d->above_ctx)
        return false;
    d->mbs = d->mb_store + d->mb_stride + 1;
    for (int i = 0; i < 4; i++)
        d->ref[i] = -1;
    return true;
}

Vp8Dec *vp8dec_new(void)
{
    Vp8Dec *d = calloc(1, sizeof(*d));
    if (d)
        for (int i = 0; i < 4; i++)
            d->ref[i] = -1;
    return d;
}

void vp8dec_free(Vp8Dec *d)
{
    if (!d)
        return;
    free_images(d);
    free(d->mb_store);
    free(d->above_ctx);
    free(d);
}

bool vp8dec_is_keyframe(const uint8_t *data, size_t len)
{
    return len >= 10 && !(data[0] & 1) && data[3] == 0x9D && data[4] == 0x01 && data[5] == 0x2A;
}

bool vp8dec_image(const Vp8Dec *d, Vp8Image *img)
{
    if (d->ref[CURRENT] < 0)
        return false;
    const Img *im = &d->img[d->ref[CURRENT]];
    img->y = im->p[0];
    img->u = im->p[1];
    img->v = im->p[2];
    img->y_stride = d->ys;
    img->uv_stride = d->uvs;
    img->width = d->w;
    img->height = d->h;
    return true;
}

static uint8_t clamp255(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static int clamp_q(int q)
{
    return q < 0 ? 0 : q > 127 ? 127 : q;
}

static void setup_dequant(Vp8Dec *d, int q_index, const int *deltas)
{
    for (int s = 0; s < 4; s++)
    {
        int q = q_index;
        if (d->seg_enabled)
            q = d->seg_abs ? d->seg_quant[s] : q + d->seg_quant[s];
        int (*f)[2] = d->dq[s];
        f[Q_Y1][0] = vp8_dc_q[clamp_q(q + deltas[0])];
        f[Q_Y1][1] = vp8_ac_q[clamp_q(q)];
        f[Q_Y2][0] = vp8_dc_q[clamp_q(q + deltas[1])] * 2;
        f[Q_Y2][1] = vp8_ac_q[clamp_q(q + deltas[2])] * 155 / 100;
        if (f[Q_Y2][1] < 8)
            f[Q_Y2][1] = 8;
        f[Q_UV][0] = vp8_dc_q[clamp_q(q + deltas[3])];
        if (f[Q_UV][0] > 132)
            f[Q_UV][0] = 132;
        f[Q_UV][1] = vp8_ac_q[clamp_q(q + deltas[4])];
    }
}

static int read_block(Bd *b, const uint8_t (*probs)[3][11], int first, int ctx, const int *dq, int16_t *out)
{
    int c = first;
    bool skip_eob = false;
    while (c < 16)
    {
        const uint8_t *p = probs[bands[c]][ctx];
        if (!skip_eob && !bd_read(b, p[0]))
            break;
        if (!bd_read(b, p[1]))
        {
            ctx = 0;
            skip_eob = true;
            c++;
            continue;
        }
        int v;
        if (!bd_read(b, p[2]))
            v = 1;
        else if (!bd_read(b, p[3]))
            v = !bd_read(b, p[4]) ? 2 : 3 + bd_read(b, p[5]);
        else
        {
            int cat;
            if (!bd_read(b, p[6]))
                cat = bd_read(b, p[7]);
            else if (!bd_read(b, p[8]))
                cat = 2 + bd_read(b, p[9]);
            else
                cat = 4 + bd_read(b, p[10]);
            int extra = 0;
            for (const uint8_t *cp = cat_probs[cat]; *cp; cp++)
                extra = (extra << 1) | bd_read(b, *cp);
            v = cat_base[cat] + extra;
        }
        ctx = v > 1 ? 2 : 1;
        if (bd_read(b, 128))
            v = -v;
        out[zigzag[c]] = (int16_t)(v * dq[c > 0]);
        skip_eob = false;
        c++;
    }
    return c > first;
}

static void iwht(const int16_t *in, int16_t *out)
{
    int16_t tmp[16];
    for (int i = 0; i < 4; i++)
    {
        int a1 = in[i] + in[12 + i], b1 = in[4 + i] + in[8 + i];
        int c1 = in[4 + i] - in[8 + i], d1 = in[i] - in[12 + i];
        tmp[i] = (int16_t)(a1 + b1);
        tmp[4 + i] = (int16_t)(c1 + d1);
        tmp[8 + i] = (int16_t)(a1 - b1);
        tmp[12 + i] = (int16_t)(d1 - c1);
    }
    for (int i = 0; i < 4; i++)
    {
        const int16_t *ip = tmp + i * 4;
        int a1 = ip[0] + ip[3], b1 = ip[1] + ip[2], c1 = ip[1] - ip[2], d1 = ip[0] - ip[3];
        out[i * 4 + 0] = (int16_t)((a1 + b1 + 3) >> 3);
        out[i * 4 + 1] = (int16_t)((c1 + d1 + 3) >> 3);
        out[i * 4 + 2] = (int16_t)((a1 - b1 + 3) >> 3);
        out[i * 4 + 3] = (int16_t)((d1 - c1 + 3) >> 3);
    }
}

static void idct_add(const int16_t *in, uint8_t *dst, int stride)
{
    bool ac = false;
    for (int i = 1; i < 16 && !ac; i++)
        ac = in[i] != 0;
    if (!ac)
    {
        if (!in[0])
            return;
        int dc = (in[0] + 4) >> 3;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                dst[r * stride + c] = clamp255(dst[r * stride + c] + dc);
        return;
    }
    static const int cs = 20091, sn = 35468;
    int16_t tmp[16];
    for (int i = 0; i < 4; i++)
    {
        int a1 = in[i] + in[8 + i], b1 = in[i] - in[8 + i];
        int t1 = (in[4 + i] * sn) >> 16, t2 = in[12 + i] + ((in[12 + i] * cs) >> 16);
        int c1 = t1 - t2;
        t1 = in[4 + i] + ((in[4 + i] * cs) >> 16);
        t2 = (in[12 + i] * sn) >> 16;
        int d1 = t1 + t2;
        tmp[i] = (int16_t)(a1 + d1);
        tmp[12 + i] = (int16_t)(a1 - d1);
        tmp[4 + i] = (int16_t)(b1 + c1);
        tmp[8 + i] = (int16_t)(b1 - c1);
    }
    for (int i = 0; i < 4; i++)
    {
        const int16_t *ip = tmp + i * 4;
        int a1 = ip[0] + ip[2], b1 = ip[0] - ip[2];
        int t1 = (ip[1] * sn) >> 16, t2 = ip[3] + ((ip[3] * cs) >> 16);
        int c1 = t1 - t2;
        t1 = ip[1] + ((ip[1] * cs) >> 16);
        t2 = (ip[3] * sn) >> 16;
        int d1 = t1 + t2;
        uint8_t *o = dst + i * stride;
        o[0] = clamp255(o[0] + (int16_t)((a1 + d1 + 4) >> 3));
        o[1] = clamp255(o[1] + (int16_t)((b1 + c1 + 4) >> 3));
        o[2] = clamp255(o[2] + (int16_t)((b1 - c1 + 4) >> 3));
        o[3] = clamp255(o[3] + (int16_t)((a1 - d1 + 4) >> 3));
    }
}

static void predict_mb(int mode, int n, uint8_t *p, int stride, int mx, int my)
{
    bool has_above = my > 0, has_left = mx > 0;
    uint8_t above[16], left[16];
    for (int i = 0; i < n; i++)
    {
        above[i] = has_above ? p[i - stride] : 127;
        left[i] = has_left ? p[i * stride - 1] : 129;
    }
    uint8_t corner = !has_above ? 127 : !has_left ? 129 : p[-stride - 1];
    switch (mode)
    {
        case DC_PRED:
        {
            int sum = 0, shift = (n == 16 ? 3 : 2) + has_above + has_left, v = 128;
            for (int i = 0; i < n; i++)
                sum += (has_above ? above[i] : 0) + (has_left ? left[i] : 0);
            if (has_above || has_left)
                v = (sum + (1 << (shift - 1))) >> shift;
            for (int r = 0; r < n; r++)
                memset(p + r * stride, v, (size_t)n);
            break;
        }
        case V_PRED:
            for (int r = 0; r < n; r++)
                memcpy(p + r * stride, above, (size_t)n);
            break;
        case H_PRED:
            for (int r = 0; r < n; r++)
                memset(p + r * stride, left[r], (size_t)n);
            break;
        default:
            for (int r = 0; r < n; r++)
                for (int c = 0; c < n; c++)
                    p[r * stride + c] = clamp255(left[r] + above[c] - corner);
            break;
    }
}

static void predict_sub(int mode, uint8_t *B, int stride, const uint8_t *A, const uint8_t *L)
{
    int P = A[-1];
#define AVG3(x, y, z) (((x) + 2 * (y) + (z) + 2) >> 2)
#define AVG2(x, y)    (((x) + (y) + 1) >> 1)
#define PUT(r, c, v)  (B[(r) * stride + (c)] = (uint8_t)(v))
    switch (mode)
    {
        case B_DC_PRED:
        {
            int v = 4;
            for (int i = 0; i < 4; i++)
                v += A[i] + L[i];
            v >>= 3;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    PUT(r, c, v);
            break;
        }
        case B_TM_PRED:
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    PUT(r, c, clamp255(L[r] + A[c] - P));
            break;
        case B_VE_PRED:
            for (int c = 0; c < 4; c++)
            {
                int v = AVG3(A[c - 1], A[c], A[c + 1]);
                for (int r = 0; r < 4; r++)
                    PUT(r, c, v);
            }
            break;
        case B_HE_PRED:
        {
            int v[4] = { AVG3(P, L[0], L[1]), AVG3(L[0], L[1], L[2]), AVG3(L[1], L[2], L[3]), AVG3(L[2], L[3], L[3]) };
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    PUT(r, c, v[r]);
            break;
        }
        case B_LD_PRED:
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                {
                    int i = r + c;
                    PUT(r, c, i < 6 ? AVG3(A[i], A[i + 1], A[i + 2]) : AVG3(A[6], A[7], A[7]));
                }
            break;
        case B_RD_PRED:
        {
            int e[9] = { L[3], L[2], L[1], L[0], P, A[0], A[1], A[2], A[3] };
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                {
                    int i = 4 - r + c;
                    PUT(r, c, AVG3(e[i - 1], e[i], e[i + 1]));
                }
            break;
        }
        case B_VR_PRED:
        {
            int e[9] = { L[3], L[2], L[1], L[0], P, A[0], A[1], A[2], A[3] };
            PUT(3, 0, AVG3(e[1], e[2], e[3]));
            PUT(2, 0, AVG3(e[2], e[3], e[4]));
            PUT(3, 1, AVG3(e[3], e[4], e[5]));
            PUT(1, 0, AVG3(e[3], e[4], e[5]));
            PUT(2, 1, AVG2(e[4], e[5]));
            PUT(0, 0, AVG2(e[4], e[5]));
            PUT(3, 2, AVG3(e[4], e[5], e[6]));
            PUT(1, 1, AVG3(e[4], e[5], e[6]));
            PUT(2, 2, AVG2(e[5], e[6]));
            PUT(0, 1, AVG2(e[5], e[6]));
            PUT(3, 3, AVG3(e[5], e[6], e[7]));
            PUT(1, 2, AVG3(e[5], e[6], e[7]));
            PUT(2, 3, AVG2(e[6], e[7]));
            PUT(0, 2, AVG2(e[6], e[7]));
            PUT(1, 3, AVG3(e[6], e[7], e[8]));
            PUT(0, 3, AVG2(e[7], e[8]));
            break;
        }
        case B_VL_PRED:
            PUT(0, 0, AVG2(A[0], A[1]));
            PUT(1, 0, AVG3(A[0], A[1], A[2]));
            PUT(2, 0, AVG2(A[1], A[2]));
            PUT(0, 1, AVG2(A[1], A[2]));
            PUT(1, 1, AVG3(A[1], A[2], A[3]));
            PUT(3, 0, AVG3(A[1], A[2], A[3]));
            PUT(2, 1, AVG2(A[2], A[3]));
            PUT(0, 2, AVG2(A[2], A[3]));
            PUT(3, 1, AVG3(A[2], A[3], A[4]));
            PUT(1, 2, AVG3(A[2], A[3], A[4]));
            PUT(2, 2, AVG2(A[3], A[4]));
            PUT(0, 3, AVG2(A[3], A[4]));
            PUT(3, 2, AVG3(A[3], A[4], A[5]));
            PUT(1, 3, AVG3(A[3], A[4], A[5]));
            PUT(2, 3, AVG3(A[4], A[5], A[6]));
            PUT(3, 3, AVG3(A[5], A[6], A[7]));
            break;
        case B_HD_PRED:
        {
            int e[9] = { L[3], L[2], L[1], L[0], P, A[0], A[1], A[2], A[3] };
            PUT(3, 0, AVG2(e[0], e[1]));
            PUT(3, 1, AVG3(e[0], e[1], e[2]));
            PUT(2, 0, AVG2(e[1], e[2]));
            PUT(3, 2, AVG2(e[1], e[2]));
            PUT(2, 1, AVG3(e[1], e[2], e[3]));
            PUT(3, 3, AVG3(e[1], e[2], e[3]));
            PUT(2, 2, AVG2(e[2], e[3]));
            PUT(1, 0, AVG2(e[2], e[3]));
            PUT(2, 3, AVG3(e[2], e[3], e[4]));
            PUT(1, 1, AVG3(e[2], e[3], e[4]));
            PUT(1, 2, AVG2(e[3], e[4]));
            PUT(0, 0, AVG2(e[3], e[4]));
            PUT(1, 3, AVG3(e[3], e[4], e[5]));
            PUT(0, 1, AVG3(e[3], e[4], e[5]));
            PUT(0, 2, AVG3(e[4], e[5], e[6]));
            PUT(0, 3, AVG3(e[5], e[6], e[7]));
            break;
        }
        default:
            PUT(0, 0, AVG2(L[0], L[1]));
            PUT(0, 1, AVG3(L[0], L[1], L[2]));
            PUT(0, 2, AVG2(L[1], L[2]));
            PUT(1, 0, AVG2(L[1], L[2]));
            PUT(0, 3, AVG3(L[1], L[2], L[3]));
            PUT(1, 1, AVG3(L[1], L[2], L[3]));
            PUT(1, 2, AVG2(L[2], L[3]));
            PUT(2, 0, AVG2(L[2], L[3]));
            PUT(1, 3, AVG3(L[2], L[3], L[3]));
            PUT(2, 1, AVG3(L[2], L[3], L[3]));
            PUT(2, 2, L[3]);
            PUT(2, 3, L[3]);
            PUT(3, 0, L[3]);
            PUT(3, 1, L[3]);
            PUT(3, 2, L[3]);
            PUT(3, 3, L[3]);
            break;
    }
#undef AVG3
#undef AVG2
#undef PUT
}

static void b_pred(Vp8Dec *d, Mb *m, uint8_t *y, int mx, int my, int16_t (*coeffs)[16])
{
    int stride = d->ys;
    uint8_t right[4];
    if (my == 0)
        memset(right, 127, 4);
    else if (mx == d->mbw - 1)
        memset(right, y[-stride + 15], 4);
    else
        memcpy(right, y - stride + 16, 4);

    for (int i = 0; i < 16; i++)
    {
        int r = i >> 2, c = i & 3;
        uint8_t *B = y + r * 4 * stride + c * 4;
        uint8_t edge[9], L[4];
        uint8_t *A = edge + 1;
        if (r == 0 && my == 0)
            memset(A, 127, 4);
        else
            memcpy(A, B - stride, 4);
        if (c == 3)
            memcpy(A + 4, right, 4);
        else if (r == 0 && my == 0)
            memset(A + 4, 127, 4);
        else
            memcpy(A + 4, B - stride + 4, 4);
        for (int k = 0; k < 4; k++)
            L[k] = (c == 0 && mx == 0) ? 129 : B[k * stride - 1];
        A[-1] = (r == 0 && my == 0) ? 127 : (c == 0 && mx == 0) ? 129 : B[-stride - 1];
        predict_sub(m->bmodes[i], B, stride, A, L);
        idct_add(coeffs[i], B, stride);
    }
}

static void predict_inter(const uint8_t *ref, int stride, int pw, int ph, int x, int y, int bw, int bh, Mv mv,
                          const int16_t (*filters)[6], uint8_t *dst)
{
    int fx = mv.d.x & 7, fy = mv.d.y & 7;
    x += mv.d.x >> 3;
    y += mv.d.y >> 3;
    uint8_t emu[21 * 21];
    const uint8_t *src;
    int ss;
    if (x < 2 || y < 2 || x + bw + 3 > pw || y + bh + 3 > ph)
    {
        int ew = bw + 5;
        for (int r = 0; r < bh + 5; r++)
        {
            int yy = y - 2 + r;
            yy = yy < 0 ? 0 : yy >= ph ? ph - 1 : yy;
            for (int c = 0; c < ew; c++)
            {
                int xx = x - 2 + c;
                xx = xx < 0 ? 0 : xx >= pw ? pw - 1 : xx;
                emu[r * ew + c] = ref[(size_t)yy * stride + xx];
            }
        }
        src = emu + 2 * ew + 2;
        ss = ew;
    }
    else
    {
        src = ref + (size_t)y * stride + x;
        ss = stride;
    }

    if (!(fx | fy))
    {
        for (int r = 0; r < bh; r++)
            memcpy(dst + r * stride, src + r * ss, (size_t)bw);
        return;
    }
    uint8_t tmp[21 * 16];
    const int16_t *f = filters[fx];
    for (int r = 0; r < bh + 5; r++)
    {
        const uint8_t *s = src + (r - 2) * ss;
        for (int c = 0; c < bw; c++)
        {
            int v = s[c - 2] * f[0] + s[c - 1] * f[1] + s[c] * f[2] + s[c + 1] * f[3] + s[c + 2] * f[4] +
                    s[c + 3] * f[5] + 64;
            tmp[r * bw + c] = clamp255(v >> 7);
        }
    }
    f = filters[fy];
    for (int r = 0; r < bh; r++)
        for (int c = 0; c < bw; c++)
        {
            const uint8_t *t = tmp + (r + 2) * bw + c;
            int v = t[-2 * bw] * f[0] + t[-bw] * f[1] + t[0] * f[2] + t[bw] * f[3] + t[2 * bw] * f[4] +
                    t[3 * bw] * f[5] + 64;
            dst[r * stride + c] = clamp255(v >> 7);
        }
}

static Mv chroma_split_mv(const Mb *m, int b, bool full)
{
    Mv mv;
    int tx = m->mvs[b].d.x + m->mvs[b + 1].d.x + m->mvs[b + 4].d.x + m->mvs[b + 5].d.x;
    int ty = m->mvs[b].d.y + m->mvs[b + 1].d.y + m->mvs[b + 4].d.y + m->mvs[b + 5].d.y;
    tx += tx < 0 ? -4 : 4;
    ty += ty < 0 ? -4 : 4;
    mv.d.x = (int16_t)(tx / 8);
    mv.d.y = (int16_t)(ty / 8);
    if (full)
    {
        mv.d.x &= ~7;
        mv.d.y &= ~7;
    }
    return mv;
}

static void inter_mb(Vp8Dec *d, Mb *m, uint8_t *dst[3], int mx, int my, int16_t (*coeffs)[16])
{
    const Img *ref = &d->img[d->ref[m->ref]];
    const int16_t (*filters)[6] = d->version ? bilinear_filters : sixtap_filters;
    bool full = d->version == 3;
    int pw = d->ys, ph = d->mbh * 16;
    int x = mx * 16, y = my * 16;
    Mv cmv[4];
    if (m->y_mode != SPLITMV)
    {
        predict_inter(ref->p[0], d->ys, pw, ph, x, y, 16, 16, m->mv, filters, dst[0]);
        Mv uv = m->mv;
        uv.d.x = (int16_t)((uv.d.x + (uv.d.x < 0 ? -1 : 1)) / 2);
        uv.d.y = (int16_t)((uv.d.y + (uv.d.y < 0 ? -1 : 1)) / 2);
        if (full)
        {
            uv.d.x &= ~7;
            uv.d.y &= ~7;
        }
        predict_inter(ref->p[1], d->uvs, pw / 2, ph / 2, x / 2, y / 2, 8, 8, uv, filters, dst[1]);
        predict_inter(ref->p[2], d->uvs, pw / 2, ph / 2, x / 2, y / 2, 8, 8, uv, filters, dst[2]);
    }
    else
    {
        for (int b = 0; b < 16; b++)
            predict_inter(ref->p[0], d->ys, pw, ph, x + (b & 3) * 4, y + (b >> 2) * 4, 4, 4, m->mvs[b], filters,
                          dst[0] + (b >> 2) * 4 * d->ys + (b & 3) * 4);
        cmv[0] = chroma_split_mv(m, 0, full);
        cmv[1] = chroma_split_mv(m, 2, full);
        cmv[2] = chroma_split_mv(m, 8, full);
        cmv[3] = chroma_split_mv(m, 10, full);
        for (int b = 0; b < 4; b++)
            for (int p = 1; p < 3; p++)
                predict_inter(ref->p[p], d->uvs, pw / 2, ph / 2, x / 2 + (b & 1) * 4, y / 2 + (b >> 1) * 4, 4, 4,
                              cmv[b], filters, dst[p] + (b >> 1) * 4 * d->uvs + (b & 1) * 4);
    }
    for (int b = 0; b < 16; b++)
        idct_add(coeffs[b], dst[0] + (b >> 2) * 4 * d->ys + (b & 3) * 4, d->ys);
    for (int b = 0; b < 8; b++)
        idct_add(coeffs[16 + b], dst[1 + (b >> 2)] + ((b >> 1) & 1) * 4 * d->uvs + (b & 1) * 4, d->uvs);
}

static int above_bmode(const Mb *m, const Mb *above, int b)
{
    if (b >= 4)
        return m->bmodes[b - 4];
    switch (above->y_mode)
    {
        case B_PRED:
            return above->bmodes[b + 12];
        case V_PRED:
            return B_VE_PRED;
        case H_PRED:
            return B_HE_PRED;
        case TM_PRED:
            return B_TM_PRED;
        default:
            return B_DC_PRED;
    }
}

static int left_bmode(const Mb *m, const Mb *left, int b)
{
    if (b & 3)
        return m->bmodes[b - 1];
    switch (left->y_mode)
    {
        case B_PRED:
            return left->bmodes[b + 3];
        case V_PRED:
            return B_VE_PRED;
        case H_PRED:
            return B_HE_PRED;
        case TM_PRED:
            return B_TM_PRED;
        default:
            return B_DC_PRED;
    }
}

static int read_mv_component(Bd *b, const uint8_t *p)
{
    int x = 0;
    if (bd_read(b, p[0]))
    {
        for (int i = 0; i < 3; i++)
            x += bd_read(b, p[9 + i]) << i;
        for (int i = 9; i > 3; i--)
            x += bd_read(b, p[9 + i]) << i;
        if (!(x & 0xFFF0) || bd_read(b, p[9 + 3]))
            x += 8;
    }
    else
        x = bd_tree(b, small_mv_tree, p + 2);
    if (x && bd_read(b, p[1]))
        x = -x;
    return x * 2;
}

static Mv read_mv(Bd *b, const uint8_t (*p)[19])
{
    Mv mv;
    mv.d.y = (int16_t)read_mv_component(b, p[0]);
    mv.d.x = (int16_t)read_mv_component(b, p[1]);
    return mv;
}

typedef struct
{
    int left, right, top, bottom;
}
Bounds;

static Mv clamp_mv(Mv mv, const Bounds *bd)
{
    Mv r = mv;
    r.d.x = (int16_t)(mv.d.x < bd->left ? bd->left : mv.d.x > bd->right ? bd->right : mv.d.x);
    r.d.y = (int16_t)(mv.d.y < bd->top ? bd->top : mv.d.y > bd->bottom ? bd->bottom : mv.d.y);
    return r;
}

static Mv biased(const Vp8Dec *d, const Mb *n, int ref)
{
    Mv mv = n->mv;
    if (d->sign_bias[n->ref] != d->sign_bias[ref])
    {
        mv.d.x = (int16_t)-mv.d.x;
        mv.d.y = (int16_t)-mv.d.y;
    }
    return mv;
}

static Mv left_block_mv(const Mb *m, const Mb *left, int b)
{
    if (b & 3)
        return m->mvs[b - 1];
    return left->y_mode == SPLITMV ? left->mvs[b + 3] : left->mv;
}

static Mv above_block_mv(const Mb *m, const Mb *above, int b)
{
    if (b >= 4)
        return m->mvs[b - 4];
    return above->y_mode == SPLITMV ? above->mvs[b + 12] : above->mv;
}

static void read_inter_modes(Vp8Dec *d, Bd *b, Mb *m, const Bounds *bounds)
{
    Mb *left = m - 1, *above = m - d->mb_stride, *aboveleft = above - 1;
    m->ref = bd_read(b, d->prob_last) ? (uint8_t)(2 + bd_read(b, d->prob_gf)) : LAST;
    m->uv_mode = DC_PRED;

    Mv near[4];
    int cnt[4] = { 0, 0, 0, 0 };
    int idx = 0;
    near[0].raw = near[1].raw = near[2].raw = near[3].raw = 0;
    const Mb *ns[3] = { above, left, aboveleft };
    for (int k = 0; k < 3; k++)
    {
        const Mb *n = ns[k];
        int weight = k == 2 ? 1 : 2;
        if (n->ref == CURRENT)
            continue;
        if (n->mv.raw)
        {
            Mv v = biased(d, n, m->ref);
            if (k == 0 || v.raw != near[idx].raw)
                near[++idx] = v;
            cnt[idx] += weight;
        }
        else
            cnt[0] += weight;
    }
    if (cnt[3] && near[idx].raw == near[1].raw)
        cnt[1] += 1;
    cnt[3] = (above->y_mode == SPLITMV) * 2 + (left->y_mode == SPLITMV) * 2 + (aboveleft->y_mode == SPLITMV);
    if (cnt[2] > cnt[1])
    {
        int t = cnt[1];
        cnt[1] = cnt[2];
        cnt[2] = t;
        Mv tm = near[1];
        near[1] = near[2];
        near[2] = tm;
    }
    if (cnt[1] >= cnt[0])
        near[0] = near[1];

    uint8_t probs[4];
    for (int i = 0; i < 4; i++)
        probs[i] = (uint8_t)vp8_mode_contexts[cnt[i]][i];
    m->y_mode = (uint8_t)bd_tree(b, mv_ref_tree, probs);

    switch (m->y_mode)
    {
        case NEARESTMV:
            m->mv = clamp_mv(near[1], bounds);
            break;
        case NEARMV:
            m->mv = clamp_mv(near[2], bounds);
            break;
        case ZEROMV:
            m->mv.raw = 0;
            break;
        case NEWMV:
        {
            Mv best = clamp_mv(near[0], bounds);
            Mv v = read_mv(b, d->ent.mv);
            m->mv.d.x = (int16_t)(v.d.x + best.d.x);
            m->mv.d.y = (int16_t)(v.d.y + best.d.y);
            break;
        }
        default:
        {
            Mv best = clamp_mv(near[0], bounds);
            int part = bd_tree(b, split_mv_tree, split_mv_probs);
            const uint8_t *map = mv_partitions[part];
            int mask = 0;
            for (int j = 0; mask != 0xFFFF; j++)
            {
                int k = 0;
                while (map[k] != j)
                    k++;
                Mv l = left_block_mv(m, left, k), a = above_block_mv(m, above, k);
                int ctx;
                if (l.raw == a.raw)
                    ctx = l.raw ? 3 : 4;
                else if (!a.raw)
                    ctx = 2;
                else if (!l.raw)
                    ctx = 1;
                else
                    ctx = 0;
                Mv v;
                switch (bd_tree(b, submv_ref_tree, submv_ref_probs[ctx]))
                {
                    case LEFT4X4:
                        v = l;
                        break;
                    case ABOVE4X4:
                        v = a;
                        break;
                    case ZERO4X4:
                        v.raw = 0;
                        break;
                    default:
                        v = read_mv(b, d->ent.mv);
                        v.d.x = (int16_t)(v.d.x + best.d.x);
                        v.d.y = (int16_t)(v.d.y + best.d.y);
                        break;
                }
                for (; k < 16; k++)
                    if (map[k] == j)
                    {
                        m->mvs[k] = v;
                        mask |= 1 << k;
                    }
            }
            m->mv = m->mvs[15];
            break;
        }
    }
}

static void decode_mb(Vp8Dec *d, Bd *hb, Bd *tb, int mx, int my, uint8_t *left_ctx)
{
    Mb *m = d->mbs + my * d->mb_stride + mx;
    Mb *left = m - 1, *above = m - d->mb_stride;

    if (d->seg_update_map)
        m->segment = bd_read(hb, d->seg_probs[0]) ? (uint8_t)(2 + bd_read(hb, d->seg_probs[2]))
                                                  : (uint8_t)bd_read(hb, d->seg_probs[1]);
    else if (d->keyframe)
        m->segment = 0;
    m->skip = d->skip_enabled ? (uint8_t)bd_read(hb, d->skip_prob) : 0;

    if (d->keyframe)
    {
        m->ref = CURRENT;
        m->mv.raw = 0;
        m->y_mode = (uint8_t)bd_tree(hb, kf_ymode_tree, kf_ymode_probs);
        if (m->y_mode == B_PRED)
            for (int i = 0; i < 16; i++)
                m->bmodes[i] = (uint8_t)bd_tree(hb, bmode_tree,
                                                vp8_kf_bmode_probs[above_bmode(m, above, i)][left_bmode(m, left, i)]);
        m->uv_mode = (uint8_t)bd_tree(hb, uv_mode_tree, kf_uv_mode_probs);
    }
    else if (bd_read(hb, d->prob_intra))
    {
        Bounds bounds = { -((mx + 1) << 7), (d->mbw - mx) << 7, -((my + 1) << 7), (d->mbh - my) << 7 };
        read_inter_modes(d, hb, m, &bounds);
    }
    else
    {
        m->ref = CURRENT;
        m->mv.raw = 0;
        m->y_mode = (uint8_t)bd_tree(hb, ymode_tree, d->ent.ymode);
        if (m->y_mode == B_PRED)
            for (int i = 0; i < 16; i++)
                m->bmodes[i] = (uint8_t)bd_tree(hb, bmode_tree, default_bmode_probs);
        m->uv_mode = (uint8_t)bd_tree(hb, uv_mode_tree, d->ent.uvmode);
    }

    int16_t coeffs[25][16];
    memset(coeffs, 0, sizeof(coeffs));
    bool has_y2 = m->y_mode != B_PRED && m->y_mode != SPLITMV;
    uint8_t *actx = d->above_ctx[mx];
    if (m->skip)
    {
        memset(actx, 0, 8);
        memset(left_ctx, 0, 8);
        if (has_y2)
            actx[8] = left_ctx[8] = 0;
        m->coeffs = 0;
    }
    else
    {
        int seg = d->seg_enabled ? m->segment : 0;
        int (*dq)[2] = d->dq[seg];
        int nz = 0, type = 3, first = 0;
        if (has_y2)
        {
            int r = read_block(tb, (const uint8_t (*)[3][11])d->ent.coef[1], 0, actx[8] + left_ctx[8], dq[Q_Y2],
                               coeffs[24]);
            actx[8] = left_ctx[8] = (uint8_t)r;
            nz |= r;
            type = 0;
            first = 1;
        }
        for (int i = 0; i < 24; i++)
        {
            int t = i < 16 ? type : 2;
            uint8_t *a = &actx[above_ctx_index[i]], *l = &left_ctx[left_ctx_index[i]];
            int r = read_block(tb, (const uint8_t (*)[3][11])d->ent.coef[t], i < 16 ? first : 0, *a + *l,
                               dq[i < 16 ? Q_Y1 : Q_UV], coeffs[i]);
            *a = *l = (uint8_t)r;
            nz |= r;
        }
        m->coeffs = (uint8_t)nz;
        if (has_y2)
        {
            int16_t dc[16];
            iwht(coeffs[24], dc);
            for (int i = 0; i < 16; i++)
                coeffs[i][0] = dc[i];
        }
    }

    const Img *cur = &d->img[d->ref[CURRENT]];
    uint8_t *dst[3] = {
        cur->p[0] + (size_t)my * 16 * d->ys + (size_t)mx * 16,
        cur->p[1] + (size_t)my * 8 * d->uvs + (size_t)mx * 8,
        cur->p[2] + (size_t)my * 8 * d->uvs + (size_t)mx * 8,
    };
    if (m->ref != CURRENT)
    {
        inter_mb(d, m, dst, mx, my, coeffs);
        return;
    }
    if (m->y_mode == B_PRED)
        b_pred(d, m, dst[0], mx, my, coeffs);
    else
    {
        predict_mb(m->y_mode, 16, dst[0], d->ys, mx, my);
        for (int b = 0; b < 16; b++)
            idct_add(coeffs[b], dst[0] + (b >> 2) * 4 * d->ys + (b & 3) * 4, d->ys);
    }
    predict_mb(m->uv_mode, 8, dst[1], d->uvs, mx, my);
    predict_mb(m->uv_mode, 8, dst[2], d->uvs, mx, my);
    for (int b = 0; b < 8; b++)
        idct_add(coeffs[16 + b], dst[1 + (b >> 2)] + ((b >> 1) & 1) * 4 * d->uvs + (b & 1) * 4, d->uvs);
}

static int s8(int x)
{
    return x < -128 ? -128 : x > 127 ? 127 : x;
}

static int iabs(int x)
{
    return x < 0 ? -x : x;
}

#define P3 px[-4 * st]
#define P2 px[-3 * st]
#define P1 px[-2 * st]
#define P0 px[-1 * st]
#define Q0 px[0]
#define Q1 px[st]
#define Q2 px[2 * st]
#define Q3 px[3 * st]

static bool simple_ok(const uint8_t *px, int st, int limit)
{
    return iabs(P0 - Q0) * 2 + (iabs(P1 - Q1) >> 1) <= limit;
}

static bool normal_ok(const uint8_t *px, int st, int e, int i)
{
    return simple_ok(px, st, 2 * e + i) && iabs(P3 - P2) <= i && iabs(P2 - P1) <= i && iabs(P1 - P0) <= i &&
           iabs(Q3 - Q2) <= i && iabs(Q2 - Q1) <= i && iabs(Q1 - Q0) <= i;
}

static bool hev(const uint8_t *px, int st, int t)
{
    return iabs(P1 - P0) > t || iabs(Q1 - Q0) > t;
}

static void common_filter(uint8_t *px, int st, bool outer)
{
    int p1 = P1 - 128, p0 = P0 - 128, q0 = Q0 - 128, q1 = Q1 - 128;
    int a = 3 * (q0 - p0);
    if (outer)
        a += s8(p1 - q1);
    a = s8(a);
    int f1 = ((a + 4 > 127) ? 127 : a + 4) >> 3;
    int f2 = ((a + 3 > 127) ? 127 : a + 3) >> 3;
    P0 = (uint8_t)(s8(p0 + f2) + 128);
    Q0 = (uint8_t)(s8(q0 - f1) + 128);
    if (!outer)
    {
        a = (f1 + 1) >> 1;
        P1 = (uint8_t)(s8(p1 + a) + 128);
        Q1 = (uint8_t)(s8(q1 - a) + 128);
    }
}

static void mb_filter(uint8_t *px, int st)
{
    int p2 = P2 - 128, p1 = P1 - 128, p0 = P0 - 128, q0 = Q0 - 128, q1 = Q1 - 128, q2 = Q2 - 128;
    int w = s8(s8(p1 - q1) + 3 * (q0 - p0));
    int a = (27 * w + 63) >> 7;
    P0 = (uint8_t)(s8(p0 + a) + 128);
    Q0 = (uint8_t)(s8(q0 - a) + 128);
    a = (18 * w + 63) >> 7;
    P1 = (uint8_t)(s8(p1 + a) + 128);
    Q1 = (uint8_t)(s8(q1 - a) + 128);
    a = (9 * w + 63) >> 7;
    P2 = (uint8_t)(s8(p2 + a) + 128);
    Q2 = (uint8_t)(s8(q2 - a) + 128);
}

static void edge_normal(uint8_t *px, int st, int step, int n, int e, int i, int t, bool mb)
{
    for (int k = 0; k < n; k++, px += step)
    {
        if (!normal_ok(px, st, e, i))
            continue;
        if (mb)
        {
            if (hev(px, st, t))
                common_filter(px, st, true);
            else
                mb_filter(px, st);
        }
        else
            common_filter(px, st, hev(px, st, t));
    }
}

static void edge_simple(uint8_t *px, int st, int step, int limit)
{
    for (int k = 0; k < 16; k++, px += step)
        if (simple_ok(px, st, limit))
            common_filter(px, st, true);
}

static void loop_filter(Vp8Dec *d)
{
    const Img *cur = &d->img[d->ref[CURRENT]];
    for (int my = 0; my < d->mbh; my++)
        for (int mx = 0; mx < d->mbw; mx++)
        {
            const Mb *m = d->mbs + my * d->mb_stride + mx;
            int level = d->lf_level;
            if (d->seg_enabled)
                level = d->seg_abs ? d->seg_lf[m->segment] : level + d->seg_lf[m->segment];
            level = level < 0 ? 0 : level > 63 ? 63 : level;
            if (d->lf_delta)
            {
                level += d->ref_delta[m->ref];
                if (m->ref == CURRENT)
                {
                    if (m->y_mode == B_PRED)
                        level += d->mode_delta[0];
                }
                else if (m->y_mode == ZEROMV)
                    level += d->mode_delta[1];
                else if (m->y_mode == SPLITMV)
                    level += d->mode_delta[3];
                else
                    level += d->mode_delta[2];
                level = level < 0 ? 0 : level > 63 ? 63 : level;
            }
            if (!level)
                continue;
            int interior = level;
            if (d->lf_sharp)
            {
                interior >>= d->lf_sharp > 4 ? 2 : 1;
                if (interior > 9 - d->lf_sharp)
                    interior = 9 - d->lf_sharp;
            }
            if (interior < 1)
                interior = 1;
            int hev_t = (level >= 15) + (level >= 40) + (level >= 20 && !d->keyframe);
            bool inner = m->coeffs || m->y_mode == SPLITMV || m->y_mode == B_PRED;
            uint8_t *y = cur->p[0] + (size_t)my * 16 * d->ys + (size_t)mx * 16;
            uint8_t *u = cur->p[1] + (size_t)my * 8 * d->uvs + (size_t)mx * 8;
            uint8_t *v = cur->p[2] + (size_t)my * 8 * d->uvs + (size_t)mx * 8;
            int ys = d->ys, uvs = d->uvs;

            if (d->lf_simple)
            {
                int mbl = (level + 2) * 2 + interior, bl = level * 2 + interior;
                if (mx)
                    edge_simple(y, 1, ys, mbl);
                if (inner)
                    for (int k = 4; k < 16; k += 4)
                        edge_simple(y + k, 1, ys, bl);
                if (my)
                    edge_simple(y, ys, 1, mbl);
                if (inner)
                    for (int k = 4; k < 16; k += 4)
                        edge_simple(y + k * ys, ys, 1, bl);
                continue;
            }
            if (mx)
            {
                edge_normal(y, 1, ys, 16, level + 2, interior, hev_t, true);
                edge_normal(u, 1, uvs, 8, level + 2, interior, hev_t, true);
                edge_normal(v, 1, uvs, 8, level + 2, interior, hev_t, true);
            }
            if (inner)
            {
                for (int k = 4; k < 16; k += 4)
                    edge_normal(y + k, 1, ys, 16, level, interior, hev_t, false);
                edge_normal(u + 4, 1, uvs, 8, level, interior, hev_t, false);
                edge_normal(v + 4, 1, uvs, 8, level, interior, hev_t, false);
            }
            if (my)
            {
                edge_normal(y, ys, 1, 16, level + 2, interior, hev_t, true);
                edge_normal(u, uvs, 1, 8, level + 2, interior, hev_t, true);
                edge_normal(v, uvs, 1, 8, level + 2, interior, hev_t, true);
            }
            if (inner)
            {
                for (int k = 4; k < 16; k += 4)
                    edge_normal(y + k * ys, ys, 1, 16, level, interior, hev_t, false);
                edge_normal(u + 4 * uvs, uvs, 1, 8, level, interior, hev_t, false);
                edge_normal(v + 4 * uvs, uvs, 1, 8, level, interior, hev_t, false);
            }
        }
}

static void set_ref(Vp8Dec *d, int which, int img)
{
    if (d->ref[which] >= 0)
        d->img[d->ref[which]].refs--;
    d->ref[which] = img;
    if (img >= 0)
        d->img[img].refs++;
}

int vp8dec_decode(Vp8Dec *d, const uint8_t *data, size_t len)
{
    if (len < 3)
        return -1;
    uint32_t tag = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16;
    bool key = !(tag & 1);
    int version = (int)((tag >> 1) & 7);
    bool show = (tag >> 4) & 1;
    size_t part0 = tag >> 5;
    data += 3;
    len -= 3;
    if (version > 3)
        return -1;
    if (key)
    {
        if (len < 7 || data[0] != 0x9D || data[1] != 0x01 || data[2] != 0x2A)
            return -1;
        int w = (data[3] | data[4] << 8) & 0x3FFF, h = (data[5] | data[6] << 8) & 0x3FFF;
        data += 7;
        len -= 7;
        if (!resize(d, w, h))
        {
            d->have_key = false;
            return -1;
        }
    }
    else if (!d->have_key)
        return -1;
    if (part0 > len)
        return -1;

    Bd b;
    bd_init(&b, data, part0);
    d->keyframe = key;
    d->version = version;
    if (key)
    {
        bd_literal(&b, 2);
        d->seg_enabled = d->seg_abs = false;
        memset(d->seg_quant, 0, sizeof(d->seg_quant));
        memset(d->seg_lf, 0, sizeof(d->seg_lf));
        memset(d->ref_delta, 0, sizeof(d->ref_delta));
        memset(d->mode_delta, 0, sizeof(d->mode_delta));
    }
    d->seg_enabled = bd_read(&b, 128);
    d->seg_update_map = false;
    if (d->seg_enabled)
    {
        d->seg_update_map = bd_read(&b, 128);
        if (bd_read(&b, 128))
        {
            d->seg_abs = bd_read(&b, 128);
            for (int i = 0; i < 4; i++)
                d->seg_quant[i] = (int8_t)bd_maybe_signed(&b, 7);
            for (int i = 0; i < 4; i++)
                d->seg_lf[i] = (int8_t)bd_maybe_signed(&b, 6);
        }
        if (d->seg_update_map)
            for (int i = 0; i < 3; i++)
                d->seg_probs[i] = bd_read(&b, 128) ? (uint8_t)bd_literal(&b, 8) : 255;
    }
    d->lf_simple = bd_read(&b, 128);
    d->lf_level = bd_literal(&b, 6);
    d->lf_sharp = bd_literal(&b, 3);
    d->lf_delta = bd_read(&b, 128);
    if (d->lf_delta && bd_read(&b, 128))
    {
        for (int i = 0; i < 4; i++)
            if (bd_read(&b, 128))
            {
                int v = bd_literal(&b, 6);
                d->ref_delta[i] = (int8_t)(bd_read(&b, 128) ? -v : v);
            }
        for (int i = 0; i < 4; i++)
            if (bd_read(&b, 128))
            {
                int v = bd_literal(&b, 6);
                d->mode_delta[i] = (int8_t)(bd_read(&b, 128) ? -v : v);
            }
    }

    int nparts = 1 << bd_literal(&b, 2);
    const uint8_t *pd = data + part0;
    size_t rem = len - part0;
    if (rem < (size_t)(3 * (nparts - 1)))
        return -1;
    const uint8_t *sizes = pd;
    pd += 3 * (nparts - 1);
    rem -= (size_t)(3 * (nparts - 1));
    Bd parts[8];
    for (int i = 0; i < nparts; i++)
    {
        size_t sz = i < nparts - 1 ? (size_t)(sizes[i * 3] | sizes[i * 3 + 1] << 8 | sizes[i * 3 + 2] << 16) : rem;
        if (sz > rem)
            sz = rem;
        bd_init(&parts[i], pd, sz);
        pd += sz;
        rem -= sz;
    }

    int q_index = bd_literal(&b, 7);
    int deltas[5];
    for (int i = 0; i < 5; i++)
        deltas[i] = bd_maybe_signed(&b, 4);

    bool refresh_gf = true, refresh_arf = true, refresh_last = true;
    int copy_gf = 0, copy_arf = 0;
    if (!key)
    {
        refresh_gf = bd_read(&b, 128);
        refresh_arf = bd_read(&b, 128);
        if (!refresh_gf)
            copy_gf = bd_literal(&b, 2);
        if (!refresh_arf)
            copy_arf = bd_literal(&b, 2);
        d->sign_bias[GOLDEN] = bd_read(&b, 128);
        d->sign_bias[ALTREF] = bd_read(&b, 128);
    }
    else
        d->sign_bias[GOLDEN] = d->sign_bias[ALTREF] = 0;
    bool refresh_entropy = bd_read(&b, 128);
    if (!key)
        refresh_last = bd_read(&b, 128);

    if (key)
    {
        memcpy(d->ent.coef, vp8_default_coef_probs, sizeof(d->ent.coef));
        memcpy(d->ent.mv, vp8_default_mv_probs, sizeof(d->ent.mv));
        memcpy(d->ent.ymode, default_ymode_probs, sizeof(d->ent.ymode));
        memcpy(d->ent.uvmode, default_uv_mode_probs, sizeof(d->ent.uvmode));
    }
    if (!refresh_entropy)
        d->saved = d->ent;

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            for (int k = 0; k < 3; k++)
                for (int l = 0; l < 11; l++)
                    if (bd_read(&b, vp8_coef_update_probs[i][j][k][l]))
                        d->ent.coef[i][j][k][l] = (uint8_t)bd_literal(&b, 8);
    d->skip_enabled = bd_read(&b, 128);
    d->skip_prob = d->skip_enabled ? (uint8_t)bd_literal(&b, 8) : 0;
    if (!key)
    {
        d->prob_intra = (uint8_t)bd_literal(&b, 8);
        d->prob_last = (uint8_t)bd_literal(&b, 8);
        d->prob_gf = (uint8_t)bd_literal(&b, 8);
        if (bd_read(&b, 128))
            for (int i = 0; i < 4; i++)
                d->ent.ymode[i] = (uint8_t)bd_literal(&b, 8);
        if (bd_read(&b, 128))
            for (int i = 0; i < 3; i++)
                d->ent.uvmode[i] = (uint8_t)bd_literal(&b, 8);
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 19; j++)
                if (bd_read(&b, vp8_mv_update_probs[i][j]))
                {
                    int x = bd_literal(&b, 7);
                    d->ent.mv[i][j] = (uint8_t)(x ? x << 1 : 1);
                }
    }
    setup_dequant(d, q_index, deltas);

    int cur = -1;
    if (d->ref[CURRENT] >= 0)
        set_ref(d, CURRENT, -1);
    for (int i = 0; i < 4 && cur < 0; i++)
        if (d->img[i].refs == 0)
            cur = i;
    if (cur < 0)
        return -1;
    set_ref(d, CURRENT, cur);
    if (!key && (d->ref[LAST] < 0 || d->ref[GOLDEN] < 0 || d->ref[ALTREF] < 0))
        return -1;

    memset(d->above_ctx, 0, sizeof(*d->above_ctx) * (size_t)d->mbw);
    for (int my = 0; my < d->mbh; my++)
    {
        uint8_t left_ctx[9] = { 0 };
        Bd *tb = &parts[my & (nparts - 1)];
        for (int mx = 0; mx < d->mbw; mx++)
            decode_mb(d, &b, tb, mx, my, left_ctx);
    }
    if (d->lf_level)
        loop_filter(d);

    if (!refresh_entropy)
        d->ent = d->saved;

    if (copy_arf == 1)
        set_ref(d, ALTREF, d->ref[LAST]);
    else if (copy_arf == 2)
        set_ref(d, ALTREF, d->ref[GOLDEN]);
    if (copy_gf == 1)
        set_ref(d, GOLDEN, d->ref[LAST]);
    else if (copy_gf == 2)
        set_ref(d, GOLDEN, d->ref[ALTREF]);
    if (refresh_gf)
        set_ref(d, GOLDEN, cur);
    if (refresh_arf)
        set_ref(d, ALTREF, cur);
    if (refresh_last)
        set_ref(d, LAST, cur);
    if (key)
        d->have_key = true;
    return show ? 1 : 0;
}
