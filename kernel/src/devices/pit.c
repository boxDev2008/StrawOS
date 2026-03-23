#include "pit.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/idt.h"
#include "system/task.h"

#include "libk/kprintf.h"
#include <stdbool.h>

#define PIT_BASE_FREQUENCY 1193182

static uint32_t frequency;

static volatile uint64_t ticks = 0;
volatile bool schedule_frozen = false;

static void pit_handler(InterruptFrame *frame)
{
    ticks++;
    pic_eoi(0);
    while (schedule_frozen) return;
    if (task_current()->pid == 0)
        task_reap_dead();
    task_yield();
}

void pit_init(void)
{
    uint16_t divisor = PIT_BASE_FREQUENCY / 1000;

    irq_register(0, pit_handler);

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint64_t pit_get_ticks(void)
{
    return ticks;
}

uint64_t pit_get_ticks_ms(void)
{
    return ticks;
}