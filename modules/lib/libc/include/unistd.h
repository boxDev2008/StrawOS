#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stat.h>

#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_ACCMODE   0x003
#define O_CREAT     0x040
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_DIRECTORY 0x10000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char *path, int flags);
int close(int fd);
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
int seek(int fd, int64_t offset, int whence);
int stat(const char *path, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);

int    chdir(const char *path);
char  *getcwd(char *buf, size_t size);
int    spawn(const char *path);
int    kill(int pid);
int    getpid(void);
void   yield(void);