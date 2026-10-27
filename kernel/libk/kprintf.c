static void reverse(char *str, int len)
{
    int i = 0;
    int j = len - 1;
    while (i < j)
    {
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

    if (is_signed && signed_val < 0)
    {
        value = (unsigned long)(-signed_val);
        buf[i++] = '-';
    }

    unsigned long tmp = value;
    int start = i;

    if (tmp == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while (tmp > 0)
        {
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

    if (zero_pad && str[0] == '-')
    {
        term_putchar('-');
        str++;
        len--;
        pad = width - (len + 1);
    }

    while (pad-- > 0)
    {
        if (zero_pad)
            term_putchar('0');
        else
            term_putchar(' ');
    }

    term_puts(str, len);
}

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt == '%')
        {
            fmt++;
            int zero_pad = 0;
            int width = 0;
            if (*fmt == '0')
            {
                zero_pad = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9')
            {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            int long_count = 0;
            while (*fmt == 'l')
            {
                long_count++;
                fmt++;
            }

            if (*fmt == '\0')
                break;

            char buffer[32];
            switch (*fmt)
            {
                case 's':
                {
                    char *str = va_arg(args, char *);
                    if (!str)
                        str = "(null)";
                    if (width > 0)
                        kprint_padded(str, width, 0);
                    else
                        term_puts(str, strlen(str));
                    break;
                }
                case 'c':
                {
                    char c = (char)va_arg(args, int);
                    term_putchar(c);
                    break;
                }
                case 'd':
                case 'i':
                {
                    long val;
                    if (long_count >= 2)
                        val = va_arg(args, long long);
                    else if (long_count == 1)
                        val = va_arg(args, long);
                    else
                        val = va_arg(args, int);
                    itoa((unsigned long)val, buffer, 10, 1);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }
                case 'u':
                {
                    unsigned long val;
                    if (long_count >= 2)
                        val = (unsigned long)va_arg(args, unsigned long long);
                    else if (long_count == 1)
                        val = va_arg(args, unsigned long);
                    else
                        val = va_arg(args, unsigned int);
                    itoa(val, buffer, 10, 0);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }
                case 'x':
                {
                    unsigned long val;
                    if (long_count >= 2)
                        val = (unsigned long)va_arg(args, unsigned long long);
                    else if (long_count == 1)
                        val = va_arg(args, unsigned long);
                    else
                        val = va_arg(args, unsigned int);
                    itoa(val, buffer, 16, 0);
                    kprint_padded(buffer, width, zero_pad);
                    break;
                }
                case 'p':
                {
                    unsigned long val = (unsigned long)va_arg(args, void *);
                    term_puts("0x", 2);
                    itoa(val, buffer, 16, 0);
                    kprint_padded(buffer, width, 1);
                    break;
                }
                case '%':
                {
                    term_putchar('%');
                    break;
                }
                default:
                {
                    term_putchar('%');
                    term_putchar(*fmt);
                    break;
                }
            }
        }
        else term_putchar(*fmt);
        fmt++;
    }
    va_end(args);
}