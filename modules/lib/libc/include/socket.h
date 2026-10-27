#pragma once

#include <stdint.h>
#include <types.h>

#define SOCK_NONBLOCK 0x1
#define SOCK_TCP      0x10
#define SOCK_UDP      0x20

int socket(uint32_t flags);
int bind(int fd, const char *name);
int listen(int fd);
int accept(int fd);
int connect(int fd, const char *name);
