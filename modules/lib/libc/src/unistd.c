#include <unistd.h>
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

int seek(int fd, int64_t offset, int whence)
{
    return (int)syscall3(SYS_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

int stat(const char *path, struct stat *statbuf)
{
    return (int)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)statbuf);
}

int fstat(int fd, struct stat *statbuf)
{
    return (int)syscall2(SYS_FSTAT, (uint64_t)fd, (uint64_t)statbuf);
}

int chdir(const char *path)
{
    return (int)syscall1(SYS_CHDIR, (uint64_t)path);
}

char *getcwd(char *buf, size_t size)
{
    int rc = (int)syscall2(SYS_GETCWD, (uint64_t)buf, (uint64_t)size);
    if (rc < 0) return NULL;
    return buf;
}

int spawn(const char *path, const char **argv)
{
    return (int)syscall2(SYS_SPAWN, (uint64_t)path, (uint64_t)argv);
}

int kill(int pid)
{
    return (int)syscall1(SYS_KILL, (uint64_t)(uint32_t)pid);
}

int getpid(void)
{
    return (int)syscall0(SYS_GETPID);
}

void yield(void)
{
    syscall0(SYS_YIELD);
}