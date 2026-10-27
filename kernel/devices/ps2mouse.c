#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define PS2_STATUS_OUT_FULL (1 << 0)
#define PS2_STATUS_IN_FULL  (1 << 1)
#define PS2_STATUS_AUX_DATA (1 << 5)

#define PS2_CTL_READ_CONFIG  0x20
#define PS2_CTL_WRITE_CONFIG 0x60
#define PS2_CTL_ENABLE_AUX   0xA8
#define PS2_CTL_TEST_AUX     0xA9
#define PS2_CTL_WRITE_AUX    0xD4

#define PS2_CONFIG_AUX_IRQ   (1 << 1)
#define PS2_CONFIG_AUX_CLOCK (1 << 5)

#define PS2_MOUSE_SET_DEFAULTS 0xF6
#define PS2_MOUSE_ENABLE       0xF4
#define PS2_MOUSE_SAMPLE_RATE  0xF3
#define PS2_MOUSE_GET_ID       0xF2
#define PS2_MOUSE_ACK          0xFA

#define PS2_TIMEOUT 100000

static uint8_t ps2_packet[4];
static int     ps2_packet_len;
static int     ps2_packet_size = 3;
static bool    ps2_mouse_ready;

static bool ps2_wait_write(void)
{
    for (int i = 0; i < PS2_TIMEOUT; i++)
    {
        if (!(inb(PS2_STATUS) & PS2_STATUS_IN_FULL))
            return true;
        pause();
    }
    return false;
}

static int ps2_read(bool want_aux)
{
    for (int i = 0; i < PS2_TIMEOUT; i++)
    {
        uint8_t status = inb(PS2_STATUS);
        if (status & PS2_STATUS_OUT_FULL)
        {
            uint8_t data = inb(PS2_DATA);
            bool is_aux = (status & PS2_STATUS_AUX_DATA) != 0;
            if (is_aux == want_aux)
                return data;
            if (!is_aux)
                keyboard_push_scancode(data);
            continue;
        }
        pause();
    }
    return -1;
}

static bool ps2_ctl_cmd(uint8_t cmd)
{
    if (!ps2_wait_write())
        return false;
    outb(PS2_CMD, cmd);
    return true;
}

static bool ps2_ctl_write_data(uint8_t data)
{
    if (!ps2_wait_write())
        return false;
    outb(PS2_DATA, data);
    return true;
}

static bool ps2_mouse_cmd(uint8_t cmd)
{
    if (!ps2_ctl_cmd(PS2_CTL_WRITE_AUX) || !ps2_ctl_write_data(cmd))
        return false;
    return ps2_read(true) == PS2_MOUSE_ACK;
}

static bool ps2_mouse_set_rate(uint8_t rate)
{
    return ps2_mouse_cmd(PS2_MOUSE_SAMPLE_RATE) && ps2_mouse_cmd(rate);
}

static void ps2_mouse_handle_byte(uint8_t data)
{
    if (ps2_packet_len == 0 && !(data & 0x08))
        return;

    ps2_packet[ps2_packet_len++] = data;
    if (ps2_packet_len < ps2_packet_size)
        return;
    ps2_packet_len = 0;

    uint8_t flags = ps2_packet[0];
    if (flags & 0xC0)
        return;

    MouseEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.buttons = flags & 0x07;
    ev.dx = (int16_t)(ps2_packet[1] - ((flags & 0x10) ? 0x100 : 0));
    ev.dy = (int16_t)-(ps2_packet[2] - ((flags & 0x20) ? 0x100 : 0));
    if (ps2_packet_size == 4)
        ev.wheel = (int8_t)-(int8_t)ps2_packet[3];

    mouse_push_event(&ev);
}

void ps2_mouse_feed(uint8_t data)
{
    if (ps2_mouse_ready)
        ps2_mouse_handle_byte(data);
}

static void ps2_mouse_irq_handler(void)
{
    while (inb(PS2_STATUS) & PS2_STATUS_OUT_FULL)
    {
        uint8_t status = inb(PS2_STATUS);
        uint8_t data = inb(PS2_DATA);
        if (status & PS2_STATUS_AUX_DATA)
            ps2_mouse_feed(data);
        else
            keyboard_push_scancode(data);
    }
}

void ps2_mouse_init(void)
{
    if (inb(PS2_STATUS) == 0xFF)
    {
        kprintf("ps2mouse: no i8042 controller\n");
        return;
    }

    uint64_t flags = irq_save();

    if (!ps2_ctl_cmd(PS2_CTL_ENABLE_AUX))
        goto fail;

    if (!ps2_ctl_cmd(PS2_CTL_TEST_AUX))
        goto fail;
    if (ps2_read(false) != 0x00)
    {
        kprintf("ps2mouse: aux port test failed\n");
        goto fail_quiet;
    }

    if (!ps2_ctl_cmd(PS2_CTL_READ_CONFIG))
        goto fail;
    int config = ps2_read(false);
    if (config < 0)
        goto fail;
    config = (config | PS2_CONFIG_AUX_IRQ) & ~PS2_CONFIG_AUX_CLOCK;
    if (!ps2_ctl_cmd(PS2_CTL_WRITE_CONFIG) || !ps2_ctl_write_data((uint8_t)config))
        goto fail;

    if (!ps2_mouse_cmd(PS2_MOUSE_SET_DEFAULTS))
    {
        kprintf("ps2mouse: no mouse on aux port\n");
        goto fail_quiet;
    }

    ps2_packet_size = 3;
    if (ps2_mouse_set_rate(200) && ps2_mouse_set_rate(100) && ps2_mouse_set_rate(80) &&
        ps2_mouse_cmd(PS2_MOUSE_GET_ID) && ps2_read(true) == 3)
        ps2_packet_size = 4;

    if (!ps2_mouse_set_rate(100) || !ps2_mouse_cmd(PS2_MOUSE_ENABLE))
        goto fail;

    ps2_packet_len = 0;
    ps2_mouse_ready = true;

    idt_set_irq_handler(IRQ_MOUSE, ps2_mouse_irq_handler);
    ioapic_set_irq(12, lapic_id(), IRQ_MOUSE);

    irq_restore(flags);
    kprintf("ps2mouse: enabled (%s)\n", ps2_packet_size == 4 ? "wheel" : "3-byte");
    return;

fail:
    kprintf("ps2mouse: controller timed out\n");
fail_quiet:
    irq_restore(flags);
}
