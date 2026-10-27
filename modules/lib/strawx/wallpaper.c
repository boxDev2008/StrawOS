static uint32_t sx_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846CA68B;
    x ^= x >> 16;
    return x;
}

typedef struct
{
    float cx, cy, radius;
    float r, g, b, strength;
}
SxGlow;

void sx_render_default_wallpaper(uint32_t *px, int w, int h)
{
    float fw = (float)w, fh = (float)h;
    const SxGlow glows[] = {
        { 0.86f * fw, 0.92f * fh, 0.75f * fw, 240, 160,  60, 0.55f },
        { 0.10f * fw, 0.05f * fh, 0.70f * fw,  60, 110, 240, 0.45f },
        { 0.55f * fw, 0.35f * fh, 0.45f * fw, 140,  80, 220, 0.30f },
    };

    for (int y = 0; y < h; y++)
    {
        float t = (float)y / fh;
        float base_r = 22 + (11 - 22) * t;
        float base_g = 26 + (12 - 26) * t;
        float base_b = 44 + (20 - 44) * t;

        for (int x = 0; x < w; x++)
        {
            float r = base_r, g = base_g, b = base_b;

            for (unsigned i = 0; i < sizeof(glows) / sizeof(glows[0]); i++)
            {
                const SxGlow *gl = &glows[i];
                float dx = (x - gl->cx) / gl->radius;
                float dy = (y - gl->cy) / gl->radius;
                float d2 = dx * dx + dy * dy;
                if (d2 >= 1.0f)
                    continue;
                float k = 1.0f - d2;
                k = k * k * gl->strength;
                r += (gl->r - r) * k;
                g += (gl->g - g) * k;
                b += (gl->b - b) * k;
            }

            float n = (float)(sx_hash32((uint32_t)(y * w + x)) & 0xFF) / 255.0f - 0.5f;
            r += n * 1.5f;
            g += n * 1.5f;
            b += n * 1.5f;

            int ir = (int)(r + 0.5f), ig = (int)(g + 0.5f), ib = (int)(b + 0.5f);
            ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
            ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
            ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
            px[(size_t)y * w + x] = SX_RGB(ir, ig, ib);
        }
    }
}

static bool sx_parse_color(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    for (; *s && n < 6; s++, n++)
    {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return false;
        v = v * 16 + (uint32_t)d;
    }
    if (n != 6)
        return false;
    *out = 0xFF000000u | v;
    return true;
}

static bool sx_render_image_wallpaper(const char *path, uint32_t *px, int w, int h)
{
    SxImage *img = sx_image_load(path);
    if (!img)
        return false;

    float scale_x = (float)w / img->width, scale_y = (float)h / img->height;
    float scale = scale_x > scale_y ? scale_x : scale_y;
    int sw = (int)(img->width * scale + 0.5f), sh = (int)(img->height * scale + 0.5f);
    if (sw < w) sw = w;
    if (sh < h) sh = h;

    SxImage *scaled = sx_image_scale(img, sw, sh);
    sx_image_free(img);
    if (!scaled)
        return false;

    int ox = (sw - w) / 2, oy = (sh - h) / 2;
    for (int y = 0; y < h; y++)
    {
        const uint32_t *src = scaled->pixels + (size_t)(y + oy) * sw + ox;
        uint32_t *dst = px + (size_t)y * w;
        for (int x = 0; x < w; x++)
            dst[x] = sx_blend(0xFF000000u, src[x], src[x] >> 24);
    }
    sx_image_free(scaled);
    return true;
}

void sx_render_wallpaper(const char *spec, uint32_t *px, int w, int h)
{
    uint32_t color;
    if (spec && strncmp(spec, "color:", 6) == 0 && sx_parse_color(spec + 6, &color))
    {
        for (size_t i = 0; i < (size_t)w * h; i++)
            px[i] = color;
        return;
    }

    if (spec && spec[0] == '/' && sx_render_image_wallpaper(spec, px, w, h))
        return;

    sx_render_default_wallpaper(px, w, h);
}
