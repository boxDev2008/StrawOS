#include <stdlib.h>
#include <string.h>

#include "discord.h"

#define QR_MAX_VERSION 10
#define QR_MAX_SIZE    (QR_MAX_VERSION * 4 + 17)

static const int8_t ecc_per_block[QR_MAX_VERSION + 1] = { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
static const int8_t block_count[QR_MAX_VERSION + 1]   = { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 };

#define ECL_M_BITS 0

typedef struct
{
    int     size;
    uint8_t dark[QR_MAX_SIZE][QR_MAX_SIZE];
    uint8_t func[QR_MAX_SIZE][QR_MAX_SIZE];
}
Grid;

static int raw_modules(int ver)
{
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2)
    {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7)
            r -= 36;
    }
    return r;
}

static int data_codewords(int ver)
{
    return raw_modules(ver) / 8 - ecc_per_block[ver] * block_count[ver];
}

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
    int z = 0;
    for (int i = 7; i >= 0; i--)
    {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

static void rs_divisor(int degree, uint8_t *out)
{
    memset(out, 0, (size_t)degree);
    out[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++)
    {
        for (int j = 0; j < degree; j++)
        {
            out[j] = gf_mul(out[j], root);
            if (j + 1 < degree)
                out[j] ^= out[j + 1];
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_remainder(const uint8_t *data, int len, const uint8_t *div, int degree, uint8_t *out)
{
    memset(out, 0, (size_t)degree);
    for (int i = 0; i < len; i++)
    {
        uint8_t factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (int j = 0; j < degree; j++)
            out[j] ^= gf_mul(div[j], factor);
    }
}

static void set_func(Grid *g, int x, int y, bool dark)
{
    g->dark[y][x] = dark;
    g->func[y][x] = 1;
}

static void draw_finder(Grid *g, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++)
        {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= g->size || y >= g->size)
                continue;
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            int dist = ax > ay ? ax : ay;
            set_func(g, x, y, dist != 2 && dist != 4);
        }
}

static void draw_alignment(Grid *g, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
        {
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            set_func(g, cx + dx, cy + dy, (ax > ay ? ax : ay) != 1);
        }
}

static int alignment_positions(int ver, int *out)
{
    if (ver == 1)
        return 0;
    int na = ver / 7 + 2;
    int step = (ver * 8 + na * 3 + 5) / (na * 4 - 4) * 2;
    out[0] = 6;
    for (int i = na - 1, pos = ver * 4 + 10; i >= 1; i--, pos -= step)
        out[i] = pos;
    return na;
}

static void draw_format(Grid *g, int mask)
{
    int data = ECL_M_BITS << 3 | mask;
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    int size = g->size;
    for (int i = 0; i <= 5; i++)
        set_func(g, 8, i, (bits >> i) & 1);
    set_func(g, 8, 7, (bits >> 6) & 1);
    set_func(g, 8, 8, (bits >> 7) & 1);
    set_func(g, 7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++)
        set_func(g, 14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++)
        set_func(g, size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++)
        set_func(g, 8, size - 15 + i, (bits >> i) & 1);
    set_func(g, 8, size - 8, true);
}

static void draw_version(Grid *g, int ver)
{
    if (ver < 7)
        return;
    int rem = ver;
    for (int i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = (long)ver << 12 | rem;
    for (int i = 0; i < 18; i++)
    {
        bool bit = (bits >> i) & 1;
        int a = g->size - 11 + i % 3;
        int b = i / 3;
        set_func(g, a, b, bit);
        set_func(g, b, a, bit);
    }
}

static void draw_patterns(Grid *g, int ver)
{
    int size = g->size;
    for (int i = 0; i < size; i++)
    {
        set_func(g, 6, i, i % 2 == 0);
        set_func(g, i, 6, i % 2 == 0);
    }
    draw_finder(g, 3, 3);
    draw_finder(g, size - 4, 3);
    draw_finder(g, 3, size - 4);
    int pos[8];
    int n = alignment_positions(ver, pos);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)))
                draw_alignment(g, pos[i], pos[j]);
    draw_format(g, 0);
    draw_version(g, ver);
}

static void draw_codewords(Grid *g, const uint8_t *data, int len)
{
    int size = g->size;
    int i = 0;
    for (int right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size; vert++)
            for (int j = 0; j < 2; j++)
            {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (!g->func[y][x] && i < len * 8)
                {
                    g->dark[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
    }
}

static bool mask_bit(int mask, int x, int y)
{
    switch (mask)
    {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(Grid *g, int mask)
{
    for (int y = 0; y < g->size; y++)
        for (int x = 0; x < g->size; x++)
            if (!g->func[y][x] && mask_bit(mask, x, y))
                g->dark[y][x] ^= 1;
}

static long penalty(const Grid *g)
{
    int size = g->size;
    long score = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int a = 0; a < size; a++)
        {
            int run = 1;
            for (int b = 1; b < size; b++)
            {
                bool cur = pass ? g->dark[b][a] : g->dark[a][b];
                bool prev = pass ? g->dark[b - 1][a] : g->dark[a][b - 1];
                if (cur == prev)
                {
                    run++;
                    if (run == 5)
                        score += 3;
                    else if (run > 5)
                        score++;
                }
                else
                {
                    run = 1;
                }
            }
        }
    int dark = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            dark += g->dark[y][x];
            if (x + 1 < size && y + 1 < size)
            {
                bool c = g->dark[y][x];
                if (c == g->dark[y][x + 1] && c == g->dark[y + 1][x] && c == g->dark[y + 1][x + 1])
                    score += 3;
            }
        }
    long total = (long)size * size;
    long k = (labs(dark * 20L - total * 10L) + total - 1) / total - 1;
    if (k > 0)
        score += k * 10;
    return score;
}

bool qr_encode(const char *text, QrCode *out)
{
    memset(out, 0, sizeof(*out));
    int len = (int)strlen(text);
    int ver = 1;
    for (; ver <= QR_MAX_VERSION; ver++)
    {
        int bits = 4 + (ver < 10 ? 8 : 16) + len * 8;
        if (bits <= data_codewords(ver) * 8)
            break;
    }
    if (ver > QR_MAX_VERSION)
        return false;

    int cap = data_codewords(ver);
    uint8_t data[512];
    memset(data, 0, sizeof(data));
    int bitpos = 0;
#define PUT(val, n) do { for (int _i = (n) - 1; _i >= 0; _i--, bitpos++) if (((val) >> _i) & 1) data[bitpos >> 3] |= (uint8_t)(0x80 >> (bitpos & 7)); } while (0)
    PUT(4, 4);
    PUT(len, ver < 10 ? 8 : 16);
    for (int i = 0; i < len; i++)
        PUT((uint8_t)text[i], 8);
    int term = cap * 8 - bitpos;
    PUT(0, term < 4 ? term : 4);
    bitpos = (bitpos + 7) & ~7;
#undef PUT
    for (int i = bitpos / 8, pad = 0xEC; i < cap; i++, pad ^= 0xEC ^ 0x11)
        data[i] = (uint8_t)pad;

    int nblocks = block_count[ver];
    int ecc_len = ecc_per_block[ver];
    int raw = raw_modules(ver) / 8;
    int short_blocks = nblocks - raw % nblocks;
    int short_len = raw / nblocks;
    uint8_t div[32];
    rs_divisor(ecc_len, div);

    uint8_t blocks[8][160];
    for (int i = 0, k = 0; i < nblocks; i++)
    {
        int dat_len = short_len - ecc_len + (i < short_blocks ? 0 : 1);
        memset(blocks[i], 0, sizeof(blocks[i]));
        memcpy(blocks[i], data + k, (size_t)dat_len);
        uint8_t ecc[32];
        rs_remainder(data + k, dat_len, div, ecc_len, ecc);
        k += dat_len;
        memcpy(blocks[i] + short_len + 1 - ecc_len, ecc, (size_t)ecc_len);
    }
    uint8_t final[512];
    int flen = 0;
    for (int i = 0; i <= short_len; i++)
        for (int j = 0; j < nblocks; j++)
            if (i != short_len - ecc_len || j >= short_blocks)
                final[flen++] = blocks[j][i];

    Grid *g = calloc(1, sizeof(Grid));
    Grid *best = calloc(1, sizeof(Grid));
    if (!g || !best)
    {
        free(g);
        free(best);
        return false;
    }
    g->size = ver * 4 + 17;
    draw_patterns(g, ver);
    draw_codewords(g, final, flen);

    long best_score = -1;
    for (int mask = 0; mask < 8; mask++)
    {
        Grid *t = malloc(sizeof(Grid));
        if (!t)
            break;
        memcpy(t, g, sizeof(Grid));
        apply_mask(t, mask);
        draw_format(t, mask);
        long s = penalty(t);
        if (best_score < 0 || s < best_score)
        {
            best_score = s;
            memcpy(best, t, sizeof(Grid));
        }
        free(t);
    }

    out->size = g->size;
    out->modules = malloc((size_t)out->size * (size_t)out->size);
    if (out->modules)
        for (int y = 0; y < out->size; y++)
            for (int x = 0; x < out->size; x++)
                out->modules[y * out->size + x] = best->dark[y][x];
    free(g);
    free(best);
    return out->modules != NULL;
}

bool qr_module(const QrCode *qr, int x, int y)
{
    if (!qr->modules || x < 0 || y < 0 || x >= qr->size || y >= qr->size)
        return false;
    return qr->modules[y * qr->size + x] != 0;
}

void qr_free(QrCode *qr)
{
    free(qr->modules);
    qr->modules = NULL;
    qr->size = 0;
}
