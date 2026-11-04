#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_COUNT    2
#define ATA_REG_LBA0     3
#define ATA_REG_LBA1     4
#define ATA_REG_LBA2     5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7

#define ATA_SR_ERR  0x01
#define ATA_SR_DRQ  0x08
#define ATA_SR_DF   0x20
#define ATA_SR_BSY  0x80

#define ATA_CMD_READ_EXT   0x24
#define ATA_CMD_WRITE_EXT  0x34
#define ATA_CMD_FLUSH_EXT  0xEA
#define ATA_CMD_IDENTIFY   0xEC

typedef struct
{
    uint16_t io;
    uint16_t ctrl;
    uint8_t  slave;
}
AtaDrive;

static AtaDrive ata_drives[4];
static int ata_drive_count;

static void ata_delay(uint16_t ctrl)
{
    for (int i = 0; i < 4; i++)
        inb(ctrl);
}

static int ata_wait(AtaDrive *d, bool drq)
{
    for (uint32_t i = 0; i < 4000000; i++)
    {
        uint8_t s = inb(d->io + ATA_REG_STATUS);
        if (s == 0xFF)
            return -1;
        if (s & ATA_SR_BSY)
            continue;
        if (s & (ATA_SR_ERR | ATA_SR_DF))
            return -1;
        if (!drq || (s & ATA_SR_DRQ))
            return 0;
    }
    return -1;
}

static void ata_select(AtaDrive *d, uint64_t lba, uint16_t count)
{
    outb(d->io + ATA_REG_DRIVE, 0x40 | (d->slave << 4));
    ata_delay(d->ctrl);
    outb(d->io + ATA_REG_COUNT, (uint8_t)(count >> 8));
    outb(d->io + ATA_REG_LBA0, (uint8_t)(lba >> 24));
    outb(d->io + ATA_REG_LBA1, (uint8_t)(lba >> 32));
    outb(d->io + ATA_REG_LBA2, (uint8_t)(lba >> 40));
    outb(d->io + ATA_REG_COUNT, (uint8_t)count);
    outb(d->io + ATA_REG_LBA0, (uint8_t)lba);
    outb(d->io + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(d->io + ATA_REG_LBA2, (uint8_t)(lba >> 16));
}

static int ata_io(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf, bool write)
{
    AtaDrive *d = dev->driver_data;
    uint16_t *p = buf;

    if (ata_wait(d, false) != 0)
        return -1;
    ata_select(d, lba, (uint16_t)count);
    outb(d->io + ATA_REG_COMMAND, write ? ATA_CMD_WRITE_EXT : ATA_CMD_READ_EXT);

    for (uint32_t s = 0; s < count; s++)
    {
        ata_delay(d->ctrl);
        if (ata_wait(d, true) != 0)
            return -1;
        for (int i = 0; i < 256; i++)
        {
            if (write)
                outw(d->io + ATA_REG_DATA, *p++);
            else
                *p++ = inw(d->io + ATA_REG_DATA);
        }
    }

    if (write)
    {
        outb(d->io + ATA_REG_COMMAND, ATA_CMD_FLUSH_EXT);
        ata_delay(d->ctrl);
        return ata_wait(d, false);
    }
    return 0;
}

static void ata_probe(uint16_t io, uint16_t ctrl, uint8_t slave)
{
    if (ata_drive_count >= (int)ARRAY_LEN(ata_drives))
        return;

    if (inb(io + ATA_REG_STATUS) == 0xFF)
        return;

    outb(ctrl, 0x02);
    outb(io + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(ctrl);
    outb(io + ATA_REG_COUNT, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(ctrl);

    if (inb(io + ATA_REG_STATUS) == 0)
        return;

    AtaDrive *d = &ata_drives[ata_drive_count];
    d->io = io;
    d->ctrl = ctrl;
    d->slave = slave;

    for (uint32_t i = 0; i < 1000000; i++)
        if (!(inb(io + ATA_REG_STATUS) & ATA_SR_BSY))
            break;
    if (inb(io + ATA_REG_LBA1) != 0 || inb(io + ATA_REG_LBA2) != 0)
        return;
    if (ata_wait(d, true) != 0)
        return;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(io + ATA_REG_DATA);

    if (!(id[83] & (1 << 10)))
        return;

    uint64_t sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                       ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (sectors == 0)
        return;

    char model[BLOCK_MODEL_MAX];
    block_copy_ata_string(model, id + 27, 20);

    ata_drive_count++;
    block_register(model, DISK_KIND_ATA, sectors, ata_io, NULL, d);
}

static void ata_pci_cb(const PciDevice *dev, void *ctx)
{
    if (dev->class_code != 0x01 || dev->subclass != 0x01)
        return;
    int *found = ctx;
    if (*found)
        return;
    *found = 1;

    pci_enable_device(dev);

    uint16_t io0 = 0x1F0, ctrl0 = 0x3F6, io1 = 0x170, ctrl1 = 0x376;
    if ((dev->prog_if & 0x01) && pci_bar_is_io(dev->bar[0]) && pci_bar_is_io(dev->bar[1]))
    {
        io0 = (uint16_t)pci_bar_addr(dev, 0);
        ctrl0 = (uint16_t)pci_bar_addr(dev, 1) + 2;
    }
    if ((dev->prog_if & 0x04) && pci_bar_is_io(dev->bar[2]) && pci_bar_is_io(dev->bar[3]))
    {
        io1 = (uint16_t)pci_bar_addr(dev, 2);
        ctrl1 = (uint16_t)pci_bar_addr(dev, 3) + 2;
    }

    ata_probe(io0, ctrl0, 0);
    ata_probe(io0, ctrl0, 1);
    ata_probe(io1, ctrl1, 0);
    ata_probe(io1, ctrl1, 1);
}

int ata_init(void)
{
    int found = 0;
    pci_enumerate(ata_pci_cb, &found);
    return ata_drive_count;
}
