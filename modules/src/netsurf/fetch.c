#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/ring.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/messages.h"
#include "utils/useragent.h"
#include "netsurf/fetch.h"
#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"

#undef socket
#include <socket.h>
#include <poll.h>
#include <net.h>
#include "tls.h"
#include "http.h"

#include "nsx.h"

#define CONNECT_TIMEOUT_MS 30000
#define IDLE_TIMEOUT_MS    60000
#define MAX_HEADER_BYTES   (64 * 1024)
#define MAX_BODY_BUFFERED  (16 * 1024 * 1024)
#define READ_BUDGET        (128 * 1024)

typedef enum
{
    ST_IDLE,
    ST_CONNECTING,
    ST_HANDSHAKE,
    ST_SENDING,
    ST_HEADERS,
    ST_BODY,
    ST_DONE,
}
FetchState;

typedef enum
{
    CHUNK_SIZE,
    CHUNK_DATA,
    CHUNK_DATA_END,
    CHUNK_TRAILER,
    CHUNK_DONE,
}
ChunkState;

typedef struct HttpFetch HttpFetch;
struct HttpFetch
{
    struct fetch *parent;
    nsurl        *url;
    bool          https;
    char          host[256];
    int           port;

    bool          only_2xx;
    bool          aborted;
    bool          locked;
    bool          started;

    char         *request;
    size_t        request_len;
    size_t        request_sent;

    FetchState    state;
    int           fd;
    TlsConn      *tls;
    uint64_t      last_activity;

    char         *header_buf;
    size_t        header_len;
    long          http_code;
    char         *location;
    char         *realm;
    long long     content_length;
    long long     body_received;
    bool          chunked;
    char          content_encoding[32];

    ChunkState    chunk_state;
    size_t        chunk_left;
    char          chunk_line[64];
    size_t        chunk_line_len;

    uint8_t      *encoded;
    size_t        encoded_len, encoded_cap;

    bool          in_ring;
    HttpFetch    *r_next, *r_prev;
};

static HttpFetch *ring;

static bool http_body_data(HttpFetch *f, const uint8_t *data, size_t len);

static void send_msg(HttpFetch *f, fetch_msg *msg)
{
    f->locked = true;
    fetch_send_callback(msg, f->parent);
    f->locked = false;
}

static void send_error(HttpFetch *f, const char *text)
{
    fetch_msg msg;
    msg.type = FETCH_ERROR;
    msg.data.error = text;
    send_msg(f, &msg);
}

static void close_connection(HttpFetch *f)
{
    if (f->tls)
    {
        tls_free(f->tls);
        f->tls = NULL;
    }
    if (f->fd >= 0)
    {
        close(f->fd);
        f->fd = -1;
    }
}

static void finish(HttpFetch *f)
{
    close_connection(f);
    f->state = ST_DONE;
}

static bool append(char **buf, size_t *len, size_t *cap, const char *data, size_t n)
{
    if (*len + n + 1 > *cap)
    {
        size_t ncap = *cap ? *cap * 2 : 1024;
        while (ncap < *len + n + 1)
            ncap *= 2;
        char *grown = realloc(*buf, ncap);
        if (!grown)
            return false;
        *buf = grown;
        *cap = ncap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
    return true;
}

static void append_str(char **buf, size_t *len, size_t *cap, const char *s)
{
    append(buf, len, cap, s, strlen(s));
}

static char *escape_quotes(const char *s)
{
    size_t n = strlen(s);
    char *out = malloc(n * 2 + 1);
    if (!out)
        return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] == '"' || s[i] == '\\')
            out[k++] = '\\';
        out[k++] = s[i];
    }
    out[k] = '\0';
    return out;
}

static bool build_multipart(const struct fetch_multipart_data *parts, char **body, size_t *body_len, char *boundary)
{
    snprintf(boundary, 64, "----StrawOSBoundary%08x%08x", (unsigned)rand(), (unsigned)rand());
    size_t len = 0, cap = 0;
    char *buf = NULL;
    for (const struct fetch_multipart_data *p = parts; p; p = p->next)
    {
        char *name = escape_quotes(p->name);
        if (!name)
            return false;
        append_str(&buf, &len, &cap, "--");
        append_str(&buf, &len, &cap, boundary);
        append_str(&buf, &len, &cap, "\r\nContent-Disposition: form-data; name=\"");
        append_str(&buf, &len, &cap, name);
        free(name);
        if (p->file)
        {
            char *fname = escape_quotes(p->value);
            append_str(&buf, &len, &cap, "\"; filename=\"");
            append_str(&buf, &len, &cap, fname ? fname : "");
            append_str(&buf, &len, &cap, "\"\r\nContent-Type: application/octet-stream\r\n\r\n");
            free(fname);
            FILE *fp = p->rawfile ? fopen(p->rawfile, "rb") : NULL;
            if (fp)
            {
                char chunk[4096];
                size_t n;
                while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
                    append(&buf, &len, &cap, chunk, n);
                fclose(fp);
            }
        }
        else
        {
            append_str(&buf, &len, &cap, "\"\r\n\r\n");
            append_str(&buf, &len, &cap, p->value ? p->value : "");
        }
        append_str(&buf, &len, &cap, "\r\n");
    }
    append_str(&buf, &len, &cap, "--");
    append_str(&buf, &len, &cap, boundary);
    append_str(&buf, &len, &cap, "--\r\n");
    *body = buf;
    *body_len = len;
    return buf != NULL;
}

static void *http_setup(struct fetch *parent, nsurl *url, bool only_2xx, bool downgrade_tls,
                        const char *post_urlenc, const struct fetch_multipart_data *post_multipart,
                        const char **headers)
{
    (void)downgrade_tls;
    HttpFetch *f = calloc(1, sizeof(HttpFetch));
    if (!f)
        return NULL;
    f->parent = parent;
    f->url = nsurl_ref(url);
    f->only_2xx = only_2xx;
    f->fd = -1;
    f->content_length = -1;
    f->https = nsurl_get_scheme_type(url) == NSURL_SCHEME_HTTPS;

    lwc_string *host = nsurl_get_component(url, NSURL_HOST);
    if (host)
    {
        snprintf(f->host, sizeof(f->host), "%s", lwc_string_data(host));
        lwc_string_unref(host);
    }
    f->port = f->https ? 443 : 80;
    lwc_string *port = nsurl_get_component(url, NSURL_PORT);
    if (port)
    {
        f->port = atoi(lwc_string_data(port));
        lwc_string_unref(port);
    }

    char *path = NULL;
    size_t path_len = 0;
    if (nsurl_get(url, NSURL_PATH | NSURL_QUERY, &path, &path_len) != NSERROR_OK || !path || !path[0])
    {
        free(path);
        path = strdup("/");
    }

    char *body = NULL;
    size_t body_len = 0;
    char boundary[64] = "";
    if (post_urlenc)
    {
        body = strdup(post_urlenc);
        body_len = body ? strlen(body) : 0;
    }
    else if (post_multipart)
    {
        build_multipart(post_multipart, &body, &body_len, boundary);
    }

    size_t len = 0, cap = 0;
    char *req = NULL;
    char line[512];
    append_str(&req, &len, &cap, body ? "POST " : "GET ");
    append_str(&req, &len, &cap, path ? path : "/");
    append_str(&req, &len, &cap, " HTTP/1.1\r\n");
    free(path);

    bool default_port = (f->https && f->port == 443) || (!f->https && f->port == 80);
    if (default_port)
        snprintf(line, sizeof(line), "Host: %s\r\n", f->host);
    else
        snprintf(line, sizeof(line), "Host: %s:%d\r\n", f->host, f->port);
    append_str(&req, &len, &cap, line);
    snprintf(line, sizeof(line), "User-Agent: %s\r\n", user_agent_string());
    append_str(&req, &len, &cap, line);
    append_str(&req, &len, &cap, "Accept-Encoding: gzip, deflate\r\nConnection: close\r\n");

    bool have_accept = false;
    for (int i = 0; headers && headers[i]; i++)
    {
        if (strncasecmp(headers[i], "Accept:", 7) == 0)
            have_accept = true;
        append_str(&req, &len, &cap, headers[i]);
        append_str(&req, &len, &cap, "\r\n");
    }
    if (!have_accept)
        append_str(&req, &len, &cap, "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n");

    char *cookie = urldb_get_cookie(url, true);
    if (cookie)
    {
        append_str(&req, &len, &cap, "Cookie: ");
        append_str(&req, &len, &cap, cookie);
        append_str(&req, &len, &cap, "\r\n");
        free(cookie);
    }

    if (body)
    {
        if (post_urlenc)
            append_str(&req, &len, &cap, "Content-Type: application/x-www-form-urlencoded\r\n");
        else
        {
            snprintf(line, sizeof(line), "Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
            append_str(&req, &len, &cap, line);
        }
        snprintf(line, sizeof(line), "Content-Length: %zu\r\n", body_len);
        append_str(&req, &len, &cap, line);
    }
    append_str(&req, &len, &cap, "\r\n");
    if (body)
    {
        append(&req, &len, &cap, body, body_len);
        free(body);
    }

    if (!req)
    {
        nsurl_unref(f->url);
        free(f);
        return NULL;
    }
    f->request = req;
    f->request_len = len;
    RING_INSERT(ring, f);
    f->in_ring = true;
    return f;
}

static bool http_start(void *vf)
{
    HttpFetch *f = vf;
    f->started = true;
    f->state = ST_IDLE;
    f->last_activity = uptime_ms();
    return true;
}

static void http_abort(void *vf)
{
    HttpFetch *f = vf;
    f->aborted = true;
    close_connection(f);
}

static void http_free(void *vf)
{
    HttpFetch *f = vf;
    if (f->in_ring)
    {
        RING_REMOVE(ring, f);
        f->in_ring = false;
    }
    close_connection(f);
    nsurl_unref(f->url);
    free(f->request);
    free(f->header_buf);
    free(f->location);
    free(f->realm);
    free(f->encoded);
    free(f);
}

static bool begin_connect(HttpFetch *f)
{
    uint8_t ip[4];
    if (net_resolve(f->host, ip) != 0)
    {
        send_error(f, "Couldn't find the server. Check the address and your network connection.");
        return false;
    }
    f->fd = socket(SOCK_TCP | SOCK_NONBLOCK);
    if (f->fd < 0)
    {
        send_error(f, "The network isn't available.");
        return false;
    }
    char target[64];
    snprintf(target, sizeof(target), "%u.%u.%u.%u:%d", ip[0], ip[1], ip[2], ip[3], f->port);
    int rc = connect(f->fd, target);
    if (rc != 0 && rc != -EINPROGRESS)
    {
        send_error(f, rc == -ENETDOWN ? "The network isn't available." : "The server refused the connection.");
        return false;
    }
    f->state = ST_CONNECTING;
    f->last_activity = uptime_ms();
    return true;
}

static bool connected(HttpFetch *f)
{
    if (f->https)
    {
        char err[160];
        f->tls = tls_start(f->fd, f->host, err, sizeof(err));
        if (!f->tls)
        {
            send_error(f, err);
            return false;
        }
        f->state = ST_HANDSHAKE;
    }
    else
    {
        f->state = ST_SENDING;
    }
    return true;
}

static int conn_write(HttpFetch *f, const void *data, size_t len)
{
    if (f->tls)
        return tls_send(f->tls, data, len);
    ssize_t n = write(f->fd, data, len);
    if (n == -EAGAIN)
        return 0;
    return n < 0 ? -1 : (int)n;
}

static int conn_read(HttpFetch *f, void *buf, size_t len)
{
    if (f->tls)
    {
        int n = tls_recv(f->tls, buf, len);
        return n == -2 ? -2 : n;
    }
    ssize_t n = read(f->fd, buf, len);
    if (n == -EAGAIN)
        return 0;
    if (n == 0)
        return -1;
    return n < 0 ? -2 : (int)n;
}

static void trim_copy(char **dst, const char *src, size_t len)
{
    while (len > 0 && (*src == ' ' || *src == '\t'))
    {
        src++;
        len--;
    }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' || src[len - 1] == '\n'))
        len--;
    free(*dst);
    *dst = malloc(len + 1);
    if (*dst)
    {
        memcpy(*dst, src, len);
        (*dst)[len] = '\0';
    }
}

static void handle_header_line(HttpFetch *f, char *line, size_t len)
{
    fetch_msg msg;
    msg.type = FETCH_HEADER;
    msg.data.header_or_data.buf = (const uint8_t *)line;
    msg.data.header_or_data.len = len;
    send_msg(f, &msg);
    if (f->aborted)
        return;

    if (len > 9 && strncasecmp(line, "Location:", 9) == 0)
    {
        trim_copy(&f->location, line + 9, len - 9);
    }
    else if (len > 15 && strncasecmp(line, "Content-Length:", 15) == 0)
    {
        f->content_length = atoll(line + 15);
    }
    else if (len > 18 && strncasecmp(line, "Transfer-Encoding:", 18) == 0)
    {
        f->chunked = strcasestr(line + 18, "chunked") != NULL;
    }
    else if (len > 17 && strncasecmp(line, "Content-Encoding:", 17) == 0)
    {
        char *value = NULL;
        trim_copy(&value, line + 17, len - 17);
        if (value)
        {
            snprintf(f->content_encoding, sizeof(f->content_encoding), "%s", value);
            free(value);
        }
    }
    else if (len > 11 && strncasecmp(line, "Set-Cookie:", 11) == 0)
    {
        char *value = NULL;
        trim_copy(&value, line + 11, len - 11);
        if (value)
        {
            fetch_set_cookie(f->parent, value);
            free(value);
        }
    }
    else if (len > 17 && strncasecmp(line, "WWW-Authenticate:", 17) == 0)
    {
        const char *r = strcasestr(line, "realm=\"");
        if (r)
        {
            r += 7;
            const char *end = strchr(r, '"');
            if (end)
                trim_copy(&f->realm, r, (size_t)(end - r));
        }
    }
}

static bool headers_complete(HttpFetch *f)
{
    fetch_msg msg;
    fetch_set_http_code(f->parent, f->http_code);

    if (f->http_code == 304)
    {
        msg.type = FETCH_NOTMODIFIED;
        send_msg(f, &msg);
        return false;
    }
    if (f->http_code >= 300 && f->http_code < 400 && f->location)
    {
        msg.type = FETCH_REDIRECT;
        msg.data.redirect = f->location;
        send_msg(f, &msg);
        return false;
    }
    if (f->http_code == 401)
    {
        msg.type = FETCH_AUTH;
        msg.data.auth.realm = f->realm;
        send_msg(f, &msg);
        return false;
    }
    if (f->only_2xx && (f->http_code < 200 || f->http_code >= 300))
    {
        send_error(f, messages_get("Not2xx"));
        return false;
    }
    return true;
}

static bool parse_headers(HttpFetch *f)
{
    char *end = strstr(f->header_buf, "\r\n\r\n");
    size_t sep = 4;
    if (!end)
    {
        end = strstr(f->header_buf, "\n\n");
        sep = 2;
    }
    if (!end)
        return true;

    size_t head_len = (size_t)(end - f->header_buf) + sep;
    char *p = f->header_buf;
    char *line_end = strchr(p, '\n');
    if (strncmp(p, "HTTP/", 5) != 0 || !line_end)
    {
        send_error(f, "The server sent a response the browser couldn't understand.");
        return false;
    }
    const char *sp = strchr(p, ' ');
    f->http_code = sp ? strtol(sp + 1, NULL, 10) : 0;

    if (f->http_code >= 100 && f->http_code < 200)
    {
        memmove(f->header_buf, f->header_buf + head_len, f->header_len - head_len + 1);
        f->header_len -= head_len;
        return parse_headers(f);
    }

    p = line_end + 1;
    while (p < f->header_buf + head_len)
    {
        char *nl = strchr(p, '\n');
        if (!nl || nl >= f->header_buf + head_len)
            break;
        size_t len = (size_t)(nl - p) + 1;
        if (len <= 2 && (p[0] == '\r' || p[0] == '\n'))
            break;
        handle_header_line(f, p, len);
        if (f->aborted)
            return false;
        p = nl + 1;
    }

    if (!headers_complete(f))
        return false;

    f->state = ST_BODY;
    size_t rest = f->header_len - head_len;
    char *buf = f->header_buf;
    f->header_buf = NULL;
    f->header_len = 0;

    bool ok = rest == 0 || http_body_data(f, (const uint8_t *)buf + head_len, rest);
    free(buf);
    return ok;
}

static bool deliver(HttpFetch *f, const uint8_t *data, size_t len)
{
    if (len == 0)
        return true;
    if (f->content_encoding[0] && strcasecmp(f->content_encoding, "identity") != 0)
    {
        if (f->encoded_len + len > MAX_BODY_BUFFERED)
        {
            send_error(f, "The page is too large.");
            return false;
        }
        char *buf = (char *)f->encoded;
        bool ok = append(&buf, &f->encoded_len, &f->encoded_cap, (const char *)data, len);
        f->encoded = (uint8_t *)buf;
        return ok;
    }
    fetch_msg msg;
    msg.type = FETCH_DATA;
    msg.data.header_or_data.buf = data;
    msg.data.header_or_data.len = len;
    send_msg(f, &msg);
    return !f->aborted;
}

static bool http_body_data(HttpFetch *f, const uint8_t *data, size_t len)
{
    if (!f->chunked)
    {
        if (f->content_length >= 0 && f->body_received + (long long)len > f->content_length)
            len = (size_t)(f->content_length - f->body_received);
        f->body_received += (long long)len;
        return deliver(f, data, len);
    }

    size_t i = 0;
    while (i < len && f->chunk_state != CHUNK_DONE)
    {
        switch (f->chunk_state)
        {
            case CHUNK_SIZE:
            case CHUNK_TRAILER:
            {
                char c = (char)data[i++];
                if (c != '\n')
                {
                    if (f->chunk_line_len < sizeof(f->chunk_line) - 1)
                        f->chunk_line[f->chunk_line_len++] = c;
                    break;
                }
                f->chunk_line[f->chunk_line_len] = '\0';
                f->chunk_line_len = 0;
                if (f->chunk_state == CHUNK_TRAILER)
                {
                    if (f->chunk_line[0] == '\0' || f->chunk_line[0] == '\r')
                        f->chunk_state = CHUNK_DONE;
                    break;
                }
                f->chunk_left = (size_t)strtoul(f->chunk_line, NULL, 16);
                f->chunk_state = f->chunk_left ? CHUNK_DATA : CHUNK_TRAILER;
                break;
            }
            case CHUNK_DATA:
            {
                size_t n = len - i < f->chunk_left ? len - i : f->chunk_left;
                if (!deliver(f, data + i, n))
                    return false;
                i += n;
                f->chunk_left -= n;
                f->body_received += (long long)n;
                if (f->chunk_left == 0)
                    f->chunk_state = CHUNK_DATA_END;
                break;
            }
            case CHUNK_DATA_END:
                if (data[i++] == '\n')
                    f->chunk_state = CHUNK_SIZE;
                break;
            default:
                i = len;
                break;
        }
    }
    return true;
}

static bool body_complete(HttpFetch *f)
{
    if (f->chunked)
        return f->chunk_state == CHUNK_DONE;
    return f->content_length >= 0 && f->body_received >= f->content_length;
}

static void complete(HttpFetch *f)
{
    if (f->encoded_len)
    {
        size_t out_len = 0;
        uint8_t *decoded = http_decode_body(f->content_encoding, f->encoded, f->encoded_len, &out_len);
        f->encoded = NULL;
        f->encoded_len = 0;
        f->content_encoding[0] = '\0';
        bool ok = deliver(f, decoded, out_len);
        free(decoded);
        if (!ok)
            return;
    }
    fetch_msg msg;
    msg.type = FETCH_FINISHED;
    send_msg(f, &msg);
}

static bool step(HttpFetch *f)
{
    uint64_t now = uptime_ms();

    switch (f->state)
    {
        case ST_IDLE:
            return begin_connect(f);

        case ST_CONNECTING:
        {
            struct pollfd pfd = { f->fd, POLLOUT, 0 };
            poll(&pfd, 1, 0);
            if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            {
                send_error(f, "The server refused the connection.");
                return false;
            }
            if (pfd.revents & POLLOUT)
            {
                f->last_activity = now;
                return connected(f);
            }
            if (now - f->last_activity > CONNECT_TIMEOUT_MS)
            {
                fetch_msg msg;
                msg.type = FETCH_TIMEDOUT;
                msg.data.error = "The server took too long to respond.";
                send_msg(f, &msg);
                return false;
            }
            return true;
        }

        case ST_HANDSHAKE:
        {
            int rc = tls_pump(f->tls);
            if (rc < 0)
            {
                char err[160];
                tls_error(f->tls, err, sizeof(err));
                send_error(f, err);
                return false;
            }
            if (rc > 0)
                f->last_activity = now;
            if (tls_ready(f->tls))
                f->state = ST_SENDING;
            break;
        }

        case ST_SENDING:
        {
            while (f->request_sent < f->request_len)
            {
                int n = conn_write(f, f->request + f->request_sent, f->request_len - f->request_sent);
                if (n < 0)
                {
                    send_error(f, "The connection to the server was lost.");
                    return false;
                }
                if (n == 0)
                    break;
                f->request_sent += (size_t)n;
                f->last_activity = now;
            }
            if (f->tls && tls_pump(f->tls) < 0)
            {
                char err[160];
                tls_error(f->tls, err, sizeof(err));
                send_error(f, err);
                return false;
            }
            if (f->request_sent == f->request_len)
                f->state = ST_HEADERS;
            break;
        }

        case ST_HEADERS:
        case ST_BODY:
        {
            uint8_t buf[16384];
            size_t budget = READ_BUDGET;
            for (;;)
            {
                if (f->tls && tls_pump(f->tls) > 0)
                    f->last_activity = now;
                int n = conn_read(f, buf, sizeof(buf));
                if (n == 0)
                    break;
                if (n < 0)
                {
                    if (f->state == ST_BODY && (n == -1 || f->body_received > 0) &&
                        (!f->chunked || f->chunk_state == CHUNK_DONE || n == -1))
                    {
                        complete(f);
                        return false;
                    }
                    if (f->state == ST_HEADERS && f->header_len > 0 && n == -1)
                    {
                        send_error(f, "The server closed the connection before sending a full response.");
                        return false;
                    }
                    send_error(f, "The connection to the server was lost.");
                    return false;
                }
                f->last_activity = now;
                if (f->state == ST_HEADERS)
                {
                    if (f->header_len + (size_t)n > MAX_HEADER_BYTES)
                    {
                        send_error(f, "The server sent too many headers.");
                        return false;
                    }
                    size_t cap = f->header_len + (size_t)n + 1;
                    char *grown = realloc(f->header_buf, cap);
                    if (!grown)
                        return false;
                    f->header_buf = grown;
                    memcpy(f->header_buf + f->header_len, buf, (size_t)n);
                    f->header_len += (size_t)n;
                    f->header_buf[f->header_len] = '\0';
                    if (!parse_headers(f))
                        return false;
                }
                else if (!http_body_data(f, buf, (size_t)n))
                {
                    return false;
                }

                if (f->state == ST_BODY && (body_complete(f) || f->http_code == 204))
                {
                    complete(f);
                    return false;
                }
                if (budget <= (size_t)n)
                    break;
                budget -= (size_t)n;
            }
            if (f->state == ST_BODY && (body_complete(f) || f->http_code == 204))
            {
                complete(f);
                return false;
            }
            break;
        }

        case ST_DONE:
            return false;
    }

    if (now - f->last_activity > IDLE_TIMEOUT_MS)
    {
        fetch_msg msg;
        msg.type = FETCH_TIMEDOUT;
        msg.data.error = "The server took too long to respond.";
        send_msg(f, &msg);
        return false;
    }
    return true;
}

static void http_poll(lwc_string *scheme)
{
    (void)scheme;
    if (!ring)
        return;

    HttpFetch *f = ring;
    HttpFetch *last = ring->r_prev;
    for (;;)
    {
        HttpFetch *next = f->r_next;
        bool is_last = f == last;

        if (!f->locked && (f->started || f->aborted))
        {
            bool keep = !f->aborted && step(f);
            if (!keep && !f->locked)
            {
                finish(f);
                RING_REMOVE(ring, f);
                f->in_ring = false;
                fetch_remove_from_queues(f->parent);
                fetch_free(f->parent);
            }
        }

        if (is_last || !ring)
            break;
        f = next;
    }
}

static bool http_initialise(lwc_string *scheme)
{
    (void)scheme;
    return true;
}

static void http_finalise(lwc_string *scheme)
{
    (void)scheme;
}

static bool http_acceptable(const nsurl *url)
{
    (void)url;
    return true;
}

nserror nsx_fetch_http_register(void)
{
    static const struct fetcher_operation_table ops = {
        .initialise = http_initialise,
        .acceptable = http_acceptable,
        .setup = http_setup,
        .start = http_start,
        .abort = http_abort,
        .free = http_free,
        .poll = http_poll,
        .finalise = http_finalise,
    };
    nserror err = fetcher_add(lwc_string_ref(corestring_lwc_http), &ops);
    if (err != NSERROR_OK)
        return err;
    return fetcher_add(lwc_string_ref(corestring_lwc_https), &ops);
}
