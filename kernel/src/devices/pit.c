#include "pit.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/idt.h"

#define PIT_BASE_FREQUENCY 1193182

static volatile uint64_t ticks = 0;

static void pit_handler(InterruptFrame *frame)
{
    ticks++;
}

void pit_init(uint32_t freq)
{
    uint16_t divisor = PIT_BASE_FREQUENCY / freq;

    irq_register(0, pit_handler);

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint64_t pit_get_ticks(void)
{
    return ticks;
}