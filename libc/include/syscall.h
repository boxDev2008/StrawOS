#pragma once

#include <stddef.h>
#include <stdint.h>

#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_LSEEK   19

#define SYS_GETPID  20

#define SYS_YIELD   30
#define SYS_JUMP    31

#define SYS_SEGBRK  40
#define SYS_MMAP    41
#define SYS_MUNMAP  42

inline uint64_t syscall1(uint64_t nr, uint64_t a0)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0) : "memory");
    return r;
}

inline uint64_t syscall2(uint64_t nr, uint64_t a0, uint64_t a1)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1) : "memory");
    return r;
}

inline uint64_t syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    uint64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1), "d"(a2) : "memory");
    return r;
}