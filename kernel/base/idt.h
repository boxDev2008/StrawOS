#define IDT_ENTRIES 256

#define IRQ_BASE 0x20

#define IRQ_TIMER IRQ_BASE 
#define IRQ_KEYBOARD (IRQ_BASE + 1)
#define IRQ_NIC (IRQ_BASE + 2)
#define IRQ_MOUSE (IRQ_BASE + 3)

typedef void (*IrqHandler)(void);

void idt_init(void);
void idt_load(void);
void idt_set_irq_handler(uint8_t vector, IrqHandler handler);
