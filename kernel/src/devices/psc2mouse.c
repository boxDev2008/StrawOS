#include "ps2mouse.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/io.h"
#include "../libk/kprintf.h"

#include <stdint.h>
#include <stddef.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

#define PS2_STATUS_OUTPUT_FULL  (1 << 0)
#define PS2_STATUS_INPUT_FULL   (1 << 1)

#define PS2_CMD_READ_CCB        0x20
#define PS2_CMD_WRITE_CCB       0x60
#define PS2_CMD_ENABLE_AUX      0xA8
#define PS2_CMD_WRITE_AUX       0xD4

#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE_STREAM 0xF4
#define MOUSE_ACK               0xFA

#define PKT_LEFT    (1 << 0)
#define PKT_RIGHT   (1 << 1)
#define PKT_MIDDLE  (1 << 2)
#define PKT_ALWAYS1 (1 << 3)
#define PKT_X_SIGN  (1 << 4)
#define PKT_Y_SIGN  (1 << 5)
#define PKT_X_OVF   (1 << 6)
#define PKT_Y_OVF   (1 << 7)

static MouseState g_mouse;
static uint32_t   g_screen_w;
static uint32_t   g_screen_h;

static void ps2_wait_write(void)
{
    uint32_t timeout = 100000;
    while (timeout-- && (inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL))
        io_wait();
}

static void ps2_wait_read(void)
{
    uint32_t timeout = 100000;
    while (timeout-- && !(inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL))
        io_wait();
}

static void ps2_flush(void)
{
    int i = 16;
    while (i-- && (inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)) {
        io_wait();
        inb(PS2_DATA);
        io_wait();
    }
}

static void mouse_write(uint8_t data)
{
    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA, data);
}

static uint8_t mouse_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

static int mouse_cmd(uint8_t cmd)
{
    mouse_write(cmd);
    uint8_t ack = mouse_read();
    if (ack != MOUSE_ACK) {
        kprintf("[mouse] cmd 0x%x: expected ACK got 0x%x\r\n", cmd, ack);
        return 0;
    }
    return 1;
}

static void mouse_irq_handler(InterruptFrame *frame)
{
    (void)frame;

    static uint8_t packet[3];
    static int     byte_idx = 0;

    uint8_t byte = inb(PS2_DATA);

    if (byte_idx == 0 && !(byte & PKT_ALWAYS1))
        return;

    packet[byte_idx++] = byte;

    if (byte_idx < 3)
        return;

    byte_idx = 0;

    if ((packet[0] & PKT_X_OVF) || (packet[0] & PKT_Y_OVF))
        return;

    int32_t dx = (int32_t)(int8_t)packet[1];
    int32_t dy = (int32_t)(int8_t)packet[2];

    if (packet[0] & PKT_X_SIGN) dx |= ~0xFF;
    if (packet[0] & PKT_Y_SIGN) dy |= ~0xFF;

    dy = -dy;

    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.x += dx;
    g_mouse.y += dy;

    if (g_mouse.x < 0)                      g_mouse.x = 0;
    if (g_mouse.x >= (int32_t)g_screen_w)   g_mouse.x = (int32_t)g_screen_w - 1;
    if (g_mouse.y < 0)                       g_mouse.y = 0;
    if (g_mouse.y >= (int32_t)g_screen_h)   g_mouse.y = (int32_t)g_screen_h - 1;

    g_mouse.buttons = packet[0] & 0x07;
}

const MouseState *ps2mouse_get_state(void)
{
    return &g_mouse;
}

void ps2mouse_init(uint32_t screen_w, uint32_t screen_h)
{
    g_screen_w = screen_w;
    g_screen_h = screen_h;

    g_mouse.x       = (int32_t)(screen_w / 2);
    g_mouse.y       = (int32_t)(screen_h / 2);
    g_mouse.dx      = 0;
    g_mouse.dy      = 0;
    g_mouse.buttons = 0;

    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_ENABLE_AUX);
    io_wait();

    ps2_flush();

    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_READ_CCB);
    ps2_wait_read();
    uint8_t ccb = inb(PS2_DATA);

    ccb |=  0x02;
    ccb &= ~0x20;

    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_WRITE_CCB);
    ps2_wait_write();
    outb(PS2_DATA, ccb);

    mouse_cmd(MOUSE_CMD_SET_DEFAULTS);

    if (!mouse_cmd(MOUSE_CMD_ENABLE_STREAM)) {
        kprintf("[mouse] enable stream failed\r\n");
        return;
    }

    irq_unmask(2);
    irq_register(12, mouse_irq_handler);

    kprintf("[mouse] PS/2 mouse initialised (%ux%u)\r\n", screen_w, screen_h);
}