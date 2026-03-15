#include "ps2keyboard.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/idt.h"

#define KEYBOARD_QUEUE_SIZE 128

static volatile uint8_t scancode_queue[KEYBOARD_QUEUE_SIZE];
static volatile uint32_t queue_head = 0;
static volatile uint32_t queue_tail = 0;

static void queue_push(uint8_t scancode)
{
    uint32_t next = (queue_tail + 1) % KEYBOARD_QUEUE_SIZE;
    if (next == queue_head) return;
    scancode_queue[queue_tail] = scancode;
    queue_tail = next;
}

static void ps2keyboard_handler(InterruptFrame *frame)
{
    uint8_t scancode = inb(0x60);
    queue_push(scancode);
}

uint8_t ps2keyboard_poll_scancode(uint8_t *out_scancode)
{
    if (queue_head == queue_tail) return 0;
    *out_scancode = scancode_queue[queue_head];
    queue_head = (queue_head + 1) % KEYBOARD_QUEUE_SIZE;
    return 1;
}

void ps2keyboard_init(void)
{
    while (inb(0x64) & 0x01)
        inb(0x60);

    irq_register(1, ps2keyboard_handler);
}