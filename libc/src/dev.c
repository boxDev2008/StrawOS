#include <dev.h>

int device(int device_id, void *data)
{
    return (int)syscall2(SYS_DEVICE, (uint64_t)device_id, (uint64_t)data);
}