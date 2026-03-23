#pragma once

#include <stddef.h>
#include <stdint.h>

#define SOCK_UDP  0
#define SOCK_TCP  1

int socket_create(int type);
int socket_connect(int fd, uint32_t ip, uint16_t port);
int socket_netsend(int fd, const void *buf, uint32_t len);
int socket_netrecv(int fd, void *buf, uint32_t len);
int socket_bind(int fd, uint16_t port);
int socket_close(int fd);