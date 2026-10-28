#define KCALL_TABLE_ADDR 0xffffffff80000000ULL

#define KCALL_EXIT 1

#define KCALL_OPEN   2
#define KCALL_CLOSE  3
#define KCALL_READ   4
#define KCALL_WRITE  5
#define KCALL_IOCTL  6
#define KCALL_MKDIR  7
#define KCALL_READDIR 8
#define KCALL_LSEEK   9
#define KCALL_STAT   10
#define KCALL_FSTAT  11
#define KCALL_UNLINK 12

#define KCALL_MEM_ALLOC 13
#define KCALL_MEM_FREE  14
#define KCALL_CHDIR  15
#define KCALL_GETCWD 16

#define KCALL_WAIT 17

#define KCALL_SPAWN 18
#define KCALL_KILL 19
#define KCALL_WAITPID 20
#define KCALL_GETPID 21

#define KCALL_SOCKET     22
#define KCALL_BIND       23
#define KCALL_LISTEN     24
#define KCALL_ACCEPT     25
#define KCALL_CONNECT    26
#define KCALL_POLL       27

#define KCALL_UPTIME     28
#define KCALL_YIELD      29
#define KCALL_RENAME     30
#define KCALL_DUP2       31
