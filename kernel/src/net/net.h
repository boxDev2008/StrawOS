#pragma once

#include "common.h"
#include "net/drivers/rtl.h"

/* -----------------------------------------------------------------------
 * Byte-order helpers (kernel is always running on little-endian x86-64)
 * ----------------------------------------------------------------------- */

static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) |
           ((x & 0x00FF0000u) >>  8) |
           ((x & 0x0000FF00u) <<  8) |
           ((x & 0x000000FFu) << 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/* -----------------------------------------------------------------------
 * Ethernet
 * ----------------------------------------------------------------------- */

#define ETH_ALEN        6
#define ETHERTYPE_IP    0x0800
#define ETHERTYPE_ARP   0x0806

typedef struct PACKED {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;      /* big-endian */
} EthHdr;

/* -----------------------------------------------------------------------
 * ARP (IPv4 over Ethernet)
 * ----------------------------------------------------------------------- */

#define ARP_HW_ETHER    1
#define ARP_PROTO_IP    0x0800
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

typedef struct PACKED {
    uint16_t htype;          /* hardware type   = ARP_HW_ETHER  */
    uint16_t ptype;          /* protocol type   = ARP_PROTO_IP  */
    uint8_t  hlen;           /* hardware length = 6             */
    uint8_t  plen;           /* protocol length = 4             */
    uint16_t op;             /* ARP_OP_REQUEST / ARP_OP_REPLY   */
    uint8_t  sha[ETH_ALEN];  /* sender hardware address         */
    uint32_t spa;            /* sender protocol address (IPv4)  */
    uint8_t  tha[ETH_ALEN];  /* target hardware address         */
    uint32_t tpa;            /* target protocol address (IPv4)  */
} ArpPkt;

/* -----------------------------------------------------------------------
 * IPv4
 * ----------------------------------------------------------------------- */

#define IP_PROTO_UDP    17
#define IP_TTL_DEFAULT  64

typedef struct PACKED {
    uint8_t  ver_ihl;        /* version (4) | IHL (5 for no options) */
    uint8_t  tos;
    uint16_t tot_len;        /* big-endian: header + payload         */
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} IpHdr;

/* -----------------------------------------------------------------------
 * ICMP
 * ----------------------------------------------------------------------- */

#define IP_PROTO_ICMP       1

#define ICMP_TYPE_ECHO_REQ  8
#define ICMP_TYPE_ECHO_REP  0

typedef struct PACKED {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} IcmpHdr;

/* One received ICMP echo reply */
typedef struct {
    uint32_t src_ip;
    uint16_t id;
    uint16_t seq;
    uint32_t rtt_ticks;   /* raw PIT tick delta — convert with pit_ms_per_tick */
} IcmpReply;

#define ICMP_REPLY_RING  8

/* -----------------------------------------------------------------------
 * UDP
 * ----------------------------------------------------------------------- */

typedef struct PACKED {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} UdpHdr;

/* -----------------------------------------------------------------------
 * ARP cache
 * ----------------------------------------------------------------------- */

#define ARP_CACHE_SIZE  16

typedef struct {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    bool     valid;
} ArpEntry;

/* -----------------------------------------------------------------------
 * Received UDP datagram (stored in a small ring for sockets to consume)
 * ----------------------------------------------------------------------- */

#define UDP_RX_RING     32
#define UDP_MAX_PAYLOAD 1472   /* 1500 MTU - 20 IP - 8 UDP */

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint8_t  data[UDP_MAX_PAYLOAD];
} UdpDgram;

/* -----------------------------------------------------------------------
 * Global network state
 * ----------------------------------------------------------------------- */

typedef struct {
    RTLDev   nic;
    uint8_t  mac[ETH_ALEN];

    /* Our IPv4 config (set by net_config) */
    uint32_t ip;             /* host byte order */
    uint32_t gateway;
    uint32_t netmask;

    /* ARP cache */
    ArpEntry arp_cache[ARP_CACHE_SIZE];

    /* UDP RX ring (kernel-level, drained by sockets) */
    UdpDgram rx_ring[UDP_RX_RING];
    uint32_t rx_head;        /* next slot to write */
    uint32_t rx_tail;        /* next slot to read  */

    /* ICMP echo reply ring */
    IcmpReply icmp_ring[ICMP_REPLY_RING];
    uint32_t  icmp_head;
    uint32_t  icmp_tail;

    /* TX packet ID counter */
    uint16_t ip_id;

    bool     up;
} NetState;

extern NetState g_net;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * Probe the RTL NIC, configure the stack with a static IP.
 * ip/gateway/netmask are in host byte order (e.g. 192.168.1.10 = 0xC0A8010A).
 * Returns true on success.
 */
bool net_init(uint32_t ip, uint32_t netmask, uint32_t gateway);

/* Configure / reconfigure IP (can be called after net_init). */
void net_config(uint32_t ip, uint32_t netmask, uint32_t gateway);

/*
 * Must be called periodically (or from interrupt context) to drain the NIC RX
 * ring and push parsed UDP datagrams into g_net.rx_ring.
 */
void net_poll(void);

/*
 * Send a UDP datagram to dst_ip:dst_port from src_port.
 * dst_ip is in host byte order.
 * Does an ARP request / cache lookup automatically.
 * Returns true on success.
 */
bool udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void *data, uint16_t len);

/*
 * Pop one UDP datagram from the kernel RX ring.
 * Returns true if a datagram was available.
 */
bool udp_recv(UdpDgram *out);

/*
 * Like udp_recv but filtered to a specific dst_port.
 */
bool udp_recv_port(uint16_t port, UdpDgram *out);

/*
 * Send an ICMP echo request to dst_ip and wait up to timeout_ms for a reply.
 * Returns true and fills *reply on success, false on timeout.
 * dst_ip is in host byte order.
 */
bool icmp_ping(uint32_t dst_ip, uint16_t seq, uint32_t timeout_ms, IcmpReply *reply);
