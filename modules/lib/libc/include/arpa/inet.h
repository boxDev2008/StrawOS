#pragma once

#include "../netinet/in.h"

int         inet_aton(const char *s, struct in_addr *out);
int         inet_pton(int af, const char *s, void *out);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
in_addr_t   inet_addr(const char *s);
