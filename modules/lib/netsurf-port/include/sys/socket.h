#pragma once

#include "../../../libc/include/sys/socket.h"

#define socket ns_posix_socket

int ns_posix_socket(int domain, int type, int protocol);
