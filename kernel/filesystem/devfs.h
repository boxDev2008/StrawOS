#define DEVFS_IOCTL_FB_GET_INFO 0x1000

#define DEVFS_IOCTL_FB_ACQUIRE  0x1001

typedef struct
{
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
}
FbDeviceInfo;

#define DEVFS_CPUINFO_MAX_CPUS 64
#define DEVFS_TASK_NAME_MAX    64

typedef struct
{
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t page_size;
}
MemInfo;

typedef struct
{
    uint64_t cpus;
    uint64_t total_ticks;
    uint64_t busy_ticks;
    uint64_t cpu_total_ticks[DEVFS_CPUINFO_MAX_CPUS];
    uint64_t cpu_busy_ticks[DEVFS_CPUINFO_MAX_CPUS];
}
CpuInfo;

typedef struct
{
    uint64_t pid;
    uint8_t  state;
    uint8_t  kernel;
    uint8_t  reserved[6];
    uint64_t cpu_ticks;
    uint64_t mem_kb;
    char     name[DEVFS_TASK_NAME_MAX];
}
TaskInfo;

typedef struct
{
    uint8_t up;
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
    uint8_t dns[4];
}
NetInfo;

VNode *devfs_create_root(void);