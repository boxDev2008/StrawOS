static volatile uint8_t kbd_ring[KBD_BUF_SIZE];
static volatile size_t kbd_head = 0, kbd_tail = 0;

void keyboard_push_scancode(uint8_t sc)
{
    size_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail)
    {
        kbd_ring[kbd_head] = sc;
        kbd_head = next;
    }
    task_wake_waiters();
}

bool keyboard_has_data(void)
{
    return kbd_tail != kbd_head;
}

static void keyboard_irq_handler(void)
{
    uint8_t status = inb(0x64);
    if (!(status & 0x01))
        return;
    uint8_t data = inb(0x60);
    if (status & 0x20)
        ps2_mouse_feed(data);
    else
        keyboard_push_scancode(data);
}

size_t keyboard_read_raw(void *buf, size_t count)
{
    uint8_t *out = (uint8_t *)buf;
    size_t n = 0;

    while (n < count && kbd_tail != kbd_head)
    {
        out[n++] = kbd_ring[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    }
    return n;
}

void keyboard_init(void)
{
    kbd_head = kbd_tail = 0;
    idt_set_irq_handler(IRQ_KEYBOARD, keyboard_irq_handler);
    ioapic_set_irq(1, lapic_id(), IRQ_KEYBOARD);
}
