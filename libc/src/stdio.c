#include <stdio.h>
#include <string.h>
#include <syscall.h>

int open(const char *path, int flags)
{
    return (int)syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

int write(int fd, const void *buf, size_t count)
{
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

int read(int fd, void *buf, size_t count)
{
    return (int)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

int seek(int fd, int64_t offset, int whence)
{
    return (int)syscall3(SYS_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

int stat(const char *path, stat_t *statbuf)
{
    return (int)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)statbuf);
}

int fstat(int fd, stat_t *statbuf)
{
    return (int)syscall2(SYS_FSTAT, (uint64_t)fd, (uint64_t)statbuf);
}

static void _write_char(char *buf, size_t *pos, size_t size, char c)
{
    if (*pos + 1 < size)
        buf[*pos] = c;
    (*pos)++;
}

static void _write_str(char *buf, size_t *pos, size_t size, const char *s, int width, int left)
{
    if (!s) s = "(null)";
    size_t len = strlen(s);
    if (!left)
        for (size_t i = len; i < (size_t)width; i++) _write_char(buf, pos, size, ' ');
    for (size_t i = 0; s[i]; i++) _write_char(buf, pos, size, s[i]);
    if (left)
        for (size_t i = len; i < (size_t)width; i++) _write_char(buf, pos, size, ' ');
}

static void _write_uint(char *buf, size_t *pos, size_t size, unsigned long long val, int base, int upper, int width, int zero_pad, int left)
{
    static const char *lo = "0123456789abcdef";
    static const char *hi = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;
    char tmp[64];
    int  n = 0;
    if (val == 0) tmp[n++] = '0';
    else while (val) { tmp[n++] = digits[val % base]; val /= base; }

    char pad = zero_pad ? '0' : ' ';
    if (!left)
        for (int i = n; i < width; i++) _write_char(buf, pos, size, pad);
    for (int i = n - 1; i >= 0; i--) _write_char(buf, pos, size, tmp[i]);
    if (left)
        for (int i = n; i < width; i++) _write_char(buf, pos, size, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t pos = 0;
    if (!buf || size == 0) return 0;

    for (; *fmt; fmt++)
    {
        if (*fmt != '%') { _write_char(buf, &pos, size, *fmt); continue; }
        fmt++;

        int left = 0, zero_pad = 0;
        while (*fmt == '-') { left = 1; fmt++; }
        while (*fmt == '0') { zero_pad = 1; fmt++; }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }

        /* skip precision */
        if (*fmt == '.') { fmt++; while (*fmt >= '0' && *fmt <= '9') fmt++; }

        int is_long = 0, is_longlong = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { is_longlong = 1; fmt++; }

        switch (*fmt)
        {
        case 'c':
            _write_char(buf, &pos, size, (char)va_arg(ap, int));
            break;
        case 's': {
            char *s = va_arg(ap, char *);
            _write_str(buf, &pos, size, s, width, left);
            break;
        }
        case 'd': case 'i': {
            long long v;
            if (is_longlong)     v = va_arg(ap, long long);
            else if (is_long)    v = va_arg(ap, long);
            else                 v = va_arg(ap, int);
            if (v < 0) { _write_char(buf, &pos, size, '-'); v = -v; }
            _write_uint(buf, &pos, size, (unsigned long long)v, 10, 0, width, zero_pad, left);
            break;
        }
        case 'u': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            _write_uint(buf, &pos, size, v, 10, 0, width, zero_pad, left);
            break;
        }
        case 'x': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            _write_uint(buf, &pos, size, v, 16, 0, width, zero_pad, left);
            break;
        }
        case 'X': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            _write_uint(buf, &pos, size, v, 16, 1, width, zero_pad, left);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            _write_char(buf, &pos, size, '0');
            _write_char(buf, &pos, size, 'x');
            _write_uint(buf, &pos, size, v, 16, 0, width, zero_pad, left);
            break;
        }
        case '%':
            _write_char(buf, &pos, size, '%');
            break;
        default:
            _write_char(buf, &pos, size, '%');
            _write_char(buf, &pos, size, *fmt);
            break;
        }
    }
    if (pos < size) buf[pos] = '\0';
    else buf[size - 1] = '\0';
    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}