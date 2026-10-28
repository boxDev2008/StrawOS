typedef struct PACKED
{
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
}
EthHeader;

#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

typedef union
{
    uint8_t  bytes[4];
    uint32_t as_u32;
}
Ipv4Addr;

static inline Ipv4Addr ipv4_from_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    Ipv4Addr ip;
    ip.bytes[0] = a; ip.bytes[1] = b; ip.bytes[2] = c; ip.bytes[3] = d;
    return ip;
}

uint16_t net_htons(uint16_t v);
uint32_t net_htonl(uint32_t v);

void net_init(Ipv4Addr our_ip, Ipv4Addr gateway_ip, Ipv4Addr netmask);

int net_dhcp_init(uint32_t timeout_ms, Ipv4Addr *ip_out, Ipv4Addr *gw_out, Ipv4Addr *mask_out);

int net_ping(Ipv4Addr dst, uint32_t timeout_ms, uint32_t *rtt_ms);

void ipv4_print(Ipv4Addr ip);
void ipv4_send(Ipv4Addr dst, uint8_t protocol, const void *payload, uint16_t payload_len);
void udp_send(Ipv4Addr dst, uint16_t src_port, uint16_t dst_port, const void *payload, uint16_t payload_len);
Ipv4Addr net_local_ip(void);
bool net_is_up(void);
void net_loopback_poll(void);
void net_status(Ipv4Addr *ip, Ipv4Addr *gw, Ipv4Addr *mask, Ipv4Addr *dns);

void inet_init(void);
void inet_tick(void);
bool inet_is_socket(VFile *file);
int64_t inet_socket_create(uint32_t flags);
int64_t inet_connect(VFile *file, const char *name);
int64_t inet_bind(VFile *file, const char *name);
int64_t inet_listen(VFile *file);
int64_t inet_accept(VFile *file);
void inet_udp_deliver(Ipv4Addr src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len);
void inet_tcp_deliver(Ipv4Addr src_ip, const uint8_t *data, uint16_t len);