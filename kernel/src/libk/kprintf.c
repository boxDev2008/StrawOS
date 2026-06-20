#include "kprintf.h"
#include "string.h"

#include <stddef.h>
#include <stdarg.h>

extern struct flanterm_context *ft_ctx;

void kputs(const char *str, size_t count)
{
    const char *p = str;
    const char *end = str + count;

    while (p < end)
    {
        const char *newline = memchr(p, '\n', end - p);
        if (!newline)
        {
            flanterm_write(ft_ctx, p, end - p);
            break;
        }
        if (newline > p)
            flanterm_write(ft_ctx, p, newline - p);
        flanterm_write(ft_ctx, "\r\n", 2);
        p = newline + 1;
    }
}

static void kputchar(char c)
{
    char buf[1];
    buf[0] = c;
    kputs(buf, 1);
}

static void reverse(char *str, int len)
{
    int i = 0;
    int j = len - 1;
    while (i < j) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
        i++;
        j--;
    }
}

static void itoa(unsigned long value, char *buf, int base, int is_signed)
{
    int i = 0;
    long signed_val = (long)value;

    if (is_signed && signed_val < 0) {
        value = (unsigned long)(-signed_val);
        buf[i++] = '-';
    }

    unsigned long tmp = value;
    int start = i;

    if (tmp == 0) {
        buf[i++] = '0';
    } else {
        while (tmp > 0) {
            int digit = tmp % base;
            if (digit < 10)
                buf[i++] = '0' + digit;
            else
                buf[i++] = 'a' + (digit - 10);
            tmp /= base;
        }
    }

    reverse(buf + start, i - start);
    buf[i] = '\0';
}

static void kprint_padded(const char *str, int width, int zero_pad)
{
    int len = strlen(str);
    int pad = width - len;

    if (zero_pad && str[0] == '-') {
        kputchar('-');
        str++;
        len--;
        pad = width - (len + 1);
    }

    while (pad-- > 0) {
        if (zero_pad)
            kputchar('0');
        else
            kputchar(' ');
    }

    kputs(str, len);
}

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            int zero_pad = 0;
            int width = 0;

            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }

            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            if (*fmt == '\0')
                break;

            char buffer[32];

            switch (*fmt) {
                case 's': {
                    char *str = va_arg(args, char *);
                    if (!str)
                        str = "(null)";

                    if (width > 0)
                        kprint_padded(str, width, 0);
                    else
                        kputs(str, strlen(str));
                    break;
                }

                case 'c': {
                    char c = (char)va_arg(args, int);
                    kputchar(c);
                    break;
                }

                case 'd': {
                    int val = va_arg(args, int);
                    itoa((unsigned long)val, buffer, 10, 1);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }

                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    itoa((unsigned long)val, buffer, 10, 0);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }

                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    itoa((unsigned long)val, buffer, 16, 0);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }

                case 'p': {
                    unsigned long val = (unsigned long)va_arg(args, void *);
                    kputs("0x", 2);
                    itoa(val, buffer, 16, 0);
                    kprint_padded(buffer, width, 1);
                    break;
                }

                case '%': {
                    kputchar('%');
                    break;
                }

                default: {
                    kputchar('%');
                    kputchar(*fmt);
                    break;
                }
            }
        } else {
            kputchar(*fmt);
        }

        fmt++;
    }

    va_end(args);
}