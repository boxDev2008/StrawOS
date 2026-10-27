#pragma once

#include "../unistd.h"

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int     access(const char *path, int mode);
int     rmdir(const char *path);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int     isatty(int fd);
unsigned int sleep(unsigned int seconds);
int     usleep(useconds_t usec);
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
int     gethostname(char *name, size_t len);
