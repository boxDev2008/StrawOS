#pragma once

#include <types.h>

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2
#define AF_INET6  10
#define PF_INET   AF_INET
#define PF_INET6  AF_INET6

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
