#include "sys/syscall.h"

void syscall_yield(void)
{
    asm volatile
    (
        "mov $0, %eax\n"
        "int $0x80"
    );
}

void syscall_print(const char *data, size_t size)
{
    asm volatile
    (
        "mov $1, %%eax\n"
        "mov %0, %%ebx\n"
        "mov %1, %%ecx\n"
        "int $0x80"
        :
        : "r"(data), "r"(size)
        : "%eax", "%ebx", "%ecx"
    );
}

void *syscall_malloc(size_t size)
{
    void *result;
    asm volatile
    (
        "movl $2, %%eax\n"
        "movl %1, %%ebx\n"
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r" (result)
        : "r" (size)
        : "%eax", "%ebx"
    );
    return result;
}

void *syscall_calloc(size_t nitems, size_t size)
{
    void *result;
    asm volatile
    (
        "movl $3, %%eax\n"
        "movl %1, %%ebx\n"
        "movl %2, %%ecx\n"
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r" (result)
        : "r" (nitems), "r" (size)
        : "%eax", "%ebx", "%ecx"
    );
    return result;
}

void *syscall_realloc(void *ptr, size_t size)
{
    void *result;
    asm volatile
    (
        "movl $4, %%eax\n"
        "movl %1, %%ebx\n"
        "movl %2, %%ecx\n"
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r" (result)
        : "r" (ptr), "r" (size)
        : "%eax", "%ebx", "%ecx"
    );
    return result;
}

void syscall_free(void *ptr)
{
    asm volatile
    (
        "mov $5, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80"
        :
        : "r"(ptr)
        : "%eax", "%ebx"
    );
}