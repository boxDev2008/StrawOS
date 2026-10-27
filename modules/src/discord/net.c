#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <socket.h>
#include <net.h>

#include "tls.h"
#include "http.h"
#include "discord.h"

#define CONNECT_TIMEOUT_MS 20000
#define REQUEST_TIMEOUT_MS 45000
#define MAX_HOSTS          16
#define MAX_SLOTS          4
#define MAX_BODY           (64 * 1024 * 1024)

typedef enum
{
    C_CONNECTING,
    C_HANDSHAKE,
    C_OPEN,
    C_CLOSED,
    C_ERROR,
}
ConnState;

struct Conn
{
    int       fd;
    TlsConn  *tls;
    bool      use_tls;
    ConnState state;
    Buf       out;
    char      host[256];
    char      error[160];
    uint64_t  started;
};

static void conn_fail(Conn *c, const char *msg)
{
    if (c->state == C_ERROR)
        return;
    c->state = C_ERROR;
    snprintf(c->error, sizeof(c->error), "%s", msg);
}

typedef struct
{
    char     host[128];
    uint8_t  ip[4];
    uint64_t when;
}
DnsEntry;

static DnsEntry dns_cache[16];

static bool resolve_cached(const char *host, uint8_t ip[4])
{
    uint64_t now = uptime_ms();
    DnsEntry *slot = &dns_cache[0];
    for (int i = 0; i < 16; i++)
    {
        DnsEntry *e = &dns_cache[i];
        if (e->host[0] && strcmp(e->host, host) == 0 && now - e->when < 600000)
        {
            memcpy(ip, e->ip, 4);
            return true;
        }
        if (e->when < slot->when)
            slot = e;
    }
    if (net_resolve(host, ip) != 0)
        return false;
    snprintf(slot->host, sizeof(slot->host), "%s", host);
    memcpy(slot->ip, ip, 4);
    slot->when = now ? now : 1;
    return true;
}

Conn *conn_open(const char *host, uint16_t port, bool use_tls)
{
    Conn *c = calloc(1, sizeof(Conn));
    if (!c)
        return NULL;
    c->fd = -1;
    c->use_tls = use_tls;
    c->started = uptime_ms();
    snprintf(c->host, sizeof(c->host), "%s", host);

    uint8_t ip[4];
    if (!resolve_cached(host, ip))
    {
        conn_fail(c, "Couldn't reach the server. Check your network connection.");
        return c;
    }
    c->fd = socket(SOCK_TCP | SOCK_NONBLOCK);
    if (c->fd < 0)
    {
        conn_fail(c, "The network isn't available.");
        return c;
    }
    char target[64];
    snprintf(target, sizeof(target), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
    int rc = connect(c->fd, target);
    if (rc != 0 && rc != -EINPROGRESS)
    {
        conn_fail(c, "The server refused the connection.");
        return c;
    }
    c->state = C_CONNECTING;
    return c;
}

static bool conn_flush(Conn *c)
{
    while (c->out.len > 0)
    {
        int n;
        if (c->tls)
        {
            n = tls_send(c->tls, c->out.data, c->out.len);
        }
        else
        {
            ssize_t w = write(c->fd, c->out.data, c->out.len);
            n = w == -EAGAIN ? 0 : (int)w;
        }
        if (n < 0)
        {
            conn_fail(c, "The connection was lost.");
            return false;
        }
        if (n == 0)
            break;
        buf_consume(&c->out, (size_t)n);
    }
    return true;
}

int conn_step(Conn *c)
{
    switch (c->state)
    {
        case C_CONNECTING:
        {
            struct pollfd pfd = { c->fd, POLLOUT, 0 };
            poll(&pfd, 1, 0);
            if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            {
                conn_fail(c, "The server refused the connection.");
                return -1;
            }
            if (!(pfd.revents & POLLOUT))
            {
                if (uptime_ms() - c->started > CONNECT_TIMEOUT_MS)
                {
                    conn_fail(c, "The server took too long to respond.");
                    return -1;
                }
                return 0;
            }
            if (!c->use_tls)
            {
                c->state = C_OPEN;
                return conn_step(c);
            }
            char err[160];
            c->tls = tls_start(c->fd, c->host, err, sizeof(err));
            if (!c->tls)
            {
                conn_fail(c, err);
                return -1;
            }
            c->state = C_HANDSHAKE;
        }
        __attribute__((fallthrough));

        case C_HANDSHAKE:
        {
            int rc = tls_pump(c->tls);
            if (rc < 0)
            {
                char err[160];
                tls_error(c->tls, err, sizeof(err));
                conn_fail(c, err);
                return -1;
            }
            if (!tls_ready(c->tls))
            {
                if (uptime_ms() - c->started > CONNECT_TIMEOUT_MS)
                {
                    conn_fail(c, "The secure connection took too long to set up.");
                    return -1;
                }
                return rc;
            }
            c->state = C_OPEN;
        }
        __attribute__((fallthrough));

        case C_OPEN:
            if (!conn_flush(c))
                return -1;
            if (c->tls && tls_pump(c->tls) < 0 && !tls_pending(c->tls))
            {
                c->state = C_CLOSED;
                return -1;
            }
            if (!conn_flush(c))
                return -1;
            return 1;

        default:
            return -1;
    }
}

bool conn_open_ready(Conn *c)
{
    return c->state == C_OPEN;
}

bool conn_failed(Conn *c)
{
    return c->state == C_ERROR || c->state == C_CLOSED;
}

void conn_write(Conn *c, const void *data, size_t len)
{
    buf_append(&c->out, data, len);
    if (c->state == C_OPEN)
        conn_flush(c);
}

int conn_read(Conn *c, void *buf, size_t len)
{
    if (c->state == C_ERROR)
        return -2;
    if (c->state != C_OPEN && c->state != C_CLOSED)
        return 0;
    if (c->tls)
    {
        if (!tls_pending(c->tls))
            tls_pump(c->tls);
        int n = tls_recv(c->tls, buf, len);
        if (n == -1)
            c->state = C_CLOSED;
        return n;
    }
    if (c->state == C_CLOSED)
        return -1;
    ssize_t n = read(c->fd, buf, len);
    if (n == -EAGAIN)
        return 0;
    if (n == 0)
    {
        c->state = C_CLOSED;
        return -1;
    }
    return n < 0 ? -2 : (int)n;
}

const char *conn_error(Conn *c)
{
    if (c->state == C_CLOSED && !c->error[0])
        return "The connection was closed.";
    return c->error[0] ? c->error : "The connection failed.";
}

void conn_close(Conn *c)
{
    if (!c)
        return;
    if (c->tls)
        tls_free(c->tls);
    if (c->fd >= 0)
        close(c->fd);
    buf_free(&c->out);
    free(c);
}

int conn_fd(Conn *c)
{
    return c->fd;
}

short conn_poll_events(Conn *c)
{
    if (c->state == C_CONNECTING)
        return POLLOUT;
    return (short)(POLLIN | (c->out.len ? POLLOUT : 0));
}

bool conn_has_buffered(Conn *c)
{
    return c->tls && c->state == C_OPEN && tls_pending(c->tls);
}

typedef struct Req Req;

struct Req
{
    char     method[8];
    char    *path;
    char    *headers;
    char    *body;
    size_t   body_len;
    HttpDone done;
    void    *user;
    int      retries;
    uint64_t not_before;
    Req     *next;
};

typedef struct
{
    Conn    *conn;
    Req     *req;
    Buf      in;
    bool     reused;
    bool     have_headers;
    size_t   header_len;
    int      status;
    long     content_length;
    bool     chunked;
    bool     close_after;
    uint64_t last_activity;
}
Slot;

typedef struct
{
    char     host[256];
    uint16_t port;
    bool     tls;
    int      max_slots;
    Slot     slots[MAX_SLOTS];
    Req     *head;
    Req     *tail;
}
Host;

static Host hosts[MAX_HOSTS];
static int  host_count;

static Host *host_for(const char *name, uint16_t port, bool tls)
{
    for (int i = 0; i < host_count; i++)
        if (hosts[i].port == port && hosts[i].tls == tls && strcmp(hosts[i].host, name) == 0)
            return &hosts[i];
    if (host_count == MAX_HOSTS)
        return NULL;
    Host *h = &hosts[host_count++];
    memset(h, 0, sizeof(*h));
    snprintf(h->host, sizeof(h->host), "%s", name);
    h->port = port;
    h->tls = tls;
    h->max_slots = strstr(name, "discord.com") ? 2 : MAX_SLOTS;
    return h;
}

static void req_free(Req *r)
{
    free(r->path);
    free(r->headers);
    free(r->body);
    free(r);
}

static void enqueue(Host *h, Req *r, bool front)
{
    r->next = NULL;
    if (front)
    {
        r->next = h->head;
        h->head = r;
        if (!h->tail)
            h->tail = r;
        return;
    }
    if (h->tail)
        h->tail->next = r;
    else
        h->head = r;
    h->tail = r;
}

bool http_request(const char *method, const char *url, const char *headers,
                  const char *body, size_t body_len, HttpDone done, void *user)
{
    Url u;
    if (!url_parse(url, &u))
        return false;
    bool tls = strcmp(u.scheme, "https") == 0;
    if (!tls && strcmp(u.scheme, "http") != 0)
        return false;
    Host *h = host_for(u.host, u.port, tls);
    if (!h)
        return false;
    Req *r = calloc(1, sizeof(Req));
    if (!r)
        return false;
    snprintf(r->method, sizeof(r->method), "%s", method);
    r->path = xstrdup(u.path);
    r->headers = xstrdup(headers ? headers : "");
    if (body && body_len)
    {
        r->body = malloc(body_len);
        if (r->body)
            memcpy(r->body, body, body_len);
        r->body_len = body_len;
    }
    r->done = done;
    r->user = user;
    enqueue(h, r, false);
    return true;
}

void http_cancel(void *user)
{
    for (int i = 0; i < host_count; i++)
    {
        Host *h = &hosts[i];
        Req **pp = &h->head;
        h->tail = NULL;
        while (*pp)
        {
            Req *r = *pp;
            if (r->user == user)
            {
                *pp = r->next;
                req_free(r);
                continue;
            }
            h->tail = r;
            pp = &r->next;
        }
        for (int s = 0; s < MAX_SLOTS; s++)
            if (h->slots[s].req && h->slots[s].req->user == user)
                h->slots[s].req->done = NULL;
    }
}

static void slot_drop_conn(Slot *s)
{
    conn_close(s->conn);
    s->conn = NULL;
    s->reused = false;
}

static void slot_reset_response(Slot *s)
{
    buf_free(&s->in);
    s->have_headers = false;
    s->header_len = 0;
    s->status = 0;
    s->content_length = -1;
    s->chunked = false;
    s->close_after = false;
}

static void finish(Host *h, Slot *s, int status, const uint8_t *body, size_t len)
{
    Req *r = s->req;
    s->req = NULL;
    if (status == 429 && r->retries < 5)
    {
        double wait = 1.0;
        JsonDoc *doc = json_parse((const char *)body, len);
        Json *ra = doc ? json_get(json_root(doc), "retry_after") : NULL;
        if (ra && ra->type == JSON_NUMBER)
            wait = ra->num;
        json_free(doc);
        r->retries++;
        r->not_before = uptime_ms() + (uint64_t)(wait * 1000.0) + 50;
        enqueue(h, r, true);
        return;
    }
    if (r->done)
        r->done(status, body, len, r->user);
    req_free(r);
}

static void fail(Host *h, Slot *s, const char *why)
{
    (void)why;
    Req *r = s->req;
    bool retry = r && s->reused && s->in.len == 0 && r->retries < 2;
    slot_drop_conn(s);
    if (!r)
        return;
    if (retry)
    {
        s->req = NULL;
        r->retries++;
        enqueue(h, r, true);
        slot_reset_response(s);
        return;
    }
    slot_reset_response(s);
    finish(h, s, 0, (const uint8_t *)why, why ? strlen(why) : 0);
}

static void send_request(Host *h, Slot *s)
{
    Buf b = { 0 };
    buf_printf(&b, "%s %s HTTP/1.1\r\nHost: %s\r\n", s->req->method, s->req->path, h->host);
    buf_puts(&b, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\r\n");
    buf_puts(&b, "Accept: */*\r\nConnection: keep-alive\r\n");
    if (s->req->body_len || strcmp(s->req->method, "GET") != 0)
        buf_printf(&b, "Content-Length: %zu\r\n", s->req->body_len);
    buf_puts(&b, s->req->headers);
    buf_puts(&b, "\r\n");
    if (s->req->body_len)
        buf_append(&b, s->req->body, s->req->body_len);
    conn_write(s->conn, b.data, b.len);
    buf_free(&b);
}

static bool parse_headers(Slot *s)
{
    char *end = strstr(s->in.data, "\r\n\r\n");
    if (!end)
        return false;
    s->header_len = (size_t)(end - s->in.data) + 4;
    s->have_headers = true;
    s->content_length = -1;
    const char *sp = strchr(s->in.data, ' ');
    s->status = sp ? atoi(sp + 1) : 0;
    char *line = strstr(s->in.data, "\r\n");
    while (line && line < end)
    {
        line += 2;
        if (strncasecmp(line, "content-length:", 15) == 0)
            s->content_length = atol(line + 15);
        else if (strncasecmp(line, "transfer-encoding:", 18) == 0)
        {
            char *e = strstr(line, "\r\n");
            size_t n = e ? (size_t)(e - line) : strlen(line);
            for (size_t i = 18; i + 7 <= n; i++)
                if (strncasecmp(line + i, "chunked", 7) == 0)
                    s->chunked = true;
        }
        else if (strncasecmp(line, "connection:", 11) == 0)
        {
            const char *v = line + 11;
            while (*v == ' ')
                v++;
            if (strncasecmp(v, "close", 5) == 0)
                s->close_after = true;
        }
        line = strstr(line, "\r\n");
    }
    return true;
}

static bool dechunk(const char *data, size_t len, Buf *out)
{
    size_t p = 0;
    for (;;)
    {
        const char *nl = NULL;
        for (size_t i = p; i + 1 < len; i++)
            if (data[i] == '\r' && data[i + 1] == '\n')
            {
                nl = data + i;
                break;
            }
        if (!nl)
            return false;
        size_t size = (size_t)strtoul(data + p, NULL, 16);
        p = (size_t)(nl - data) + 2;
        if (size == 0)
            return true;
        if (p + size + 2 > len)
            return false;
        buf_append(out, data + p, size);
        p += size + 2;
    }
}

static void slot_pump(Host *h, Slot *s)
{
    if (!s->conn)
        return;
    int rc = conn_step(s->conn);
    if (rc < 0 && conn_failed(s->conn) && !conn_has_buffered(s->conn))
    {
        if (!s->req)
        {
            slot_drop_conn(s);
            return;
        }
        if (!(s->have_headers && s->content_length < 0 && !s->chunked))
        {
            fail(h, s, conn_error(s->conn));
            return;
        }
    }

    uint8_t tmp[16384];
    for (int guard = 0; guard < 64; guard++)
    {
        int n = conn_read(s->conn, tmp, sizeof(tmp));
        if (n == 0)
            break;
        if (n < 0)
        {
            if (!s->req)
            {
                slot_drop_conn(s);
                return;
            }
            if (s->have_headers && s->content_length < 0 && !s->chunked)
            {
                const uint8_t *body = (const uint8_t *)s->in.data + s->header_len;
                size_t blen = s->in.len - s->header_len;
                int status = s->status;
                Buf copy = { 0 };
                buf_append(&copy, body, blen);
                slot_drop_conn(s);
                slot_reset_response(s);
                finish(h, s, status, (const uint8_t *)copy.data, copy.len);
                buf_free(&copy);
                return;
            }
            fail(h, s, n == -1 ? "The connection was closed." : conn_error(s->conn));
            return;
        }
        if (!s->req)
            continue;
        buf_append(&s->in, tmp, (size_t)n);
        s->last_activity = uptime_ms();
        if (s->in.len > MAX_BODY)
        {
            fail(h, s, "The response was too large.");
            return;
        }
    }

    if (!s->req)
        return;
    if (!s->have_headers && s->in.len && !parse_headers(s))
        goto timeout;
    if (!s->have_headers)
        goto timeout;
    if (s->status >= 100 && s->status < 200)
    {
        buf_consume(&s->in, s->header_len);
        s->have_headers = false;
        return;
    }

    const char *body = s->in.data + s->header_len;
    size_t avail = s->in.len - s->header_len;
    bool head_like = strcmp(s->req->method, "HEAD") == 0 || s->status == 204 || s->status == 304;
    if (head_like || (s->content_length >= 0 && avail >= (size_t)s->content_length))
    {
        size_t blen = head_like ? 0 : (size_t)s->content_length;
        Buf copy = { 0 };
        buf_append(&copy, body, blen);
        int status = s->status;
        bool close_after = s->close_after;
        slot_reset_response(s);
        s->reused = true;
        if (close_after)
            slot_drop_conn(s);
        finish(h, s, status, (const uint8_t *)copy.data, copy.len);
        buf_free(&copy);
        return;
    }
    if (s->chunked)
    {
        Buf out = { 0 };
        if (dechunk(body, avail, &out))
        {
            int status = s->status;
            bool close_after = s->close_after;
            slot_reset_response(s);
            s->reused = true;
            if (close_after)
                slot_drop_conn(s);
            finish(h, s, status, (const uint8_t *)(out.data ? out.data : ""), out.len);
            buf_free(&out);
            return;
        }
        buf_free(&out);
    }

timeout:
    if (s->req && uptime_ms() - s->last_activity > REQUEST_TIMEOUT_MS)
        fail(h, s, "The server took too long to respond.");
}

static Req *take_ready(Host *h)
{
    uint64_t now = uptime_ms();
    Req **pp = &h->head;
    Req *prev = NULL;
    while (*pp)
    {
        Req *r = *pp;
        if (r->not_before <= now)
        {
            *pp = r->next;
            if (h->tail == r)
                h->tail = prev;
            r->next = NULL;
            return r;
        }
        prev = r;
        pp = &r->next;
    }
    return NULL;
}

void http_pump(void)
{
    for (int i = 0; i < host_count; i++)
    {
        Host *h = &hosts[i];
        for (int k = 0; k < h->max_slots; k++)
        {
            Slot *s = &h->slots[k];
            if (!s->req && h->head)
            {
                Req *r = take_ready(h);
                if (r)
                {
                    if (s->conn && conn_failed(s->conn))
                        slot_drop_conn(s);
                    if (!s->conn)
                        s->conn = conn_open(h->host, h->port, h->tls);
                    s->req = r;
                    slot_reset_response(s);
                    s->last_activity = uptime_ms();
                    if (!s->conn)
                    {
                        fail(h, s, "Out of memory.");
                        continue;
                    }
                    send_request(h, s);
                }
            }
            slot_pump(h, s);
        }
    }
}

int http_pollfds(struct pollfd *fds, int max)
{
    int n = 0;
    for (int i = 0; i < host_count; i++)
        for (int k = 0; k < hosts[i].max_slots && n < max; k++)
        {
            Slot *s = &hosts[i].slots[k];
            if (s->conn && conn_fd(s->conn) >= 0 && !conn_failed(s->conn))
            {
                fds[n].fd = conn_fd(s->conn);
                fds[n].events = conn_poll_events(s->conn);
                fds[n].revents = 0;
                n++;
            }
        }
    return n;
}

bool http_busy(void)
{
    for (int i = 0; i < host_count; i++)
    {
        if (hosts[i].head)
            return true;
        for (int k = 0; k < hosts[i].max_slots; k++)
        {
            Slot *s = &hosts[i].slots[k];
            if (s->req)
                return true;
            if (s->conn && conn_has_buffered(s->conn))
                return true;
        }
    }
    return false;
}

typedef struct WsMsg WsMsg;

struct WsMsg
{
    char  *data;
    size_t len;
    WsMsg *next;
};

struct Ws
{
    Conn    *conn;
    WsState  state;
    bool     handshaken;
    Buf      in;
    Buf      msg;
    bool     in_msg;
    WsMsg   *head;
    WsMsg   *tail;
    int      close_code;
    char     error[192];
};

static void ws_fail(Ws *ws, const char *msg)
{
    if (ws->state == WS_CLOSED)
        return;
    ws->state = WS_CLOSED;
    if (!ws->error[0])
        snprintf(ws->error, sizeof(ws->error), "%s", msg);
}

Ws *ws_open(const char *url, const char *origin)
{
    Ws *ws = calloc(1, sizeof(Ws));
    if (!ws)
        return NULL;
    ws->state = WS_CONNECTING;
    bool tls = strncmp(url, "wss://", 6) == 0;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s%s", tls ? "https://" : "http://", url + (tls ? 6 : 5));
    Url u;
    if ((!tls && strncmp(url, "ws://", 5) != 0) || !url_parse(tmp, &u))
    {
        ws_fail(ws, "Bad gateway address.");
        return ws;
    }
    ws->conn = conn_open(u.host, u.port, tls);
    if (!ws->conn)
    {
        ws_fail(ws, "Out of memory.");
        return ws;
    }
    uint8_t key[16];
    random_bytes(key, sizeof(key));
    char key64[32];
    base64_encode(key, sizeof(key), key64, false);
    Buf b = { 0 };
    buf_printf(&b, "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n", u.path, u.host);
    buf_printf(&b, "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n", key64);
    buf_puts(&b, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\r\n");
    if (origin)
        buf_printf(&b, "Origin: %s\r\n", origin);
    buf_puts(&b, "\r\n");
    conn_write(ws->conn, b.data, b.len);
    buf_free(&b);
    return ws;
}

static void ws_send_frame(Ws *ws, int op, const void *data, size_t len)
{
    if (!ws->conn || ws->state == WS_CLOSED)
        return;
    uint8_t hdr[14];
    size_t h = 0;
    hdr[h++] = (uint8_t)(0x80 | op);
    if (len < 126)
    {
        hdr[h++] = (uint8_t)(0x80 | len);
    }
    else if (len < 65536)
    {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    }
    else
    {
        hdr[h++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            hdr[h++] = (uint8_t)((uint64_t)len >> (i * 8));
    }
    uint8_t mask[4];
    random_bytes(mask, 4);
    memcpy(hdr + h, mask, 4);
    h += 4;
    uint8_t *frame = malloc(h + len);
    if (!frame)
        return;
    memcpy(frame, hdr, h);
    const uint8_t *src = data;
    for (size_t i = 0; i < len; i++)
        frame[h + i] = src[i] ^ mask[i & 3];
    conn_write(ws->conn, frame, h + len);
    free(frame);
}

void ws_send(Ws *ws, const char *text, size_t len)
{
    if (ws->state == WS_OPEN)
        ws_send_frame(ws, 1, text, len);
}

static void push_message(Ws *ws)
{
    WsMsg *m = malloc(sizeof(WsMsg));
    if (!m)
        return;
    m->data = ws->msg.data ? ws->msg.data : xstrdup("");
    m->len = ws->msg.len;
    m->next = NULL;
    ws->msg.data = NULL;
    ws->msg.len = ws->msg.cap = 0;
    if (ws->tail)
        ws->tail->next = m;
    else
        ws->head = m;
    ws->tail = m;
}

static void parse_frames(Ws *ws)
{
    for (;;)
    {
        const uint8_t *p = (const uint8_t *)ws->in.data;
        size_t avail = ws->in.len;
        if (avail < 2)
            return;
        int op = p[0] & 0x0F;
        bool fin = (p[0] & 0x80) != 0;
        bool masked = (p[1] & 0x80) != 0;
        uint64_t len = p[1] & 0x7F;
        size_t h = 2;
        if (len == 126)
        {
            if (avail < 4)
                return;
            len = (uint64_t)p[2] << 8 | p[3];
            h = 4;
        }
        else if (len == 127)
        {
            if (avail < 10)
                return;
            len = 0;
            for (int i = 0; i < 8; i++)
                len = len << 8 | p[2 + i];
            h = 10;
        }
        uint8_t mask[4] = { 0 };
        if (masked)
        {
            if (avail < h + 4)
                return;
            memcpy(mask, p + h, 4);
            h += 4;
        }
        if (len > MAX_BODY)
        {
            ws_fail(ws, "The server sent an oversized message.");
            return;
        }
        if (avail < h + len)
            return;
        uint8_t *payload = (uint8_t *)ws->in.data + h;
        if (masked)
            for (uint64_t i = 0; i < len; i++)
                payload[i] ^= mask[i & 3];

        switch (op)
        {
            case 0:
            case 1:
            case 2:
                if (op != 0)
                {
                    buf_free(&ws->msg);
                    ws->in_msg = true;
                }
                if (ws->in_msg)
                    buf_append(&ws->msg, payload, (size_t)len);
                if (fin && ws->in_msg)
                {
                    push_message(ws);
                    ws->in_msg = false;
                }
                break;
            case 8:
                ws->close_code = len >= 2 ? (payload[0] << 8 | payload[1]) : 1005;
                if (len > 2)
                {
                    size_t n = (size_t)len - 2;
                    if (n >= sizeof(ws->error))
                        n = sizeof(ws->error) - 1;
                    memcpy(ws->error, payload + 2, n);
                    ws->error[n] = '\0';
                }
                ws_send_frame(ws, 8, payload, len >= 2 ? 2 : 0);
                conn_step(ws->conn);
                ws_fail(ws, "The server closed the connection.");
                buf_consume(&ws->in, h + (size_t)len);
                return;
            case 9:
                ws_send_frame(ws, 10, payload, (size_t)len);
                break;
            default:
                break;
        }
        buf_consume(&ws->in, h + (size_t)len);
    }
}

WsState ws_step(Ws *ws)
{
    if (ws->state == WS_CLOSED || !ws->conn)
        return WS_CLOSED;
    int rc = conn_step(ws->conn);
    uint8_t tmp[16384];
    for (int guard = 0; guard < 256; guard++)
    {
        int n = conn_read(ws->conn, tmp, sizeof(tmp));
        if (n == 0)
            break;
        if (n < 0)
        {
            if (ws->in.len && ws->handshaken)
                parse_frames(ws);
            ws_fail(ws, n == -1 ? "The connection was closed." : conn_error(ws->conn));
            return ws->state;
        }
        buf_append(&ws->in, tmp, (size_t)n);
    }
    if (rc < 0 && conn_failed(ws->conn) && !ws->in.len)
    {
        ws_fail(ws, conn_error(ws->conn));
        return ws->state;
    }
    if (!ws->handshaken && ws->in.len)
    {
        char *end = strstr(ws->in.data, "\r\n\r\n");
        if (!end)
            return ws->state;
        int status = 0;
        const char *sp = strchr(ws->in.data, ' ');
        if (sp)
            status = atoi(sp + 1);
        if (status != 101)
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "The gateway answered with HTTP %d.", status);
            ws_fail(ws, msg);
            return ws->state;
        }
        buf_consume(&ws->in, (size_t)(end - ws->in.data) + 4);
        ws->handshaken = true;
        ws->state = WS_OPEN;
    }
    if (ws->handshaken)
        parse_frames(ws);
    return ws->state;
}

bool ws_recv(Ws *ws, char **msg, size_t *len)
{
    WsMsg *m = ws->head;
    if (!m)
        return false;
    ws->head = m->next;
    if (!ws->head)
        ws->tail = NULL;
    *msg = m->data;
    *len = m->len;
    free(m);
    return true;
}

int ws_close_code(Ws *ws)
{
    return ws->close_code;
}

const char *ws_error(Ws *ws)
{
    return ws->error[0] ? ws->error : "The connection was closed.";
}

int ws_pollfd(Ws *ws, struct pollfd *pfd)
{
    if (!ws || !ws->conn || ws->state == WS_CLOSED || conn_fd(ws->conn) < 0)
        return 0;
    pfd->fd = conn_fd(ws->conn);
    pfd->events = conn_poll_events(ws->conn);
    pfd->revents = 0;
    return 1;
}

bool ws_has_buffered(Ws *ws)
{
    return ws && (ws->head || (ws->conn && conn_has_buffered(ws->conn)));
}

void ws_free(Ws *ws)
{
    if (!ws)
        return;
    conn_close(ws->conn);
    buf_free(&ws->in);
    buf_free(&ws->msg);
    while (ws->head)
    {
        WsMsg *m = ws->head;
        ws->head = m->next;
        free(m->data);
        free(m);
    }
    free(ws);
}
