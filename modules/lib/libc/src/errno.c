#include <errno.h>

static int errno;
int *__errno_location(void)
{
    return &errno;
}