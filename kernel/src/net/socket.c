#include "net/socket.h"
#include "net/net.h"
#include "memory/heap.h"
#include "libk/kprintf.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Socket table — heap-allocated at socket_init()
 * ----------------------------------------------------------------------- */

static Socket *sock_table = NULL;

static uint16_t next_ephemeral = SOCK_EPHEMERAL_BASE;

void socket_init(void)
{
    sock_table = kmalloc(sizeof(Socket) * MAX_SOCKETS);
    if (!sock_table) {
        kprintf("[socket] failed to allocate socket table\n");
        return;
    }
    memset(sock_table, 0, sizeof(Socket) * MAX_SOCKETS);
    kprintf("[socket] table ready  max=%d  (%u KB)\n",
            MAX_SOCKETS,
            (unsigned)(sizeof(Socket) * MAX_SOCKETS / 1024));
}

static uint16_t alloc_ephemeral_port(void)
{
    /* Trivial bump-allocator; wraps within ephemeral range */
    uint16_t p = next_ephemeral++;
    if (next_ephemeral == 0) next_ephemeral = SOCK_EPHEMERAL_BASE;
    return p;
}

/* -----------------------------------------------------------------------
 * k_socket
 * ----------------------------------------------------------------------- */

int64_t k_socket(int type)
{
    (void)type;  /* only UDP for now */
    if (!sock_table) return -1;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sock_table[i].used) {
            sock_table[i].used        = true;
            sock_table[i].type        = SOCK_UDP;
            sock_table[i].remote_ip   = 0;
            sock_table[i].remote_port = 0;
            sock_table[i].local_port  = 0;
            return (int64_t)slot_to_sock_fd(i);
        }
    }
    return -1;  /* no free slots */
}

/* -----------------------------------------------------------------------
 * k_connect
 * ----------------------------------------------------------------------- */

int64_t k_connect(int fd, uint32_t ip, uint16_t port)
{
    if (!is_sock_fd(fd)) return -1;
    int slot = sock_fd_to_slot(fd);
    if (slot < 0 || slot >= MAX_SOCKETS) return -1;

    Socket *s = &sock_table[slot];
    if (!s->used) return -1;

    s->remote_ip   = ip;
    s->remote_port = port;

    if (s->local_port == 0)
        s->local_port = alloc_ephemeral_port();

    return 0;
}

/* -----------------------------------------------------------------------
 * k_netsend
 * ----------------------------------------------------------------------- */

int64_t k_netsend(int fd, const void *buf, uint32_t len)
{
    if (!buf || len == 0) return -1;
    if (!is_sock_fd(fd))  return -1;

    int slot = sock_fd_to_slot(fd);
    if (slot < 0 || slot >= MAX_SOCKETS) return -1;

    Socket *s = &sock_table[slot];
    if (!s->used)        return -1;
    if (!s->remote_ip)   return -1;   /* not connected */

    if (len > UDP_MAX_PAYLOAD) len = UDP_MAX_PAYLOAD;

    bool ok = udp_send(s->remote_ip, s->local_port, s->remote_port, buf, (uint16_t)len);
    return ok ? (int64_t)len : -1;
}

/* -----------------------------------------------------------------------
 * k_netrecv
 * ----------------------------------------------------------------------- */

int64_t k_netrecv(int fd, void *buf, uint32_t len)
{
    if (!buf || len == 0) return -1;
    if (!is_sock_fd(fd))  return -1;

    int slot = sock_fd_to_slot(fd);
    if (slot < 0 || slot >= MAX_SOCKETS) return -1;

    Socket *s = &sock_table[slot];
    if (!s->used) return -1;

    /* Drain NIC into the kernel UDP ring first */
    net_poll();

    UdpDgram dgram;
    if (s->local_port) {
        if (!udp_recv_port(s->local_port, &dgram)) return 0;
    } else {
        if (!udp_recv(&dgram)) return 0;
    }

    uint32_t copy = dgram.len < len ? dgram.len : len;
    memcpy(buf, dgram.data, copy);
    return (int64_t)copy;
}

/* -----------------------------------------------------------------------
 * k_bind
 * ----------------------------------------------------------------------- */

int64_t k_bind(int fd, uint16_t port)
{
    if (!is_sock_fd(fd)) return -1;
    int slot = sock_fd_to_slot(fd);
    if (slot < 0 || slot >= MAX_SOCKETS) return -1;
    if (!sock_table[slot].used) return -1;
    sock_table[slot].local_port = port;
    return 0;
}

/* -----------------------------------------------------------------------
 * k_sockclose
 * ----------------------------------------------------------------------- */

int64_t k_sockclose(int fd)
{
    if (!is_sock_fd(fd)) return -1;
    int slot = sock_fd_to_slot(fd);
    if (slot < 0 || slot >= MAX_SOCKETS) return -1;

    sock_table[slot].used = false;
    return 0;
}