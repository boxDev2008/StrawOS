#include <socket.h>
#include <syscall.h>

int socket_create(int type)
{
    return (int)syscall1(SYS_SOCKET, (uint64_t)type);
}
int socket_connect(int fd, uint32_t ip, uint16_t port)
{
    return (int)syscall3(SYS_CONNECT, (uint64_t)fd, (uint64_t)ip, (uint64_t)port);
}
int socket_netsend(int fd, const void *buf, uint32_t len)
{
    return (int)syscall3(SYS_SEND, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}
int socket_netrecv(int fd, void *buf, uint32_t len)
{
    return (int)syscall3(SYS_RECV, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}
int socket_bind(int fd, uint16_t port)
{
    return (int)syscall2(SYS_BIND, (uint64_t)fd, (uint64_t)port);
}
int socket_close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}