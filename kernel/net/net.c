uint16_t net_htons(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

uint32_t net_htonl(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

typedef struct PACKED
{
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
}
PACKED ArpPacket;

#define ARP_HTYPE_ETHERNET 1
#define ARP_OPER_REQUEST 1
#define ARP_OPER_REPLY   2

#define ARP_CACHE_SIZE 8

typedef struct
{
    int valid;
    Ipv4Addr ip;
    uint8_t mac[6];
}
ArpEntry;

static ArpEntry arp_cache[ARP_CACHE_SIZE];
static int      arp_next_slot;

typedef struct PACKED
{
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_fragoff;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
}
PACKED Ipv4Header;

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

typedef struct PACKED
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
}
PACKED UdpHeader;

typedef struct PACKED
{
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
}
PACKED IcmpHeader;

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY   0

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2

#define DHCP_HTYPE_ETHERNET 1
#define DHCP_MAGIC_COOKIE   0x63825363u

#define DHCP_OPT_PAD          0
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS          6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_LIST   55
#define DHCP_OPT_END          255

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6

typedef struct PACKED
{
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
}
PACKED DhcpHeader;

#define DHCP_OPTIONS_SPACE 312

static uint8_t  our_mac[6];
static Ipv4Addr our_ip;
static Ipv4Addr gateway_ip;
static Ipv4Addr netmask;
static Ipv4Addr dns_ip;
static volatile bool net_up;

static const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint16_t ip_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;

    for (size_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1];

    if (len & 1)
        sum += (uint32_t)bytes[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

void ipv4_print(Ipv4Addr ip)
{
    kprintf("%d.%d.%d.%d", ip.bytes[0], ip.bytes[1], ip.bytes[2], ip.bytes[3]);
}

static int ipv4_equal(Ipv4Addr a, Ipv4Addr b)
{
    return a.bytes[0] == b.bytes[0] && a.bytes[1] == b.bytes[1] &&
           a.bytes[2] == b.bytes[2] && a.bytes[3] == b.bytes[3];
}

static int ipv4_same_subnet(Ipv4Addr a, Ipv4Addr b, Ipv4Addr mask)
{
    for (int i = 0; i < 4; i++)
        if ((a.bytes[i] & mask.bytes[i]) != (b.bytes[i] & mask.bytes[i]))
            return 0;
    return 1;
}

static void eth_send(const uint8_t *dst_mac, uint16_t ethertype, const void *payload, uint16_t payload_len)
{
    uint8_t buf[1514];
    EthHeader *eth = (EthHeader *)buf;

    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, our_mac, 6);
    eth->ethertype = net_htons(ethertype);

    uint16_t total = (uint16_t)(sizeof(EthHeader) + payload_len);
    if (total > sizeof(buf))
        total = sizeof(buf);

    memcpy(buf + sizeof(EthHeader), payload, total - sizeof(EthHeader));

    uint64_t flags = irq_save();
    nic_send(buf, total);
    irq_restore(flags);
}

static void arp_send_request(Ipv4Addr target_ip)
{
    ArpPacket arp;
    memset(&arp, 0, sizeof(arp));

    arp.htype = net_htons(ARP_HTYPE_ETHERNET);
    arp.ptype = net_htons(ETHERTYPE_IPV4);
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = net_htons(ARP_OPER_REQUEST);
    memcpy(arp.sender_mac, our_mac, 6);
    memcpy(arp.sender_ip, our_ip.bytes, 4);
    memset(arp.target_mac, 0, 6);
    memcpy(arp.target_ip, target_ip.bytes, 4);

    eth_send(BROADCAST_MAC, ETHERTYPE_ARP, &arp, sizeof(arp));
}

static void arp_handle_packet(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(ArpPacket))
        return;

    const ArpPacket *arp = (const ArpPacket *)data;

    if (net_htons(arp->htype) != ARP_HTYPE_ETHERNET || net_htons(arp->ptype) != ETHERTYPE_IPV4)
        return;

    Ipv4Addr sender_ip = ipv4_from_bytes(arp->sender_ip[0], arp->sender_ip[1], arp->sender_ip[2], arp->sender_ip[3]);

    if (net_htons(arp->oper) == ARP_OPER_REQUEST)
    {
        Ipv4Addr target_ip = ipv4_from_bytes(arp->target_ip[0], arp->target_ip[1], arp->target_ip[2], arp->target_ip[3]);
        if (!ipv4_equal(target_ip, our_ip))
            return;

        ArpPacket reply;
        memset(&reply, 0, sizeof(reply));
        reply.htype = net_htons(ARP_HTYPE_ETHERNET);
        reply.ptype = net_htons(ETHERTYPE_IPV4);
        reply.hlen = 6;
        reply.plen = 4;
        reply.oper = net_htons(ARP_OPER_REPLY);
        memcpy(reply.sender_mac, our_mac, 6);
        memcpy(reply.sender_ip, our_ip.bytes, 4);
        memcpy(reply.target_mac, arp->sender_mac, 6);
        memcpy(reply.target_ip, arp->sender_ip, 4);

        eth_send(arp->sender_mac, ETHERTYPE_ARP, &reply, sizeof(reply));
    }
    else if (net_htons(arp->oper) == ARP_OPER_REPLY)
    {
        for (int i = 0; i < ARP_CACHE_SIZE; i++)
        {
            if (arp_cache[i].valid && ipv4_equal(arp_cache[i].ip, sender_ip))
            {
                memcpy(arp_cache[i].mac, arp->sender_mac, 6);
                return;
            }
        }
        ArpEntry *e = &arp_cache[arp_next_slot];
        arp_next_slot = (arp_next_slot + 1) % ARP_CACHE_SIZE;
        e->valid = 1;
        e->ip = sender_ip;
        memcpy(e->mac, arp->sender_mac, 6);
    }
}

static int arp_lookup(Ipv4Addr ip, uint8_t *mac_out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && ipv4_equal(arp_cache[i].ip, ip))
        {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static int arp_resolve(Ipv4Addr ip, uint8_t *mac_out, uint32_t timeout_ms)
{
    uint64_t start = task_uptime_ms();
    uint64_t last_request = 0;
    bool first = true;

    while (task_uptime_ms() - start < timeout_ms)
    {
        if (arp_lookup(ip, mac_out))
            return 1;
        if (first || task_uptime_ms() - last_request >= 300)
        {
            arp_send_request(ip);
            last_request = task_uptime_ms();
            first = false;
        }
        task_sleep_ms(5);
    }

    return arp_lookup(ip, mac_out);
}

static volatile int      ping_reply_received;
static volatile uint16_t ping_expected_id;
static volatile uint16_t ping_expected_seq;

static void icmp_handle_packet(Ipv4Addr src_ip, const uint8_t *data, uint16_t len)
{
    if (len < sizeof(IcmpHeader))
        return;

    const IcmpHeader *icmp = (const IcmpHeader *)data;

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST)
    {
        uint16_t total_len = len;
        uint8_t buf[1500];
        if (total_len > sizeof(buf))
            total_len = sizeof(buf);

        memcpy(buf, data, total_len);
        IcmpHeader *reply = (IcmpHeader *)buf;
        reply->type = ICMP_TYPE_ECHO_REPLY;
        reply->checksum = 0;
        reply->checksum = net_htons(ip_checksum(buf, total_len));

        extern void ipv4_send(Ipv4Addr dst, uint8_t protocol, const void *payload, uint16_t len);
        ipv4_send(src_ip, IPV4_PROTO_ICMP, buf, total_len);
    }
    else if (icmp->type == ICMP_TYPE_ECHO_REPLY)
    {
        if (net_htons(icmp->id) == ping_expected_id && net_htons(icmp->seq) == ping_expected_seq)
            ping_reply_received = 1;
    }
}

static void ipv4_send_from(Ipv4Addr src, Ipv4Addr dst, const uint8_t *dst_mac, uint8_t protocol, const void *payload, uint16_t payload_len)
{
    static uint16_t next_id = 1;

    uint8_t buf[1500];
    Ipv4Header *ip = (Ipv4Header *)buf;

    uint16_t total_len = (uint16_t)(sizeof(Ipv4Header) + payload_len);
    if (total_len > sizeof(buf))
        total_len = sizeof(buf);

    ip->version_ihl = (4 << 4) | 5;
    ip->dscp_ecn = 0;
    ip->total_length = net_htons(total_len);
    ip->id = net_htons(next_id++);
    ip->flags_fragoff = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    memcpy(ip->src, src.bytes, 4);
    memcpy(ip->dst, dst.bytes, 4);
    ip->checksum = net_htons(ip_checksum(ip, sizeof(Ipv4Header)));

    memcpy(buf + sizeof(Ipv4Header), payload, total_len - sizeof(Ipv4Header));

    eth_send(dst_mac, ETHERTYPE_IPV4, buf, total_len);
}

void ipv4_send(Ipv4Addr dst, uint8_t protocol, const void *payload, uint16_t payload_len)
{
    Ipv4Addr next_hop = ipv4_same_subnet(dst, our_ip, netmask) ? dst : gateway_ip;

    uint8_t dst_mac[6];
    if (!arp_lookup(next_hop, dst_mac))
    {
        arp_send_request(next_hop);
        return;
    }

    ipv4_send_from(our_ip, dst, dst_mac, protocol, payload, payload_len);
}

Ipv4Addr net_local_ip(void)
{
    return our_ip;
}

bool net_is_up(void)
{
    return net_up;
}

void net_status(Ipv4Addr *ip, Ipv4Addr *gw, Ipv4Addr *mask, Ipv4Addr *dns)
{
    *ip = our_ip;
    *gw = gateway_ip;
    *mask = netmask;
    *dns = dns_ip;
}

void udp_send(Ipv4Addr dst, uint16_t src_port, uint16_t dst_port, const void *payload, uint16_t payload_len)
{
    uint8_t buf[1500];
    UdpHeader *udp = (UdpHeader *)buf;

    uint16_t total_len = (uint16_t)(sizeof(UdpHeader) + payload_len);
    if (total_len > sizeof(buf))
        total_len = sizeof(buf);

    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    udp->length = net_htons(total_len);
    udp->checksum = 0;

    memcpy(buf + sizeof(UdpHeader), payload, total_len - sizeof(UdpHeader));

    ipv4_send(dst, IPV4_PROTO_UDP, buf, total_len);
}

typedef void (*UdpCallback)(Ipv4Addr src_ip, uint16_t src_port, const uint8_t *data, uint16_t len);
static uint16_t    udp_listen_port;
static UdpCallback udp_listen_cb;

static void udp_listen(uint16_t port, UdpCallback cb)
{
    udp_listen_port = port;
    udp_listen_cb = cb;
}

static void udp_handle_packet(Ipv4Addr src_ip, const uint8_t *data, uint16_t len)
{
    if (len < sizeof(UdpHeader))
        return;

    const UdpHeader *udp = (const UdpHeader *)data;
    uint16_t dst_port = net_htons(udp->dst_port);
    uint16_t src_port = net_htons(udp->src_port);

    uint16_t udp_len = net_htons(udp->length);
    if (udp_len > len)
        udp_len = len;
    uint16_t payload_len = (uint16_t)(udp_len - sizeof(UdpHeader));

    if (udp_listen_cb && dst_port == udp_listen_port)
        udp_listen_cb(src_ip, src_port, data + sizeof(UdpHeader), payload_len);
    else
        inet_udp_deliver(src_ip, src_port, dst_port, data + sizeof(UdpHeader), payload_len);
}

static void ipv4_handle_packet(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(Ipv4Header))
        return;

    const Ipv4Header *ip = (const Ipv4Header *)data;

    uint8_t version = ip->version_ihl >> 4;
    uint8_t ihl_words = ip->version_ihl & 0xF;
    if (version != 4 || ihl_words < 5)
        return;

    uint16_t header_len = (uint16_t)(ihl_words * 4);
    if (len < header_len)
        return;

    Ipv4Addr dst_ip = ipv4_from_bytes(ip->dst[0], ip->dst[1], ip->dst[2], ip->dst[3]);

    int have_ip = !ipv4_equal(our_ip, ipv4_from_bytes(0, 0, 0, 0));
    Ipv4Addr broadcast_ip = ipv4_from_bytes(255, 255, 255, 255);
    if (have_ip && !ipv4_equal(dst_ip, our_ip) && !ipv4_equal(dst_ip, broadcast_ip))
        return;

    Ipv4Addr src_ip = ipv4_from_bytes(ip->src[0], ip->src[1], ip->src[2], ip->src[3]);

    uint16_t total_len = net_htons(ip->total_length);
    if (total_len > len)
        total_len = len;
    uint16_t payload_len = (uint16_t)(total_len - header_len);

    if (ip->protocol == IPV4_PROTO_ICMP && have_ip)
        icmp_handle_packet(src_ip, data + header_len, payload_len);
    else if (ip->protocol == IPV4_PROTO_UDP)
        udp_handle_packet(src_ip, data + header_len, payload_len);
    else if (ip->protocol == IPV4_PROTO_TCP && have_ip)
        inet_tcp_deliver(src_ip, data + header_len, payload_len);
}

static void net_handle_frame(const uint8_t *frame, uint16_t len)
{
    if (len < sizeof(EthHeader))
        return;

    const EthHeader *eth = (const EthHeader *)frame;
    uint16_t ethertype = net_htons(eth->ethertype);

    const uint8_t *payload = frame + sizeof(EthHeader);
    uint16_t payload_len = (uint16_t)(len - sizeof(EthHeader));

    if (ethertype == ETHERTYPE_ARP)
        arp_handle_packet(payload, payload_len);
    else if (ethertype == ETHERTYPE_IPV4)
        ipv4_handle_packet(payload, payload_len);
}

static volatile int      dhcp_got_offer;
static volatile int      dhcp_got_ack;
static volatile int      dhcp_got_nak;
static uint32_t          dhcp_xid;
static Ipv4Addr          dhcp_offered_ip;
static Ipv4Addr          dhcp_server_id;
static Ipv4Addr          dhcp_offered_mask;
static Ipv4Addr          dhcp_offered_gw;
static Ipv4Addr          dhcp_offered_dns;

static uint8_t *dhcp_put_option(uint8_t *p, uint8_t code, uint8_t len, const void *data)
{
    *p++ = code;
    *p++ = len;
    memcpy(p, data, len);
    return p + len;
}

static void dhcp_send(uint8_t msg_type, Ipv4Addr requested_ip, Ipv4Addr server_id, int include_server_id)
{
    uint8_t packet[sizeof(DhcpHeader) + DHCP_OPTIONS_SPACE];
    memset(packet, 0, sizeof(packet));

    DhcpHeader *hdr = (DhcpHeader *)packet;
    hdr->op = DHCP_OP_BOOTREQUEST;
    hdr->htype = DHCP_HTYPE_ETHERNET;
    hdr->hlen = 6;
    hdr->hops = 0;
    hdr->xid = net_htonl(dhcp_xid);
    hdr->secs = 0;
    hdr->flags = 0;
    memcpy(hdr->chaddr, our_mac, 6);

    uint8_t *opt = packet + sizeof(DhcpHeader);
    uint32_t cookie = net_htonl(DHCP_MAGIC_COOKIE);
    memcpy(opt, &cookie, 4);
    opt += 4;

    opt = dhcp_put_option(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);

    if (msg_type == DHCP_MSG_REQUEST)
    {
        opt = dhcp_put_option(opt, DHCP_OPT_REQUESTED_IP, 4, requested_ip.bytes);
        if (include_server_id)
            opt = dhcp_put_option(opt, DHCP_OPT_SERVER_ID, 4, server_id.bytes);
    }

    uint8_t params[] = { DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
    opt = dhcp_put_option(opt, DHCP_OPT_PARAM_LIST, sizeof(params), params);

    *opt++ = DHCP_OPT_END;

    uint16_t dhcp_len = (uint16_t)(opt - packet);

    Ipv4Addr zero_ip = ipv4_from_bytes(0, 0, 0, 0);
    Ipv4Addr bcast_ip = ipv4_from_bytes(255, 255, 255, 255);

    uint8_t udp_buf[sizeof(UdpHeader) + sizeof(packet)];
    UdpHeader *udp = (UdpHeader *)udp_buf;
    udp->src_port = net_htons(DHCP_CLIENT_PORT);
    udp->dst_port = net_htons(DHCP_SERVER_PORT);
    udp->length = net_htons((uint16_t)(sizeof(UdpHeader) + dhcp_len));
    udp->checksum = 0;
    memcpy(udp_buf + sizeof(UdpHeader), packet, dhcp_len);

    ipv4_send_from(zero_ip, bcast_ip, BROADCAST_MAC, IPV4_PROTO_UDP, udp_buf, (uint16_t)(sizeof(UdpHeader) + dhcp_len));
}

static void dhcp_handle_reply(Ipv4Addr src_ip, uint16_t src_port, const uint8_t *data, uint16_t len)
{
    (void)src_port;

    if (len < sizeof(DhcpHeader) + 4)
        return;

    const DhcpHeader *hdr = (const DhcpHeader *)data;

    if (hdr->op != DHCP_OP_BOOTREPLY)
        return;
    if (net_htonl(hdr->xid) != dhcp_xid)
        return;
    if (memcmp(hdr->chaddr, our_mac, 6) != 0)
        return;

    const uint8_t *opt = data + sizeof(DhcpHeader);
    uint16_t opt_len = (uint16_t)(len - sizeof(DhcpHeader));

    uint32_t cookie;
    memcpy(&cookie, opt, 4);
    if (net_htonl(cookie) != DHCP_MAGIC_COOKIE)
        return;
    opt += 4;
    opt_len -= 4;

    uint8_t msg_type = 0;
    Ipv4Addr mask = ipv4_from_bytes(0, 0, 0, 0);
    Ipv4Addr router = ipv4_from_bytes(0, 0, 0, 0);
    Ipv4Addr server_id = src_ip;

    uint16_t i = 0;
    while (i < opt_len)
    {
        uint8_t code = opt[i];
        if (code == DHCP_OPT_PAD) { i++; continue; }
        if (code == DHCP_OPT_END) break;
        if (i + 1 >= opt_len) break;

        uint8_t olen = opt[i + 1];
        if (i + 2 + olen > opt_len) break;
        const uint8_t *oval = opt + i + 2;

        switch (code)
        {
            case DHCP_OPT_MSG_TYPE:
                if (olen >= 1) msg_type = oval[0];
                break;
            case DHCP_OPT_SUBNET_MASK:
                if (olen >= 4) mask = ipv4_from_bytes(oval[0], oval[1], oval[2], oval[3]);
                break;
            case DHCP_OPT_ROUTER:
                if (olen >= 4) router = ipv4_from_bytes(oval[0], oval[1], oval[2], oval[3]);
                break;
            case DHCP_OPT_DNS:
                if (olen >= 4) dhcp_offered_dns = ipv4_from_bytes(oval[0], oval[1], oval[2], oval[3]);
                break;
            case DHCP_OPT_SERVER_ID:
                if (olen >= 4) server_id = ipv4_from_bytes(oval[0], oval[1], oval[2], oval[3]);
                break;
            default:
                break;
        }

        i = (uint16_t)(i + 2 + olen);
    }

    Ipv4Addr offered_ip = ipv4_from_bytes(hdr->yiaddr[0], hdr->yiaddr[1], hdr->yiaddr[2], hdr->yiaddr[3]);

    if (msg_type == DHCP_MSG_OFFER && !dhcp_got_offer)
    {
        dhcp_offered_ip = offered_ip;
        dhcp_server_id = server_id;
        dhcp_offered_mask = mask;
        dhcp_offered_gw = router;
        dhcp_got_offer = 1;
    }
    else if (msg_type == DHCP_MSG_ACK)
    {
        dhcp_offered_ip = offered_ip;
        dhcp_offered_mask = mask;
        dhcp_offered_gw = router;
        dhcp_got_ack = 1;
    }
    else if (msg_type == DHCP_MSG_NAK)
    {
        dhcp_got_nak = 1;
    }
}

int net_dhcp_init(uint32_t timeout_ms, Ipv4Addr *ip_out, Ipv4Addr *gw_out, Ipv4Addr *mask_out)
{
    memcpy(our_mac, nic_mac(), 6);
    our_ip = ipv4_from_bytes(0, 0, 0, 0);
    gateway_ip = ipv4_from_bytes(0, 0, 0, 0);
    netmask = ipv4_from_bytes(0, 0, 0, 0);
    memset(arp_cache, 0, sizeof(arp_cache));
    dhcp_offered_dns = ipv4_from_bytes(0, 0, 0, 0);

    nic_set_rx_callback(net_handle_frame);
    udp_listen(DHCP_CLIENT_PORT, dhcp_handle_reply);

    dhcp_xid = ((uint32_t)our_mac[2] << 24) | ((uint32_t)our_mac[3] << 16) | ((uint32_t)our_mac[4] << 8) | our_mac[5];

    dhcp_got_offer = 0;
    dhcp_got_ack = 0;
    dhcp_got_nak = 0;

    kprintf("dhcp: sending DISCOVER (xid=%x)\n", dhcp_xid);

    uint64_t max_spins = (uint64_t)timeout_ms * 50000;
    uint64_t resend_every = 300000;
    Ipv4Addr zero_ip = ipv4_from_bytes(0, 0, 0, 0);

    dhcp_send(DHCP_MSG_DISCOVER, zero_ip, zero_ip, 0);
    uint64_t spins = 0;
    while (!dhcp_got_offer)
    {
        if (spins >= max_spins)
        {
            kprintf("dhcp: timed out waiting for OFFER\n");
            return 0;
        }
        if (spins > 0 && spins % resend_every == 0)
            dhcp_send(DHCP_MSG_DISCOVER, zero_ip, zero_ip, 0);
        task_sleep_ms(1);
        spins += 50000;
    }

    kprintf("dhcp: got OFFER of ");
    ipv4_print(dhcp_offered_ip);
    kprintf(" from ");
    ipv4_print(dhcp_server_id);
    kprintf("\n");

    dhcp_send(DHCP_MSG_REQUEST, dhcp_offered_ip, dhcp_server_id, 1);
    spins = 0;
    while (!dhcp_got_ack && !dhcp_got_nak)
    {
        if (spins >= max_spins)
        {
            kprintf("dhcp: timed out waiting for ACK\n");
            return 0;
        }
        if (spins > 0 && spins % resend_every == 0)
            dhcp_send(DHCP_MSG_REQUEST, dhcp_offered_ip, dhcp_server_id, 1);
        task_sleep_ms(1);
        spins += 50000;
    }

    if (dhcp_got_nak)
    {
        kprintf("dhcp: server sent NAK, giving up\n");
        return 0;
    }

    kprintf("dhcp: got ACK, lease = ");
    ipv4_print(dhcp_offered_ip);
    kprintf("\n");

    udp_listen(0, NULL);
    net_init(dhcp_offered_ip, dhcp_offered_gw, dhcp_offered_mask);
    if (dhcp_offered_dns.as_u32)
        dns_ip = dhcp_offered_dns;

    if (ip_out) *ip_out = dhcp_offered_ip;
    if (gw_out) *gw_out = dhcp_offered_gw;
    if (mask_out) *mask_out = dhcp_offered_mask;

    return 1;
}

void net_init(Ipv4Addr ip, Ipv4Addr gw, Ipv4Addr mask)
{
    memset(arp_cache, 0, sizeof(arp_cache));

    memcpy(our_mac, nic_mac(), 6);
    our_ip = ip;
    gateway_ip = gw;
    netmask = mask;
    dns_ip = ipv4_from_bytes(gw.bytes[0], gw.bytes[1], gw.bytes[2], 3);

    nic_set_rx_callback(net_handle_frame);

    uint8_t mac[6];
    if (!arp_resolve(gateway_ip, mac, 3000))
        kprintf("net: gateway did not answer ARP\n");
    net_up = true;

    kprintf("net: up, ip=");
    ipv4_print(our_ip);
    kprintf(" gw=");
    ipv4_print(gateway_ip);
    kprintf("\n");
}

int net_ping(Ipv4Addr dst, uint32_t timeout_ms, uint32_t *rtt_ms)
{
    static uint16_t seq = 0;

    IcmpHeader icmp;
    memset(&icmp, 0, sizeof(icmp));
    icmp.type = ICMP_TYPE_ECHO_REQUEST;
    icmp.code = 0;
    icmp.id = net_htons(0xBEEF);
    icmp.seq = net_htons(++seq);
    icmp.checksum = 0;
    icmp.checksum = net_htons(ip_checksum(&icmp, sizeof(icmp)));

    ping_reply_received = 0;
    ping_expected_id = 0xBEEF;
    ping_expected_seq = seq;

    ipv4_send(dst, IPV4_PROTO_ICMP, &icmp, sizeof(icmp));

    uint64_t max_spins = (uint64_t)timeout_ms * 50000;
    for (uint64_t spins = 0; spins < max_spins; spins++)
    {
        if (ping_reply_received)
        {
            if (rtt_ms)
                *rtt_ms = 0;
            return 1;
        }
        pause();
    }

    return 0;
}