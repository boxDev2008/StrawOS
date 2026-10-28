#pragma once

#include <stdint.h>
#include "../sys/socket.h"

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr
{
    in_addr_t s_addr;
};

struct in6_addr
{
    uint8_t s6_addr[16];
};

#define INADDR_ANY       ((in_addr_t)0)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#define INADDR_NONE      ((in_addr_t)0xffffffff)
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

uint32_t htonl(uint32_t x);
uint16_t htons(uint16_t x);
uint32_t ntohl(uint32_t x);
uint16_t ntohs(uint16_t x);
