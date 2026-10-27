#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "netsurf/bitmap.h"
#include "content/llcache.h"
#include "content/content.h"
#include "content/content_protected.h"
#include "content/content_factory.h"
#include "desktop/gui_internal.h"
#include "image/image_cache.h"

#include "nsx.h"

static nserror sximg_create(const content_handler *handler, lwc_string *imime_type,
                             const struct http_parameter *params, llcache_handle *llcache,
                             const char *fallback_charset, bool quirks, struct content **c)
{
    struct content *img = calloc(1, sizeof(struct content));
    if (!img)
        return NSERROR_NOMEM;
    nserror error = content__init(img, handler, imime_type, params, llcache, fallback_charset, quirks);
    if (error != NSERROR_OK)
    {
        free(img);
        return error;
    }
    *c = img;
    return NSERROR_OK;
}

static struct bitmap *sximg_cache_convert(struct content *c)
{
    size_t size;
    const uint8_t *data = content__get_source_data(c, &size);
    if (!data || size == 0 || size > INT32_MAX)
        return NULL;

    SxImage *img = sx_image_decode(data, size);
    if (!img)
        return NULL;

    size_t count = (size_t)img->width * (size_t)img->height;
    bool opaque = true;
    for (size_t i = 0; i < count && opaque; i++)
        opaque = (img->pixels[i] >> 24) == 0xFF;

    struct bitmap *bitmap = guit->bitmap->create(img->width, img->height, opaque ? BITMAP_OPAQUE : BITMAP_NONE);
    if (!bitmap)
    {
        sx_image_free(img);
        return NULL;
    }

    memcpy(guit->bitmap->get_buffer(bitmap), img->pixels, count * 4);
    sx_image_free(img);
    guit->bitmap->modified(bitmap);
    return bitmap;
}

static bool sximg_convert(struct content *c)
{
    size_t size;
    const uint8_t *data = content__get_source_data(c, &size);
    int w, h;
    if (!data || !sx_image_info(data, size, &w, &h))
    {
        union content_msg_data msg;
        msg.errordata.errorcode = NSERROR_UNKNOWN;
        msg.errordata.errormsg = "Unable to decode image";
        content_broadcast(c, CONTENT_MSG_ERROR, &msg);
        return false;
    }

    c->width = w;
    c->height = h;
    c->size = (size_t)w * (size_t)h * 4;
    image_cache_add(c, NULL, sximg_cache_convert);

    const char *mime = lwc_string_data(content__get_mime_type(c));
    char *title = messages_get_buff(strstr(mime, "png") ? "PNGTitle" : "JPEGTitle",
                                    nsurl_access_leaf(llcache_handle_get_url(c->llcache)), w, h);
    if (title)
    {
        content__set_title(c, title);
        free(title);
    }

    content_set_ready(c);
    content_set_done(c);
    content_set_status(c, "");
    return true;
}

static nserror sximg_clone(const struct content *old, struct content **newc)
{
    struct content *c = calloc(1, sizeof(struct content));
    if (!c)
        return NSERROR_NOMEM;
    nserror error = content__clone(old, c);
    if (error != NSERROR_OK)
    {
        content_destroy(c);
        return error;
    }
    if ((old->status == CONTENT_STATUS_READY || old->status == CONTENT_STATUS_DONE) && !sximg_convert(c))
    {
        content_destroy(c);
        return NSERROR_CLONE_FAILED;
    }
    *newc = c;
    return NSERROR_OK;
}

static const content_handler sximg_content_handler = {
    .create = sximg_create,
    .data_complete = sximg_convert,
    .destroy = image_cache_destroy,
    .redraw = image_cache_redraw,
    .clone = sximg_clone,
    .get_internal = image_cache_get_internal,
    .type = image_cache_content_type,
    .is_opaque = image_cache_is_opaque,
    .no_share = false,
};

static const char *nspng_types[] = {
    "image/png",
    "image/x-png",
};

static const char *nsjpeg_types[] = {
    "image/jpeg",
    "image/jpg",
    "image/pjpeg",
};

CONTENT_FACTORY_REGISTER_TYPES(nspng, nspng_types, sximg_content_handler);
CONTENT_FACTORY_REGISTER_TYPES(nsjpeg, nsjpeg_types, sximg_content_handler);
