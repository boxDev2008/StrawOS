#pragma once
#include <stdint.h>

inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

inline void io_wait(void)
{
    outb(0x80, 0);
}