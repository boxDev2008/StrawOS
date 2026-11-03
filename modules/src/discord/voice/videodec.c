#include <stdlib.h>
#include <string.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include "videodec.h"

struct VideoDec
{
    vpx_codec_ctx_t ctx;
    vpx_image_t    *img;
};

static vpx_codec_iface_t *iface(bool vp9)
{
    return vp9 ? vpx_codec_vp9_dx() : vpx_codec_vp8_dx();
}

VideoDec *videodec_new(bool vp9)
{
    VideoDec *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    vpx_codec_dec_cfg_t cfg = { 1, 0, 0 };
    if (vpx_codec_dec_init(&d->ctx, iface(vp9), &cfg, 0) != VPX_CODEC_OK)
    {
        free(d);
        return NULL;
    }
    return d;
}

void videodec_free(VideoDec *d)
{
    if (!d)
        return;
    vpx_codec_destroy(&d->ctx);
    free(d);
}

int videodec_decode(VideoDec *d, const uint8_t *data, size_t len)
{
    if (vpx_codec_decode(&d->ctx, data, (unsigned int)len, NULL, 0) != VPX_CODEC_OK)
        return -1;
    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img, *last = NULL;
    while ((img = vpx_codec_get_frame(&d->ctx, &iter)))
        last = img;
    if (!last)
        return 0;
    d->img = last;
    return 1;
}

bool videodec_image(VideoDec *d, VideoImage *out)
{
    vpx_image_t *img = d->img;
    if (!img || img->fmt != VPX_IMG_FMT_I420)
        return false;
    out->y = img->planes[VPX_PLANE_Y];
    out->u = img->planes[VPX_PLANE_U];
    out->v = img->planes[VPX_PLANE_V];
    out->y_stride = img->stride[VPX_PLANE_Y];
    out->uv_stride = img->stride[VPX_PLANE_U];
    out->width = (int)img->d_w;
    out->height = (int)img->d_h;
    return true;
}

bool videodec_is_keyframe(bool vp9, const uint8_t *data, size_t len)
{
    vpx_codec_stream_info_t si;
    memset(&si, 0, sizeof(si));
    si.sz = sizeof(si);
    return vpx_codec_peek_stream_info(iface(vp9), data, (unsigned int)len, &si) == VPX_CODEC_OK && si.is_kf;
}
