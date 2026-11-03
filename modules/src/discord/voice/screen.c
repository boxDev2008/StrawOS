#include <stdlib.h>
#include <string.h>
#include <strawx.h>
#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include "screen.h"

struct Screen
{
    int             max_w, max_h;
    int             kbps, fps;
    int             sw, sh;
    int             w, h;
    uint32_t       *cur;
    uint32_t       *last;
    size_t          cap;
    bool            have_last;
    int            *xmap;
    int            *ymap;
    uint8_t        *planes;
    vpx_image_t     img;
    vpx_codec_ctx_t enc;
    bool            enc_ok;
    int64_t         pts;
};

static void fit(int sw, int sh, int max_w, int max_h, int *w, int *h)
{
    int64_t tw = sw, th = sh;
    if (tw > max_w)
    {
        th = th * max_w / tw;
        tw = max_w;
    }
    if (th > max_h)
    {
        tw = tw * max_h / th;
        th = max_h;
    }
    *w = (int)tw & ~1;
    *h = (int)th & ~1;
}

static void release(Screen *s)
{
    if (s->enc_ok)
        vpx_codec_destroy(&s->enc);
    s->enc_ok = false;
    free(s->cur);
    free(s->last);
    free(s->xmap);
    free(s->ymap);
    free(s->planes);
    s->cur = s->last = NULL;
    s->xmap = s->ymap = NULL;
    s->planes = NULL;
    s->have_last = false;
}

static bool start_encoder(Screen *s)
{
    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK)
        return false;
    cfg.g_w = (unsigned)s->w;
    cfg.g_h = (unsigned)s->h;
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = 1000;
    cfg.g_threads = 1;
    cfg.g_lag_in_frames = 0;
    cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
    cfg.rc_end_usage = VPX_CBR;
    cfg.rc_target_bitrate = (unsigned)s->kbps;
    cfg.rc_min_quantizer = 4;
    cfg.rc_max_quantizer = 56;
    cfg.rc_dropframe_thresh = 0;
    cfg.rc_undershoot_pct = 95;
    cfg.rc_overshoot_pct = 15;
    cfg.rc_buf_sz = 1000;
    cfg.rc_buf_initial_sz = 500;
    cfg.rc_buf_optimal_sz = 600;
    cfg.kf_mode = VPX_KF_DISABLED;
    if (vpx_codec_enc_init(&s->enc, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK)
        return false;
    s->enc_ok = true;
    vpx_codec_control(&s->enc, VP8E_SET_CPUUSED, 16);
    vpx_codec_control(&s->enc, VP8E_SET_SCREEN_CONTENT_MODE, 1);
    vpx_codec_control(&s->enc, VP8E_SET_STATIC_THRESHOLD, 1);
    vpx_codec_control(&s->enc, VP8E_SET_NOISE_SENSITIVITY, 0);
    vpx_codec_control(&s->enc, VP8E_SET_TOKEN_PARTITIONS, VP8_ONE_TOKENPARTITION);
    vpx_codec_control(&s->enc, VP8E_SET_MAX_INTRA_BITRATE_PCT, 600);
    return true;
}

static bool setup(Screen *s, int sw, int sh)
{
    release(s);
    s->sw = sw;
    s->sh = sh;
    fit(sw, sh, s->max_w, s->max_h, &s->w, &s->h);
    s->cap = (size_t)sw * (size_t)sh;
    s->cur = malloc(s->cap * 4);
    s->last = malloc(s->cap * 4);
    s->xmap = malloc(sizeof(int) * (size_t)(s->w + 1));
    s->ymap = malloc(sizeof(int) * (size_t)(s->h + 1));
    s->planes = malloc((size_t)s->w * (size_t)s->h * 3 / 2);
    if (!s->cur || !s->last || !s->xmap || !s->ymap || !s->planes)
        return false;
    vpx_img_wrap(&s->img, VPX_IMG_FMT_I420, (unsigned)s->w, (unsigned)s->h, 1, s->planes);
    for (int x = 0; x <= s->w; x++)
        s->xmap[x] = (int)((int64_t)x * sw / s->w);
    for (int y = 0; y <= s->h; y++)
        s->ymap[y] = (int)((int64_t)y * sh / s->h);
    return start_encoder(s);
}

Screen *screen_open(int max_w, int max_h, int kbps, int fps)
{
    int sw = 0, sh = 0;
    sx_screen_capture(NULL, 0, &sw, &sh);
    if (sw < 16 || sh < 16)
        return NULL;
    Screen *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->max_w = max_w;
    s->max_h = max_h;
    s->kbps = kbps;
    s->fps = fps;
    if (!setup(s, sw, sh))
    {
        screen_close(s);
        return NULL;
    }
    return s;
}

void screen_close(Screen *s)
{
    if (!s)
        return;
    release(s);
    free(s);
}

int screen_width(const Screen *s)
{
    return s->w;
}

int screen_height(const Screen *s)
{
    return s->h;
}

static uint32_t sample(const Screen *s, const uint32_t *px, int x, int y)
{
    if (s->w == s->sw && s->h == s->sh)
        return px[(size_t)y * s->sw + x];
    int x0 = s->xmap[x], x1 = s->xmap[x + 1], y0 = s->ymap[y], y1 = s->ymap[y + 1];
    if (x1 <= x0)
        x1 = x0 + 1;
    if (y1 <= y0)
        y1 = y0 + 1;
    uint32_t r = 0, g = 0, b = 0, n = 0;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            uint32_t p = px[(size_t)yy * s->sw + xx];
            r += (p >> 16) & 0xFF;
            g += (p >> 8) & 0xFF;
            b += p & 0xFF;
            n++;
        }
    return (r / n) << 16 | (g / n) << 8 | (b / n);
}

static void convert(Screen *s, const uint32_t *px)
{
    vpx_image_t *im = &s->img;
    for (int y = 0; y < s->h; y += 2)
    {
        uint8_t *y0 = im->planes[VPX_PLANE_Y] + (size_t)y * (size_t)im->stride[VPX_PLANE_Y];
        uint8_t *y1 = y0 + im->stride[VPX_PLANE_Y];
        uint8_t *u = im->planes[VPX_PLANE_U] + (size_t)(y / 2) * (size_t)im->stride[VPX_PLANE_U];
        uint8_t *v = im->planes[VPX_PLANE_V] + (size_t)(y / 2) * (size_t)im->stride[VPX_PLANE_V];
        for (int x = 0; x < s->w; x += 2)
        {
            uint32_t q[4] = { sample(s, px, x, y), sample(s, px, x + 1, y), sample(s, px, x, y + 1),
                              sample(s, px, x + 1, y + 1) };
            int rs = 0, gs = 0, bs = 0;
            for (int i = 0; i < 4; i++)
            {
                int r = (q[i] >> 16) & 0xFF, g = (q[i] >> 8) & 0xFF, b = q[i] & 0xFF;
                (i < 2 ? y0 : y1)[x + (i & 1)] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                rs += r;
                gs += g;
                bs += b;
            }
            rs = (rs + 2) >> 2;
            gs = (gs + 2) >> 2;
            bs = (bs + 2) >> 2;
            u[x / 2] = (uint8_t)(((-38 * rs - 74 * gs + 112 * bs + 128) >> 8) + 128);
            v[x / 2] = (uint8_t)(((112 * rs - 94 * gs - 18 * bs + 128) >> 8) + 128);
        }
    }
}

int screen_grab(Screen *s)
{
    int sw = 0, sh = 0;
    if (!sx_screen_capture(s->cur, s->cap, &sw, &sh))
    {
        if (sw < 16 || sh < 16 || (sw == s->sw && sh == s->sh) || !setup(s, sw, sh))
            return -1;
        if (!sx_screen_capture(s->cur, s->cap, &sw, &sh))
            return -1;
    }
    else if (sw != s->sw || sh != s->sh)
    {
        if (!setup(s, sw, sh) || !sx_screen_capture(s->cur, s->cap, &sw, &sh))
            return -1;
    }
    if (s->have_last && memcmp(s->cur, s->last, s->cap * 4) == 0)
        return 0;
    uint32_t *t = s->last;
    s->last = s->cur;
    s->cur = t;
    s->have_last = true;
    convert(s, s->last);
    return 1;
}

size_t screen_encode(Screen *s, bool key, uint64_t now_ms, uint8_t *out, size_t cap)
{
    if (!s->have_last || !s->enc_ok)
        return 0;
    int64_t pts = (int64_t)now_ms;
    if (pts <= s->pts)
        pts = s->pts + 1;
    unsigned long duration = (unsigned long)(pts - s->pts);
    if (!s->pts || duration > 1000)
        duration = (unsigned long)(1000 / s->fps);
    s->pts = pts;
    if (vpx_codec_encode(&s->enc, &s->img, pts, duration, key ? VPX_EFLAG_FORCE_KF : 0, VPX_DL_REALTIME) !=
        VPX_CODEC_OK)
        return 0;
    size_t len = 0;
    vpx_codec_iter_t iter = NULL;
    const vpx_codec_cx_pkt_t *pkt;
    while ((pkt = vpx_codec_get_cx_data(&s->enc, &iter)))
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT && len + pkt->data.frame.sz <= cap)
        {
            memcpy(out + len, pkt->data.frame.buf, pkt->data.frame.sz);
            len += pkt->data.frame.sz;
        }
    return len;
}
