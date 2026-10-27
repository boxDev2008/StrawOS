int abs(int x)
{
    return x < 0 ? -x : x;
}

#define HEAP_PAGE_SIZE  4096
#define HEAP_ARENA_SIZE (64 * 1024)
#define HEAP_LARGE      0xFFFFFFFFu
#define HEAP_MAGIC      0x5354524Bu

typedef struct
{
    uint32_t cls;
    uint32_t magic;
    uint64_t size;
}
HeapHeader;

typedef struct HeapFree
{
    struct HeapFree *next;
}
HeapFree;

static const uint32_t heap_classes[] = {
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096,
};
#define HEAP_CLASS_N ((int)(sizeof(heap_classes) / sizeof(heap_classes[0])))

static HeapFree *heap_free_lists[HEAP_CLASS_N];
static uint8_t  *heap_arena;
static size_t    heap_arena_left;

static size_t heap_large_pages(size_t size)
{
    return (size + sizeof(HeapHeader) + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
}

static int heap_class_for(size_t size)
{
    for (int i = 0; i < HEAP_CLASS_N; i++)
        if (size <= heap_classes[i])
            return i;
    return -1;
}

static void *heap_carve(size_t bytes)
{
    if (heap_arena_left < bytes)
    {
        uint8_t *arena = (uint8_t *)syscall(SYS_MEM_ALLOC, HEAP_ARENA_SIZE, 0, 0, 0);
        if (!arena)
            return NULL;
        heap_arena = arena;
        heap_arena_left = HEAP_ARENA_SIZE;
    }
    void *p = heap_arena;
    heap_arena += bytes;
    heap_arena_left -= bytes;
    return p;
}

void *malloc(size_t size)
{
    int cls = heap_class_for(size ? size : 1);
    HeapHeader *h;

    if (cls < 0)
    {
        if (size > SIZE_MAX - sizeof(HeapHeader) - HEAP_PAGE_SIZE)
            return NULL;
        h = (HeapHeader *)syscall(SYS_MEM_ALLOC, size + sizeof(HeapHeader), 0, 0, 0);
        if (!h)
            return NULL;
        h->cls = HEAP_LARGE;
        h->magic = HEAP_MAGIC;
        h->size = size;
        return h + 1;
    }

    if (heap_free_lists[cls])
    {
        HeapFree *f = heap_free_lists[cls];
        heap_free_lists[cls] = f->next;
        h = (HeapHeader *)f - 1;
    }
    else
    {
        h = heap_carve(sizeof(HeapHeader) + heap_classes[cls]);
        if (!h)
            return NULL;
        h->cls = (uint32_t)cls;
        h->magic = HEAP_MAGIC;
    }
    h->size = size;
    return h + 1;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    HeapHeader *h = (HeapHeader *)ptr - 1;
    if (h->magic != HEAP_MAGIC)
        return;

    if (h->cls == HEAP_LARGE)
    {
        h->magic = 0;
        syscall(SYS_MEM_FREE, (uint64_t)h, h->size + sizeof(HeapHeader), 0, 0);
        return;
    }

    HeapFree *f = (HeapFree *)ptr;
    f->next = heap_free_lists[h->cls];
    heap_free_lists[h->cls] = f;
}

void *calloc(size_t nmemb, size_t size)
{
    if (size && nmemb > SIZE_MAX / size)
        return NULL;
    void *p = malloc(nmemb * size);
    if (p)
        memset(p, 0, nmemb * size);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    HeapHeader *h = (HeapHeader *)ptr - 1;
    if (h->cls != HEAP_LARGE ? size <= heap_classes[h->cls]
                             : heap_large_pages(size) == heap_large_pages(h->size))
    {
        h->size = size;
        return ptr;
    }

    void *n = malloc(size);
    if (!n)
        return NULL;
    memcpy(n, ptr, h->size < size ? h->size : size);
    free(ptr);
    return n;
}

static void (*atexit_handlers[32])(void);
static int atexit_count;

__attribute__((weak)) int atexit(void (*fn)(void))
{
    if (atexit_count >= 32)
        return -1;
    atexit_handlers[atexit_count++] = fn;
    return 0;
}

__attribute__((weak)) void exit(int code)
{
    while (atexit_count > 0)
        atexit_handlers[--atexit_count]();
    fflush(stdout);
    fflush(stderr);
    syscall(SYS_EXIT, (uint64_t)(int64_t)code, 0, 0, 0);
    for (;;)
        ;
}

void abort(void)
{
    exit(1);
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
    return 36;
}

unsigned long long strtoull(const char *s, char **endptr, int base)
{
    while (isspace(*s)) s++;

    int neg = 0;
    if (*s == '+') s++;
    else if (*s == '-') { neg = 1; s++; }

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

static long double strtod_pow(long double base, int e)
{
    long double r = 1.0L;
    unsigned n = e < 0 ? (unsigned)-e : (unsigned)e;
    while (n)
    {
        if (n & 1)
            r *= base;
        base *= base;
        n >>= 1;
    }
    return e < 0 ? 1.0L / r : r;
}

static int strtod_match(const char *s, const char *word)
{
    int i = 0;
    for (; word[i]; i++)
        if (tolower((unsigned char)s[i]) != word[i])
            return 0;
    return i;
}

double strtod(const char *s, char **endptr)
{
    const char *start = s;
    while (isspace((unsigned char)*s))
        s++;

    int neg = 0;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';

    int n;
    if ((n = strtod_match(s, "infinity")) || (n = strtod_match(s, "inf")))
    {
        if (endptr)
            *endptr = (char *)(s + n);
        return neg ? -INFINITY : INFINITY;
    }
    if ((n = strtod_match(s, "nan")))
    {
        if (endptr)
            *endptr = (char *)(s + n);
        return NAN;
    }

    bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (hex)
    {
        const char *p = s + 2;
        uint64_t mant = 0;
        int exp = 0, any = 0;
        for (;; p++)
        {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else break;
            any = 1;
            if (mant >> 60)
                exp += 4;
            else
                mant = mant * 16 + (uint64_t)d;
        }
        if (*p == '.')
        {
            for (p++;; p++)
            {
                int d;
                if (*p >= '0' && *p <= '9') d = *p - '0';
                else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
                else break;
                any = 1;
                if (!(mant >> 60))
                {
                    mant = mant * 16 + (uint64_t)d;
                    exp -= 4;
                }
            }
        }
        if (!any)
        {
            if (endptr)
                *endptr = (char *)(s + 1);
            return neg ? -0.0 : 0.0;
        }
        if (*p == 'p' || *p == 'P')
        {
            const char *q = p + 1;
            int eneg = 0, e = 0, edig = 0;
            if (*q == '-' || *q == '+')
                eneg = *q++ == '-';
            while (isdigit((unsigned char)*q))
            {
                if (e < 100000)
                    e = e * 10 + (*q - '0');
                q++;
                edig = 1;
            }
            if (edig)
            {
                exp += eneg ? -e : e;
                p = q;
            }
        }
        if (endptr)
            *endptr = (char *)p;
        double v = ldexp((double)mant, exp);
        return neg ? -v : v;
    }

    uint64_t mant = 0;
    int digits = 0, dexp = 0, any = 0;
    for (; isdigit((unsigned char)*s); s++)
    {
        any = 1;
        if (digits < 19)
        {
            mant = mant * 10 + (uint64_t)(*s - '0');
            if (mant)
                digits++;
        }
        else
        {
            dexp++;
        }
    }
    if (*s == '.')
    {
        for (s++; isdigit((unsigned char)*s); s++)
        {
            any = 1;
            if (digits < 19)
            {
                mant = mant * 10 + (uint64_t)(*s - '0');
                if (mant)
                    digits++;
                dexp--;
            }
        }
    }

    if (!any)
    {
        if (endptr)
            *endptr = (char *)start;
        return 0.0;
    }

    if (*s == 'e' || *s == 'E')
    {
        const char *q = s + 1;
        int eneg = 0, e = 0, edig = 0;
        if (*q == '-' || *q == '+')
            eneg = *q++ == '-';
        while (isdigit((unsigned char)*q))
        {
            if (e < 100000)
                e = e * 10 + (*q - '0');
            q++;
            edig = 1;
        }
        if (edig)
        {
            dexp += eneg ? -e : e;
            s = q;
        }
    }

    if (endptr)
        *endptr = (char *)s;

    double v;
    if (mant == 0)
        v = 0.0;
    else if (dexp > 330)
        v = INFINITY;
    else if (dexp < -360)
        v = 0.0;
    else
        v = (double)((long double)mant * strtod_pow(10.0L, dexp));
    return neg ? -v : v;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

long double strtold(const char *s, char **endptr)
{
    return strtod(s, endptr);
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

static unsigned long rand_state = 1;

int rand_r(unsigned int *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (int)((*seed >> 1) & RAND_MAX);
}

int rand(void)
{
    rand_state = rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((rand_state >> 33) & RAND_MAX);
}

void srand(unsigned int seed)
{
    rand_state = seed;
}

long random(void)
{
    return rand();
}

void srandom(unsigned int seed)
{
    srand(seed);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    size_t lo = 0, hi = nmemb;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        const void *item = (const char *)base + mid * size;
        int c = compar(key, item);
        if (c == 0)
            return (void *)item;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

long labs(long x)
{
    return x < 0 ? -x : x;
}

long long llabs(long long x)
{
    return x < 0 ? -x : x;
}

div_t div(int a, int b)
{
    div_t d = { a / b, a % b };
    return d;
}

ldiv_t ldiv(long a, long b)
{
    ldiv_t d = { a / b, a % b };
    return d;
}

int mkstemp(char *tmpl)
{
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return -1;
    }
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0; attempt < 100; attempt++)
    {
        unsigned long v = (unsigned long)uptime_ms() * 2654435761u + (unsigned long)rand();
        for (int i = 0; i < 6; i++)
        {
            tmpl[len - 6 + i] = chars[v % 36];
            v /= 36;
        }
        struct stat st;
        if (stat(tmpl, &st) == 0)
            continue;
        int fd = open(tmpl, O_RDWR | O_CREAT);
        if (fd >= 0)
            return fd;
    }
    errno = EEXIST;
    return -1;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    return 0;
}

int unsetenv(const char *name)
{
    (void)name;
    return 0;
}
