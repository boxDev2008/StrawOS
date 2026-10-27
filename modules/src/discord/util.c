#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include "tls.h"
#include "discord.h"

void buf_append(Buf *b, const void *data, size_t len)
{
    if (b->len + len + 1 > b->cap)
    {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1)
            cap *= 2;
        char *n = realloc(b->data, cap);
        if (!n)
            return;
        b->data = n;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void buf_puts(Buf *b, const char *s)
{
    buf_append(b, s, strlen(s));
}

void buf_putc(Buf *b, char c)
{
    buf_append(b, &c, 1);
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof(tmp))
    {
        buf_append(b, tmp, (size_t)n);
        return;
    }
    char *big = malloc((size_t)n + 1);
    if (!big)
        return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_append(b, big, (size_t)n);
    free(big);
}

void buf_json_string(Buf *b, const char *s)
{
    buf_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        switch (*p)
        {
            case '"':  buf_puts(b, "\\\""); break;
            case '\\': buf_puts(b, "\\\\"); break;
            case '\n': buf_puts(b, "\\n"); break;
            case '\r': buf_puts(b, "\\r"); break;
            case '\t': buf_puts(b, "\\t"); break;
            default:
                if (*p < 0x20)
                    buf_printf(b, "\\u%04x", *p);
                else
                    buf_putc(b, (char)*p);
        }
    }
    buf_putc(b, '"');
}

void buf_consume(Buf *b, size_t n)
{
    if (n >= b->len)
    {
        b->len = 0;
        if (b->data)
            b->data[0] = '\0';
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = '\0';
}

void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

char *xstrdup(const char *s)
{
    return s ? xstrndup(s, strlen(s)) : NULL;
}

char *xstrndup(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static const char b64_std[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b64_url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t base64_encode(const uint8_t *in, size_t len, char *out, bool url)
{
    const char *tab = url ? b64_url : b64_std;
    size_t o = 0;
    size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = tab[v >> 18 & 63];
        out[o++] = tab[v >> 12 & 63];
        out[o++] = tab[v >> 6 & 63];
        out[o++] = tab[v & 63];
    }
    if (i < len)
    {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len)
            v |= (uint32_t)in[i + 1] << 8;
        out[o++] = tab[v >> 18 & 63];
        out[o++] = tab[v >> 12 & 63];
        if (i + 1 < len)
            out[o++] = tab[v >> 6 & 63];
        else if (!url)
            out[o++] = '=';
        if (!url)
            out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

size_t base64_decode(const char *in, size_t len, uint8_t *out)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i++)
    {
        int v = b64_value(in[i]);
        if (v < 0)
            continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o;
}

int utf8_decode(const char *s, int len, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (len <= 0)
        return 0;
    if (p[0] < 0x80)
    {
        *cp = p[0];
        return 1;
    }
    int n = p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : p[0] >= 0xC0 ? 2 : 1;
    if (n == 1 || n > len)
    {
        *cp = 0xFFFD;
        return 1;
    }
    uint32_t v = p[0] & (0x7F >> n);
    for (int i = 1; i < n; i++)
    {
        if ((p[i] & 0xC0) != 0x80)
        {
            *cp = 0xFFFD;
            return i;
        }
        v = v << 6 | (p[i] & 0x3F);
    }
    *cp = v;
    return n;
}

static br_hmac_drbg_context drbg;
static bool drbg_seeded;

void random_bytes(void *out, size_t len)
{
    if (!drbg_seeded)
    {
        unsigned char seed[32];
        tls_entropy(seed);
        br_hmac_drbg_init(&drbg, &br_sha256_vtable, seed, sizeof(seed));
        drbg_seeded = true;
    }
    br_hmac_drbg_generate(&drbg, out, len);
}

Snowflake parse_snowflake(const char *s)
{
    Snowflake v = 0;
    if (!s)
        return 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (Snowflake)(*s++ - '0');
    return v;
}

static int digits(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

time_t parse_iso8601(const char *s)
{
    if (!s || strlen(s) < 19)
        return 0;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = digits(s, 4) - 1900;
    tm.tm_mon = digits(s + 5, 2) - 1;
    tm.tm_mday = digits(s + 8, 2);
    tm.tm_hour = digits(s + 11, 2);
    tm.tm_min = digits(s + 14, 2);
    tm.tm_sec = digits(s + 17, 2);
    time_t t = timegm(&tm);
    const char *z = s + 19;
    while (*z == '.' || (*z >= '0' && *z <= '9'))
        z++;
    if ((*z == '+' || *z == '-') && strlen(z) >= 6)
    {
        int off = digits(z + 1, 2) * 3600 + digits(z + 4, 2) * 60;
        t += *z == '+' ? -off : off;
    }
    return t;
}
