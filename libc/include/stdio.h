#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <syscall.h>

#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_ACCMODE   0x003
#define O_CREAT     0x040
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_DIRECTORY 0x10000

#define S_TYPE_FILE 1
#define S_TYPE_DIR 2
#define S_TYPE_SYMLINK 3

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef enum
{
    STAT_FILE = 1,
    STAT_DIR  = 2
}
stat_type_t;

typedef struct stat
{
    uint64_t    st_ino;
    stat_type_t st_type;
    uint64_t    st_size;
}
stat_t;

typedef struct dirent
{
    stat_type_t d_type;
    char        d_name[256];
}
dirent_t;

inline int open(const char *path, int flags)
{
    return (int)syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

inline int close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

inline int write(int fd, const void *buf, size_t count)
{
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

inline int read(int fd, void *buf, size_t count)
{
    return (int)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

inline int seek(int fd, int64_t offset, int whence)
{
    return (int)syscall3(SYS_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

inline int stat(const char *path, stat_t *statbuf)
{
    return (int)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)statbuf);
}

inline int fstat(int fd, stat_t *statbuf)
{
    return (int)syscall2(SYS_FSTAT, (uint64_t)fd, (uint64_t)statbuf);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);