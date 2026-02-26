#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef long ssize_t;

#define PACKED        __attribute__((packed))
#define NORETURN      __attribute__((noreturn))
#define UNUSED        __attribute__((unused))
#define ALWAYS_INLINE __attribute__((always_inline)) static inline
#define ALIGN(n)      __attribute__((aligned(n)))

#define MIN(a, b)          ((a) < (b) ? (a) : (b))
#define MAX(a, b)          ((a) > (b) ? (a) : (b))
#define ALIGN_UP(x, a)     (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)   ((x) & ~((a) - 1))
#define ARRAY_LEN(a)       (sizeof(a) / sizeof((a)[0]))
#define DIV_CEIL(a, b)     (((a) + (b) - 1) / (b))

#define PAGE_SIZE   0x1000UL
#define PAGE_SHIFT  12

#define HHDM_BASE        0xFFFF800000000000UL   /* physical → virtual via HHDM */
#define KERNEL_BASE      0xFFFFFFFF80000000UL   /* kernel link address */
#define HEAP_BASE        0xFFFFFF8000000000UL   /* kernel heap region */
#define HEAP_MAX         0xFFFFFF9000000000UL
#define USER_STACK_TOP   0x00007FFFFFFFE000UL   /* top of user stack */
#define USER_SPACE_MAX   0x00007FFFFFFFFFFFUL

extern uint64_t g_hhdm_offset;
#define PHYS_TO_VIRT(p)  ((void *)((uint64_t)(p) + g_hhdm_offset))
#define VIRT_TO_PHYS(v)  ((uint64_t)(v) - g_hhdm_offset)

ALWAYS_INLINE uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

ALWAYS_INLINE void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

ALWAYS_INLINE uint64_t read_cr2(void)
{
    uint64_t v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v;
}

ALWAYS_INLINE uint64_t read_cr3(void)
{
    uint64_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v;
}

ALWAYS_INLINE void write_cr3(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
}

ALWAYS_INLINE void sti(void)  { __asm__ volatile("sti" ::: "memory"); }
ALWAYS_INLINE void cli(void)  { __asm__ volatile("cli" ::: "memory"); }
ALWAYS_INLINE void hlt(void)  { __asm__ volatile("hlt"); }
ALWAYS_INLINE void pause(void){ __asm__ volatile("pause"); }