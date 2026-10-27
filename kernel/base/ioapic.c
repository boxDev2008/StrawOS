#define IOAPIC_BASE_PHYS 0xFEC00000ULL

#define IOAPIC_REG_SELECT 0x00
#define IOAPIC_REG_WINDOW 0x10

#define IOAPIC_REDTBL_BASE 0x10

static volatile uint32_t *ioapic_regs = NULL;

static void ioapic_ensure_mapped(void)
{
    if (ioapic_regs == NULL)
        ioapic_regs = (volatile uint32_t *)vmm_map_mmio(IOAPIC_BASE_PHYS, PAGE_SIZE);
}

static void ioapic_write(uint8_t reg, uint32_t val)
{
    ioapic_ensure_mapped();
    ioapic_regs[IOAPIC_REG_SELECT / 4] = reg;
    ioapic_regs[IOAPIC_REG_WINDOW / 4] = val;
}

void ioapic_set_irq(uint8_t irq, uint32_t lapic_id, uint8_t vector)
{
    uint8_t reg = IOAPIC_REDTBL_BASE + irq * 2;

    uint32_t low = vector;
    uint32_t high = lapic_id << 24;

    ioapic_write(reg + 1, high);
    ioapic_write(reg, low);
}