#define INET_MAX_SOCKETS 32
#define TCP_RX_SIZE      (128 * 1024)
#define TCP_TX_SIZE      (64 * 1024)
#define TCP_MSS          1400
#define TCP_RTO_MIN_MS   300
#define TCP_RTO_MAX_MS   8000
#define TCP_MAX_RETRIES  10
#define TCP_CONNECT_MS   15000
#define UDP_QUEUE        16
#define UDP_MAX_DGRAM    1500

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct PACKED
{
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
}
TcpHeader;

typedef enum
{
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
    TCP_CLOSE_WAIT,
    TCP_DONE,
}
TcpState;

typedef enum
{
    INET_TCP,
    INET_UDP,
}
InetKind;

typedef struct
{
    uint16_t len;
    Ipv4Addr src_ip;
    uint16_t src_port;
    uint8_t  data[UDP_MAX_DGRAM];
}
UdpDatagram;

typedef struct InetSocket
{
    bool      used;
    InetKind  kind;
    uint32_t  flags;
    VNode     node;

    Ipv4Addr  remote_ip;
    uint16_t  remote_port;
    uint16_t  local_port;
    bool      connected;
    bool      orphan;

    TcpState  state;
    int       error;
    uint32_t  iss;
    uint32_t  snd_una;
    uint32_t  snd_nxt;
    uint32_t  snd_wnd;
    uint32_t  rcv_nxt;
    bool      fin_received;
    bool      close_requested;
    bool      fin_sent;
    uint32_t  fin_seq;

    uint8_t  *rx;
    size_t    rx_head;
    size_t    rx_count;

    uint8_t  *tx;
    size_t    tx_count;

    uint64_t  rto_deadline;
    uint32_t  rto_ms;
    int       retries;
    uint64_t  done_deadline;

    UdpDatagram *udp_queue;
    int       udp_head;
    int       udp_count;
}
InetSocket;

static InetSocket inet_sockets[INET_MAX_SOCKETS];
static VFileOps   inet_file_ops;
static uint16_t   inet_next_port = 49152;
static uint32_t   inet_seq_seed;

static uint16_t inet_alloc_port(void)
{
    for (int tries = 0; tries < 16384; tries++)
    {
        uint16_t p = inet_next_port++;
        if (inet_next_port < 49152)
            inet_next_port = 49152;
        bool taken = false;
        for (int i = 0; i < INET_MAX_SOCKETS; i++)
            if (inet_sockets[i].used && inet_sockets[i].local_port == p)
                taken = true;
        if (!taken)
            return p;
    }
    return 0;
}

static uint32_t inet_random(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    inet_seq_seed = inet_seq_seed * 1103515245u + 12345u + lo + (hi << 7);
    return inet_seq_seed;
}

static bool seq_lt(uint32_t a, uint32_t b)  { return (int32_t)(a - b) < 0; }
static bool seq_le(uint32_t a, uint32_t b)  { return (int32_t)(a - b) <= 0; }

static uint16_t tcp_checksum(Ipv4Addr src, Ipv4Addr dst, const uint8_t *seg, uint16_t len)
{
    uint32_t sum = 0;
    sum += ((uint32_t)src.bytes[0] << 8) | src.bytes[1];
    sum += ((uint32_t)src.bytes[2] << 8) | src.bytes[3];
    sum += ((uint32_t)dst.bytes[0] << 8) | dst.bytes[1];
    sum += ((uint32_t)dst.bytes[2] << 8) | dst.bytes[3];
    sum += IPV4_PROTO_TCP;
    sum += len;
    for (uint16_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)seg[i] << 8) | seg[i + 1];
    if (len & 1)
        sum += (uint32_t)seg[len - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t tcp_window(InetSocket *s)
{
    size_t space = TCP_RX_SIZE - s->rx_count;
    return (uint16_t)(space > 65535 ? 65535 : space);
}

static void tcp_send_segment(InetSocket *s, uint32_t seq, uint8_t flags, const uint8_t *data, uint16_t len)
{
    uint8_t buf[sizeof(TcpHeader) + 4 + TCP_MSS];
    TcpHeader *h = (TcpHeader *)buf;
    uint16_t hlen = sizeof(TcpHeader);

    memset(h, 0, sizeof(*h));
    h->src_port = net_htons(s->local_port);
    h->dst_port = net_htons(s->remote_port);
    h->seq = net_htonl(seq);
    h->ack = net_htonl((flags & TCP_ACK) ? s->rcv_nxt : 0);
    h->flags = flags;
    h->window = net_htons(tcp_window(s));

    if (flags & TCP_SYN)
    {
        uint8_t *opt = buf + hlen;
        opt[0] = 2;
        opt[1] = 4;
        opt[2] = (uint8_t)(TCP_MSS >> 8);
        opt[3] = (uint8_t)(TCP_MSS & 0xFF);
        hlen += 4;
    }
    h->offset = (uint8_t)((hlen / 4) << 4);

    if (len)
        memcpy(buf + hlen, data, len);

    uint16_t total = (uint16_t)(hlen + len);
    h->checksum = net_htons(tcp_checksum(net_local_ip(), s->remote_ip, buf, total));
    ipv4_send(s->remote_ip, IPV4_PROTO_TCP, buf, total);
}

static void tcp_send_ack(InetSocket *s)
{
    tcp_send_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
}

static void tcp_arm_timer(InetSocket *s)
{
    s->rto_deadline = task_uptime_ms() + s->rto_ms;
}

static void tcp_output(InetSocket *s)
{
    if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT && s->state != TCP_FIN_WAIT)
        return;

    uint32_t window = s->snd_wnd ? s->snd_wnd : 1;
    bool sent = false;

    for (;;)
    {
        uint32_t in_flight = s->snd_nxt - s->snd_una;
        if (s->fin_sent && in_flight > 0)
            in_flight--;
        if (in_flight >= s->tx_count || in_flight >= window)
            break;

        uint32_t len = (uint32_t)s->tx_count - in_flight;
        if (len > window - in_flight)
            len = window - in_flight;
        if (len > TCP_MSS)
            len = TCP_MSS;

        tcp_send_segment(s, s->snd_nxt, TCP_ACK | TCP_PSH, s->tx + in_flight, (uint16_t)len);
        s->snd_nxt += len;
        sent = true;
    }

    if (s->close_requested && !s->fin_sent && s->snd_nxt - s->snd_una == s->tx_count)
    {
        s->fin_seq = s->snd_nxt;
        tcp_send_segment(s, s->snd_nxt, TCP_ACK | TCP_FIN, NULL, 0);
        s->snd_nxt++;
        s->fin_sent = true;
        s->state = s->state == TCP_CLOSE_WAIT ? TCP_DONE : TCP_FIN_WAIT;
        sent = true;
    }

    if (sent && s->rto_deadline == 0)
        tcp_arm_timer(s);
}

static void inet_free(InetSocket *s)
{
    if (s->rx)
        pmm_free_pages(s->rx, TCP_RX_SIZE / PAGE_SIZE);
    if (s->tx)
        pmm_free_pages(s->tx, TCP_TX_SIZE / PAGE_SIZE);
    if (s->udp_queue)
        kfree(s->udp_queue);
    memset(s, 0, sizeof(*s));
}

static void tcp_reset(InetSocket *s, int error)
{
    s->state = TCP_CLOSED;
    s->error = error;
    s->rto_deadline = 0;
    if (s->orphan)
        inet_free(s);
    task_wake_waiters();
}

static InetSocket *inet_find_tcp(Ipv4Addr ip, uint16_t remote_port, uint16_t local_port)
{
    for (int i = 0; i < INET_MAX_SOCKETS; i++)
    {
        InetSocket *s = &inet_sockets[i];
        if (s->used && s->kind == INET_TCP && s->local_port == local_port && s->remote_port == remote_port &&
            s->remote_ip.as_u32 == ip.as_u32)
            return s;
    }
    return NULL;
}

void inet_tcp_deliver(Ipv4Addr src_ip, const uint8_t *data, uint16_t len)
{
    if (len < sizeof(TcpHeader))
        return;

    const TcpHeader *h = (const TcpHeader *)data;
    uint16_t hlen = (uint16_t)((h->offset >> 4) * 4);
    if (hlen < sizeof(TcpHeader) || hlen > len)
        return;

    InetSocket *s = inet_find_tcp(src_ip, net_htons(h->src_port), net_htons(h->dst_port));
    if (!s)
        return;

    uint8_t flags = h->flags;
    uint32_t seq = net_htonl(h->seq);
    uint32_t ack = net_htonl(h->ack);
    const uint8_t *payload = data + hlen;
    uint32_t plen = len - hlen;

    if (flags & TCP_RST)
    {
        tcp_reset(s, s->state == TCP_SYN_SENT ? -ECONNREFUSED : -EPIPE);
        return;
    }

    if (s->state == TCP_SYN_SENT)
    {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == s->iss + 1)
        {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->snd_wnd = net_htons(h->window);
            s->state = TCP_ESTABLISHED;
            s->rto_deadline = 0;
            s->retries = 0;
            tcp_send_ack(s);
            task_wake_waiters();
        }
        return;
    }

    if (s->state == TCP_CLOSED)
        return;

    if (flags & TCP_ACK)
    {
        if (seq_lt(s->snd_una, ack) && seq_le(ack, s->snd_nxt))
        {
            uint32_t acked = ack - s->snd_una;
            uint32_t data_acked = acked;
            if (s->fin_sent && seq_lt(s->fin_seq, ack))
                data_acked--;
            if (data_acked > s->tx_count)
                data_acked = (uint32_t)s->tx_count;
            memmove(s->tx, s->tx + data_acked, s->tx_count - data_acked);
            s->tx_count -= data_acked;
            s->snd_una = ack;
            s->retries = 0;
            s->rto_ms = TCP_RTO_MIN_MS * 2;
            s->rto_deadline = s->snd_una == s->snd_nxt ? 0 : task_uptime_ms() + s->rto_ms;
            task_wake_waiters();
        }
        s->snd_wnd = net_htons(h->window);
    }

    bool need_ack = false;

    if (plen > 0)
    {
        if (seq == s->rcv_nxt && !s->fin_received)
        {
            size_t space = TCP_RX_SIZE - s->rx_count;
            size_t n = plen < space ? plen : space;
            size_t tail = (s->rx_head + s->rx_count) % TCP_RX_SIZE;
            size_t first = n < TCP_RX_SIZE - tail ? n : TCP_RX_SIZE - tail;
            memcpy(s->rx + tail, payload, first);
            memcpy(s->rx, payload + first, n - first);
            s->rx_count += n;
            s->rcv_nxt += (uint32_t)n;
            task_wake_waiters();
        }
        need_ack = true;
    }

    if ((flags & TCP_FIN) && seq + plen == s->rcv_nxt && !s->fin_received)
    {
        s->rcv_nxt++;
        s->fin_received = true;
        if (s->state == TCP_ESTABLISHED)
            s->state = TCP_CLOSE_WAIT;
        else if (s->state == TCP_FIN_WAIT)
            s->state = TCP_DONE;
        need_ack = true;
        task_wake_waiters();
    }

    if (need_ack)
        tcp_send_ack(s);

    if (s->state == TCP_DONE && s->snd_una == s->snd_nxt && s->orphan)
    {
        inet_free(s);
        return;
    }

    tcp_output(s);
}

void inet_udp_deliver(Ipv4Addr src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len)
{
    for (int i = 0; i < INET_MAX_SOCKETS; i++)
    {
        InetSocket *s = &inet_sockets[i];
        if (!s->used || s->kind != INET_UDP || s->local_port != dst_port)
            continue;
        if (s->connected && (s->remote_ip.as_u32 != src_ip.as_u32 || s->remote_port != src_port))
            continue;
        if (s->udp_count == UDP_QUEUE)
            return;

        UdpDatagram *d = &s->udp_queue[(s->udp_head + s->udp_count) % UDP_QUEUE];
        d->len = len > UDP_MAX_DGRAM ? UDP_MAX_DGRAM : len;
        d->src_ip = src_ip;
        d->src_port = src_port;
        memcpy(d->data, data, d->len);
        s->udp_count++;
        task_wake_waiters();
        return;
    }
}

void inet_tick(void)
{
    uint64_t now = task_uptime_ms();

    for (int i = 0; i < INET_MAX_SOCKETS; i++)
    {
        InetSocket *s = &inet_sockets[i];
        if (!s->used || s->kind != INET_TCP)
            continue;

        if (s->orphan && s->done_deadline && now >= s->done_deadline)
        {
            inet_free(s);
            continue;
        }

        if (!s->rto_deadline || now < s->rto_deadline)
            continue;

        if (++s->retries > TCP_MAX_RETRIES)
        {
            tcp_reset(s, s->state == TCP_SYN_SENT ? -ETIMEDOUT : -EPIPE);
            continue;
        }

        s->rto_ms = s->rto_ms * 2 > TCP_RTO_MAX_MS ? TCP_RTO_MAX_MS : s->rto_ms * 2;
        s->rto_deadline = now + s->rto_ms;

        if (s->state == TCP_SYN_SENT)
        {
            tcp_send_segment(s, s->iss, TCP_SYN, NULL, 0);
            continue;
        }

        s->snd_nxt = s->snd_una;
        bool fin_was_sent = s->fin_sent;
        s->fin_sent = false;
        if (fin_was_sent && s->state == TCP_FIN_WAIT)
            s->state = TCP_ESTABLISHED;
        else if (fin_was_sent && s->state == TCP_DONE)
            s->state = TCP_CLOSE_WAIT;
        if (s->tx_count == 0 && !s->close_requested)
            tcp_send_segment(s, s->snd_nxt, TCP_ACK, NULL, 0);
        tcp_output(s);
    }
}

static InetSocket *inet_from_file(VFile *file)
{
    if (!file || !file->node || file->node->file_ops != &inet_file_ops)
        return NULL;
    return (InetSocket *)file->node->private_data;
}

static ssize_t inet_read(VFile *file, void *buf, size_t count)
{
    InetSocket *s = inet_from_file(file);
    if (!s)
        return -EBADF;

    uint64_t flags = irq_save();
    for (;;)
    {
        if (s->kind == INET_UDP)
        {
            if (s->udp_count > 0)
            {
                UdpDatagram *d = &s->udp_queue[s->udp_head];
                size_t n = count < d->len ? count : d->len;
                memcpy(buf, d->data, n);
                s->udp_head = (s->udp_head + 1) % UDP_QUEUE;
                s->udp_count--;
                irq_restore(flags);
                return (ssize_t)n;
            }
        }
        else
        {
            if (s->rx_count > 0)
            {
                size_t n = count < s->rx_count ? count : s->rx_count;
                size_t first = n < TCP_RX_SIZE - s->rx_head ? n : TCP_RX_SIZE - s->rx_head;
                memcpy(buf, s->rx + s->rx_head, first);
                memcpy((uint8_t *)buf + first, s->rx, n - first);
                bool window_was_small = tcp_window(s) < TCP_MSS * 2;
                s->rx_head = (s->rx_head + n) % TCP_RX_SIZE;
                s->rx_count -= n;
                if (window_was_small && s->state != TCP_CLOSED)
                    tcp_send_ack(s);
                irq_restore(flags);
                return (ssize_t)n;
            }
            if (s->fin_received || s->state == TCP_CLOSED)
            {
                int err = s->state == TCP_CLOSED && !s->fin_received ? s->error : 0;
                irq_restore(flags);
                return err;
            }
        }

        if (s->flags & SOCK_NONBLOCK)
        {
            irq_restore(flags);
            return -EAGAIN;
        }
        task_block(TASK_BLOCK_FOREVER);
    }
}

static ssize_t inet_write(VFile *file, const void *buf, size_t count)
{
    InetSocket *s = inet_from_file(file);
    if (!s)
        return -EBADF;

    if (s->kind == INET_UDP)
    {
        if (!s->connected)
            return -EINVAL;
        size_t n = count > UDP_MAX_DGRAM - 28 ? UDP_MAX_DGRAM - 28 : count;
        uint64_t flags = irq_save();
        udp_send(s->remote_ip, s->local_port, s->remote_port, buf, (uint16_t)n);
        irq_restore(flags);
        return (ssize_t)n;
    }

    const uint8_t *src = buf;
    size_t written = 0;
    uint64_t flags = irq_save();
    while (written < count)
    {
        if (s->state == TCP_SYN_SENT)
        {
            if (s->flags & SOCK_NONBLOCK)
                break;
            task_block(TASK_BLOCK_FOREVER);
            continue;
        }
        if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT)
        {
            irq_restore(flags);
            return written ? (ssize_t)written : (s->error ? s->error : -EPIPE);
        }

        size_t space = TCP_TX_SIZE - s->tx_count;
        if (space == 0)
        {
            if (s->flags & SOCK_NONBLOCK)
                break;
            task_block(TASK_BLOCK_FOREVER);
            continue;
        }

        size_t n = count - written < space ? count - written : space;
        memcpy(s->tx + s->tx_count, src + written, n);
        s->tx_count += n;
        written += n;
        tcp_output(s);
    }
    irq_restore(flags);
    return written ? (ssize_t)written : -EAGAIN;
}

static int inet_poll(VFile *file)
{
    InetSocket *s = inet_from_file(file);
    if (!s)
        return POLLNVAL;

    int r = 0;
    if (s->kind == INET_UDP)
    {
        if (s->udp_count)
            r |= POLLIN;
        r |= POLLOUT;
        return r;
    }

    if (s->rx_count > 0 || s->fin_received)
        r |= POLLIN;
    if (s->state == TCP_CLOSED && s->connected)
        r |= POLLIN | POLLHUP;
    if ((s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) && s->tx_count < TCP_TX_SIZE)
        r |= POLLOUT;
    return r;
}

static int inet_close(VFile *file)
{
    InetSocket *s = inet_from_file(file);
    if (!s)
        return -1;

    uint64_t flags = irq_save();
    if (s->kind == INET_TCP && (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT))
    {
        s->orphan = true;
        s->close_requested = true;
        s->done_deadline = task_uptime_ms() + 10000;
        tcp_output(s);
    }
    else if (s->kind == INET_TCP && s->state == TCP_SYN_SENT)
    {
        tcp_send_segment(s, s->snd_nxt, TCP_RST, NULL, 0);
        inet_free(s);
    }
    else
    {
        inet_free(s);
    }
    irq_restore(flags);
    return 0;
}

static bool inet_parse_addr(const char *name, Ipv4Addr *ip, uint16_t *port)
{
    uint32_t parts[5];
    int n = 0;
    uint32_t v = 0;
    bool digit = false;

    for (const char *p = name;; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            v = v * 10 + (uint32_t)(*p - '0');
            digit = true;
            if (v > 65535)
                return false;
        }
        else if ((*p == '.' && n < 3) || (*p == ':' && n == 3) || (*p == '\0' && n == 4))
        {
            if (!digit)
                return false;
            parts[n++] = v;
            v = 0;
            digit = false;
            if (*p == '\0')
                break;
        }
        else
        {
            return false;
        }
    }

    for (int i = 0; i < 4; i++)
        if (parts[i] > 255)
            return false;
    *ip = ipv4_from_bytes((uint8_t)parts[0], (uint8_t)parts[1], (uint8_t)parts[2], (uint8_t)parts[3]);
    *port = (uint16_t)parts[4];
    return true;
}

bool inet_is_socket(VFile *file)
{
    return inet_from_file(file) != NULL;
}

int64_t inet_socket_create(uint32_t flags)
{
    uint64_t irq = irq_save();
    InetSocket *s = NULL;
    for (int i = 0; i < INET_MAX_SOCKETS && !s; i++)
        if (!inet_sockets[i].used)
            s = &inet_sockets[i];
    if (!s)
    {
        irq_restore(irq);
        return -EMFILE;
    }
    memset(s, 0, sizeof(*s));
    s->used = true;
    irq_restore(irq);

    s->kind = (flags & SOCK_UDP) ? INET_UDP : INET_TCP;
    s->flags = flags & SOCK_NONBLOCK;
    s->node.type = VNODE_SOCKET;
    s->node.file_ops = &inet_file_ops;
    s->node.private_data = s;
    strncpy(s->node.name, s->kind == INET_UDP ? "udp" : "tcp", VFS_NAME_MAX - 1);

    if (s->kind == INET_UDP)
    {
        s->udp_queue = kcalloc(UDP_QUEUE, sizeof(UdpDatagram));
        s->local_port = inet_alloc_port();
        if (!s->udp_queue || !s->local_port)
        {
            inet_free(s);
            return -ENOMEM;
        }
    }
    else
    {
        s->rx = pmm_alloc_pages(TCP_RX_SIZE / PAGE_SIZE);
        s->tx = pmm_alloc_pages(TCP_TX_SIZE / PAGE_SIZE);
        if (!s->rx || !s->tx)
        {
            inet_free(s);
            return -ENOMEM;
        }
    }

    int fd = fd_alloc();
    if (fd == FD_INVALID)
    {
        inet_free(s);
        return -EMFILE;
    }
    fd_table[fd].file.node = &s->node;
    fd_table[fd].file.flags = VFS_O_RDWR;
    return fd;
}

int64_t inet_connect(VFile *file, const char *name)
{
    InetSocket *s = inet_from_file(file);
    if (!s || !name)
        return -EBADF;
    if (!net_is_up())
        return -ENETDOWN;

    Ipv4Addr ip;
    uint16_t port;
    if (!inet_parse_addr(name, &ip, &port) || port == 0)
        return -EINVAL;

    if (s->kind == INET_UDP)
    {
        s->remote_ip = ip;
        s->remote_port = port;
        s->connected = true;
        return 0;
    }

    if (s->state != TCP_CLOSED || s->connected)
        return -EINVAL;

    uint64_t flags = irq_save();
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = inet_alloc_port();
    s->iss = inet_random();
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->rto_ms = 1000;
    s->state = TCP_SYN_SENT;
    s->connected = true;
    tcp_send_segment(s, s->iss, TCP_SYN, NULL, 0);
    tcp_arm_timer(s);

    if (s->flags & SOCK_NONBLOCK)
    {
        irq_restore(flags);
        return -EINPROGRESS;
    }

    uint64_t start = task_uptime_ms();
    while (s->state == TCP_SYN_SENT)
    {
        uint64_t elapsed = task_uptime_ms() - start;
        if (elapsed >= TCP_CONNECT_MS)
        {
            tcp_send_segment(s, s->snd_nxt, TCP_RST, NULL, 0);
            s->state = TCP_CLOSED;
            s->rto_deadline = 0;
            irq_restore(flags);
            return -ETIMEDOUT;
        }
        task_block((uint32_t)(TCP_CONNECT_MS - elapsed));
    }

    int64_t rc = s->state == TCP_ESTABLISHED ? 0 : (s->error ? s->error : -ECONNREFUSED);
    irq_restore(flags);
    return rc;
}

void inet_init(void)
{
    inet_file_ops.read  = inet_read;
    inet_file_ops.write = inet_write;
    inet_file_ops.close = inet_close;
    inet_file_ops.poll  = inet_poll;
    inet_seq_seed = (uint32_t)task_uptime_ms() * 2654435761u;
}
