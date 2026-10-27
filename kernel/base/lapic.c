#define LAPIC_BASE_PHYS 0xFEE00000ULL

#define LAPIC_REG_ID          0x020
#define LAPIC_REG_EOI         0x0B0
#define LAPIC_REG_SPURIOUS    0x0F0

#define LAPIC_SPURIOUS_ENABLE 0x100
#define LAPIC_SPURIOUS_VECTOR 0xFF

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE (1ULL << 11)

static volatile uint32_t *lapic_regs = NULL;

static inline void lapic_write(uint32_t reg, uint32_t val)
{
    lapic_regs[reg / 4] = val;
}

static inline uint32_t lapic_read(uint32_t reg)
{
    return lapic_regs[reg / 4];
}

static bool lapic_present(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (edx & (1 << 9)) != 0;
}

void lapic_init(void)
{
    if (!lapic_present())
    {
        kprintf("LAPIC: not present per CPUID, halting\n");
        panic();
    }

    lapic_regs = (volatile uint32_t *)vmm_map_mmio(LAPIC_BASE_PHYS, PAGE_SIZE);
    lapic_enable();

    kprintf("LAPIC enabled, id=%d\n", (int)lapic_id());
}

void lapic_enable(void)
{
    uint64_t base = rdmsr(IA32_APIC_BASE_MSR);
    base |= IA32_APIC_BASE_ENABLE;
    base = (base & ~0xFFFFFFFFFFFFF000ULL) | (LAPIC_BASE_PHYS & 0xFFFFFFFFFFFFF000ULL) | IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, base);

    lapic_write(LAPIC_REG_SPURIOUS, LAPIC_SPURIOUS_VECTOR | LAPIC_SPURIOUS_ENABLE);
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_REG_EOI, 0);
}

uint32_t lapic_id(void)
{
    return lapic_read(LAPIC_REG_ID) >> 24;
}

#define LAPIC_REG_LVT_TIMER    0x320
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURCNT  0x390
#define LAPIC_REG_TIMER_DIVIDE  0x3E0

#define LAPIC_TIMER_PERIODIC   (1 << 17)
#define LAPIC_TIMER_MASKED     (1 << 16)

#define LAPIC_TIMER_VECTOR     0x20

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQUENCY 1193182

static uint32_t lapic_timer_ticks_per_10ms = 0;

static void pit_sleep_ms(uint32_t ms)
{
    uint32_t count = (uint32_t)((uint64_t)PIT_FREQUENCY * ms / 1000);

    outb(PIT_COMMAND, 0x30);
    outb(PIT_CHANNEL0, count & 0xFF);
    outb(PIT_CHANNEL0, (count >> 8) & 0xFF);

    for (;;)
    {
        outb(PIT_COMMAND, 0xE2);
        uint8_t status = inb(PIT_CHANNEL0);
        if (status & 0x80)
            break;
    }
}

static void lapic_timer_calibrate(void)
{
    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_MASKED);

    lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);
    pit_sleep_ms(10);
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CURCNT);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0);

    lapic_timer_ticks_per_10ms = elapsed;
}

void lapic_timer_init(uint32_t hz)
{
    lapic_timer_calibrate();
    lapic_timer_start(hz);
}

void lapic_timer_start(uint32_t hz)
{
    uint64_t ticks_per_sec = (uint64_t)lapic_timer_ticks_per_10ms * 100;
    uint32_t count = (uint32_t)(ticks_per_sec / hz);

    if (count == 0)
        count = 1;

    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_REG_TIMER_INITCNT, count);
}