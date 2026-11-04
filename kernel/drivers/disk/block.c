#define BLOCK_MAX_PARTITIONS 8
#define BLOCK_BOUNCE_PAGES   (BLOCK_MAX_TRANSFER * BLOCK_SECTOR_SIZE / PAGE_SIZE)

#define GPT_ENTRIES      128
#define GPT_ENTRY_SIZE   128
#define GPT_ENTRY_SECTORS (GPT_ENTRIES * GPT_ENTRY_SIZE / BLOCK_SECTOR_SIZE)
#define GPT_ALIGN        2048

typedef struct PACKED
{
    char     signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint8_t  disk_guid[16];
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc;
}
GptHeader;

typedef struct PACKED
{
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
}
GptEntry;

static const uint8_t gpt_bios_boot_guid[16] = {
    'H', 'a', 'h', '!', 'I', 'd', 'o', 'n', 't', 'N', 'e', 'e', 'd', 'E', 'F', 'I'
};

static const uint8_t gpt_esp_guid[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static BlockDevice block_devices[BLOCK_MAX_DEVICES];
static uint8_t *block_bounce;
static int block_disk_count;
static VFileOps block_file_ops;

static uint32_t block_crc32(const void *data, size_t len, uint32_t crc)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint64_t block_random(void)
{
    static uint64_t state;
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    state ^= ((uint64_t)hi << 32 | lo) + 0x9E3779B97F4A7C15ULL;
    state ^= state >> 30;
    state *= 0xBF58476D1CE4E5B9ULL;
    state ^= state >> 27;
    state *= 0x94D049BB133111EBULL;
    state ^= state >> 31;
    return state;
}

static void block_random_guid(uint8_t *guid)
{
    uint64_t a = block_random(), b = block_random();
    memcpy(guid, &a, 8);
    memcpy(guid + 8, &b, 8);
    guid[7] = (guid[7] & 0x0F) | 0x40;
    guid[8] = (guid[8] & 0x3F) | 0x80;
}

bool block_wait32(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms)
{
    for (uint64_t i = 0; i < (uint64_t)timeout_ms * 1000; i++)
    {
        if ((*reg & mask) == value)
            return true;
        inb(0x80);
    }
    return (*reg & mask) == value;
}

void block_copy_ata_string(char *out, const uint16_t *words, int count)
{
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        out[n++] = (char)(words[i] >> 8);
        out[n++] = (char)(words[i] & 0xFF);
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0'))
        n--;
    out[n] = '\0';
}

void block_init(void)
{
    block_bounce = (uint8_t *)pmm_alloc_pages(BLOCK_BOUNCE_PAGES);
    if (!block_bounce)
        kprintf("block: failed to allocate the transfer buffer\n");
}

static BlockDevice *block_alloc(void)
{
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
    {
        if (!block_devices[i].used)
        {
            memset(&block_devices[i], 0, sizeof(BlockDevice));
            block_devices[i].used = true;
            return &block_devices[i];
        }
    }
    return NULL;
}

BlockDevice *block_get(int i)
{
    if (i < 0 || i >= BLOCK_MAX_DEVICES || !block_devices[i].used)
        return NULL;
    return &block_devices[i];
}

static int block_root_io(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf, bool write)
{
    if (!dev || !block_bounce || count == 0)
        return -1;
    if (lba + count > dev->sectors || lba + count < lba)
        return -1;

    while (dev->parent)
    {
        lba += dev->offset;
        dev = dev->parent;
    }

    uint8_t *p = buf;
    while (count)
    {
        uint32_t n = MIN(count, BLOCK_MAX_TRANSFER);
        size_t bytes = (size_t)n * BLOCK_SECTOR_SIZE;
        if (write)
            memcpy(block_bounce, p, bytes);
        if (dev->io(dev, lba, n, block_bounce, write) != 0)
        {
            kprintf("block: %s %s error at lba %llu\n", dev->name, write ? "write" : "read",
                    (unsigned long long)lba);
            return -1;
        }
        if (!write)
            memcpy(p, block_bounce, bytes);
        p += bytes;
        lba += n;
        count -= n;
    }
    return 0;
}

int block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf)
{
    return block_root_io(dev, lba, count, buf, false);
}

int block_write(BlockDevice *dev, uint64_t lba, uint32_t count, const void *buf)
{
    return block_root_io(dev, lba, count, (void *)buf, true);
}

int block_flush(BlockDevice *dev)
{
    while (dev && dev->parent)
        dev = dev->parent;
    if (!dev || !dev->flush)
        return 0;
    return dev->flush(dev);
}

static BlockDevice *block_partition_slot(BlockDevice *disk, int index)
{
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
        if (block_devices[i].used && block_devices[i].parent == disk && block_devices[i].index == index)
            return &block_devices[i];

    BlockDevice *p = block_alloc();
    if (!p)
        return NULL;
    p->parent = disk;
    p->index = index;
    p->kind = disk->kind;
    memcpy(p->model, disk->model, BLOCK_MODEL_MAX);
    size_t len = strlen(disk->name);
    memcpy(p->name, disk->name, len);
    p->name[len] = 'p';
    p->name[len + 1] = (char)('0' + index);
    p->name[len + 2] = '\0';
    return p;
}

static void block_add_partition(BlockDevice *disk, int index, uint64_t start, uint64_t sectors)
{
    if (start >= disk->sectors || sectors == 0)
        return;
    if (start + sectors > disk->sectors)
        sectors = disk->sectors - start;

    BlockDevice *p = block_partition_slot(disk, index);
    if (!p)
        return;
    p->offset = start;
    p->sectors = sectors;
}

static int block_scan_gpt(BlockDevice *disk, uint8_t *sector)
{
    GptHeader hdr;
    memcpy(&hdr, sector, sizeof(hdr));
    if (memcmp(hdr.signature, "EFI PART", 8) != 0 || hdr.entry_size < sizeof(GptEntry))
        return -1;

    int found = 0;
    uint32_t per_sector = BLOCK_SECTOR_SIZE / hdr.entry_size;
    uint32_t total = MIN(hdr.entry_count, GPT_ENTRIES);

    for (uint32_t i = 0; i < total && found < BLOCK_MAX_PARTITIONS; i++)
    {
        if (i % per_sector == 0 &&
            block_read(disk, hdr.entries_lba + i / per_sector, 1, sector) != 0)
            break;

        GptEntry e;
        memcpy(&e, sector + (i % per_sector) * hdr.entry_size, sizeof(e));

        static const uint8_t zero[16];
        if (memcmp(e.type_guid, zero, 16) == 0 || e.last_lba < e.first_lba)
            continue;

        block_add_partition(disk, ++found, e.first_lba, e.last_lba - e.first_lba + 1);
    }
    return found;
}

static int block_scan_mbr(BlockDevice *disk, uint8_t *sector)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    int found = 0;
    for (int i = 0; i < 4; i++)
    {
        const uint8_t *e = sector + 446 + i * 16;
        uint8_t type = e[4];
        uint32_t start, count;
        memcpy(&start, e + 8, 4);
        memcpy(&count, e + 12, 4);
        if (type == 0 || type == 0xEE || count == 0)
            continue;
        block_add_partition(disk, ++found, start, count);
    }
    return found;
}

static void block_scan_partitions(BlockDevice *disk)
{
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
        if (block_devices[i].used && block_devices[i].parent == disk)
            block_devices[i].sectors = 0;

    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (block_read(disk, 1, 1, sector) == 0 && block_scan_gpt(disk, sector) >= 0)
        return;
    if (block_read(disk, 0, 1, sector) == 0)
        block_scan_mbr(disk, sector);
}

BlockDevice *block_register(const char *model, uint8_t kind, uint64_t sectors,
                            BlockIoFn io, BlockFlushFn flush, void *driver_data)
{
    BlockDevice *dev = block_alloc();
    if (!dev)
        return NULL;

    int n = block_disk_count++;
    memcpy(dev->name, "disk", 4);
    if (n >= 10)
    {
        dev->name[4] = (char)('0' + n / 10);
        dev->name[5] = (char)('0' + n % 10);
    }
    else
    {
        dev->name[4] = (char)('0' + n);
    }

    size_t len = MIN(strlen(model), (size_t)BLOCK_MODEL_MAX - 1);
    memcpy(dev->model, model, len);
    dev->model[len] = '\0';
    dev->kind = kind;
    dev->sectors = sectors;
    dev->io = io;
    dev->flush = flush;
    dev->driver_data = driver_data;

    kprintf("block: %s \"%s\" %llu MiB\n", dev->name, dev->model,
            (unsigned long long)(sectors / 2048));

    block_scan_partitions(dev);
    return dev;
}

static bool block_busy(BlockDevice *dev)
{
    if (dev->mounts)
        return true;
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
        if (block_devices[i].used && block_devices[i].parent == dev && block_devices[i].mounts)
            return true;
    return false;
}

static void gpt_write_header(GptHeader *hdr, uint8_t *sector, uint64_t my_lba, uint64_t alt_lba, uint64_t entries_lba)
{
    hdr->my_lba = my_lba;
    hdr->alternate_lba = alt_lba;
    hdr->entries_lba = entries_lba;
    hdr->header_crc = 0;
    hdr->header_crc = block_crc32(hdr, sizeof(GptHeader), 0);
    memset(sector, 0, BLOCK_SECTOR_SIZE);
    memcpy(sector, hdr, sizeof(GptHeader));
}

static int block_write_gpt(BlockDevice *disk)
{
    uint64_t last = disk->sectors - 1;
    uint64_t first_usable = 2 + GPT_ENTRY_SECTORS;
    uint64_t last_usable = last - 1 - GPT_ENTRY_SECTORS;
    uint64_t part_start = GPT_ALIGN;
    uint64_t part_end = ALIGN_DOWN(last_usable + 1, GPT_ALIGN) - 1;
    if (part_end <= part_start)
        return -1;

    uint8_t *entries = kcalloc(GPT_ENTRY_SECTORS, BLOCK_SECTOR_SIZE);
    if (!entries)
        return -1;

    GptEntry *e = (GptEntry *)entries;
    memcpy(e->type_guid, gpt_esp_guid, 16);
    block_random_guid(e->unique_guid);
    e->first_lba = part_start;
    e->last_lba = part_end;
    const char *label = "StrawOS";
    for (int i = 0; label[i]; i++)
        e->name[i] = (uint16_t)label[i];

    GptEntry *bios = e + 1;
    memcpy(bios->type_guid, gpt_bios_boot_guid, 16);
    block_random_guid(bios->unique_guid);
    bios->first_lba = first_usable;
    bios->last_lba = part_start - 1;
    const char *bios_label = "BIOS boot";
    for (int i = 0; bios_label[i]; i++)
        bios->name[i] = (uint16_t)bios_label[i];

    GptHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000;
    hdr.header_size = sizeof(GptHeader);
    hdr.first_usable = first_usable;
    hdr.last_usable = last_usable;
    block_random_guid(hdr.disk_guid);
    hdr.entry_count = GPT_ENTRIES;
    hdr.entry_size = GPT_ENTRY_SIZE;
    hdr.entries_crc = block_crc32(entries, GPT_ENTRY_SECTORS * BLOCK_SECTOR_SIZE, 0);

    uint8_t sector[BLOCK_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    uint8_t *pe = sector + 446;
    pe[1] = 0x00;
    pe[2] = 0x02;
    pe[4] = 0xEE;
    pe[5] = pe[6] = pe[7] = 0xFF;
    uint32_t mbr_start = 1;
    uint32_t mbr_count = last > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)last;
    memcpy(pe + 8, &mbr_start, 4);
    memcpy(pe + 12, &mbr_count, 4);
    sector[510] = 0x55;
    sector[511] = 0xAA;

    int rc = -1;
    if (block_write(disk, 0, 1, sector) != 0)
        goto out;
    if (block_write(disk, 2, GPT_ENTRY_SECTORS, entries) != 0)
        goto out;
    if (block_write(disk, last - GPT_ENTRY_SECTORS, GPT_ENTRY_SECTORS, entries) != 0)
        goto out;
    gpt_write_header(&hdr, sector, last, 1, last - GPT_ENTRY_SECTORS);
    if (block_write(disk, last, 1, sector) != 0)
        goto out;
    gpt_write_header(&hdr, sector, 1, last, 2);
    if (block_write(disk, 1, 1, sector) != 0)
        goto out;

    memset(sector, 0, sizeof(sector));
    block_write(disk, part_start, 1, sector);
    rc = block_flush(disk);

out:
    kfree(entries);
    return rc;
}

static int block_ioctl(VFile *file, uint64_t request, void *arg)
{
    BlockDevice *dev = file->node->private_data;
    if (!dev || !dev->used)
        return -1;

    switch (request)
    {
        case DEVFS_IOCTL_DISK_GET_INFO:
        {
            if (!arg)
                return -1;
            DiskInfo info;
            memset(&info, 0, sizeof(info));
            info.sectors = dev->sectors;
            info.sector_size = BLOCK_SECTOR_SIZE;
            info.partition = dev->parent != NULL;
            info.kind = dev->kind;
            info.mounted = block_busy(dev);
            info.start = dev->offset;
            memcpy(info.model, dev->model, BLOCK_MODEL_MAX);
            memcpy(arg, &info, sizeof(info));
            return 0;
        }
        case DEVFS_IOCTL_DISK_PARTITION:
        {
            if (dev->parent || block_busy(dev))
                return -1;
            if (block_write_gpt(dev) != 0)
                return -1;
            block_scan_partitions(dev);
            block_publish_all();
            return 0;
        }
        case DEVFS_IOCTL_DISK_READ:
        case DEVFS_IOCTL_DISK_WRITE:
        {
            DiskIo *io = arg;
            if (!io || !io->buf || io->count == 0)
                return -1;
            if (request == DEVFS_IOCTL_DISK_READ)
                return block_read(dev, io->lba, io->count, io->buf);
            if (block_busy(dev))
                return -1;
            if (block_write(dev, io->lba, io->count, io->buf) != 0)
                return -1;
            return block_flush(dev);
        }
        case DEVFS_IOCTL_DISK_FORMAT:
        {
            if (dev->sectors == 0 || block_busy(dev))
                return -1;
            return fat32_format(dev, "STRAWOS");
        }
        default:
            return -1;
    }
}

static int block_poll(VFile *file)
{
    (void)file;
    return POLLIN | POLLOUT;
}

BlockDevice *block_from_node(VNode *node)
{
    if (!node || node->type != VNODE_DEVICE || node->file_ops != &block_file_ops)
        return NULL;
    BlockDevice *dev = node->private_data;
    return dev && dev->used && dev->sectors ? dev : NULL;
}

void block_publish_all(void)
{
    block_file_ops.ioctl = block_ioctl;
    block_file_ops.poll = block_poll;

    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
    {
        BlockDevice *dev = &block_devices[i];
        if (!dev->used || dev->sectors == 0)
            continue;

        char path[32] = "/dev/";
        strcat(path, dev->name);
        if (!vfs_resolve(path))
            devfs_register(dev->name, &block_file_ops, dev);
    }
}
