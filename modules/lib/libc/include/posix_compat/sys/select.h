#pragma once

#include <sys/time.h>

#define FD_SETSIZE 64

typedef struct
{
    uint64_t bits;
}
fd_set;

#define FD_ZERO(s)     ((s)->bits = 0)
#define FD_SET(fd, s)  ((s)->bits |= (1ULL << (fd)))
#define FD_CLR(fd, s)  ((s)->bits &= ~(1ULL << (fd)))
#define FD_ISSET(fd, s) (((s)->bits >> (fd)) & 1)

int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout);
