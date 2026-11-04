#pragma once

#include <stdint.h>

#define DISK_GET_INFO  0x1200
#define DISK_PARTITION 0x1201
#define DISK_FORMAT    0x1202
#define DISK_READ      0x1203
#define DISK_WRITE     0x1204

#define DISK_KIND_ATA  0
#define DISK_KIND_AHCI 1
#define DISK_KIND_NVME 2

struct disk_info
{
    uint64_t sectors;
    uint32_t sector_size;
    uint8_t  partition;
    uint8_t  kind;
    uint8_t  mounted;
    uint8_t  reserved;
    char     model[41];
    char     pad[7];
    uint64_t start;
};

struct disk_io
{
    uint64_t lba;
    uint32_t count;
    uint32_t reserved;
    void    *buf;
};

int mount(const char *device, const char *path);
int umount(const char *path);
