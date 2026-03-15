#include <syscall.h>

uint64_t syscall0(uint64_t nr)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr) : "memory");
    return r;
}

uint64_t syscall1(uint64_t nr, uint64_t a0)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0) : "memory");
    return r;
}

uint64_t syscall2(uint64_t nr, uint64_t a0, uint64_t a1)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1) : "memory");
    return r;
}

uint64_t syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1), "d"(a2) : "memory");
    return r;
}