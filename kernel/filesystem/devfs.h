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

VNode *devfs_create_root(void);