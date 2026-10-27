#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "netsurf/bitmap.h"
#include "netsurf/content.h"

#include "nsx.h"

static void *bitmap_create(int width, int height, enum gui_bitmap_flags flags)
{
    if (width <= 0 || height <= 0 || (int64_t)width * height > 8192 * 8192)
        return NULL;
    NsxBitmap *b = calloc(1, sizeof(NsxBitmap));
    if (!b)
        return NULL;
    b->width = width;
    b->height = height;
    b->opaque = flags & BITMAP_OPAQUE;
    size_t bytes = sizeof(uint32_t) * (size_t)width * (size_t)height;
    b->pixels = malloc(bytes);
    if (!b->pixels)
    {
        free(b);
        return NULL;
    }
    if (flags & BITMAP_CLEAR)
        memset(b->pixels, 0, bytes);
    return b;
}

static void bitmap_destroy(void *bitmap)
{
    NsxBitmap *b = bitmap;
    if (!b)
        return;
    free(b->pixels);
    free(b->scaled);
    free(b);
}

static void bitmap_set_opaque(void *bitmap, bool opaque)
{
    ((NsxBitmap *)bitmap)->opaque = opaque;
}

static bool bitmap_get_opaque(void *bitmap)
{
    return ((NsxBitmap *)bitmap)->opaque;
}

static unsigned char *bitmap_get_buffer(void *bitmap)
{
    return (unsigned char *)((NsxBitmap *)bitmap)->pixels;
}

static size_t bitmap_get_rowstride(void *bitmap)
{
    return (size_t)((NsxBitmap *)bitmap)->width * 4;
}

static int bitmap_get_width(void *bitmap)
{
    return ((NsxBitmap *)bitmap)->width;
}

static int bitmap_get_height(void *bitmap)
{
    return ((NsxBitmap *)bitmap)->height;
}

static void bitmap_modified(void *bitmap)
{
    NsxBitmap *b = bitmap;
    free(b->scaled);
    b->scaled = NULL;
    b->scaled_w = b->scaled_h = 0;
}

static nserror bitmap_render(struct bitmap *bitmap, struct hlcache_handle *content)
{
    (void)bitmap;
    (void)content;
    return NSERROR_NOT_IMPLEMENTED;
}

static struct gui_bitmap_table bitmap_table = {
    .create = bitmap_create,
    .destroy = bitmap_destroy,
    .set_opaque = bitmap_set_opaque,
    .get_opaque = bitmap_get_opaque,
    .get_buffer = bitmap_get_buffer,
    .get_rowstride = bitmap_get_rowstride,
    .get_width = bitmap_get_width,
    .get_height = bitmap_get_height,
    .modified = bitmap_modified,
    .render = bitmap_render,
};

struct gui_bitmap_table *nsx_bitmap_table = &bitmap_table;
