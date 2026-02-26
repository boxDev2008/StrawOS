#include <stdio.h>
#include <syscall.h>

int open(const char *path, int flags)
{
    return (int)syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

int write(int fd, const void *buf, size_t count)
{
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

int read(int fd, void *buf, size_t count)
{
    return (int)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}