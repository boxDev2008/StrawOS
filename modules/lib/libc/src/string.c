#include <string.h>
#include <stdint.h>
#include <stdlib.h>

void *memset(void *dst, int c, size_t n)
{
    uint8_t *p = dst;
    while (n--) *p++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p   = (const unsigned char *)s;
    const unsigned char  val = (unsigned char)c;

    while (n--) {
        if (*p == val)
            return (void *)p;
        p++;
    }

    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char *strstr(const char *s, const char *needle)
{
    if (!*needle) return (char *)s;
    for (; *s; s++)
    {
        const char *p = s, *q = needle;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*q) return (char *)s;
    }
    return NULL;
}

char *strrstr(const char *s, const char *needle)
{
    if (!*needle) return (char *)(s + strlen(s));
    char *last = NULL;
    for (; *s; s++)
    {
        const char *p = s, *q = needle;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*q) last = (char *)s;
    }
    return last;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char *)last;
}

char *strpbrk(const char *s, const char *accept)
{
    for (; *s; s++) {
        const char *a = accept;
        while (*a) {
            if (*s == *a++) return (char *)s;
        }
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (*s) {
        const char *a = accept;
        while (*a && *a != *s) a++;
        if (!*a) break;
        n++; s++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (*s) {
        const char *r = reject;
        while (*r && *r != *s) r++;
        if (*r) break;
        n++; s++;
    }
    return n;
}

char *strtok_r(char *s, const char *delim, char **saveptr)
{
    if (!s) s = *saveptr;
    s += strspn(s, delim);
    if (!*s) { *saveptr = s; return NULL; }
    char *token = s;
    s += strcspn(s, delim);
    if (*s) { *s++ = '\0'; }
    *saveptr = s;
    return token;
}

char *strtok(char *s, const char *delim)
{
    static char *saved;
    return strtok_r(s, delim, &saved);
}

long strtol(const char *s, char **endptr, int base)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    long result = 0;
    const char *start = s;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        s++;
    }
    if (s == start) s = (char *)(s - (base == 16 ? 2 : 0)); /* no digits */
    if (endptr) *endptr = (char *)s;
    return neg ? -result : result;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtol(s, endptr, base);
}

double strtod(const char *s, char **endptr)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    double result = 0.0;
    while (*s >= '0' && *s <= '9') result = result * 10.0 + (*s++ - '0');

    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s++ - '0') * frac;
            frac *= 0.1;
        }
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int eneg = 0;
        if (*s == '-') { eneg = 1; s++; }
        else if (*s == '+') s++;
        int exp = 0;
        while (*s >= '0' && *s <= '9') exp = exp * 10 + (*s++ - '0');
        double scale = 1.0;
        while (exp-- > 0) scale *= 10.0;
        result = eneg ? result / scale : result * scale;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -result : result;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

size_t strxfrm(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);
    if (n) strncpy(dst, src, n);
    return len;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) strcpy(copy, s);
    return copy;
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *copy = malloc(len + 1);
    if (copy) { strncpy(copy, s, len); copy[len] = '\0'; }
    return copy;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

// TODO(box): add the correct error codes for straw os
static const char *const _errmsgs[] = {
    "Success",                /* 0 */
};
#define _NERR (sizeof(_errmsgs) / sizeof(_errmsgs[0]))

char *strerror(int errnum)
{
    static char buf[32];
    if (errnum >= 0 && (size_t)errnum < _NERR)
        return (char *)_errmsgs[errnum];
    /* Format unknown codes */
    const char *prefix = "Unknown error: ";
    char *p = buf;
    while (*prefix) *p++ = *prefix++;
    if (errnum < 0) { *p++ = '-'; errnum = -errnum; }
    char tmp[16]; int i = 0;
    do { tmp[i++] = '0' + errnum % 10; errnum /= 10; } while (errnum);
    while (i--) *p++ = tmp[i];
    *p = '\0';
    return buf;
}

int strerror_r(int errnum, char *buf, size_t buflen)
{
    const char *msg = strerror(errnum);
    size_t len = strlen(msg);
    if (len >= buflen) return 34; /* ERANGE */
    strcpy(buf, msg);
    return 0;
}