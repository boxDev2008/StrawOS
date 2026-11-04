#define BLOCK_SECTOR_SIZE   512
#define BLOCK_MAX_DEVICES   32
#define BLOCK_MAX_TRANSFER  128
#define BLOCK_NAME_MAX      16
#define BLOCK_MODEL_MAX     41

#define DEVFS_IOCTL_DISK_GET_INFO  0x1200
#define DEVFS_IOCTL_DISK_PARTITION 0x1201
#define DEVFS_IOCTL_DISK_FORMAT    0x1202
#define DEVFS_IOCTL_DISK_READ      0x1203
#define DEVFS_IOCTL_DISK_WRITE     0x1204

#define DISK_KIND_ATA  0
#define DISK_KIND_AHCI 1
#define DISK_KIND_NVME 2

typedef struct BlockDevice BlockDevice;

typedef int (*BlockIoFn)(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf, bool write);
typedef int (*BlockFlushFn)(BlockDevice *dev);

struct BlockDevice
{
    char          name[BLOCK_NAME_MAX];
    char          model[BLOCK_MODEL_MAX];
    uint8_t       kind;
    uint64_t      sectors;
    uint64_t      offset;
    BlockDevice  *parent;
    int           index;
    int           mounts;
    BlockIoFn     io;
    BlockFlushFn  flush;
    void         *driver_data;
    bool          used;
};

typedef struct
{
    uint64_t sectors;
    uint32_t sector_size;
    uint8_t  partition;
    uint8_t  kind;
    uint8_t  mounted;
    uint8_t  reserved;
    char     model[BLOCK_MODEL_MAX];
    char     pad[7];
    uint64_t start;
}
DiskInfo;

typedef struct
{
    uint64_t lba;
    uint32_t count;
    uint32_t reserved;
    void    *buf;
}
DiskIo;

void         block_init(void);
BlockDevice *block_register(const char *model, uint8_t kind, uint64_t sectors,
                            BlockIoFn io, BlockFlushFn flush, void *driver_data);
int          block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf);
int          block_write(BlockDevice *dev, uint64_t lba, uint32_t count, const void *buf);
int          block_flush(BlockDevice *dev);
BlockDevice *block_get(int i);
BlockDevice *block_from_node(VNode *node);
void         block_publish_all(void);
bool         block_wait32(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms);
void         block_copy_ata_string(char *out, const uint16_t *words, int count);
