#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include "../libc/include/dirent.h"
#include "include/iconv.h"

struct ns_dirent
{
    ino_t          d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};

typedef struct ns_DIR
{
    int              fd;
    char             path[256];
    struct ns_dirent entry;
}
ns_DIR;

static unsigned char native_to_dtype(enum stat_type type)
{
    switch (type)
    {
        case STAT_FILE:   return 8;
        case STAT_DIR:    return 4;
        case STAT_DEVICE: return 2;
        case STAT_SOCKET: return 12;
    }
    return 0;
}

ns_DIR *ns_opendir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        errno = ENOENT;
        return NULL;
    }
    if (st.type != STAT_DIR)
    {
        errno = ENOTDIR;
        return NULL;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        errno = ENOENT;
        return NULL;
    }
    ns_DIR *dir = calloc(1, sizeof(ns_DIR));
    if (!dir)
    {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    dir->fd = fd;
    snprintf(dir->path, sizeof(dir->path), "%s", path);
    return dir;
}

ns_DIR *ns_fdopendir(int fd)
{
    ns_DIR *dir = calloc(1, sizeof(ns_DIR));
    if (!dir)
        return NULL;
    dir->fd = fd;
    return dir;
}

struct ns_dirent *ns_readdir(ns_DIR *dir)
{
    struct dirent native;
    if (!dir || readdir(dir->fd, &native) != 1)
        return NULL;
    memset(&dir->entry, 0, sizeof(dir->entry));
    memcpy(dir->entry.d_name, native.name, NAME_MAX);
    dir->entry.d_name[NAME_MAX] = '\0';
    dir->entry.d_type = native_to_dtype(native.type);
    dir->entry.d_reclen = sizeof(struct ns_dirent);
    return &dir->entry;
}

int ns_closedir(ns_DIR *dir)
{
    if (!dir)
        return -1;
    close(dir->fd);
    free(dir);
    return 0;
}

void ns_rewinddir(ns_DIR *dir)
{
    if (dir && dir->path[0])
    {
        close(dir->fd);
        dir->fd = open(dir->path, O_RDONLY);
    }
}

int ns_dirfd(ns_DIR *dir)
{
    return dir ? dir->fd : -1;
}

int ns_alphasort(const struct ns_dirent **a, const struct ns_dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

int ns_scandir(const char *path, struct ns_dirent ***list,
               int (*filter)(const struct ns_dirent *),
               int (*compar)(const struct ns_dirent **, const struct ns_dirent **))
{
    ns_DIR *dir = ns_opendir(path);
    if (!dir)
        return -1;

    struct ns_dirent **items = NULL;
    int count = 0, cap = 0;
    struct ns_dirent *e;
    while ((e = ns_readdir(dir)) != NULL)
    {
        if (filter && !filter(e))
            continue;
        if (count == cap)
        {
            cap = cap ? cap * 2 : 16;
            struct ns_dirent **grown = realloc(items, sizeof(*items) * (size_t)cap);
            if (!grown)
                goto fail;
            items = grown;
        }
        items[count] = malloc(sizeof(struct ns_dirent));
        if (!items[count])
            goto fail;
        *items[count] = *e;
        count++;
    }
    ns_closedir(dir);
    if (compar && count > 1)
        qsort(items, (size_t)count, sizeof(*items), (int (*)(const void *, const void *))compar);
    *list = items;
    return count;

fail:
    for (int i = 0; i < count; i++)
        free(items[i]);
    free(items);
    ns_closedir(dir);
    errno = ENOMEM;
    return -1;
}

typedef enum
{
    ENC_UTF8,
    ENC_LATIN1,
    ENC_ASCII,
    ENC_CP1252,
    ENC_UTF16LE,
    ENC_UTF16BE,
    ENC_UCS4,
}
Encoding;

typedef struct
{
    Encoding from;
    Encoding to;
    bool     translit;
}
IconvState;

static const uint16_t cp1252_high[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

static bool parse_encoding(const char *name, Encoding *out, bool *translit)
{
    char clean[64];
    size_t n = 0;
    for (const char *p = name; *p && n < sizeof(clean) - 1; p++)
    {
        if (*p == '/')
        {
            if (strcasestr(p, "TRANSLIT"))
                *translit = true;
            break;
        }
        if (*p != '-' && *p != '_')
            clean[n++] = (char)toupper((unsigned char)*p);
    }
    clean[n] = '\0';

    if (strcmp(clean, "UTF8") == 0) *out = ENC_UTF8;
    else if (strcmp(clean, "ISO88591") == 0 || strcmp(clean, "LATIN1") == 0 || strcmp(clean, "L1") == 0) *out = ENC_LATIN1;
    else if (strcmp(clean, "ASCII") == 0 || strcmp(clean, "USASCII") == 0 || strcmp(clean, "ANSIX3.41968") == 0) *out = ENC_ASCII;
    else if (strcmp(clean, "WINDOWS1252") == 0 || strcmp(clean, "CP1252") == 0) *out = ENC_CP1252;
    else if (strcmp(clean, "UTF16LE") == 0) *out = ENC_UTF16LE;
    else if (strcmp(clean, "UTF16BE") == 0 || strcmp(clean, "UTF16") == 0) *out = ENC_UTF16BE;
    else if (strcmp(clean, "UCS4") == 0 || strcmp(clean, "UCS4BE") == 0 || strcmp(clean, "UTF32BE") == 0) *out = ENC_UCS4;
    else return false;
    return true;
}

iconv_t iconv_open(const char *to, const char *from)
{
    IconvState *st = calloc(1, sizeof(IconvState));
    if (!st)
        return (iconv_t)-1;
    if (!parse_encoding(from, &st->from, &st->translit) || !parse_encoding(to, &st->to, &st->translit))
    {
        free(st);
        errno = EINVAL;
        return (iconv_t)-1;
    }
    return st;
}

int iconv_close(iconv_t cd)
{
    free(cd);
    return 0;
}

static int decode_one(Encoding enc, const unsigned char *s, size_t len, uint32_t *cp)
{
    switch (enc)
    {
        case ENC_UTF8:
        {
            if (s[0] < 0x80) { *cp = s[0]; return 1; }
            int n = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : s[0] >= 0xC0 ? 2 : 0;
            if (n == 0) return -1;
            if ((size_t)n > len) return 0;
            uint32_t v = s[0] & (0x7F >> n);
            for (int i = 1; i < n; i++)
            {
                if ((s[i] & 0xC0) != 0x80) return -1;
                v = v << 6 | (s[i] & 0x3F);
            }
            *cp = v;
            return n;
        }
        case ENC_LATIN1:
        case ENC_ASCII:
            *cp = s[0];
            return 1;
        case ENC_CP1252:
            *cp = s[0] >= 0x80 && s[0] < 0xA0 && cp1252_high[s[0] - 0x80] ? cp1252_high[s[0] - 0x80] : s[0];
            return 1;
        case ENC_UTF16LE:
        case ENC_UTF16BE:
        {
            if (len < 2) return 0;
            uint32_t u = enc == ENC_UTF16LE ? (uint32_t)(s[0] | s[1] << 8) : (uint32_t)(s[0] << 8 | s[1]);
            if (u >= 0xD800 && u < 0xDC00)
            {
                if (len < 4) return 0;
                uint32_t lo = enc == ENC_UTF16LE ? (uint32_t)(s[2] | s[3] << 8) : (uint32_t)(s[2] << 8 | s[3]);
                *cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                return 4;
            }
            *cp = u;
            return 2;
        }
        case ENC_UCS4:
            if (len < 4) return 0;
            *cp = (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
            return 4;
    }
    return -1;
}

static int encode_one(Encoding enc, uint32_t cp, unsigned char *out, size_t len, bool translit)
{
    switch (enc)
    {
        case ENC_UTF8:
            if (cp < 0x80) { if (len < 1) return 0; out[0] = (unsigned char)cp; return 1; }
            if (cp < 0x800) { if (len < 2) return 0; out[0] = (unsigned char)(0xC0 | cp >> 6); out[1] = (unsigned char)(0x80 | (cp & 0x3F)); return 2; }
            if (cp < 0x10000)
            {
                if (len < 3) return 0;
                out[0] = (unsigned char)(0xE0 | cp >> 12);
                out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                out[2] = (unsigned char)(0x80 | (cp & 0x3F));
                return 3;
            }
            if (len < 4) return 0;
            out[0] = (unsigned char)(0xF0 | cp >> 18);
            out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[3] = (unsigned char)(0x80 | (cp & 0x3F));
            return 4;
        case ENC_LATIN1:
        case ENC_ASCII:
        case ENC_CP1252:
        {
            if (len < 1) return 0;
            uint32_t limit = enc == ENC_ASCII ? 0x80 : 0x100;
            if (cp < limit && !(enc == ENC_CP1252 && cp >= 0x80 && cp < 0xA0))
            {
                out[0] = (unsigned char)cp;
                return 1;
            }
            if (enc == ENC_CP1252)
                for (int i = 0; i < 32; i++)
                    if (cp1252_high[i] == cp)
                    {
                        out[0] = (unsigned char)(0x80 + i);
                        return 1;
                    }
            if (!translit)
                return -1;
            out[0] = '?';
            return 1;
        }
        case ENC_UTF16LE:
        case ENC_UTF16BE:
        {
            uint16_t units[2];
            int n = 1;
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                units[0] = (uint16_t)(0xD800 + (cp >> 10));
                units[1] = (uint16_t)(0xDC00 + (cp & 0x3FF));
                n = 2;
            }
            else
            {
                units[0] = (uint16_t)cp;
            }
            if (len < (size_t)n * 2) return 0;
            for (int i = 0; i < n; i++)
            {
                out[i * 2] = (unsigned char)(enc == ENC_UTF16LE ? units[i] : units[i] >> 8);
                out[i * 2 + 1] = (unsigned char)(enc == ENC_UTF16LE ? units[i] >> 8 : units[i]);
            }
            return n * 2;
        }
        case ENC_UCS4:
            if (len < 4) return 0;
            out[0] = (unsigned char)(cp >> 24);
            out[1] = (unsigned char)(cp >> 16);
            out[2] = (unsigned char)(cp >> 8);
            out[3] = (unsigned char)cp;
            return 4;
    }
    return -1;
}

size_t iconv(iconv_t cd, char **in, size_t *in_left, char **out, size_t *out_left)
{
    IconvState *st = cd;
    if (!in || !*in)
        return 0;

    size_t irreversible = 0;
    while (*in_left > 0)
    {
        uint32_t cp;
        int used = decode_one(st->from, (const unsigned char *)*in, *in_left, &cp);
        if (used == 0)
        {
            errno = EINVAL;
            return (size_t)-1;
        }
        if (used < 0)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        int written = encode_one(st->to, cp, (unsigned char *)*out, *out_left, st->translit);
        if (written == 0)
        {
            errno = E2BIG;
            return (size_t)-1;
        }
        if (written < 0)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        *in += used;
        *in_left -= (size_t)used;
        *out += written;
        *out_left -= (size_t)written;
    }
    return irreversible;
}

#include "include/regex.h"

int regcomp(regex_t *re, const char *pattern, int flags)
{
    (void)pattern;
    (void)flags;
    re->re_nsub = 0;
    return REG_ESPACE;
}

int regexec(const regex_t *re, const char *s, size_t nmatch, regmatch_t *match, int flags)
{
    (void)re;
    (void)s;
    (void)nmatch;
    (void)match;
    (void)flags;
    return REG_NOMATCH;
}

size_t regerror(int code, const regex_t *re, char *buf, size_t size)
{
    (void)code;
    (void)re;
    return (size_t)snprintf(buf, size, "regular expressions are not supported");
}

void regfree(regex_t *re)
{
    (void)re;
}

int ns_posix_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    errno = EAFNOSUPPORT;
    return -1;
}
