#pragma once

#include "unistd.h"
#include <sys/stat.h>

#define O_ACCMODE 0x3
#define O_TRUNC   0x200
#define O_EXCL    0x0
#define O_APPEND  0x0
#define O_NONBLOCK 0x0
#define O_CLOEXEC 0x0
#define O_DIRECTORY 0x0
#define O_NOFOLLOW 0x0
#define O_BINARY  0x0

#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200

#define F_GETFL 3
#define F_SETFL 4
#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 1

int fcntl(int fd, int cmd, ...);
int unlinkat(int dirfd, const char *path, int flags);
int fstatat(int dirfd, const char *path, struct stat *st, int flags);