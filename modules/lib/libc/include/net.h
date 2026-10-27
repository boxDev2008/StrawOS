#pragma once

#include <stdint.h>
#include <stdbool.h>

struct net_info
{
    bool    up;
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
    uint8_t dns[4];
};

int net_info(struct net_info *out);
int net_resolve(const char *host, uint8_t ip[4]);
int net_connect(const char *host, uint16_t port);
