#pragma once

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_SEEK    7
#define SYS_STAT    8
#define SYS_FSTAT   9

#define SYS_GETPID  20
#define SYS_YIELD   30

#define SYS_MMAP    41
#define SYS_MUNMAP  42

#define SYS_DEVICE  50