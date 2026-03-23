#pragma once

#include <stddef.h>
#include <stdint.h>

#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_SEEK    7
#define SYS_STAT    8
#define SYS_FSTAT   9
#define SYS_MKDIR   10
#define SYS_REMOVE  11
#define SYS_RENAME  12
#define SYS_CHDIR   13
#define SYS_GETCWD  14
#define SYS_READDIR 15

#define SYS_SPAWN   18
#define SYS_KILL    19
#define SYS_GETPID  20
#define SYS_WAITPID 21

#define SYS_MMAP    41
#define SYS_MUNMAP  42

#define SYS_DEVICE  50
#define SYS_TIME    51

#define SYS_SOCKET    60
#define SYS_BIND      61
#define SYS_CONNECT   62
#define SYS_SEND      63
#define SYS_RECV      64
#define SYS_SOCKCLOSE 65

uint64_t syscall0(uint64_t nr);
uint64_t syscall1(uint64_t nr, uint64_t a0);
uint64_t syscall2(uint64_t nr, uint64_t a0, uint64_t a1);
uint64_t syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2);