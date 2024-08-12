#include "stdlib.h"
#include "sys/syscall.h"

void *malloc(size_t size)
{
    return syscall_malloc(size);
}

void *calloc(size_t nitems, size_t size)
{
    return syscall_calloc(nitems, size);
}

void *realloc(void *ptr, size_t size)
{
    return syscall_realloc(ptr, size);
}

void free(void *ptr)
{
    syscall_free(ptr);
}