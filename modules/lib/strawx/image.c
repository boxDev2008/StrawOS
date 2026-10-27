#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SX_IMAGE_MAX_PIXELS (32u * 1024u * 1024u)

static const char *sx_image_err = "";

const char *sx_image_error(void)
{
    return sx_image_err;
}

static void *sx_image_fail(const char *msg)
{
    sx_image_err = msg;
    return NULL;
}

SxImage *sx_image_create(int width, int height)
{
    if (width <= 0 || height <= 0 || (uint64_t)width * (uint64_t)height > SX_IMAGE_MAX_PIXELS)
        return sx_image_fail("image is too large");

    SxImage *img = malloc(sizeof(SxImage));
    if (!img)
        return sx_image_fail("out of memory");
    img->pixels = malloc((size_t)width * (size_t)height * 4);
    if (!img->pixels)
    {
        free(img);
        return sx_image_fail("out of memory");
    }
    img->width = width;
    img->height = height;
    return img;
}

void sx_image_free(SxImage *img)
{
    if (!img)
        return;
    free(img->pixels);
    free(img);
}

uint8_t *sx_inflate_alloc(const uint8_t *src, size_t size, size_t max_out, size_t *out_len)
{
    if (size > INT32_MAX)
        return NULL;
    int len = 0;
    char *out = stbi_zlib_decode_noheader_malloc((const char *)src, (int)size, &len);
    if (!out)
        return NULL;
    if ((size_t)len > max_out)
    {
        free(out);
        return NULL;
    }
    *out_len = (size_t)len;
    return (uint8_t *)out;
}

bool sx_image_info(const uint8_t *data, size_t size, int *width, int *height)
{
    int comp;
    if (size == 0 || size > INT32_MAX)
        return false;
    return stbi_info_from_memory(data, (int)size, width, height, &comp) != 0;
}

SxImage *sx_image_decode(const uint8_t *data, size_t size)
{
    int w, h, comp;
    if (!sx_image_info(data, size, &w, &h))
        return sx_image_fail("not a supported image format");
    if ((uint64_t)w * (uint64_t)h > SX_IMAGE_MAX_PIXELS)
        return sx_image_fail("image is too large");

    unsigned char *rgba = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!rgba)
        return sx_image_fail("the image data is damaged");

    SxImage *img = sx_image_create(w, h);
    if (img)
    {
        size_t count = (size_t)w * (size_t)h;
        for (size_t i = 0; i < count; i++)
        {
            const unsigned char *p = rgba + i * 4;
            img->pixels[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
    }
    stbi_image_free(rgba);
    return img;
}

SxImage *sx_image_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return sx_image_fail("can't open the file");

    struct stat st;
    if (fstat(fd, &st) != 0 || st.size == 0)
    {
        close(fd);
        return sx_image_fail("empty file");
    }

    uint8_t *data = malloc(st.size);
    if (!data)
    {
        close(fd);
        return sx_image_fail("out of memory");
    }

    size_t got = 0;
    while (got < st.size)
    {
        ssize_t n = read(fd, data + got, st.size - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    close(fd);

    SxImage *img = sx_image_decode(data, got);
    free(data);
    return img;
}

bool sx_image_is_supported(const char *path)
{
    static const char *const exts[] = { "png", "jpg", "jpeg", "bmp", "gif", "tga", "ppm", "pgm", "pnm", NULL };
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    char ext[8];
    int n = 0;
    for (const char *p = dot + 1; *p && n < 7; p++)
        ext[n++] = (char)tolower((unsigned char)*p);
    ext[n] = '\0';
    for (int i = 0; exts[i]; i++)
        if (strcmp(ext, exts[i]) == 0)
            return true;
    return false;
}

static uint32_t sx_premultiply(uint32_t p)
{
    uint32_t a = p >> 24;
    uint32_t rb = ((p & 0xFF00FF) * a + 0x800080) >> 8 & 0xFF00FF;
    uint32_t g = ((p & 0x00FF00) * a + 0x008000) >> 8 & 0x00FF00;
    return (a << 24) | rb | g;
}

static SxImage *sx_image_downscale(const SxImage *img, int w, int h)
{
    SxImage *out = sx_image_create(w, h);
    if (!out)
        return NULL;

    for (int y = 0; y < h; y++)
    {
        int sy0 = (int)((int64_t)y * img->height / h);
        int sy1 = (int)((int64_t)(y + 1) * img->height / h);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;

        for (int x = 0; x < w; x++)
        {
            int sx0 = (int)((int64_t)x * img->width / w);
            int sx1 = (int)((int64_t)(x + 1) * img->width / w);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;

            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy++)
            {
                const uint32_t *row = img->pixels + (size_t)yy * img->width;
                for (int xx = sx0; xx < sx1; xx++)
                {
                    uint32_t p = sx_premultiply(row[xx]);
                    sa += p >> 24;
                    sr += (p >> 16) & 0xFF;
                    sg += (p >> 8) & 0xFF;
                    sb += p & 0xFF;
                    n++;
                }
            }

            uint32_t a = (sa + n / 2) / n;
            uint32_t r = 0, g = 0, b = 0;
            if (a)
            {
                r = (sr * 255 / n + a / 2) / a;
                g = (sg * 255 / n + a / 2) / a;
                b = (sb * 255 / n + a / 2) / a;
            }
            out->pixels[(size_t)y * w + x] = (a << 24) | ((r > 255 ? 255 : r) << 16) |
                                             ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
        }
    }
    return out;
}

static SxImage *sx_image_upscale(const SxImage *img, int w, int h)
{
    SxImage *out = sx_image_create(w, h);
    if (!out)
        return NULL;

    for (int y = 0; y < h; y++)
    {
        float fy = (y + 0.5f) * img->height / h - 0.5f;
        if (fy < 0)
            fy = 0;
        int y0 = (int)fy;
        int y1 = y0 + 1 < img->height ? y0 + 1 : y0;
        uint32_t ty = (uint32_t)((fy - y0) * 256);

        for (int x = 0; x < w; x++)
        {
            float fx = (x + 0.5f) * img->width / w - 0.5f;
            if (fx < 0)
                fx = 0;
            int x0 = (int)fx;
            int x1 = x0 + 1 < img->width ? x0 + 1 : x0;
            uint32_t tx = (uint32_t)((fx - x0) * 256);

            uint32_t p00 = sx_premultiply(img->pixels[(size_t)y0 * img->width + x0]);
            uint32_t p01 = sx_premultiply(img->pixels[(size_t)y0 * img->width + x1]);
            uint32_t p10 = sx_premultiply(img->pixels[(size_t)y1 * img->width + x0]);
            uint32_t p11 = sx_premultiply(img->pixels[(size_t)y1 * img->width + x1]);

            uint32_t c[4];
            for (int ch = 0; ch < 4; ch++)
            {
                int shift = ch * 8;
                uint32_t a = (p00 >> shift) & 0xFF, b = (p01 >> shift) & 0xFF;
                uint32_t d = (p10 >> shift) & 0xFF, e = (p11 >> shift) & 0xFF;
                uint32_t top = a * (256 - tx) + b * tx;
                uint32_t bot = d * (256 - tx) + e * tx;
                c[ch] = (top * (256 - ty) + bot * ty + 32768) >> 16;
            }

            uint32_t al = c[3];
            uint32_t r = al ? c[2] * 255 / al : 0, g = al ? c[1] * 255 / al : 0, b = al ? c[0] * 255 / al : 0;
            out->pixels[(size_t)y * w + x] = (al << 24) | ((r > 255 ? 255 : r) << 16) |
                                             ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
        }
    }
    return out;
}

SxImage *sx_image_scale(const SxImage *img, int w, int h)
{
    if (!img || w <= 0 || h <= 0)
        return NULL;
    if (w <= img->width && h <= img->height)
        return sx_image_downscale(img, w, h);
    return sx_image_upscale(img, w, h);
}

void sx_draw_image(SxCanvas *c, const SxImage *img, int x, int y)
{
    SxRect r = sx_rect_intersect(sx_rect(x, y, img->width, img->height), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        const uint32_t *src = img->pixels + (size_t)(yy - y) * img->width + (r.x - x);
        uint32_t *dst = c->pixels + (size_t)yy * c->stride + r.x;
        for (int xx = 0; xx < r.w; xx++)
        {
            uint32_t p = src[xx];
            uint32_t a = p >> 24;
            if (a == 255)
                dst[xx] = p;
            else if (a)
                dst[xx] = sx_blend(dst[xx], p, a);
        }
    }
}

void sx_draw_image_scaled(SxCanvas *c, const SxImage *img, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    SxRect r = sx_rect_intersect(sx_rect(x, y, w, h), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++)
    {
        int sy = (int)((int64_t)(yy - y) * img->height / h);
        const uint32_t *src = img->pixels + (size_t)sy * img->width;
        uint32_t *dst = c->pixels + (size_t)yy * c->stride;
        for (int xx = r.x; xx < r.x + r.w; xx++)
        {
            uint32_t p = src[(int64_t)(xx - x) * img->width / w];
            uint32_t a = p >> 24;
            if (a == 255)
                dst[xx] = p;
            else if (a)
                dst[xx] = sx_blend(dst[xx], p, a);
        }
    }
}
