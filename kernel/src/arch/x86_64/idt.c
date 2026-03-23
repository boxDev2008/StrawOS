#include "idt.h"
#include "io.h"

#include <stdint.h>

/* ── IDT entry ────────────────────────────────────────────────── */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;       /* IST index (0 = use rsp0 from TSS) */
    uint8_t  type_attr; /* 0x8E = present, DPL0, interrupt gate */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) IDTEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IDTPointer;

static IDTEntry  g_idt[256];
static IDTPointer g_idtp;

static IRQHandler g_irq_handlers[16];

/* External ISR stubs from isr_stubs.asm */
extern void *isr_stub_table[256];

static void idt_set_entry(int vec, void *handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    g_idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    g_idt[vec].selector    = 0x08;  /* kernel code */
    g_idt[vec].ist         = 0;
    g_idt[vec].type_attr   = type_attr;
    g_idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    g_idt[vec].offset_high = (uint32_t)(addr >> 32);
    g_idt[vec].reserved    = 0;
}

/* ── 8259 PIC ─────────────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(void) {
    /* Save masks */
    uint8_t m1 = inb(PIC1_DATA);
    uint8_t m2 = inb(PIC2_DATA);

    outb(PIC1_CMD,  0x11); io_wait(); /* ICW1: init + ICW4 */
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait(); /* ICW2: PIC1 → vectors 32–39 */
    outb(PIC2_DATA, 0x28); io_wait(); /* ICW2: PIC2 → vectors 40–47 */
    outb(PIC1_DATA, 0x04); io_wait(); /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait(); /* ICW3: cascade identity */
    outb(PIC1_DATA, 0x01); io_wait(); /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    /* Restore masks (all masked initially) */
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void irq_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = 1 << (irq & 7);
    outb(port, inb(port) | bit);
}

void irq_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = 1 << (irq & 7);
    outb(port, inb(port) & ~bit);
}

void irq_register(uint8_t irq, IRQHandler h) {
    g_irq_handlers[irq] = h;
    irq_unmask(irq);
}

/* ── Exception / IRQ dispatch ─────────────────────────────────── */

static const char *exception_names[] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "FPU Error", "Alignment Check", "Machine Check", "SIMD FP",
};

void interrupt_dispatch(InterruptFrame *frame) {
    uint64_t vec = frame->vector;

    if (vec < 32) {
        /* CPU exception — print diagnostic and halt */
        extern void kprintf(const char *fmt, ...);
        const char *name = (vec < 20) ? exception_names[vec] : "Unknown";

        if (vec == 14) {
            /* Page Fault: read CR2 for the faulting address */
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            kprintf("\r\n[EXCEPTION] #PF (Page Fault)\r\n"
                    "  RIP=0x%p  CR2=0x%p  err=0x%x\r\n"
                    "  CS=0x%x  SS=0x%x  RSP=0x%p  RFLAGS=0x%x\r\n",
                    (void*)frame->rip, (void*)cr2,
                    (uint32_t)frame->error_code,
                    (uint32_t)frame->cs, (uint32_t)frame->ss,
                    (void*)frame->rsp, (uint32_t)frame->rflags);
        } else {
            kprintf("\r\n[EXCEPTION] #%u (%s)\r\n"
                    "  RIP=0x%p  err=0x%x\r\n"
                    "  CS=0x%x  SS=0x%x  RSP=0x%p  RFLAGS=0x%x\r\n",
                    (uint32_t)vec, name,
                    (void*)frame->rip, (uint32_t)frame->error_code,
                    (uint32_t)frame->cs, (uint32_t)frame->ss,
                    (void*)frame->rsp, (uint32_t)frame->rflags);
        }
        /* Halt — unrecoverable */
        for (;;) __asm__ volatile("hlt");
    } else if (vec < 48) {
        /* Hardware IRQ */
        uint8_t irq = (uint8_t)(vec - 32);
        if (g_irq_handlers[irq]) g_irq_handlers[irq](frame);
        pic_eoi(irq);
    }
    else if (vec == 0x80) {
        extern void syscall_int80_handler(InterruptFrame *frame);
        syscall_int80_handler(frame);
    }
    /* Vectors 48–255: ignored (spurious, etc.) */
}

/* ── Init ─────────────────────────────────────────────────────── */

void idt_init(void) {
    /* Mask all IRQs, then remap PIC */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    pic_remap();

    /* Install all 256 stubs.
     * Vectors 0-31: exception gates (DPL=0, interrupt gate 0x8E)
     * Vector  0x80: syscall trap gate (DPL=3, 0xEF) — trap gate keeps IF set
     *               so the PIT keeps ticking during blocking syscalls
     * Vectors 32-47: IRQ gates (DPL=0, 0x8E)
     */
    for (int i = 0; i < 256; i++) {
        uint8_t attr = (i == 0x80) ? 0xEF : 0x8E;  /* 0xEF = trap gate, 0x8E = interrupt gate */
        idt_set_entry(i, isr_stub_table[i], attr);
    }

    g_idtp.limit = sizeof(g_idt) - 1;
    g_idtp.base  = (uint64_t)&g_idt;

    __asm__ volatile("lidt %0" :: "m"(g_idtp) : "memory");

    //kprintf("[IDT] Initialized (%d entries, PIC remapped)\n", 256);
}