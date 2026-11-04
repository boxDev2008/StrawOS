#define PCI_MAX_BARS 6

typedef struct
{
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;

    uint8_t  irq_line;

    uint32_t bar[PCI_MAX_BARS];
}
PciDevice;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);

typedef void (*PciEnumCallback)(const PciDevice *dev, void *ctx);
void pci_enumerate(PciEnumCallback cb, void *ctx);

int pci_find_device(uint16_t vendor_id, uint16_t device_id, PciDevice *out);

void pci_enable_device(const PciDevice *dev);

int      pci_bar_is_io(uint32_t bar);
uint64_t pci_bar_addr(const PciDevice *dev, int index);
