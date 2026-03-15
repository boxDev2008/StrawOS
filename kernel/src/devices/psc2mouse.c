/*
 * ps2mouse.c — PS/2 mouse driver for StrawOS
 *
 * Uses the 8042 PS/2 controller (ports 0x60 / 0x64).
 * IRQ 12 fires for every byte; packets arrive in groups of 3.
 *
 * Packet layout (standard PS/2):
 *   Byte 0: [Y-ovf][X-ovf][Y-sgn][X-sgn][1][M][R][L]
 *   Byte 1: X movement (signed, 9-bit with sign in byte 0)
 *   Byte 2: Y movement (signed, 9-bit with sign in byte 0)  — Y axis inverted
 */

#include "ps2mouse.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/io.h"
#include "../libk/kprintf.h"

#include <stdint.h>
#include <stddef.h>

/* ── 8042 ports ──────────────────────────────────────────────────────────── */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64   /* read  */
#define PS2_CMD     0x64   /* write */

/* Status register bits */
#define PS2_STATUS_OUTPUT_FULL  (1 << 0)  /* data waiting in output buffer */
#define PS2_STATUS_INPUT_FULL   (1 << 1)  /* controller still processing    */

/* Controller commands */
#define PS2_CMD_READ_CCB        0x20      /* read command/config byte       */
#define PS2_CMD_WRITE_CCB       0x60      /* write command/config byte      */
#define PS2_CMD_ENABLE_AUX      0xA8      /* enable aux (mouse) port        */
#define PS2_CMD_WRITE_AUX       0xD4      /* next byte goes to mouse        */

/* Mouse commands */
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE_STREAM 0xF4
#define MOUSE_ACK               0xFA

/* Packet byte-0 bit masks */
#define PKT_LEFT    (1 << 0)
#define PKT_RIGHT   (1 << 1)
#define PKT_MIDDLE  (1 << 2)
#define PKT_ALWAYS1 (1 << 3)
#define PKT_X_SIGN  (1 << 4)
#define PKT_Y_SIGN  (1 << 5)
#define PKT_X_OVF   (1 << 6)
#define PKT_Y_OVF   (1 << 7)

/* ── module state ────────────────────────────────────────────────────────── */
static MouseState g_mouse;
static uint32_t   g_screen_w;
static uint32_t   g_screen_h;

/* ── low-level helpers ───────────────────────────────────────────────────── */

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

/* Drain any stale bytes sitting in the 8042 output buffer. */
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

/* Returns 1 on ACK, 0 on failure. */
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

/* ── IRQ 12 handler ──────────────────────────────────────────────────────── */

static void mouse_irq_handler(InterruptFrame *frame)
{
    (void)frame;

    static uint8_t packet[3];
    static int     byte_idx = 0;

    /*
     * IRQ 12 fired, so a mouse byte is waiting — just read it directly.
     *
     * Do NOT check PS2_STATUS_AUX_DATA (bit 5 of the status port): it is
     * not reliably set in QEMU and many real BIOSes, so checking it causes
     * every single byte to be silently dropped, making the mouse dead.
     */
    uint8_t byte = inb(PS2_DATA);

    /*
     * Synchronisation: byte 0 must always have bit 3 set (the "always 1"
     * flag).  If we're out of sync, discard and wait for a good first byte.
     */
    if (byte_idx == 0 && !(byte & PKT_ALWAYS1))
        return;

    packet[byte_idx++] = byte;

    if (byte_idx < 3)
        return;   /* not a full packet yet */

    byte_idx = 0;

    /* Discard packets with overflow — data is unreliable. */
    if ((packet[0] & PKT_X_OVF) || (packet[0] & PKT_Y_OVF))
        return;

    /* Sign-extend the 9-bit deltas using the sign bits from byte 0. */
    int32_t dx = (int32_t)(int8_t)packet[1];
    int32_t dy = (int32_t)(int8_t)packet[2];

    if (packet[0] & PKT_X_SIGN) dx |= ~0xFF;
    if (packet[0] & PKT_Y_SIGN) dy |= ~0xFF;

    /* PS/2 Y axis is inverted relative to screen coordinates. */
    dy = -dy;

    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.x += dx;
    g_mouse.y += dy;

    if (g_mouse.x < 0)                      g_mouse.x = 0;
    if (g_mouse.x >= (int32_t)g_screen_w)   g_mouse.x = (int32_t)g_screen_w - 1;
    if (g_mouse.y < 0)                       g_mouse.y = 0;
    if (g_mouse.y >= (int32_t)g_screen_h)   g_mouse.y = (int32_t)g_screen_h - 1;

    /* bit 0 = left, bit 1 = right, bit 2 = middle */
    g_mouse.buttons = packet[0] & 0x07;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

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

    /* 1. Enable the auxiliary (mouse) port on the 8042. */
    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_ENABLE_AUX);
    io_wait();

    /*
     * 2. Read-modify-write the Controller Command Byte (CCB).
     *
     *    Flush first: if the keyboard fired before we get here, there may be
     *    a scancode sitting in the output buffer.  Reading that instead of
     *    the CCB would give us a garbage value to write back — corrupting the
     *    PIC mask state and breaking both keyboard and mouse.
     */
    ps2_flush();

    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_READ_CCB);
    ps2_wait_read();
    uint8_t ccb = inb(PS2_DATA);

    ccb |=  0x02;   /* enable aux IRQ (IRQ 12)          */
    ccb &= ~0x20;   /* clear "disable aux clock" bit     */

    ps2_wait_write();
    outb(PS2_CMD, PS2_CMD_WRITE_CCB);
    ps2_wait_write();
    outb(PS2_DATA, ccb);

    /*
     * 3. Configure the mouse.
     *
     *    We intentionally skip MOUSE_CMD_RESET here.  Reset sends back a
     *    multi-byte response (0xAA 0x00) that requires careful draining.
     *    If anything goes slightly wrong with the drain timing, the leftover
     *    bytes desync our packet parser AND can be mistaken for keyboard
     *    data, silently breaking IRQ 1.  SET_DEFAULTS is enough.
     */
    mouse_cmd(MOUSE_CMD_SET_DEFAULTS);

    /* 4. Tell the mouse to start sending movement packets. */
    if (!mouse_cmd(MOUSE_CMD_ENABLE_STREAM)) {
        kprintf("[mouse] enable stream failed\r\n");
        return;
    }

    /*
     * 5. Register the handler.  Do this AFTER all polling reads above to
     *    avoid the handler racing with our init sequence.
     *
     *    IRQ 12 is on PIC2, so IRQ 2 (the cascade line on PIC1) must also
     *    be unmasked or PIC2 interrupts will never reach the CPU.
     */
    irq_unmask(2);
    irq_register(12, mouse_irq_handler);

    kprintf("[mouse] PS/2 mouse initialised (%ux%u)\r\n", screen_w, screen_h);
}