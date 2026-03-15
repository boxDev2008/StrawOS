#include <stdio.h>
#include <string.h>
#include <stat.h>
#include <syscall.h>
#include <stdlib.h>

static FILE _stdin  = { .fd = 0 };
static FILE _stdout = { .fd = 1 };
static FILE _stderr = { .fd = 2 };
 
FILE *stdin   = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

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

int stat(const char *path, struct stat *statbuf)
{
    return (int)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)statbuf);
}

int fstat(int fd, struct stat *statbuf)
{
    return (int)syscall2(SYS_FSTAT, (uint64_t)fd, (uint64_t)statbuf);
}

/* Write the entire write-buffer to the OS.  Returns 0 or EOF. */
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
 
/* Fill the read buffer.  Returns number of bytes now available, or 0. */
static int _fill(FILE *f)
{
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

int mkdir(const char *pathname, unsigned int mode)
{
    return (int)syscall2(SYS_MKDIR, (uint64_t)pathname, (uint64_t)mode);
}

int remove(const char *path)
{
    return (int)syscall1(SYS_REMOVE, (uint64_t)path);
}

int rename(const char *from, const char *to)
{
    return (int)syscall2(SYS_RENAME, (uint64_t)from, (uint64_t)to);
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
    case 'r': flags = plus ? O_RDWR : O_RDONLY;                         break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;  break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default:  return NULL;
    }

    int fd = open(path, flags);
    if (fd < 0)
        return NULL;

    FILE *f = malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
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

    free(f);   /* was: invalidate-in-place, now actually free */
    return rc;
}

FILE *freopen(const char *path, const char *mode, FILE *f)
{
    if (!f || !mode)
        return NULL;
 
    /* flush and close the current fd */
    _flush(f);
    if (f->fd >= 0)
        close(f->fd);
 
    /* reset all state */
    f->fd    = -1;
    f->error =  0;
    f->eof   =  0;
    f->rpos  =  0;
    f->rlen  =  0;
    f->wlen  =  0;
 
    if (path == NULL) {
        /* NULL path: not meaningful without fcntl/dup2; signal error */
        f->error = 1;
        return NULL;
    }
 
    /* parse mode into flags (same logic as fopen) */
    int flags = 0;
    int plus  = 0;
    for (const char *m = mode; *m; m++)
        if (*m == '+') { plus = 1; break; }
 
    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY;                          break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;   break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;  break;
    default:
        f->error = 1;
        return NULL;
    }
 
    int fd = open(path, flags);
    if (fd < 0) {
        f->error = 1;
        return NULL;
    }
 
    f->fd = fd;
    return f;
}

/* ------------------------------------------------------------------ */
/*  fflush                                                             */
/* ------------------------------------------------------------------ */
int fflush(FILE *f)
{
    if (!f)
        return 0;   /* fflush(NULL) – no-op in this implementation */
    return _flush(f);
}
 
/* ------------------------------------------------------------------ */
/*  fread                                                              */
/* ------------------------------------------------------------------ */
size_t fread(void *buf, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !buf || size == 0 || nmemb == 0)
        return 0;
 
    size_t total  = size * nmemb;
    size_t copied = 0;
    char  *dst    = (char *)buf;
 
    while (copied < total) {
        /* consume from read buffer first */
        while (f->rpos < f->rlen && copied < total)
            dst[copied++] = f->rbuf[f->rpos++];
 
        if (copied == total)
            break;
 
        /* need more data */
        if (f->eof || f->error)
            break;
 
        if (_fill(f) == 0)
            break;
    }
 
    return (size > 0) ? (copied / size) : 0;
}
 
/* ------------------------------------------------------------------ */
/*  fwrite                                                             */
/* ------------------------------------------------------------------ */
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !buf || size == 0 || nmemb == 0)
        return 0;
 
    size_t       total   = size * nmemb;
    size_t       written = 0;
    const char  *src     = (const char *)buf;
 
    while (written < total) {
        /* fill write buffer */
        while (f->wlen < FILE_BUF_SIZE && written < total)
            f->wbuf[f->wlen++] = src[written++];
 
        if (f->wlen == FILE_BUF_SIZE) {
            if (_flush(f) == EOF)
                break;
        }
    }
 
    return (size > 0) ? (written / size) : 0;
}
 
/* ------------------------------------------------------------------ */
/*  fgetc                                                              */
/* ------------------------------------------------------------------ */
int fgetc(FILE *f)
{
    if (!f || f->error || f->eof)
        return EOF;
 
    if (f->rpos >= f->rlen) {
        if (_fill(f) == 0)
            return EOF;
    }
    return (unsigned char)f->rbuf[f->rpos++];
}
 
/* ------------------------------------------------------------------ */
/*  fputc                                                              */
/* ------------------------------------------------------------------ */
int fputc(int c, FILE *f)
{
    if (!f || f->error)
        return EOF;
 
    f->wbuf[f->wlen++] = (char)(unsigned char)c;
 
    if (f->wlen == FILE_BUF_SIZE || (unsigned char)c == '\n') {
        if (_flush(f) == EOF)
            return EOF;
    }
    return (unsigned char)c;
}

int ungetc(int c, FILE *f)
{
    if (!f || c == EOF)
        return EOF;
 
    if (f->rpos > 0) {
        /* there is room to step back in the existing buffer */
        f->rbuf[--f->rpos] = (char)(unsigned char)c;
    } else if (f->rlen < FILE_BUF_SIZE) {
        /* buffer has space: shift existing data right by one to make
           room at index 0 for the pushed-back byte                    */
        for (int i = f->rlen; i > 0; i--)
            f->rbuf[i] = f->rbuf[i - 1];
        f->rbuf[0] = (char)(unsigned char)c;
        f->rlen++;
    } else {
        /* buffer is completely full with unconsumed data – can't fit   */
        return EOF;
    }
 
    f->eof = 0;   /* ungetc always clears EOF per the C standard */
    return (unsigned char)c;
}

/* ------------------------------------------------------------------ */
/*  fgets                                                              */
/* ------------------------------------------------------------------ */
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
 
/* ------------------------------------------------------------------ */
/*  fputs / puts                                                       */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/*  fseek / ftell / rewind                                             */
/* ------------------------------------------------------------------ */
int fseek(FILE *f, long offset, int whence)
{
    if (!f || f->error)
        return -1;
 
    if (_flush(f) == EOF)
        return -1;
 
    /* discard read buffer */
    f->rpos = f->rlen = 0;
 
    int rc = seek(f->fd, (int64_t)offset, whence);
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
 
    /* current OS position, then subtract unread buffered bytes */
    int pos = seek(f->fd, 0, SEEK_CUR);
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
 
/* ------------------------------------------------------------------ */
/*  feof / ferror / clearerr                                           */
/* ------------------------------------------------------------------ */
int  feof(FILE *f)    { return f && f->eof;   }
int  ferror(FILE *f)  { return f && f->error; }
void clearerr(FILE *f){ if (f) { f->eof = 0; f->error = 0; } }
 
/* ------------------------------------------------------------------ */
/*  vfprintf / fprintf                                                 */
/* ------------------------------------------------------------------ */
int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[1024];
    int  n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) return n;
    /* clamp to buffer */
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

    for (; *fmt; fmt++) {
        /* Match literal whitespace: skip all whitespace in input */
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            while (*b == ' ' || *b == '\t' || *b == '\n') b++;
            continue;
        }

        /* Match literal non-% character */
        if (*fmt != '%') {
            if (*b != *fmt) goto done;
            b++;
            continue;
        }

        fmt++; /* skip '%' */

        /* Suppress assignment flag */
        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }

        /* Field width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* Length modifier */
        int is_long = 0, is_longlong = 0, is_short = 0;
        if (*fmt == 'h') { is_short = 1; fmt++; }
        else if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { is_longlong = 1; is_long = 0; fmt++; }

        char spec = *fmt;

        /* %% — match a literal percent */
        if (spec == '%') {
            if (*b != '%') goto done;
            b++;
            continue;
        }

        /* Skip leading whitespace for most specifiers */
        if (spec != 'c' && spec != '[') {
            while (*b == ' ' || *b == '\t' || *b == '\n') b++;
        }

        if (!*b) goto done;

        switch (spec) {

        /* ---- %d / %i ---- */
        case 'd':
        case 'i': {
            const char *start = b;
            int neg = 0;
            int base = (spec == 'i') ? 0 : 10;

            if (*b == '+') b++;
            else if (*b == '-') { neg = 1; b++; }

            /* Auto-detect base for %i */
            if (base == 0) {
                if (*b == '0') {
                    b++;
                    if (*b == 'x' || *b == 'X') { base = 16; b++; }
                    else base = 8;
                } else base = 10;
            }

            const char *num_start = b;
            long long val = 0;
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

            if (b == num_start) goto done; /* no digits consumed */
            if (neg) val = -val;

            if (!suppress) {
                if (is_longlong)     *va_arg(ap, long long *)      = val;
                else if (is_long)    *va_arg(ap, long *)           = (long)val;
                else if (is_short)   *va_arg(ap, short *)          = (short)val;
                else                 *va_arg(ap, int *)             = (int)val;
                assigned++;
            }
            break;
        }

        /* ---- %u / %o / %x / %X ---- */
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            int base = (spec == 'o') ? 8 : (spec == 'u') ? 10 : 16;
            /* consume optional 0x prefix for hex */
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

        /* ---- %s ---- */
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

        /* ---- %c ---- */
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

        /* ---- %n ---- */
        case 'n': {
            if (!suppress) {
                *va_arg(ap, int *) = (int)(b - buf);
                /* %n does not increment assigned */
            }
            break;
        }

        /* ---- %[ scanset ] ---- */
        case '[': {
            fmt++;
            int negate = 0;
            if (*fmt == '^') { negate = 1; fmt++; }

            /* build a 256-entry lookup table */
            unsigned char set[256] = {0};
            /* a ']' as the very first char (after optional ^) is literal */
            if (*fmt == ']') { set[(unsigned char)']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') {
                if (*(fmt+1) == '-' && *(fmt+2) && *(fmt+2) != ']') {
                    /* range */
                    unsigned char lo = (unsigned char)*fmt;
                    unsigned char hi = (unsigned char)*(fmt+2);
                    for (unsigned int c = lo; c <= hi; c++) set[c] = 1;
                    fmt += 3;
                } else {
                    set[(unsigned char)*fmt] = 1;
                    fmt++;
                }
            }
            /* fmt now points to ']' — the outer loop's fmt++ will advance past it */

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

        /* precision: for integers, minimum digits (zero-padded); for strings, max chars */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt++ - '0'); }
        }

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
            if (!s) s = "(null)";
            if (precision >= 0) {
                /* precision = max chars to print */
                size_t slen = 0;
                while (slen < (size_t)precision && s[slen]) slen++;
                if (!left)
                    for (size_t i = slen; i < (size_t)width; i++) _write_char(buf, &pos, size, ' ');
                for (size_t i = 0; i < slen; i++) _write_char(buf, &pos, size, s[i]);
                if (left)
                    for (size_t i = slen; i < (size_t)width; i++) _write_char(buf, &pos, size, ' ');
            } else {
                _write_str(buf, &pos, size, s, width, left);
            }
            break;
        }
        case 'd': case 'i': {
            long long v;
            if (is_longlong)     v = va_arg(ap, long long);
            else if (is_long)    v = va_arg(ap, long);
            else                 v = va_arg(ap, int);
            if (v < 0) { _write_char(buf, &pos, size, '-'); v = -v; }
            int eff_w = (precision >= 0) ? precision : width;
            int eff_z = (precision >= 0) ? 1 : zero_pad;
            _write_uint(buf, &pos, size, (unsigned long long)v, 10, 0, eff_w, eff_z, left);
            break;
        }
        case 'u': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            int eff_w = (precision >= 0) ? precision : width;
            int eff_z = (precision >= 0) ? 1 : zero_pad;
            _write_uint(buf, &pos, size, v, 10, 0, eff_w, eff_z, left);
            break;
        }
        case 'x': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            int eff_w = (precision >= 0) ? precision : width;
            int eff_z = (precision >= 0) ? 1 : zero_pad;
            _write_uint(buf, &pos, size, v, 16, 0, eff_w, eff_z, left);
            break;
        }
        case 'X': {
            unsigned long long v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned int);
            int eff_w = (precision >= 0) ? precision : width;
            int eff_z = (precision >= 0) ? 1 : zero_pad;
            _write_uint(buf, &pos, size, v, 16, 1, eff_w, eff_z, left);
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

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(1, buf, r);
    return r;
}