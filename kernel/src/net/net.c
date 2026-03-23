#include "net/net.h"
#include "memory/heap.h"
#include "libk/kprintf.h"
#include "libk/string.h"
#include "devices/pit.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Global state
 * ----------------------------------------------------------------------- */

NetState g_net;

/* Scratch TX buffer — one frame at a time (kernel is single-threaded for now) */
static uint8_t tx_buf[1514];

/* -----------------------------------------------------------------------
 * Checksum helper (RFC 1071)
 * ----------------------------------------------------------------------- */

static uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *p   = (const uint16_t *)data;
    uint32_t        sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += *(const uint8_t *)p;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

/* -----------------------------------------------------------------------
 * ARP cache
 * ----------------------------------------------------------------------- */

static ArpEntry *arp_cache_lookup(uint32_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (g_net.arp_cache[i].valid && g_net.arp_cache[i].ip == ip)
            return &g_net.arp_cache[i];
    return NULL;
}

static void arp_cache_insert(uint32_t ip, const uint8_t *mac)
{
    /* Prefer an existing entry for this IP (update) */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_net.arp_cache[i].valid && g_net.arp_cache[i].ip == ip) {
            memcpy(g_net.arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }
    /* Find an empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_net.arp_cache[i].valid) {
            g_net.arp_cache[i].ip    = ip;
            g_net.arp_cache[i].valid = true;
            memcpy(g_net.arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }
    /* Cache full — evict slot 0 (simple FIFO) */
    g_net.arp_cache[0].ip    = ip;
    memcpy(g_net.arp_cache[0].mac, mac, ETH_ALEN);
}

/* -----------------------------------------------------------------------
 * ARP send helpers
 * ----------------------------------------------------------------------- */

static void arp_send_request(uint32_t target_ip)
{
    static const uint8_t bcast[ETH_ALEN] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    uint8_t frame[sizeof(EthHdr) + sizeof(ArpPkt)];
    EthHdr *eth = (EthHdr *)frame;
    ArpPkt *arp = (ArpPkt *)(frame + sizeof(EthHdr));

    memcpy(eth->dst, bcast,       ETH_ALEN);
    memcpy(eth->src, g_net.mac,   ETH_ALEN);
    eth->ethertype = htons(ETHERTYPE_ARP);

    arp->htype = htons(ARP_HW_ETHER);
    arp->ptype = htons(ARP_PROTO_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->op    = htons(ARP_OP_REQUEST);
    memcpy(arp->sha, g_net.mac, ETH_ALEN);
    arp->spa = htonl(g_net.ip);
    memset(arp->tha, 0, ETH_ALEN);
    arp->tpa = htonl(target_ip);

    rtl_send(&g_net.nic, frame, (uint16_t)sizeof(frame));
}

static void arp_send_reply(const ArpPkt *req, const uint8_t *requester_mac)
{
    uint8_t frame[sizeof(EthHdr) + sizeof(ArpPkt)];
    EthHdr *eth = (EthHdr *)frame;
    ArpPkt *arp = (ArpPkt *)(frame + sizeof(EthHdr));

    memcpy(eth->dst, requester_mac, ETH_ALEN);
    memcpy(eth->src, g_net.mac,     ETH_ALEN);
    eth->ethertype = htons(ETHERTYPE_ARP);

    arp->htype = htons(ARP_HW_ETHER);
    arp->ptype = htons(ARP_PROTO_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->op    = htons(ARP_OP_REPLY);
    memcpy(arp->sha, g_net.mac,    ETH_ALEN);
    arp->spa = htonl(g_net.ip);
    memcpy(arp->tha, requester_mac, ETH_ALEN);
    arp->tpa = req->spa;

    rtl_send(&g_net.nic, frame, (uint16_t)sizeof(frame));
}

/* -----------------------------------------------------------------------
 * ARP resolution with busy-wait (up to ~100 ms via net_poll)
 * ----------------------------------------------------------------------- */

static const uint8_t *arp_resolve(uint32_t ip)
{
    /* Check if destination is outside our subnet — use gateway instead */
    if ((ip & g_net.netmask) != (g_net.ip & g_net.netmask))
        ip = g_net.gateway;

    ArpEntry *e = arp_cache_lookup(ip);
    if (e) return e->mac;

    /* Send ARP request and poll for a reply (up to 1000 polls) */
    arp_send_request(ip);
    for (int i = 0; i < 1000; i++) {
        net_poll();
        e = arp_cache_lookup(ip);
        if (e) return e->mac;
    }

    kprintf("[net] ARP timeout for %u.%u.%u.%u\n",
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
            (ip >>  8) & 0xFF,  ip        & 0xFF);
    return NULL;
}

/* -----------------------------------------------------------------------
 * Frame processing
 * ----------------------------------------------------------------------- */

static void process_arp(const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(ArpPkt)) return;
    const ArpPkt *arp = (const ArpPkt *)payload;

    /* Only care about IPv4-over-Ethernet */
    if (ntohs(arp->htype) != ARP_HW_ETHER) return;
    if (ntohs(arp->ptype) != ARP_PROTO_IP)  return;

    uint32_t spa = ntohl(arp->spa);
    arp_cache_insert(spa, arp->sha);

    if (ntohs(arp->op) == ARP_OP_REQUEST) {
        /* Is it asking for our IP? */
        if (ntohl(arp->tpa) == g_net.ip)
            arp_send_reply(arp, arp->sha);
    }
    /* Replies are implicitly handled by the cache insert above */
}

static void process_udp(uint32_t src_ip, const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(UdpHdr)) return;
    const UdpHdr *udp = (const UdpHdr *)payload;

    uint16_t udp_len = ntohs(udp->length);
    if (udp_len < 8 || udp_len > len) return;

    uint16_t data_len = udp_len - (uint16_t)sizeof(UdpHdr);
    if (data_len > UDP_MAX_PAYLOAD) data_len = UDP_MAX_PAYLOAD;

    uint32_t next = (g_net.rx_head + 1) % UDP_RX_RING;
    if (next == g_net.rx_tail) {
        /* Ring full — drop */
        return;
    }

    UdpDgram *dgram  = &g_net.rx_ring[g_net.rx_head];
    dgram->src_ip    = src_ip;
    dgram->src_port  = ntohs(udp->src_port);
    dgram->dst_port  = ntohs(udp->dst_port);
    dgram->len       = data_len;
    memcpy(dgram->data, (const uint8_t *)udp + sizeof(UdpHdr), data_len);

    g_net.rx_head = next;
}

static void process_icmp(uint32_t src_ip, const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(IcmpHdr)) return;
    const IcmpHdr *icmp = (const IcmpHdr *)payload;

    if (icmp->type != ICMP_TYPE_ECHO_REP) return;

    uint32_t next = (g_net.icmp_head + 1) % ICMP_REPLY_RING;
    if (next == g_net.icmp_tail) return;  /* ring full, drop */

    IcmpReply *r = &g_net.icmp_ring[g_net.icmp_head];
    r->src_ip    = src_ip;
    r->id        = ntohs(icmp->id);
    r->seq       = ntohs(icmp->seq);
    r->rtt_ticks = 0;  /* filled in by icmp_ping */
    g_net.icmp_head = next;
}

static void process_ip(const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(IpHdr)) return;
    const IpHdr *ip = (const IpHdr *)payload;

    /* Only IPv4, no options (IHL must be 5) */
    if ((ip->ver_ihl >> 4) != 4)      return;
    if ((ip->ver_ihl & 0x0F) != 5)    return;

    uint16_t tot_len = ntohs(ip->tot_len);
    if (tot_len > len)                 return;

    /* Accept packets addressed to us or broadcast */
    uint32_t dst = ntohl(ip->dst);
    if (dst != g_net.ip &&
        dst != 0xFFFFFFFF &&
        dst != (g_net.ip | ~g_net.netmask))
        return;

    uint32_t src_ip    = ntohl(ip->src);
    const uint8_t *data = payload + sizeof(IpHdr);
    uint16_t data_len   = (uint16_t)(tot_len - sizeof(IpHdr));

    if (ip->protocol == IP_PROTO_UDP)
        process_udp(src_ip, data, data_len);
    else if (ip->protocol == IP_PROTO_ICMP)
        process_icmp(src_ip, data, data_len);
}

static void process_frame(const uint8_t *frame, uint16_t len)
{
    if (len < (uint16_t)sizeof(EthHdr)) return;
    const EthHdr *eth = (const EthHdr *)frame;

    const uint8_t *payload = frame + sizeof(EthHdr);
    uint16_t payload_len   = (uint16_t)(len - sizeof(EthHdr));

    switch (ntohs(eth->ethertype)) {
    case ETHERTYPE_ARP:
        process_arp(payload, payload_len);
        break;
    case ETHERTYPE_IP:
        process_ip(payload, payload_len);
        break;
    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

bool net_init(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    memset(&g_net, 0, sizeof(g_net));

    if (!rtl_init(&g_net.nic)) {
        kprintf("[net] RTL NIC not found\n");
        return false;
    }

    memcpy(g_net.mac, g_net.nic.mac, ETH_ALEN);
    net_config(ip, netmask, gateway);
    g_net.up = true;

    kprintf("[net] up  MAC=%02x:%02x:%02x:%02x:%02x:%02x  IP=%u.%u.%u.%u\n",
            g_net.mac[0], g_net.mac[1], g_net.mac[2],
            g_net.mac[3], g_net.mac[4], g_net.mac[5],
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
            (ip >>  8) & 0xFF,  ip        & 0xFF);
    return true;
}

void net_config(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    g_net.ip      = ip;
    g_net.netmask = netmask;
    g_net.gateway = gateway;
}

void net_poll(void)
{
    if (!g_net.up) return;

    static uint8_t rx_buf[1514];
    uint16_t len;

    /* Drain up to 16 frames per call to avoid starvation */
    for (int i = 0; i < 16; i++) {
        len = rtl_recv(&g_net.nic, rx_buf, (uint16_t)sizeof(rx_buf));
        if (!len) break;
        process_frame(rx_buf, len);
    }
}

bool udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void *data, uint16_t len)
{
    if (!g_net.up)             return false;
    if (len > UDP_MAX_PAYLOAD) return false;

    /* ---- loopback shortcut: skip the NIC entirely ---- */
    if (dst_ip == g_net.ip) {
        uint32_t next = (g_net.rx_head + 1) % UDP_RX_RING;
        if (next != g_net.rx_tail) {
            UdpDgram *dgram = &g_net.rx_ring[g_net.rx_head];
            dgram->src_ip   = g_net.ip;
            dgram->src_port = src_port;
            dgram->dst_port = dst_port;
            dgram->len      = len;
            memcpy(dgram->data, data, len);
            g_net.rx_head   = next;
        }
        return true;
    }

    const uint8_t *dst_mac = arp_resolve(dst_ip);
    if (!dst_mac)                            return false;

    uint16_t udp_len  = (uint16_t)(sizeof(UdpHdr) + len);
    uint16_t ip_len   = (uint16_t)(sizeof(IpHdr)  + udp_len);
    uint16_t frame_sz = (uint16_t)(sizeof(EthHdr)  + ip_len);

    memset(tx_buf, 0, frame_sz);

    /* Ethernet */
    EthHdr *eth = (EthHdr *)tx_buf;
    memcpy(eth->dst, dst_mac,   ETH_ALEN);
    memcpy(eth->src, g_net.mac, ETH_ALEN);
    eth->ethertype = htons(ETHERTYPE_IP);

    /* IP */
    IpHdr *ip      = (IpHdr *)(tx_buf + sizeof(EthHdr));
    ip->ver_ihl    = (4 << 4) | 5;
    ip->tos        = 0;
    ip->tot_len    = htons(ip_len);
    ip->id         = htons(g_net.ip_id++);
    ip->frag_off   = 0;
    ip->ttl        = IP_TTL_DEFAULT;
    ip->protocol   = IP_PROTO_UDP;
    ip->checksum   = 0;
    ip->src        = htonl(g_net.ip);
    ip->dst        = htonl(dst_ip);
    ip->checksum   = ip_checksum(ip, sizeof(IpHdr));

    /* UDP */
    UdpHdr *udp   = (UdpHdr *)(tx_buf + sizeof(EthHdr) + sizeof(IpHdr));
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(udp_len);
    udp->checksum = 0;   /* UDP checksum is optional for IPv4 */

    /* Payload */
    memcpy(tx_buf + sizeof(EthHdr) + sizeof(IpHdr) + sizeof(UdpHdr), data, len);

    return rtl_send(&g_net.nic, tx_buf, frame_sz);
}

bool udp_recv(UdpDgram *out)
{
    if (g_net.rx_tail == g_net.rx_head) return false;
    *out = g_net.rx_ring[g_net.rx_tail];
    g_net.rx_tail = (g_net.rx_tail + 1) % UDP_RX_RING;
    return true;
}

bool udp_recv_port(uint16_t port, UdpDgram *out)
{
    /* Linear scan of the ring — fine for a small ring */
    uint32_t i = g_net.rx_tail;
    while (i != g_net.rx_head) {
        if (g_net.rx_ring[i].dst_port == port) {
            *out = g_net.rx_ring[i];
            /* Compact: shift tail up if this was the head of the ring */
            if (i == g_net.rx_tail)
                g_net.rx_tail = (g_net.rx_tail + 1) % UDP_RX_RING;
            return true;
        }
        i = (i + 1) % UDP_RX_RING;
    }
    return false;
}

bool icmp_ping(uint32_t dst_ip, uint16_t seq, uint32_t timeout_ms, IcmpReply *reply)
{
    if (!g_net.up) return false;

    /* Build ICMP echo request */
    static const uint16_t PING_ID = 0x1234;
    static const char payload[]   = "kernel-ping";
    uint16_t payload_len          = (uint16_t)sizeof(payload) - 1;

    uint16_t icmp_len  = (uint16_t)(sizeof(IcmpHdr) + payload_len);
    uint16_t ip_len    = (uint16_t)(sizeof(IpHdr) + icmp_len);
    uint16_t frame_sz  = (uint16_t)(sizeof(EthHdr) + ip_len);

    /* Resolve destination MAC (gateway if off-subnet) */
    const uint8_t *dst_mac = arp_resolve(dst_ip);
    if (!dst_mac) return false;

    memset(tx_buf, 0, frame_sz);

    /* Ethernet */
    EthHdr *eth = (EthHdr *)tx_buf;
    memcpy(eth->dst, dst_mac,   ETH_ALEN);
    memcpy(eth->src, g_net.mac, ETH_ALEN);
    eth->ethertype = htons(ETHERTYPE_IP);

    /* IP */
    IpHdr *ip   = (IpHdr *)(tx_buf + sizeof(EthHdr));
    ip->ver_ihl = (4 << 4) | 5;
    ip->tos     = 0;
    ip->tot_len = htons(ip_len);
    ip->id      = htons(g_net.ip_id++);
    ip->frag_off = 0;
    ip->ttl     = IP_TTL_DEFAULT;
    ip->protocol = IP_PROTO_ICMP;
    ip->checksum = 0;
    ip->src     = htonl(g_net.ip);
    ip->dst     = htonl(dst_ip);
    ip->checksum = ip_checksum(ip, sizeof(IpHdr));

    /* ICMP header */
    IcmpHdr *icmp = (IcmpHdr *)(tx_buf + sizeof(EthHdr) + sizeof(IpHdr));
    icmp->type    = ICMP_TYPE_ECHO_REQ;
    icmp->code    = 0;
    icmp->checksum = 0;
    icmp->id      = htons(PING_ID);
    icmp->seq     = htons(seq);
    memcpy((uint8_t *)icmp + sizeof(IcmpHdr), payload, payload_len);
    icmp->checksum = ip_checksum(icmp, icmp_len);

    /* Record send time and transmit */
    uint64_t t0 = pit_get_ticks_ms();
    if (!rtl_send(&g_net.nic, tx_buf, frame_sz)) return false;

    /* Poll for reply up to timeout_ms */
    while ((pit_get_ticks_ms() - t0) < timeout_ms) {
        net_poll();
        while (g_net.icmp_tail != g_net.icmp_head) {
            IcmpReply *r = &g_net.icmp_ring[g_net.icmp_tail];
            g_net.icmp_tail = (g_net.icmp_tail + 1) % ICMP_REPLY_RING;

            if (r->id == PING_ID && r->seq == seq) {
                r->rtt_ticks = (uint32_t)(pit_get_ticks_ms() - t0);
                if (reply) *reply = *r;
                return true;
            }
        }
    }
    return false;  /* timed out */
}
