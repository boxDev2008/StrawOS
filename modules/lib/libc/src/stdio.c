static FILE _stdin  = { .fd = 0, .bufmode = _IOLBF };
static FILE _stdout = { .fd = 1, .bufmode = _IOLBF };
static FILE _stderr = { .fd = 2, .bufmode = _IONBF };
 
FILE *stdin   = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

static int _flush(FILE *f);

static void stdio_flush_at_exit(void)
{
    fflush(stdout);
    fflush(stderr);
}

__attribute__((constructor)) static void stdio_register_flush(void)
{
    atexit(stdio_flush_at_exit);
}

static int _flush(FILE *f)
{
    if (!f || f->wlen == 0)
        return 0;
 
    int written = 0;
    while (written < f->wlen) {
        int n = write(f->fd, f->wbuf + written, f->wlen - written);
        if (n <= 0) {
            f->error = 1;
            return EOF;
        }
        written += n;
    }
    f->wlen = 0;
    return 0;
}
 
static int _fill(FILE *f)
{
    if (f == stdin)
        _flush(stdout);
    f->rpos = 0;
    f->rlen = 0;
    int n = read(f->fd, f->rbuf, FILE_BUF_SIZE);
    if (n < 0) {
        f->error = 1;
        return 0;
    }
    if (n == 0) {
        f->eof = 1;
        return 0;
    }
    f->rlen = n;
    return n;
}
 
FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode)
        return NULL;

    int flags = 0;
    int plus = 0;
    for (const char *m = mode; *m; m++)
        if (*m == '+') { plus = 1; break; }

    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT; break;
    default:  return NULL;
    }

    if (mode[0] == 'w')
        unlink(path);

    int fd = open(path, flags);
    if (fd < 0)
        return NULL;
    if (mode[0] == 'a')
        lseek(fd, 0, SEEK_END);

    FILE *f = malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    return f;
}

FILE *fdopen(int fd, const char *mode)
{
    if (fd < 0 || !mode)
        return NULL;

    FILE *f = malloc(sizeof(FILE));
    if (!f)
        return NULL;
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    return f;
}

int fclose(FILE *f)
{
    if (!f || f->fd < 0)
        return EOF;

    int rc = _flush(f);
    if (close(f->fd) != 0)
        rc = EOF;

    free(f);
    return rc;
}

FILE *freopen(const char *path, const char *mode, FILE *f)
{
    if (!f || !mode)
        return NULL;
 
    _flush(f);
    if (f->fd >= 0)
        close(f->fd);
 
    f->fd    = -1;
    f->error =  0;
    f->eof   =  0;
    f->rpos  =  0;
    f->rlen  =  0;
    f->wlen  =  0;
 
    if (path == NULL)
    {
        f->error = 1;
        return NULL;
    }
 
    int flags = 0;
    int plus  = 0;
    for (const char *m = mode; *m; m++)
        if (*m == '+') { plus = 1; break; }
 
    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT; break;
    default:
        f->error = 1;
        return NULL;
    }
 
    if (mode[0] == 'w')
        unlink(path);

    int fd = open(path, flags);
    if (fd < 0) {
        f->error = 1;
        return NULL;
    }
    if (mode[0] == 'a')
        lseek(fd, 0, SEEK_END);

    f->fd = fd;
    return f;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    (void)buf;
    (void)size;
    if (!f || mode < _IOFBF || mode > _IONBF)
        return -1;
    _flush(f);
    f->bufmode = mode;
    return 0;
}

char *tmpnam(char *s)
{
    static char name[L_tmpnam];
    static unsigned counter;
    char *out = s ? s : name;

    mkdir("/tmp", 0);
    for (int tries = 0; tries < TMP_MAX; tries++)
    {
        snprintf(out, L_tmpnam, "/tmp/tmp%u", ++counter);
        struct stat st;
        if (stat(out, &st) != 0)
            return out;
    }
    return NULL;
}

FILE *tmpfile(void)
{
    char name[L_tmpnam];
    if (!tmpnam(name))
        return NULL;
    return fopen(name, "w+");
}

int fflush(FILE *f)
{
    if (!f)
        return 0;
    return _flush(f);
}

size_t fread(void *buf, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !buf || size == 0 || nmemb == 0)
        return 0;
 
    size_t total  = size * nmemb;
    size_t copied = 0;
    char  *dst    = (char *)buf;
 
    while (copied < total)
    {
        while (f->rpos < f->rlen && copied < total)
            dst[copied++] = f->rbuf[f->rpos++];
 
        if (copied == total)
            break;
 
        if (f->eof || f->error)
            break;
 
        if (_fill(f) == 0)
            break;
    }
 
    return (size > 0) ? (copied / size) : 0;
}
 
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !buf || size == 0 || nmemb == 0)
        return 0;
 
    size_t total = size * nmemb;
    size_t written = 0;
    const char  *src = (const char *)buf;
    bool newline = false;

    while (written < total)
    {
        while (f->wlen < FILE_BUF_SIZE && written < total)
        {
            if (src[written] == '\n')
                newline = true;
            f->wbuf[f->wlen++] = src[written++];
        }

        if (f->wlen == FILE_BUF_SIZE)
        {
            if (_flush(f) == EOF)
                break;
        }
    }

    if (f->bufmode == _IONBF || (f->bufmode == _IOLBF && newline))
        _flush(f);

    return (size > 0) ? (written / size) : 0;
}

int fgetc(FILE *f)
{
    if (!f || f->error || f->eof)
        return EOF;
 
    if (f->rpos >= f->rlen)
    {
        if (_fill(f) == 0)
            return EOF;
    }

    return (unsigned char)f->rbuf[f->rpos++];
}

int fputc(int c, FILE *f)
{
    if (!f || f->error)
        return EOF;
 
    f->wbuf[f->wlen++] = (char)(unsigned char)c;
 
    if (f->wlen == FILE_BUF_SIZE || f->bufmode == _IONBF || (f->bufmode == _IOLBF && (unsigned char)c == '\n'))
    {
        if (_flush(f) == EOF)
            return EOF;
    }

    return (unsigned char)c;
}

int ungetc(int c, FILE *f)
{
    if (!f || c == EOF)
        return EOF;
 
    if (f->rpos > 0)
    {
        f->rbuf[--f->rpos] = (char)(unsigned char)c;
    }
    else if (f->rlen < FILE_BUF_SIZE)
    {
        for (int i = f->rlen; i > 0; i--)
            f->rbuf[i] = f->rbuf[i - 1];
        f->rbuf[0] = (char)(unsigned char)c;
        f->rlen++;
    }
    else
    {
        return EOF;
    }
 
    f->eof = 0;
    return (unsigned char)c;
}

char *fgets(char *s, int n, FILE *f)
{
    if (!s || n <= 0 || !f)
        return NULL;
 
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *f)
{
    if (!s || !f)
        return EOF;
    for (; *s; s++)
        if (fputc((unsigned char)*s, f) == EOF)
            return EOF;
    return 0;
}
 
int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fseek(FILE *f, long offset, int whence)
{
    if (!f || f->error)
        return -1;
 
    if (_flush(f) == EOF)
        return -1;
 
    f->rpos = f->rlen = 0;
 
    int rc = lseek(f->fd, (int64_t)offset, whence);
    if (rc < 0) {
        f->error = 1;
        return -1;
    }
    f->eof = 0;
    return 0;
}
 
long ftell(FILE *f)
{
    if (!f || f->error)
        return -1L;
 
    if (_flush(f) == EOF)
        return -1L;
 
    int pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0)
        return -1L;
 
    return (long)(pos - (f->rlen - f->rpos));
}
 
void rewind(FILE *f)
{
    if (!f) return;
    f->error = 0;
    f->eof   = 0;
    fseek(f, 0L, SEEK_SET);
}

int  feof(FILE *f)    { return f && f->eof;   }
int  ferror(FILE *f)  { return f && f->error; }
void clearerr(FILE *f){ if (f) { f->eof = 0; f->error = 0; } }

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[4096];
    int  n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) return n;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    size_t written = fwrite(buf, 1, (size_t)n, f);
    return (int)written;
}
 
int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sscanf(const char *buf, const char *fmt, ...)
{
    if (!buf || !fmt) return EOF;

    va_list ap;
    va_start(ap, fmt);

    int assigned = 0;
    const char *b = buf;

    for (; *fmt; fmt++)
    {
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            while (*b == ' ' || *b == '\t' || *b == '\n') b++;
            continue;
        }

        if (*fmt != '%') {
            if (*b != *fmt) goto done;
            b++;
            continue;
        }

        fmt++;

        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        int is_long = 0, is_longlong = 0, is_short = 0;
        if (*fmt == 'h') { is_short = 1; fmt++; }
        else if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { is_longlong = 1; is_long = 0; fmt++; }

        char spec = *fmt;

        if (spec == '%')
        {
            if (*b != '%') goto done;
            b++;
            continue;
        }

        if (spec != 'c' && spec != '[')
        {
            while (*b == ' ' || *b == '\t' || *b == '\n') b++;
        }

        if (!*b) goto done;

        switch (spec)
        {
        case 'd':
        case 'i': {
            const char *start = b;
            int neg = 0;
            int base = (spec == 'i') ? 0 : 10;

            if (*b == '+') b++;
            else if (*b == '-') { neg = 1; b++; }

            if (base == 0)
            {
                if (*b == '0')
                {
                    b++;
                    if (*b == 'x' || *b == 'X') { base = 16; b++; }
                    else base = 8;
                }
                else base = 10;
            }

            const char *num_start = b;
            long long val = 0;
            int digits = 0;
            int lim = width ? width - (int)(b - start) : INT_MAX;

            while (digits < lim && *b)
            {
                int d;
                if (*b >= '0' && *b <= '9')      d = *b - '0';
                else if (*b >= 'a' && *b <= 'f') d = *b - 'a' + 10;
                else if (*b >= 'A' && *b <= 'F') d = *b - 'A' + 10;
                else break;
                if (d >= base) break;
                val = val * base + d;
                b++; digits++;
            }

            if (b == num_start) goto done;
            if (neg) val = -val;

            if (!suppress)
            {
                if (is_longlong)     *va_arg(ap, long long *)      = val;
                else if (is_long)    *va_arg(ap, long *)           = (long)val;
                else if (is_short)   *va_arg(ap, short *)          = (short)val;
                else                 *va_arg(ap, int *)             = (int)val;
                assigned++;
            }
            break;
        }

        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            int base = (spec == 'o') ? 8 : (spec == 'u') ? 10 : 16;
            const char *start = b;
            if (base == 16 && *b == '0' && (*(b+1)=='x' || *(b+1)=='X')) b += 2;

            unsigned long long val = 0;
            int digits = 0;
            int lim = width ? width - (int)(b - start) : INT_MAX;

            while (digits < lim && *b) {
                int d;
                if (*b >= '0' && *b <= '9')      d = *b - '0';
                else if (*b >= 'a' && *b <= 'f') d = *b - 'a' + 10;
                else if (*b >= 'A' && *b <= 'F') d = *b - 'A' + 10;
                else break;
                if (d >= base) break;
                val = val * base + d;
                b++; digits++;
            }

            if (!digits) goto done;

            if (!suppress) {
                if (is_longlong)     *va_arg(ap, unsigned long long *) = val;
                else if (is_long)    *va_arg(ap, unsigned long *)      = (unsigned long)val;
                else if (is_short)   *va_arg(ap, unsigned short *)     = (unsigned short)val;
                else                 *va_arg(ap, unsigned int *)       = (unsigned int)val;
                assigned++;
            }
            break;
        }

        case 's': {
            int count = 0;
            int lim = width ? width : INT_MAX;
            char *dst = suppress ? NULL : va_arg(ap, char *);

            while (count < lim && *b &&
                   *b != ' ' && *b != '\t' && *b != '\n') {
                if (dst) dst[count] = *b;
                b++; count++;
            }

            if (!count) goto done;
            if (dst) { dst[count] = '\0'; assigned++; }
            break;
        }

        case 'c': {
            int lim = width ? width : 1;
            char *dst = suppress ? NULL : va_arg(ap, char *);

            int count = 0;
            while (count < lim && *b) {
                if (dst) dst[count] = *b;
                b++; count++;
            }

            if (!count) goto done;
            if (!suppress) assigned++;
            break;
        }

        case 'n': {
            if (!suppress) {
                *va_arg(ap, int *) = (int)(b - buf);
            }
            break;
        }

        case '[': {
            fmt++;
            int negate = 0;
            if (*fmt == '^') { negate = 1; fmt++; }

            unsigned char set[256] = {0};
            if (*fmt == ']') { set[(unsigned char)']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') {
                if (*(fmt+1) == '-' && *(fmt+2) && *(fmt+2) != ']') {
                    unsigned char lo = (unsigned char)*fmt;
                    unsigned char hi = (unsigned char)*(fmt+2);
                    for (unsigned int c = lo; c <= hi; c++) set[c] = 1;
                    fmt += 3;
                } else {
                    set[(unsigned char)*fmt] = 1;
                    fmt++;
                }
            }

            int count = 0;
            int lim   = width ? width : INT_MAX;
            char *dst = suppress ? NULL : va_arg(ap, char *);

            while (count < lim && *b) {
                int in_set = set[(unsigned char)*b];
                if (negate ? in_set : !in_set) break;
                if (dst) dst[count] = *b;
                b++; count++;
            }

            if (!count) goto done;
            if (dst) { dst[count] = '\0'; assigned++; }
            break;
        }

        default:
            goto done;
        }
    }

done:
    va_end(ap);
    return assigned;
}

typedef struct
{
    char  *buf;
    size_t size;
    size_t pos;
}
FmtOut;

static void fmt_putc(FmtOut *o, char c)
{
    if (o->pos + 1 < o->size)
        o->buf[o->pos] = c;
    o->pos++;
}

static void fmt_pad(FmtOut *o, char c, int n)
{
    while (n-- > 0)
        fmt_putc(o, c);
}

enum
{
    FMT_LEFT  = 1,
    FMT_PLUS  = 2,
    FMT_SPACE = 4,
    FMT_ALT   = 8,
    FMT_ZERO  = 16,
};

static void fmt_emit(FmtOut *o, const char *prefix, const char *body, int body_len, int width, int flags)
{
    int plen = (int)strlen(prefix);
    int total = plen + body_len;
    if (!(flags & FMT_LEFT) && !(flags & FMT_ZERO))
        fmt_pad(o, ' ', width - total);
    for (int i = 0; i < plen; i++)
        fmt_putc(o, prefix[i]);
    if (!(flags & FMT_LEFT) && (flags & FMT_ZERO))
        fmt_pad(o, '0', width - total);
    for (int i = 0; i < body_len; i++)
        fmt_putc(o, body[i]);
    if (flags & FMT_LEFT)
        fmt_pad(o, ' ', width - total);
}

static void fmt_integer(FmtOut *o, unsigned long long v, bool neg, int base, bool upper, int width, int prec,
                        int flags, char conv)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[72];
    int n = 0;

    if (!(v == 0 && prec == 0))
        do
        {
            tmp[n++] = digits[v % (unsigned)base];
            v /= (unsigned)base;
        }
        while (v);

    char body[160];
    int len = 0;
    int zeros = prec > n ? prec - n : 0;
    if (conv == 'o' && (flags & FMT_ALT) && zeros == 0 && (n == 0 || tmp[n - 1] != '0'))
        zeros = 1;
    while (zeros-- > 0 && len < 80)
        body[len++] = '0';
    while (n > 0)
        body[len++] = tmp[--n];

    char prefix[4] = "";
    if (neg)
        strcpy(prefix, "-");
    else if (flags & FMT_PLUS)
        strcpy(prefix, "+");
    else if (flags & FMT_SPACE)
        strcpy(prefix, " ");
    if ((flags & FMT_ALT) && base == 16 && len > 0 && !(len == 1 && body[0] == '0'))
        strcat(prefix, upper ? "0X" : "0x");

    if (prec >= 0)
        flags &= ~FMT_ZERO;
    fmt_emit(o, prefix, body, len, width, flags);
}

static long double fmt_pow10(int e)
{
    long double r = 1.0L, b = 10.0L;
    unsigned n = e < 0 ? (unsigned)-e : (unsigned)e;
    while (n)
    {
        if (n & 1)
            r *= b;
        b *= b;
        n >>= 1;
    }
    return e < 0 ? 1.0L / r : r;
}

static int fmt_digits(double v, int ndigits, char *digits)
{
    if (ndigits > 18)
        ndigits = 18;
    if (v == 0.0)
    {
        for (int i = 0; i < ndigits; i++)
            digits[i] = '0';
        return 0;
    }

    int e2;
    frexp(v, &e2);
    int e = (int)((e2 - 1) * 0.30102999566398114);
    long double x = (long double)v * fmt_pow10(-e);
    while (x >= 10.0L)
    {
        x /= 10.0L;
        e++;
    }
    while (x < 1.0L)
    {
        x *= 10.0L;
        e--;
    }

    for (int i = 0; i < ndigits; i++)
    {
        int d = (int)x;
        if (d > 9)
            d = 9;
        digits[i] = (char)('0' + d);
        x = (x - d) * 10.0L;
    }

    bool round_up = x > 5.0L || (x == 5.0L && ((digits[ndigits - 1] - '0') & 1));
    if (round_up)
    {
        int i = ndigits - 1;
        while (i >= 0 && digits[i] == '9')
            digits[i--] = '0';
        if (i >= 0)
        {
            digits[i]++;
        }
        else
        {
            digits[0] = '1';
            for (int k = 1; k < ndigits; k++)
                digits[k] = '0';
            e++;
        }
    }
    return e;
}

static int fmt_fixed(char *body, double v, int prec)
{
    int len = 0;
    char digits[24];
    int e = fmt_digits(v, 18, digits);
    int ndig = e + 1 + prec;

    if (v == 0.0 || ndig <= 0)
    {
        bool round_up = false;
        if (v != 0.0 && ndig == 0)
        {
            long double scaled = (long double)v * fmt_pow10(prec);
            round_up = scaled > 0.5L;
        }
        body[len++] = '0';
        if (prec > 0)
        {
            body[len++] = '.';
            for (int i = 0; i < prec; i++)
                body[len++] = (i == prec - 1 && round_up) ? '1' : '0';
        }
        else if (round_up)
        {
            body[0] = '1';
        }
        return len;
    }

    if (ndig > 18)
        ndig = 18;
    int generated = ndig;
    e = fmt_digits(v, generated, digits);
    ndig = generated;

    int int_digits = e + 1;
    if (int_digits <= 0)
    {
        body[len++] = '0';
    }
    else
    {
        for (int i = 0; i < int_digits; i++)
            body[len++] = i < ndig ? digits[i] : '0';
    }

    if (prec > 0)
    {
        body[len++] = '.';
        for (int i = 0; i < prec; i++)
        {
            int idx = int_digits + i;
            body[len++] = idx < 0 ? '0' : (idx < ndig ? digits[idx] : '0');
        }
    }
    return len;
}

static int fmt_exp(char *body, double v, int prec, bool upper)
{
    char digits[24];
    int nd = prec + 1;
    int e = fmt_digits(v, nd, digits);
    int len = 0;

    body[len++] = digits[0];
    if (prec > 0)
    {
        body[len++] = '.';
        for (int i = 1; i < nd; i++)
            body[len++] = i < 18 ? digits[i] : '0';
    }
    body[len++] = upper ? 'E' : 'e';
    body[len++] = e < 0 ? '-' : '+';
    int ae = e < 0 ? -e : e;
    if (ae >= 100)
        body[len++] = (char)('0' + ae / 100);
    body[len++] = (char)('0' + ae / 10 % 10);
    body[len++] = (char)('0' + ae % 10);
    return len;
}

static int fmt_strip_zeros(char *body, int len)
{
    int dot = -1, exp_at = len;
    for (int i = 0; i < len; i++)
    {
        if (body[i] == '.')
            dot = i;
        if (body[i] == 'e' || body[i] == 'E')
        {
            exp_at = i;
            break;
        }
    }
    if (dot < 0)
        return len;

    int end = exp_at;
    while (end > dot + 1 && body[end - 1] == '0')
        end--;
    if (end == dot + 1)
        end = dot;

    int tail = len - exp_at;
    memmove(body + end, body + exp_at, (size_t)tail);
    return end + tail;
}

static int fmt_hex(char *body, double v, int prec, bool upper)
{
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;
    union { double d; uint64_t u; } bits = { v };
    int exp = (int)((bits.u >> 52) & 0x7FF);
    uint64_t mant = bits.u & 0xFFFFFFFFFFFFFull;
    int lead = 1;

    if (exp == 0)
    {
        if (mant == 0)
        {
            lead = 0;
            exp = 0;
        }
        else
        {
            lead = 0;
            exp = -1022;
        }
    }
    else
    {
        exp -= 1023;
    }

    if (prec >= 0 && prec < 13)
    {
        int drop = (13 - prec) * 4;
        uint64_t half = 1ull << (drop - 1);
        uint64_t rem = mant & ((1ull << drop) - 1);
        mant >>= drop;
        if (rem > half || (rem == half && (mant & 1)))
            mant++;
        if (mant >> (prec * 4))
        {
            mant &= (1ull << (prec * 4)) - 1;
            lead++;
        }
        mant <<= drop;
    }

    body[len++] = '0';
    body[len++] = upper ? 'X' : 'x';
    body[len++] = (char)('0' + lead);

    int nd = prec >= 0 ? prec : 13;
    char frac[13];
    for (int i = 0; i < 13; i++)
        frac[i] = hex[(mant >> (48 - 4 * i)) & 0xF];
    if (prec < 0)
        while (nd > 0 && frac[nd - 1] == '0')
            nd--;
    if (nd > 0)
    {
        body[len++] = '.';
        for (int i = 0; i < nd; i++)
            body[len++] = i < 13 ? frac[i] : '0';
    }

    body[len++] = upper ? 'P' : 'p';
    body[len++] = exp < 0 ? '-' : '+';
    char tmp[8];
    int n = 0, ae = exp < 0 ? -exp : exp;
    do
    {
        tmp[n++] = (char)('0' + ae % 10);
        ae /= 10;
    }
    while (ae);
    while (n)
        body[len++] = tmp[--n];
    return len;
}

static void fmt_float(FmtOut *o, double v, char conv, int width, int prec, int flags)
{
    bool upper = conv == 'E' || conv == 'F' || conv == 'G' || conv == 'A';
    bool neg = signbit(v);
    if (neg)
        v = -v;

    char prefix[4] = "";
    if (neg)
        strcpy(prefix, "-");
    else if (flags & FMT_PLUS)
        strcpy(prefix, "+");
    else if (flags & FMT_SPACE)
        strcpy(prefix, " ");

    char body[512];
    int len;

    if (isnan(v) || isinf(v))
    {
        strcpy(body, isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"));
        fmt_emit(o, prefix, body, (int)strlen(body), width, flags & ~FMT_ZERO);
        return;
    }

    if (prec > 300)
        prec = 300;

    switch (conv)
    {
        case 'f':
        case 'F':
            len = fmt_fixed(body, v, prec < 0 ? 6 : prec);
            if ((flags & FMT_ALT) && prec == 0)
                body[len++] = '.';
            break;

        case 'e':
        case 'E':
            len = fmt_exp(body, v, prec < 0 ? 6 : prec, upper);
            break;

        case 'a':
        case 'A':
            len = fmt_hex(body, v, prec, upper);
            break;

        default:
        {
            int p = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
            char digits[24];
            int x = v == 0.0 ? 0 : fmt_digits(v, p, digits);
            if (p > x && x >= -4)
                len = fmt_fixed(body, v, p - 1 - x);
            else
                len = fmt_exp(body, v, p - 1, upper);
            if (!(flags & FMT_ALT))
                len = fmt_strip_zeros(body, len);
            break;
        }
    }

    fmt_emit(o, prefix, body, len, width, flags);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    char dummy;
    FmtOut o = { size ? buf : &dummy, size ? size : 1, 0 };

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            fmt_putc(&o, *fmt);
            continue;
        }
        fmt++;

        int flags = 0;
        for (;; fmt++)
        {
            if (*fmt == '-') flags |= FMT_LEFT;
            else if (*fmt == '+') flags |= FMT_PLUS;
            else if (*fmt == ' ') flags |= FMT_SPACE;
            else if (*fmt == '#') flags |= FMT_ALT;
            else if (*fmt == '0') flags |= FMT_ZERO;
            else break;
        }

        int width = 0;
        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            if (width < 0)
            {
                flags |= FMT_LEFT;
                width = -width;
            }
            fmt++;
        }
        else
        {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        int prec = -1;
        if (*fmt == '.')
        {
            fmt++;
            prec = 0;
            if (*fmt == '*')
            {
                prec = va_arg(ap, int);
                fmt++;
            }
            else
            {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }

        int length = 0;
        if (*fmt == 'h')
        {
            length = -1;
            fmt++;
            if (*fmt == 'h')
            {
                length = -2;
                fmt++;
            }
        }
        else if (*fmt == 'l')
        {
            length = 1;
            fmt++;
            if (*fmt == 'l')
            {
                length = 2;
                fmt++;
            }
        }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't')
        {
            length = 2;
            fmt++;
        }
        else if (*fmt == 'L')
        {
            fmt++;
        }

        char conv = *fmt;
        if (!conv)
            break;

        switch (conv)
        {
            case 'd':
            case 'i':
            {
                long long v;
                if (length >= 2) v = va_arg(ap, long long);
                else if (length == 1) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                if (length == -1) v = (short)v;
                if (length == -2) v = (signed char)v;
                bool neg = v < 0;
                unsigned long long uv = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
                fmt_integer(&o, uv, neg, 10, false, width, prec, flags, conv);
                break;
            }

            case 'u':
            case 'x':
            case 'X':
            case 'o':
            {
                unsigned long long v;
                if (length >= 2) v = va_arg(ap, unsigned long long);
                else if (length == 1) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                if (length == -1) v = (unsigned short)v;
                if (length == -2) v = (unsigned char)v;
                int base = conv == 'o' ? 8 : (conv == 'u' ? 10 : 16);
                fmt_integer(&o, v, false, base, conv == 'X', width, prec, flags & ~(FMT_PLUS | FMT_SPACE), conv);
                break;
            }

            case 'p':
            {
                uintptr_t v = (uintptr_t)va_arg(ap, void *);
                fmt_integer(&o, v, false, 16, false, width, prec, flags | FMT_ALT, 'p');
                break;
            }

            case 'c':
            {
                char ch = (char)va_arg(ap, int);
                fmt_emit(&o, "", &ch, 1, width, flags & ~FMT_ZERO);
                break;
            }

            case 's':
            {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                int len = 0;
                while (s[len] && (prec < 0 || len < prec))
                    len++;
                fmt_emit(&o, "", s, len, width, flags & ~FMT_ZERO);
                break;
            }

            case 'f': case 'F':
            case 'e': case 'E':
            case 'g': case 'G':
            case 'a': case 'A':
                fmt_float(&o, va_arg(ap, double), conv, width, prec, flags);
                break;

            case 'n':
                *va_arg(ap, int *) = (int)o.pos;
                break;

            case '%':
                fmt_putc(&o, '%');
                break;

            default:
                fmt_putc(&o, '%');
                fmt_putc(&o, conv);
                break;
        }
    }

    if (size)
        o.buf[o.pos < size ? o.pos : size - 1] = '\0';
    return (int)o.pos;
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

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}