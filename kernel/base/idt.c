typedef struct PACKED
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
}
IdtEntry;

typedef struct PACKED
{
    uint16_t limit;
    uint64_t base;
}
IdtDescriptor;

static IdtEntry idt[IDT_ENTRIES] ALIGN(16);
static IdtDescriptor idt_desc;

static IrqHandler irq_handlers[IDT_ENTRIES - IRQ_BASE];

static void idt_set_entry(int vec, void (*isr)(void), uint8_t type_attr)
{
    uint64_t addr = (uint64_t)isr;

    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = GDT_CODE_SEL;
    idt[vec].ist         = 0;
    idt[vec].type_attr   = type_attr;
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].reserved    = 0;
}

#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP 0x8F

void idt_exception_handler(uint64_t vector, uint64_t error_code, uint64_t *frame)
{
    if (!bkl_held())
        bkl_lock();

    Task *t = task_current();
    bool in_program = t && t->owns_image;
    uint64_t rip = frame[2];

    if (vector == 14)
    {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("EXCEPTION %d (error code %x) at addr %llx in %s\n",
                (int)vector, (unsigned)error_code, (unsigned long long)cr2, t ? t->name : "?");
    }
    else
    {
        kprintf("EXCEPTION %d (error code %x) in %s\n", (int)vector, (unsigned)error_code, t ? t->name : "?");
    }

    if (in_program)
    {
        kprintf("  rip=%llx (offset %llx in image) rsp=%llx\n", (unsigned long long)rip,
                (unsigned long long)(rip - t->image.load_bias), (unsigned long long)frame[5]);
        kprintf("killing %s\n", t->name);
        task_exit(-1);
    }

    kprintf("halted\n");
    panic();
}

#define EXC_NOERR(n) \
    __attribute__((naked)) static void exc_stub_##n(void) \
    { \
        __asm__ volatile( \
            "push $0\n" \
            "push $" #n "\n" \
            "jmp exc_common\n" \
        ); \
    }

#define EXC_HASERR(n) \
    __attribute__((naked)) static void exc_stub_##n(void) \
    { \
        __asm__ volatile( \
            "push $" #n "\n" \
            "jmp exc_common\n" \
        ); \
    }

__attribute__((naked, used)) static void exc_common(void)
{
    __asm__ volatile(
        "mov 0(%rsp), %rdi\n"
        "mov 8(%rsp), %rsi\n"
        "mov %rsp, %rdx\n"
        "call idt_exception_handler\n"
        "add $16, %rsp\n"
        "iretq\n"
    );
}

EXC_NOERR(0)  EXC_NOERR(1)  EXC_NOERR(2)  EXC_NOERR(3)
EXC_NOERR(4)  EXC_NOERR(5)  EXC_NOERR(6)  EXC_NOERR(7)
EXC_HASERR(8) EXC_NOERR(9)  EXC_HASERR(10) EXC_HASERR(11)
EXC_HASERR(12) EXC_HASERR(13) EXC_HASERR(14) EXC_NOERR(15)
EXC_NOERR(16) EXC_HASERR(17) EXC_NOERR(18) EXC_NOERR(19)
EXC_NOERR(20) EXC_HASERR(21) EXC_NOERR(22) EXC_NOERR(23)
EXC_NOERR(24) EXC_NOERR(25) EXC_NOERR(26) EXC_NOERR(27)
EXC_NOERR(28) EXC_HASERR(29) EXC_HASERR(30) EXC_NOERR(31)

static void (*const exc_stubs[32])(void) = {
    exc_stub_0,  exc_stub_1,  exc_stub_2,  exc_stub_3,
    exc_stub_4,  exc_stub_5,  exc_stub_6,  exc_stub_7,
    exc_stub_8,  exc_stub_9,  exc_stub_10, exc_stub_11,
    exc_stub_12, exc_stub_13, exc_stub_14, exc_stub_15,
    exc_stub_16, exc_stub_17, exc_stub_18, exc_stub_19,
    exc_stub_20, exc_stub_21, exc_stub_22, exc_stub_23,
    exc_stub_24, exc_stub_25, exc_stub_26, exc_stub_27,
    exc_stub_28, exc_stub_29, exc_stub_30, exc_stub_31,
};

void idt_irq_handler(uint64_t vector)
{
    IrqHandler handler = irq_handlers[vector - IRQ_BASE];

    lapic_eoi();

    bool took_lock = !bkl_held();
    if (took_lock)
        bkl_lock();

    if (handler)
        handler();

    if (took_lock)
    {
        task_check_kill();
        bkl_unlock();
    }
}

__attribute__((naked, used)) static void irq_common(void)
{
    __asm__ volatile(
        "push %rax\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %r8\n"
        "push %r9\n"
        "push %r10\n"
        "push %r11\n"

        "mov 72(%rsp), %rdi\n"
        "call idt_irq_handler\n"

        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rax\n"

        "add $8, %rsp\n"
        "iretq\n"
    );
}

#define IRQ_STUB(n) \
    __attribute__((naked)) static void irq_stub_##n(void) \
    { \
        __asm__ volatile( \
            "push $" #n "\n" \
            "jmp irq_common\n" \
        ); \
    }

IRQ_STUB(0x20)
IRQ_STUB(0x21)
IRQ_STUB(0x22)
IRQ_STUB(0x23)

__attribute__((naked)) static void spurious_stub(void)
{
    __asm__ volatile("iretq\n");
}

void idt_set_irq_handler(uint8_t vector, IrqHandler handler)
{
    irq_handlers[vector - IRQ_BASE] = handler;
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_disable(void)
{
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void idt_init(void)
{
    pic_disable();

    for (int i = 0; i < 32; i++)
        idt_set_entry(i, exc_stubs[i], IDT_GATE_INTERRUPT);

    idt_set_entry(IRQ_TIMER, irq_stub_0x20, IDT_GATE_INTERRUPT);
    idt_set_entry(IRQ_KEYBOARD, irq_stub_0x21, IDT_GATE_INTERRUPT);
    idt_set_entry(IRQ_NIC, irq_stub_0x22, IDT_GATE_INTERRUPT);
    idt_set_entry(IRQ_MOUSE, irq_stub_0x23, IDT_GATE_INTERRUPT);
    idt_set_entry(LAPIC_SPURIOUS_VECTOR, spurious_stub, IDT_GATE_INTERRUPT);

    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base = (uint64_t)&idt;

    idt_load();
}

void idt_load(void)
{
    __asm__ volatile("lidt %0" :: "m"(idt_desc) : "memory");
}