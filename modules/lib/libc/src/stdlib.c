#include <stdlib.h>
#include <syscall.h>
#include <ctype.h>

int abs(int x)
{
    return x < 0 ? -x : x;
}

void *mmap(size_t len, int prot)
{
    long r = syscall2(SYS_MMAP, (long)len, (long)prot);
    return (r == -1) ? (void *)-1 : (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t len)
{
    return (int)syscall2(SYS_MUNMAP, (long)addr, (long)len);
}

int exit(int code)
{
    return (int)syscall1(SYS_EXIT, (long)code);
}

void abort(void)
{
    exit(1);
}

void yield(void)
{
    syscall0(SYS_YIELD);
}

char *getenv(const char *name)
{
    return NULL;
}

int system(const char *command)
{
    return 0;
}

static int digit_val(char c)
{
    if (isdigit(c))  return c - '0';
    if (isalpha(c))  return tolower(c) - 'a' + 10;
    return 36; /* invalid */
}

unsigned long long strtoull(const char *s, char **endptr, int base)
{
    while (isspace(*s)) s++;

    int neg = 0;
    if (*s == '+') s++;
    else if (*s == '-') { neg = 1; s++; }

    /* auto-detect base */
    if (base == 0) {
        if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && *s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) {
        s += 2;
    }

    unsigned long long val = 0;
    int any = 0;
    int dv;
    while ((dv = digit_val(*s)) < base) {
        val = val * (unsigned)base + (unsigned)dv;
        s++; any = 1;
    }
    if (endptr) *endptr = any ? (char *)s : (char *)(s - any);
    /* note: overflow clamping omitted for size; add if needed */
    return neg ? (unsigned long long)(-(long long)val) : val;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtoull(s, endptr, base);
}

long long strtoll(const char *s, char **endptr, int base)
{
    return (long long)strtoull(s, endptr, base);
}

long strtol(const char *s, char **endptr, int base)
{
    return (long)strtoull(s, endptr, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

long atol(const char *s)
{
    return strtol(s, NULL, 10);
}

long long atoll(const char *s)
{
    return strtoll(s, NULL, 10);
}

double strtod(const char *s, char **endptr)
{
    while (isspace(*s)) s++;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    /* integer part */
    double val = 0.0;
    int any = 0;
    while (isdigit(*s)) { val = val * 10.0 + (*s - '0'); s++; any = 1; }

    /* fractional part */
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (isdigit(*s)) {
            val += (*s - '0') * frac;
            frac *= 0.1;
            s++; any = 1;
        }
    }

    /* exponent */
    if (any && (*s == 'e' || *s == 'E')) {
        s++;
        int eneg = 0;
        if (*s == '-') { eneg = 1; s++; }
        else if (*s == '+') s++;
        int exp = 0;
        while (isdigit(*s)) { exp = exp * 10 + (*s - '0'); s++; }
        double base = 10.0;
        while (exp-- > 0) val = eneg ? val / base : val * base;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

double atof(const char *s)
{
    return strtod(s, NULL);
}

char *itoa(int value, char *buf, int base)
{
    if (base < 2 || base > 36) { buf[0] = '\0'; return buf; }
    char *p = buf;
    unsigned int uv = (unsigned int)value;
    if (base == 10 && value < 0) { *p++ = '-'; uv = (unsigned int)-value; }
    char *start = p;
    do {
        int d = uv % base;
        *p++ = d < 10 ? '0' + d : 'a' + d - 10;
        uv /= base;
    } while (uv);
    *p = '\0';
    /* reverse digits */
    char *end = p - 1;
    while (start < end) { char t = *start; *start++ = *end; *end-- = t; }
    return buf;
}

static void swap_bytes(char *a, char *b, size_t size)
{
    while (size--)
    {
        char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

static void quicksort(char *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (nmemb < 2)
        return;

    char *pivot = base + (nmemb / 2) * size;
    size_t i = 0;
    size_t j = nmemb - 1;

    while (1)
    {
        while (compar(base + i * size, pivot) < 0)
            i++;

        while (compar(base + j * size, pivot) > 0)
            j--;

        if (i >= j)
            break;

        swap_bytes(base + i * size, base + j * size, size);

        if (pivot == base + i * size)
            pivot = base + j * size;
        else if (pivot == base + j * size)
            pivot = base + i * size;

        i++;
        if (j > 0)
            j--;
    }

    quicksort(base, i, size, compar);
    quicksort(base + i * size, nmemb - i, size, compar);
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (!base || nmemb < 2 || size == 0)
        return;

    quicksort((char *)base, nmemb, size, compar);
}