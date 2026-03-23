#pragma once

#include "common.h"
#include "net/net.h"

/* -----------------------------------------------------------------------
 * Socket type (UDP only for now)
 * ----------------------------------------------------------------------- */

#define SOCK_UDP    1

/* -----------------------------------------------------------------------
 * Socket descriptor table
 *
 * Sockets are allocated from a flat table indexed by a small integer
 * (the "socket fd").  They live in the kernel socket table, separate
 * from the VFS fd table.  The syscall layer uses negative numbers to
 * distinguish socket fds from file fds — socket fds are returned as
 * -(slot+1) so that fd 0 maps to -1, fd 1 to -2, etc.  The helpers
 * is_sock_fd / sock_fd_to_slot translate back and forth.
 * ----------------------------------------------------------------------- */

#define MAX_SOCKETS     (1<<14)
#define SOCK_EPHEMERAL_BASE 49152   /* start of dynamic/private port range */

typedef struct {
    bool     used;
    int      type;         /* SOCK_UDP */
    uint32_t remote_ip;    /* host byte order; 0 = unconnected */
    uint16_t remote_port;
    uint16_t local_port;
} Socket;

/* -----------------------------------------------------------------------
 * Inline fd helpers
 * ----------------------------------------------------------------------- */

static inline bool is_sock_fd(int fd)    { return fd < -1; }
static inline int  sock_fd_to_slot(int fd) { return -(fd + 2); }
static inline int  slot_to_sock_fd(int slot) { return -(slot + 2); }

/* -----------------------------------------------------------------------
 * Kernel socket syscall handlers (called from syscall.c)
 * ----------------------------------------------------------------------- */

/*
 * Initialise the socket table (heap-allocates MAX_SOCKETS slots).
 * Must be called once after heap_init(), before any k_socket() calls.
 */
void socket_init(void);

/*
 * Allocate a new UDP socket.
 * type is currently ignored (always UDP).
 * Returns a socket fd (< -1) on success, -1 on failure.
 */
int64_t k_socket(int type);

/*
 * Associate the socket with a remote IP:port (host byte order).
 * Also assigns a local ephemeral port if one hasn't been assigned yet.
 * Returns 0 on success, -1 on failure.
 */
int64_t k_connect(int fd, uint32_t ip, uint16_t port);

/*
 * Send data over the connected socket.
 * Returns number of bytes sent, or -1.
 */
int64_t k_netsend(int fd, const void *buf, uint32_t len);

/*
 * Receive data.  Polls the NIC, then looks for a datagram addressed
 * to the socket's local port.
 * Returns number of bytes received (capped at len), or 0 if none ready.
 */
int64_t k_netrecv(int fd, void *buf, uint32_t len);

/*
 * Bind a local port so udp_recv_port can filter incoming datagrams to this socket.
 * Call before k_netrecv if you want to receive on a specific port.
 */
int64_t k_bind(int fd, uint16_t port);

/*
 * Close the socket and free the slot.
 */
int64_t k_sockclose(int fd);