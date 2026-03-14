#include <stdlib.h>
#include <syscall.h>

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

int abs(int x)
{
    return x < 0 ? -x : x;
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