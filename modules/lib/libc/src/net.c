#define DNS_CACHE 16

static struct
{
    char     host[128];
    uint8_t  ip[4];
    uint64_t expires;
}
dns_cache[DNS_CACHE];
static int dns_cache_next;

static bool parse_ip(const char *s, uint8_t out[4])
{
    int n = 0;
    unsigned v = 0;
    bool digit = false;
    for (;; s++)
    {
        if (*s >= '0' && *s <= '9')
        {
            v = v * 10 + (unsigned)(*s - '0');
            digit = true;
            if (v > 255)
                return false;
        }
        else if ((*s == '.' && n < 3) || (*s == '\0' && n == 3))
        {
            if (!digit)
                return false;
            out[n++] = (uint8_t)v;
            v = 0;
            digit = false;
            if (*s == '\0')
                return true;
        }
        else
        {
            return false;
        }
    }
}

_Static_assert(sizeof(struct net_info) == 17, "net_info must match the kernel's NetInfo");

int net_info(struct net_info *out)
{
    memset(out, 0, sizeof(*out));
    if (read_device("/dev/net", out, sizeof(*out)) != sizeof(*out))
        return -1;
    return 0;
}

static size_t dns_build_query(uint8_t *buf, uint16_t id, const char *host)
{
    memset(buf, 0, 12);
    buf[0] = (uint8_t)(id >> 8);
    buf[1] = (uint8_t)id;
    buf[2] = 0x01;
    buf[5] = 1;

    size_t pos = 12;
    const char *label = host;
    while (*label)
    {
        const char *dot = strchr(label, '.');
        size_t len = dot ? (size_t)(dot - label) : strlen(label);
        if (len == 0 || len > 63 || pos + len + 6 > 512)
            return 0;
        buf[pos++] = (uint8_t)len;
        memcpy(buf + pos, label, len);
        pos += len;
        label += len;
        if (*label == '.')
            label++;
    }
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = 1;
    buf[pos++] = 0;
    buf[pos++] = 1;
    return pos;
}

static size_t dns_skip_name(const uint8_t *buf, size_t len, size_t pos)
{
    while (pos < len)
    {
        uint8_t l = buf[pos];
        if (l == 0)
            return pos + 1;
        if ((l & 0xC0) == 0xC0)
            return pos + 2;
        pos += (size_t)l + 1;
    }
    return len + 1;
}

static int dns_parse(const uint8_t *buf, size_t len, uint16_t id, uint8_t ip[4], uint32_t *ttl)
{
    if (len < 12 || ((buf[0] << 8) | buf[1]) != id || !(buf[2] & 0x80))
        return 1;
    if ((buf[3] & 0x0F) != 0)
        return -1;

    int qd = (buf[4] << 8) | buf[5];
    int an = (buf[6] << 8) | buf[7];
    size_t pos = 12;
    for (int i = 0; i < qd; i++)
        pos = dns_skip_name(buf, len, pos) + 4;

    for (int i = 0; i < an && pos < len; i++)
    {
        pos = dns_skip_name(buf, len, pos);
        if (pos + 10 > len)
            break;
        uint16_t type = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        uint32_t t = ((uint32_t)buf[pos + 4] << 24) | ((uint32_t)buf[pos + 5] << 16) |
                     ((uint32_t)buf[pos + 6] << 8) | buf[pos + 7];
        uint16_t rdlen = (uint16_t)((buf[pos + 8] << 8) | buf[pos + 9]);
        pos += 10;
        if (type == 1 && rdlen == 4 && pos + 4 <= len)
        {
            memcpy(ip, buf + pos, 4);
            *ttl = t;
            return 0;
        }
        pos += rdlen;
    }
    return -1;
}

int net_resolve(const char *host, uint8_t ip[4])
{
    if (!host || !*host)
        return -1;
    if (parse_ip(host, ip))
        return 0;
    if (strcmp(host, "localhost") == 0)
    {
        ip[0] = 127; ip[1] = 0; ip[2] = 0; ip[3] = 1;
        return 0;
    }

    uint64_t now = uptime_ms();
    for (int i = 0; i < DNS_CACHE; i++)
    {
        if (dns_cache[i].expires > now && strcmp(dns_cache[i].host, host) == 0)
        {
            memcpy(ip, dns_cache[i].ip, 4);
            return 0;
        }
    }

    struct net_info info;
    if (net_info(&info) != 0 || !info.up)
        return -ENETDOWN;

    char server[32];
    snprintf(server, sizeof(server), "%u.%u.%u.%u:53", info.dns[0], info.dns[1], info.dns[2], info.dns[3]);

    int fd = socket(SOCK_UDP);
    if (fd < 0)
        return fd;
    if (connect(fd, server) != 0)
    {
        close(fd);
        return -1;
    }

    uint8_t query[512], reply[1500];
    uint16_t id = (uint16_t)(uptime_ms() * 2654435761u >> 7);
    size_t qlen = dns_build_query(query, id, host);
    if (!qlen)
    {
        close(fd);
        return -1;
    }

    int result = -ETIMEDOUT;
    for (int attempt = 0; attempt < 3 && result == -ETIMEDOUT; attempt++)
    {
        write(fd, query, qlen);
        uint64_t deadline = uptime_ms() + (attempt == 0 ? 400 : 1500 * attempt);
        while (uptime_ms() < deadline)
        {
            struct pollfd pfd = { fd, POLLIN, 0 };
            if (poll(&pfd, 1, (int64_t)(deadline - uptime_ms())) <= 0)
                continue;
            ssize_t n = read(fd, reply, sizeof(reply));
            if (n <= 0)
                continue;
            uint32_t ttl = 60;
            int rc = dns_parse(reply, (size_t)n, id, ip, &ttl);
            if (rc == 1)
                continue;
            result = rc == 0 ? 0 : -1;
            if (rc == 0)
            {
                int slot = dns_cache_next++ % DNS_CACHE;
                strncpy(dns_cache[slot].host, host, sizeof(dns_cache[slot].host) - 1);
                memcpy(dns_cache[slot].ip, ip, 4);
                dns_cache[slot].expires = uptime_ms() + (uint64_t)(ttl < 30 ? 30 : ttl) * 1000;
            }
            break;
        }
    }

    close(fd);
    return result;
}

int net_connect(const char *host, uint16_t port)
{
    uint8_t ip[4];
    int rc = net_resolve(host, ip);
    if (rc != 0)
        return rc < 0 ? rc : -1;

    int fd = socket(SOCK_TCP);
    if (fd < 0)
        return fd;

    char addr[32];
    snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
    rc = connect(fd, addr);
    if (rc != 0)
    {
        close(fd);
        return rc;
    }
    return fd;
}
