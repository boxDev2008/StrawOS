#pragma once

#include <stddef.h>

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

int open(const char *path, int flags);
int close(int fd);
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);

