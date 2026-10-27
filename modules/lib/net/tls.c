#include "../tls/trust_anchors.h"

#define TLS_TIMEOUT_MS 20000

struct TlsConn
{
    int                      fd;
    bool                     eof;
    bool                     io_error;
    br_ssl_client_context    sc;
    br_x509_minimal_context  xc;
    br_sslio_context         io;
    unsigned char            iobuf[BR_SSL_BUFSIZE_BIDI];
};

static int tls_sock_read(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    uint64_t start = uptime_ms();
    for (;;)
    {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 200) > 0)
        {
            ssize_t n = read(fd, buf, len);
            return n > 0 ? (int)n : -1;
        }
        if (uptime_ms() - start > TLS_TIMEOUT_MS)
            return -1;
    }
}

static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    ssize_t n = write(*(int *)ctx, buf, len);
    return n > 0 ? (int)n : -1;
}

static bool tls_has_rdrand(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (c >> 30) & 1;
}

void tls_entropy(unsigned char out[32])
{
    br_sha256_context h;
    br_sha256_init(&h);

    for (int i = 0; i < 64; i++)
    {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        br_sha256_update(&h, &lo, sizeof(lo));
        br_sha256_update(&h, &hi, sizeof(hi));
        if (i % 8 == 0)
            yield();
    }

    if (tls_has_rdrand())
    {
        for (int i = 0; i < 8; i++)
        {
            uint64_t v = 0;
            unsigned char ok = 0;
            __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
            if (ok)
                br_sha256_update(&h, &v, sizeof(v));
        }
    }

    uint64_t t = uptime_ms();
    time_t now = time(NULL);
    struct meminfo m;
    meminfo(&m);
    void *stack_addr = &h;
    br_sha256_update(&h, &t, sizeof(t));
    br_sha256_update(&h, &now, sizeof(now));
    br_sha256_update(&h, &m, sizeof(m));
    br_sha256_update(&h, &stack_addr, sizeof(stack_addr));
    br_sha256_out(&h, out);
}

static const char *tls_error_text(int err)
{
    switch (err)
    {
        case BR_ERR_X509_EXPIRED:          return "the site's certificate has expired or isn't valid yet";
        case BR_ERR_X509_NOT_TRUSTED:      return "the site's certificate isn't from a trusted authority";
        case BR_ERR_X509_BAD_SERVER_NAME:  return "the certificate is for a different site";
        case BR_ERR_BAD_VERSION:
        case BR_ERR_BAD_CIPHER_SUITE:      return "the site needs TLS features this browser doesn't support";
        case BR_ERR_IO:                    return "the connection was closed during the secure handshake";
        default:                           return NULL;
    }
}

TlsConn *tls_start(int fd, const char *host, char *err, size_t err_size)
{
    TlsConn *c = calloc(1, sizeof(TlsConn));
    if (!c)
    {
        snprintf(err, err_size, "out of memory");
        return NULL;
    }
    c->fd = fd;

    br_ssl_client_init_full(&c->sc, &c->xc, TAs, TAs_NUM);

    time_t now = time(NULL);
    br_x509_minimal_set_time(&c->xc, (uint32_t)(now / 86400 + 719528), (uint32_t)(now % 86400));

    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof(c->iobuf), 1);

    unsigned char seed[32];
    tls_entropy(seed);
    br_ssl_engine_inject_entropy(&c->sc.eng, seed, sizeof(seed));

    if (!br_ssl_client_reset(&c->sc, host, 0))
    {
        snprintf(err, err_size, "could not start the secure connection");
        free(c);
        return NULL;
    }
    return c;
}

int tls_pump(TlsConn *c)
{
    br_ssl_engine_context *eng = &c->sc.eng;
    int progress = 0;
    if (c->io_error)
        return -1;
    for (;;)
    {
        unsigned state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_CLOSED)
            return progress ? 1 : -1;

        bool did = false;
        if (state & BR_SSL_SENDREC)
        {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(eng, &len);
            ssize_t n = write(c->fd, buf, len);
            if (n > 0)
            {
                br_ssl_engine_sendrec_ack(eng, (size_t)n);
                did = true;
            }
            else if (n != -EAGAIN)
            {
                c->io_error = true;
                return -1;
            }
        }
        if ((state & BR_SSL_RECVREC) && !c->eof)
        {
            size_t len;
            unsigned char *buf = br_ssl_engine_recvrec_buf(eng, &len);
            ssize_t n = read(c->fd, buf, len);
            if (n > 0)
            {
                br_ssl_engine_recvrec_ack(eng, (size_t)n);
                did = true;
            }
            else if (n == 0)
            {
                c->eof = true;
            }
            else if (n != -EAGAIN)
            {
                c->io_error = true;
                return -1;
            }
        }
        if (!did)
            break;
        progress = 1;
    }
    return progress;
}

bool tls_ready(TlsConn *c)
{
    return (br_ssl_engine_current_state(&c->sc.eng) & BR_SSL_SENDAPP) != 0;
}

int tls_send(TlsConn *c, const void *data, size_t len)
{
    br_ssl_engine_context *eng = &c->sc.eng;
    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_CLOSED)
        return -1;
    if (!(state & BR_SSL_SENDAPP))
        return 0;
    size_t space;
    unsigned char *buf = br_ssl_engine_sendapp_buf(eng, &space);
    size_t n = len < space ? len : space;
    memcpy(buf, data, n);
    br_ssl_engine_sendapp_ack(eng, n);
    br_ssl_engine_flush(eng, 0);
    return (int)n;
}

int tls_recv(TlsConn *c, void *out, size_t len)
{
    br_ssl_engine_context *eng = &c->sc.eng;
    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_RECVAPP)
    {
        size_t avail;
        unsigned char *buf = br_ssl_engine_recvapp_buf(eng, &avail);
        size_t n = len < avail ? len : avail;
        memcpy(out, buf, n);
        br_ssl_engine_recvapp_ack(eng, n);
        return (int)n;
    }
    if (c->io_error)
        return -2;
    if (state & BR_SSL_CLOSED)
        return br_ssl_engine_last_error(eng) == BR_ERR_OK ? -1 : -2;
    if (c->eof)
        return -1;
    return 0;
}

bool tls_pending(TlsConn *c)
{
    return (br_ssl_engine_current_state(&c->sc.eng) & BR_SSL_RECVAPP) != 0;
}

void tls_error(TlsConn *c, char *err, size_t err_size)
{
    int e = br_ssl_engine_last_error(&c->sc.eng);
    const char *text = tls_error_text(e);
    if (c->io_error || (e == BR_ERR_IO && c->eof))
        snprintf(err, err_size, "The connection was closed during the secure handshake");
    else if (text)
        snprintf(err, err_size, "Secure connection failed: %s", text);
    else
        snprintf(err, err_size, "Secure connection failed (TLS error %d)", e);
}

void tls_free(TlsConn *c)
{
    free(c);
}

TlsConn *tls_connect(int fd, const char *host, char *err, size_t err_size)
{
    TlsConn *c = tls_start(fd, host, err, err_size);
    if (!c)
        return NULL;

    br_sslio_init(&c->io, &c->sc.eng, tls_sock_read, &c->fd, tls_sock_write, &c->fd);

    if (br_sslio_flush(&c->io) != 0 || br_ssl_engine_current_state(&c->sc.eng) == BR_SSL_CLOSED)
    {
        int e = br_ssl_engine_last_error(&c->sc.eng);
        const char *text = tls_error_text(e);
        if (text)
            snprintf(err, err_size, "Secure connection failed: %s", text);
        else
            snprintf(err, err_size, "Secure connection failed (TLS error %d)", e);
        free(c);
        return NULL;
    }
    return c;
}

int tls_write_all(TlsConn *c, const void *data, size_t len)
{
    if (br_sslio_write_all(&c->io, data, len) != 0)
        return -1;
    return br_sslio_flush(&c->io);
}

int tls_read(TlsConn *c, void *buf, size_t len)
{
    int n = br_sslio_read(&c->io, buf, len);
    if (n < 0)
    {
        int e = br_ssl_engine_last_error(&c->sc.eng);
        return e == BR_ERR_OK || e == BR_ERR_IO ? 0 : -1;
    }
    return n;
}

void tls_close(TlsConn *c)
{
    if (!c)
        return;
    br_sslio_close(&c->io);
    free(c);
}
