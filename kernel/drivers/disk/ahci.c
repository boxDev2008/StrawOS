#define AHCI_MAX_PORTS 32

#define AHCI_HBA_CAP   0x00
#define AHCI_HBA_GHC   0x04
#define AHCI_HBA_PI    0x0C
#define AHCI_HBA_CAP2  0x24
#define AHCI_HBA_BOHC  0x28

#define AHCI_GHC_AE    (1u << 31)

#define AHCI_PORT(n)   (0x100 + (n) * 0x80)
#define AHCI_PX_CLB    0x00
#define AHCI_PX_CLBU   0x04
#define AHCI_PX_FB     0x08
#define AHCI_PX_FBU    0x0C
#define AHCI_PX_IS     0x10
#define AHCI_PX_IE     0x14
#define AHCI_PX_CMD    0x18
#define AHCI_PX_TFD    0x20
#define AHCI_PX_SIG    0x24
#define AHCI_PX_SSTS   0x28
#define AHCI_PX_SERR   0x30
#define AHCI_PX_CI     0x38

#define AHCI_CMD_ST    (1u << 0)
#define AHCI_CMD_FRE   (1u << 4)
#define AHCI_CMD_FR    (1u << 14)
#define AHCI_CMD_CR    (1u << 15)

#define AHCI_IS_TFES   (1u << 30)
#define AHCI_TFD_BSY   0x80
#define AHCI_TFD_DRQ   0x08
#define AHCI_TFD_ERR   0x01

#define AHCI_SIG_ATA   0x00000101

#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_FLUSH_CACHE   0xEA
#define ATA_CMD_IDENTIFY_DEV  0xEC

typedef struct PACKED
{
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
}
AhciCmdHeader;

typedef struct PACKED
{
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
}
AhciPrd;

typedef struct PACKED
{
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    AhciPrd prd[1];
}
AhciCmdTable;

typedef struct
{
    volatile uint8_t *port;
    AhciCmdHeader    *headers;
    AhciCmdTable     *table;
    uint8_t          *scratch;
}
AhciPort;

static AhciPort ahci_ports[AHCI_MAX_PORTS * 2];
static int ahci_port_count;

static inline uint32_t ahci_read(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void ahci_write(volatile uint8_t *base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static bool ahci_port_stop(volatile uint8_t *port)
{
    ahci_write(port, AHCI_PX_CMD, ahci_read(port, AHCI_PX_CMD) & ~AHCI_CMD_ST);
    if (!block_wait32((volatile uint32_t *)(port + AHCI_PX_CMD), AHCI_CMD_CR, 0, 500))
        return false;
    ahci_write(port, AHCI_PX_CMD, ahci_read(port, AHCI_PX_CMD) & ~AHCI_CMD_FRE);
    return block_wait32((volatile uint32_t *)(port + AHCI_PX_CMD), AHCI_CMD_FR, 0, 500);
}

static int ahci_command(AhciPort *p, uint8_t command, uint64_t lba, uint32_t count, void *buf, uint32_t bytes, bool write)
{
    volatile uint8_t *port = p->port;

    if (!block_wait32((volatile uint32_t *)(port + AHCI_PX_TFD), AHCI_TFD_BSY | AHCI_TFD_DRQ, 0, 1000))
        return -1;

    ahci_write(port, AHCI_PX_IS, 0xFFFFFFFF);

    AhciCmdHeader *h = &p->headers[0];
    h->flags = 5 | (write ? (1 << 6) : 0);
    h->prdtl = bytes ? 1 : 0;
    h->prdbc = 0;

    AhciCmdTable *t = p->table;
    memset(t, 0, sizeof(AhciCmdTable));
    if (bytes)
    {
        uint64_t phys = virt_to_phys(buf);
        t->prd[0].dba = (uint32_t)phys;
        t->prd[0].dbau = (uint32_t)(phys >> 32);
        t->prd[0].dbc = bytes - 1;
    }

    uint8_t *fis = t->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = command;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    __asm__ volatile("mfence" ::: "memory");
    ahci_write(port, AHCI_PX_CI, 1);

    for (uint64_t i = 0; i < 10000000; i++)
    {
        if (ahci_read(port, AHCI_PX_IS) & AHCI_IS_TFES)
            return -1;
        if (!(ahci_read(port, AHCI_PX_CI) & 1))
            return (ahci_read(port, AHCI_PX_TFD) & AHCI_TFD_ERR) ? -1 : 0;
        inb(0x80);
    }
    return -1;
}

static int ahci_io(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf, bool write)
{
    return ahci_command(dev->driver_data, write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT,
                        lba, count, buf, count * BLOCK_SECTOR_SIZE, write);
}

static int ahci_flush(BlockDevice *dev)
{
    return ahci_command(dev->driver_data, ATA_CMD_FLUSH_CACHE, 0, 0, NULL, 0, false);
}

static void ahci_init_port(volatile uint8_t *hba, int n)
{
    volatile uint8_t *port = hba + AHCI_PORT(n);

    uint32_t ssts = ahci_read(port, AHCI_PX_SSTS);
    if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1)
        return;
    if (ahci_read(port, AHCI_PX_SIG) != AHCI_SIG_ATA)
        return;
    if (ahci_port_count >= (int)ARRAY_LEN(ahci_ports))
        return;
    if (!ahci_port_stop(port))
    {
        kprintf("ahci: port %d did not stop\n", n);
        return;
    }

    uint8_t *mem = (uint8_t *)pmm_alloc_pages(2);
    if (!mem)
        return;
    memset(mem, 0, 2 * PAGE_SIZE);

    AhciPort *p = &ahci_ports[ahci_port_count];
    p->port = port;
    p->headers = (AhciCmdHeader *)mem;
    p->table = (AhciCmdTable *)(mem + 2048);
    p->scratch = mem + PAGE_SIZE;

    uint64_t clb = virt_to_phys(mem);
    uint64_t fb = clb + 1024;
    uint64_t ctba = clb + 2048;
    p->headers[0].ctba = (uint32_t)ctba;
    p->headers[0].ctbau = (uint32_t)(ctba >> 32);

    ahci_write(port, AHCI_PX_CLB, (uint32_t)clb);
    ahci_write(port, AHCI_PX_CLBU, (uint32_t)(clb >> 32));
    ahci_write(port, AHCI_PX_FB, (uint32_t)fb);
    ahci_write(port, AHCI_PX_FBU, (uint32_t)(fb >> 32));
    ahci_write(port, AHCI_PX_SERR, 0xFFFFFFFF);
    ahci_write(port, AHCI_PX_IS, 0xFFFFFFFF);
    ahci_write(port, AHCI_PX_IE, 0);

    ahci_write(port, AHCI_PX_CMD, ahci_read(port, AHCI_PX_CMD) | AHCI_CMD_FRE);
    ahci_write(port, AHCI_PX_CMD, ahci_read(port, AHCI_PX_CMD) | AHCI_CMD_ST);

    if (ahci_command(p, ATA_CMD_IDENTIFY_DEV, 0, 0, p->scratch, 512, false) != 0)
    {
        kprintf("ahci: identify failed on port %d\n", n);
        return;
    }

    uint16_t *id = (uint16_t *)p->scratch;
    uint64_t sectors;
    if (id[83] & (1 << 10))
        sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                  ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    else
        sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    if (sectors == 0)
        return;

    char model[BLOCK_MODEL_MAX];
    block_copy_ata_string(model, id + 27, 20);

    ahci_port_count++;
    block_register(model, DISK_KIND_AHCI, sectors, ahci_io, ahci_flush, p);
}

static void ahci_init_controller(const PciDevice *dev)
{
    uint64_t abar = pci_bar_addr(dev, 5);
    if (!abar || pci_bar_is_io(dev->bar[5]))
        return;

    pci_enable_device(dev);
    volatile uint8_t *hba = (volatile uint8_t *)vmm_map_mmio(abar, 0x2000);

    if (ahci_read(hba, AHCI_HBA_CAP2) & 1)
    {
        ahci_write(hba, AHCI_HBA_BOHC, ahci_read(hba, AHCI_HBA_BOHC) | (1u << 1));
        block_wait32((volatile uint32_t *)(hba + AHCI_HBA_BOHC), 1u, 0, 100);
    }

    ahci_write(hba, AHCI_HBA_GHC, ahci_read(hba, AHCI_HBA_GHC) | AHCI_GHC_AE);
    ahci_write(hba, AHCI_HBA_GHC, ahci_read(hba, AHCI_HBA_GHC) & ~2u);

    if (!(ahci_read(hba, AHCI_HBA_CAP) & (1u << 31)) && virt_to_phys(block_bounce) >> 32)
    {
        kprintf("ahci: controller lacks 64-bit DMA\n");
        return;
    }

    uint32_t pi = ahci_read(hba, AHCI_HBA_PI);
    for (int i = 0; i < AHCI_MAX_PORTS; i++)
        if (pi & (1u << i))
            ahci_init_port(hba, i);
}

static void ahci_pci_cb(const PciDevice *dev, void *ctx)
{
    (void)ctx;
    if (dev->class_code == 0x01 && dev->subclass == 0x06 && dev->prog_if == 0x01)
        ahci_init_controller(dev);
}

int ahci_init(void)
{
    pci_enumerate(ahci_pci_cb, NULL);
    return ahci_port_count;
}
