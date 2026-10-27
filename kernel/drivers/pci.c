#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_REG_VENDOR_DEVICE 0x00
#define PCI_REG_COMMAND       0x04
#define PCI_REG_CLASS         0x08
#define PCI_REG_HEADER_TYPE   0x0E
#define PCI_REG_BAR0          0x10
#define PCI_REG_INTERRUPT     0x3C
#define PCI_REG_STATUS        0x06
#define PCI_REG_CAP_PTR       0x34

#define PCI_STATUS_CAP_LIST   (1 << 4)
#define PCI_CAP_ID_PM         0x01
#define PCI_PM_CTRL           0x04
#define PCI_PM_STATE_MASK     0x3

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    return (1UL << 31) |
        ((uint32_t)bus  << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8)  |
        (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, slot, func, offset & 0xFC);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val)
{
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val)
{
    uint32_t old = pci_config_read32(bus, slot, func, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t new_val = (old & ~mask) | ((uint32_t)val << shift);
    pci_config_write32(bus, slot, func, offset & 0xFC, new_val);
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val)
{
    uint32_t old = pci_config_read32(bus, slot, func, offset & 0xFC);
    uint32_t shift = (offset & 3) * 8;
    uint32_t mask = 0xFFu << shift;
    uint32_t new_val = (old & ~mask) | ((uint32_t)val << shift);
    pci_config_write32(bus, slot, func, offset & 0xFC, new_val);
}

static int pci_probe_function(uint8_t bus, uint8_t slot, uint8_t func, PciDevice *dev)
{
    uint32_t vendor_device = pci_config_read32(bus, slot, func, PCI_REG_VENDOR_DEVICE);
    uint16_t vendor_id = (uint16_t)(vendor_device & 0xFFFF);

    if (vendor_id == 0xFFFF)
        return 0;

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = (uint16_t)(vendor_device >> 16);

    uint32_t class_reg = pci_config_read32(bus, slot, func, PCI_REG_CLASS);
    dev->prog_if    = (uint8_t)(class_reg >> 8);
    dev->subclass   = (uint8_t)(class_reg >> 16);
    dev->class_code = (uint8_t)(class_reg >> 24);

    uint32_t interrupt_reg = pci_config_read32(bus, slot, func, PCI_REG_INTERRUPT);
    dev->irq_line = (uint8_t)(interrupt_reg & 0xFF);

    for (int i = 0; i < PCI_MAX_BARS; i++)
        dev->bar[i] = pci_config_read32(bus, slot, func, PCI_REG_BAR0 + i * 4);

    return 1;
}

void pci_enumerate(PciEnumCallback cb, void *ctx)
{
    for (uint32_t bus = 0; bus < 256; bus++)
    {
        for (uint32_t slot = 0; slot < 32; slot++)
        {
            uint8_t header_type = pci_config_read8((uint8_t)bus, (uint8_t)slot, 0, PCI_REG_HEADER_TYPE);
            uint32_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint32_t func = 0; func < max_func; func++)
            {
                PciDevice dev;
                if (pci_probe_function((uint8_t)bus, (uint8_t)slot, (uint8_t)func, &dev))
                    cb(&dev, ctx);
            }
        }
    }
}

typedef struct
{
    uint16_t vendor_id;
    uint16_t device_id;
    PciDevice *out;
    int found;
}
PciFindCtx;

static void pci_find_cb(const PciDevice *dev, void *ctx_)
{
    PciFindCtx *ctx = (PciFindCtx *)ctx_;
    if (ctx->found)
        return;
    if (dev->vendor_id == ctx->vendor_id && dev->device_id == ctx->device_id)
    {
        *ctx->out = *dev;
        ctx->found = 1;
    }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, PciDevice *out)
{
    PciFindCtx ctx = { vendor_id, device_id, out, 0 };
    pci_enumerate(pci_find_cb, &ctx);
    return ctx.found;
}

#define PCI_CMD_IO_SPACE     (1 << 0)
#define PCI_CMD_MEM_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER   (1 << 2)

static void pci_set_power_d0(const PciDevice *dev)
{
    if (!(pci_config_read16(dev->bus, dev->slot, dev->func, PCI_REG_STATUS) & PCI_STATUS_CAP_LIST))
        return;

    uint8_t cap = pci_config_read8(dev->bus, dev->slot, dev->func, PCI_REG_CAP_PTR) & ~0x3;
    for (int guard = 0; cap && guard < 48; guard++)
    {
        if (pci_config_read8(dev->bus, dev->slot, dev->func, cap) == PCI_CAP_ID_PM)
        {
            uint16_t pmcsr = pci_config_read16(dev->bus, dev->slot, dev->func, cap + PCI_PM_CTRL);
            uint16_t state = pmcsr & PCI_PM_STATE_MASK;
            if (state == 0)
                return;

            kprintf("pci: %x:%x.%x waking from D%d\n", dev->bus, dev->slot, dev->func, state);
            pci_config_write16(dev->bus, dev->slot, dev->func, cap + PCI_PM_CTRL,
                               pmcsr & ~PCI_PM_STATE_MASK);
            pit_sleep_ms(10);

            for (int i = 0; i < PCI_MAX_BARS; i++)
                pci_config_write32(dev->bus, dev->slot, dev->func, PCI_REG_BAR0 + i * 4, dev->bar[i]);
            return;
        }
        cap = pci_config_read8(dev->bus, dev->slot, dev->func, cap + 1) & ~0x3;
    }
}

void pci_enable_device(const PciDevice *dev)
{
    pci_set_power_d0(dev);

    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
    pci_config_write16(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND, cmd);
}

int pci_bar_is_io(uint32_t bar)
{
    return bar & 0x1;
}

uint64_t pci_bar_addr(const PciDevice *dev, int index)
{
    uint32_t bar = dev->bar[index];

    if (pci_bar_is_io(bar))
        return bar & ~0x3u;

    uint32_t type = (bar >> 1) & 0x3;
    uint64_t addr = bar & ~0xFu;

    if (type == 0x2 && index < PCI_MAX_BARS - 1)
        addr |= ((uint64_t)dev->bar[index + 1]) << 32;

    return addr;
}