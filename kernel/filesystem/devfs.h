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
    int8_t   nice;
    uint8_t  reserved[5];
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

#define DEVFS_IOCTL_AUDIO_GET_INFO   0x1100
#define DEVFS_IOCTL_AUDIO_GET_STATE  0x1101
#define DEVFS_IOCTL_AUDIO_SET_VOLUME 0x1102
#define DEVFS_IOCTL_AUDIO_GET_VOLUME 0x1103
#define DEVFS_IOCTL_AUDIO_DROP       0x1104
#define DEVFS_IOCTL_AUDIO_LIST_DEVICES      0x1105
#define DEVFS_IOCTL_AUDIO_SET_OUTPUT_DEVICE 0x1106
#define DEVFS_IOCTL_AUDIO_SET_INPUT_DEVICE  0x1107
#define DEVFS_IOCTL_AUDIO_SET_INPUT_VOLUME  0x1108
#define DEVFS_IOCTL_AUDIO_GET_INPUT_VOLUME  0x1109

#define AUDIO_MAX_DEVICES 16

typedef struct
{
    uint32_t id;
    uint8_t  input;
    uint8_t  selected;
    uint8_t  type;
    uint8_t  reserved;
    char     name[48];
    char     key[40];
}
AudioDevice;

typedef struct
{
    uint32_t    count;
    uint32_t    reserved;
    AudioDevice devices[AUDIO_MAX_DEVICES];
}
AudioDeviceList;

typedef struct
{
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint8_t  playback;
    uint8_t  capture;
    uint8_t  reserved[2];
    uint32_t period_bytes;
    uint32_t queue_bytes;
    uint32_t latency_bytes;
}
AudioInfo;

typedef struct
{
    uint32_t play_queued;
    uint32_t play_free;
    uint32_t capture_avail;
    uint32_t reserved;
}
AudioState;

VNode *devfs_create_root(void);
VNode *devfs_register(const char *name, VFileOps *file_ops, void *private_data);