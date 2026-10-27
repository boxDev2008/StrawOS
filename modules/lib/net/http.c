#define HTTP_MAX_BODY     (16 * 1024 * 1024)
#define HTTP_MAX_REDIRECT 8
#define HTTP_USER_AGENT   "Mozilla/5.0 (StrawOS; x86_64) StrawBrowser/1.0"

bool url_parse(const char *text, Url *out)
{
    memset(out, 0, sizeof(*out));
    const char *p = strstr(text, "://");
    const char *rest;
    if (p)
    {
        size_t n = (size_t)(p - text);
        if (n >= sizeof(out->scheme))
            return false;
        for (size_t i = 0; i < n; i++)
            out->scheme[i] = (char)tolower((unsigned char)text[i]);
        rest = p + 3;
    }
    else
    {
        strcpy(out->scheme, "http");
        rest = text;
    }

    if (strcmp(out->scheme, "file") == 0)
    {
        snprintf(out->path, sizeof(out->path), "%s", rest[0] == '/' ? rest : "/");
        return true;
    }

    size_t host_len = strcspn(rest, ":/?#");
    if (host_len == 0 || host_len >= sizeof(out->host))
        return false;
    for (size_t i = 0; i < host_len; i++)
        out->host[i] = (char)tolower((unsigned char)rest[i]);
    rest += host_len;

    out->port = strcmp(out->scheme, "https") == 0 ? 443 : 80;
    if (*rest == ':')
    {
        rest++;
        unsigned port = 0;
        while (*rest >= '0' && *rest <= '9')
            port = port * 10 + (unsigned)(*rest++ - '0');
        if (port == 0 || port > 65535)
            return false;
        out->port = (uint16_t)port;
    }

    if (*rest == '/' || *rest == '?')
        snprintf(out->path, sizeof(out->path), "%s%s", *rest == '?' ? "/" : "", rest);
    else
        strcpy(out->path, "/");

    char *hash = strchr(out->path, '#');
    if (hash)
        *hash = '\0';
    return true;
}

static void url_normalize_path(char *path)
{
    char query[URL_MAX] = "";
    char *q = strpbrk(path, "?#");
    if (q)
    {
        snprintf(query, sizeof(query), "%s", q);
        *q = '\0';
    }

    char out[URL_MAX];
    size_t len = 0;
    out[len++] = '/';
    const char *p = path;
    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t n = (size_t)(p - start);
        bool trailing = *p == '/';
        if (n == 1 && start[0] == '.')
            continue;
        if (n == 2 && start[0] == '.' && start[1] == '.')
        {
            if (len > 1)
            {
                len--;
                while (len > 1 && out[len - 1] != '/')
                    len--;
            }
            continue;
        }
        if (len + n + 2 >= sizeof(out))
            break;
        memcpy(out + len, start, n);
        len += n;
        if (trailing)
            out[len++] = '/';
    }
    if (len > 1 && path[strlen(path) - 1] != '/' && out[len - 1] == '/')
        len--;
    out[len] = '\0';
    snprintf(path, URL_MAX, "%s%s", out, query);
}

static void url_format(const Url *u, char *out, size_t size)
{
    if (strcmp(u->scheme, "file") == 0)
    {
        snprintf(out, size, "file://%s", u->path);
        return;
    }
    bool default_port = (strcmp(u->scheme, "https") == 0 && u->port == 443) ||
                        (strcmp(u->scheme, "http") == 0 && u->port == 80);
    if (default_port)
        snprintf(out, size, "%s://%s%s", u->scheme, u->host, u->path);
    else
        snprintf(out, size, "%s://%s:%u%s", u->scheme, u->host, u->port, u->path);
}

void url_resolve(const char *base, const char *ref, char *out, size_t size)
{
    while (*ref == ' ' || *ref == '\t' || *ref == '\n' || *ref == '\r')
        ref++;

    const char *colon = strchr(ref, ':');
    const char *slash = strchr(ref, '/');
    if (colon && (!slash || colon < slash))
    {
        snprintf(out, size, "%s", ref);
        return;
    }

    Url b;
    if (!url_parse(base, &b))
    {
        snprintf(out, size, "%s", ref);
        return;
    }

    if (ref[0] == '/' && ref[1] == '/')
    {
        snprintf(out, size, "%s:%s", b.scheme, ref);
        return;
    }

    if (ref[0] == '#')
    {
        char tmp[URL_MAX];
        url_format(&b, tmp, sizeof(tmp));
        snprintf(out, size, "%s%s", tmp, ref);
        return;
    }

    char path[URL_MAX];
    if (ref[0] == '/')
    {
        snprintf(path, sizeof(path), "%s", ref);
    }
    else if (ref[0] == '?' || ref[0] == '\0')
    {
        char base_path[URL_MAX];
        snprintf(base_path, sizeof(base_path), "%s", b.path);
        char *q = strchr(base_path, '?');
        if (q)
            *q = '\0';
        snprintf(path, sizeof(path), "%s%s", base_path, ref);
    }
    else
    {
        char dir[URL_MAX];
        snprintf(dir, sizeof(dir), "%s", b.path);
        char *q = strchr(dir, '?');
        if (q)
            *q = '\0';
        char *last = strrchr(dir, '/');
        if (last)
            last[1] = '\0';
        else
            strcpy(dir, "/");
        snprintf(path, sizeof(path), "%s%s", dir, ref);
    }

    url_normalize_path(path);
    snprintf(b.path, sizeof(b.path), "%s", path);
    url_format(&b, out, size);
}

typedef struct
{
    int      fd;
    TlsConn *tls;
}
HttpConn;

static int conn_read(HttpConn *c, void *buf, size_t len)
{
    if (c->tls)
        return tls_read(c->tls, buf, len);

    for (int waited = 0; waited < 20000; waited += 200)
    {
        struct pollfd pfd = { c->fd, POLLIN, 0 };
        if (poll(&pfd, 1, 200) > 0)
        {
            ssize_t n = read(c->fd, buf, len);
            return n < 0 ? -1 : (int)n;
        }
    }
    return -1;
}

static int conn_write(HttpConn *c, const void *data, size_t len)
{
    if (c->tls)
        return tls_write_all(c->tls, data, len);
    const uint8_t *p = data;
    size_t done = 0;
    while (done < len)
    {
        ssize_t n = write(c->fd, p + done, len - done);
        if (n <= 0)
            return -1;
        done += (size_t)n;
    }
    return 0;
}

static void conn_close(HttpConn *c)
{
    if (c->tls)
        tls_close(c->tls);
    if (c->fd >= 0)
        close(c->fd);
    c->tls = NULL;
    c->fd = -1;
}

static const char *header_value(const char *headers, const char *name)
{
    size_t n = strlen(name);
    const char *line = headers;
    while (line && *line)
    {
        if (strncasecmp(line, name, n) == 0 && line[n] == ':')
        {
            const char *v = line + n + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            return v;
        }
        line = strchr(line, '\n');
        if (line)
            line++;
    }
    return NULL;
}

static void header_copy(const char *headers, const char *name, char *out, size_t size)
{
    out[0] = '\0';
    const char *v = header_value(headers, name);
    if (!v)
        return;
    size_t n = strcspn(v, "\r\n");
    if (n >= size)
        n = size - 1;
    memcpy(out, v, n);
    out[n] = '\0';
}

static bool dechunk(uint8_t *data, size_t len, size_t *out_len)
{
    size_t in = 0, out = 0;
    for (;;)
    {
        size_t size = 0;
        bool any = false;
        while (in < len)
        {
            char ch = (char)data[in];
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                    ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0)
                break;
            size = size * 16 + (size_t)d;
            any = true;
            in++;
        }
        while (in < len && data[in] != '\n')
            in++;
        in++;
        if (!any)
            return false;
        if (size == 0)
            break;
        if (in + size > len)
            size = len > in ? len - in : 0;
        memmove(data + out, data + in, size);
        out += size;
        in += size;
        while (in < len && (data[in] == '\r' || data[in] == '\n'))
        {
            in++;
            if (data[in - 1] == '\n')
                break;
        }
        if (in >= len)
            break;
    }
    *out_len = out;
    return true;
}

uint8_t *http_decode_body(const char *encoding, uint8_t *body, size_t len, size_t *out_len)
{
    *out_len = len;
    if (!encoding[0] || strcasecmp(encoding, "identity") == 0)
        return body;

    const uint8_t *src = body;
    size_t src_len = len;
    if (strcasecmp(encoding, "gzip") == 0)
    {
        if (len < 18 || body[0] != 0x1F || body[1] != 0x8B)
            return body;
        uint8_t flags = body[3];
        size_t pos = 10;
        if (flags & 4)
            pos += 2 + (size_t)(body[pos] | (body[pos + 1] << 8));
        if (flags & 8)
            while (pos < len && body[pos++])
                ;
        if (flags & 16)
            while (pos < len && body[pos++])
                ;
        if (flags & 2)
            pos += 2;
        if (pos >= len)
            return body;
        src = body + pos;
        src_len = len - pos;
    }
    else if (strcasecmp(encoding, "deflate") == 0)
    {
        if (len > 2 && (body[0] & 0x0F) == 8 && ((body[0] << 8) | body[1]) % 31 == 0)
        {
            src = body + 2;
            src_len = len - 2;
        }
    }
    else
    {
        return body;
    }

    uint8_t *out = sx_inflate_alloc(src, src_len, HTTP_MAX_BODY, out_len);
    if (!out)
    {
        *out_len = len;
        return body;
    }
    free(body);
    return out;
}

static bool read_file_url(const Url *u, HttpResponse *out)
{
    int fd = open(u->path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || st.type != STAT_FILE)
    {
        if (fd >= 0)
            close(fd);
        snprintf(out->error, sizeof(out->error), "File not found: %s", u->path);
        return false;
    }
    out->body = malloc(st.size + 1);
    size_t got = 0;
    while (out->body && got < st.size)
    {
        ssize_t n = read(fd, out->body + got, st.size - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    close(fd);
    if (!out->body)
        return false;
    out->body[got] = 0;
    out->body_len = got;
    out->status = 200;
    const char *ext = strrchr(u->path, '.');
    if (ext && (strcasecmp(ext, ".png") == 0))
        strcpy(out->content_type, "image/png");
    else if (ext && (strcasecmp(ext, ".bmp") == 0))
        strcpy(out->content_type, "image/bmp");
    else if (ext && (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0))
        strcpy(out->content_type, "text/html");
    else
        strcpy(out->content_type, "text/plain");
    return true;
}

static bool http_fetch_once(const Url *u, HttpResponse *out, char *location, HttpProgress progress, void *user)
{
    location[0] = '\0';
    HttpConn c = { -1, NULL };

    if (progress && !progress(user, "Looking up", 0))
        return false;

    c.fd = net_connect(u->host, u->port);
    if (c.fd < 0)
    {
        if (c.fd == -ENETDOWN)
            snprintf(out->error, sizeof(out->error), "No network connection");
        else if (c.fd == -ETIMEDOUT)
            snprintf(out->error, sizeof(out->error), "%s took too long to respond", u->host);
        else if (c.fd == -ECONNREFUSED)
            snprintf(out->error, sizeof(out->error), "%s refused the connection", u->host);
        else
            snprintf(out->error, sizeof(out->error), "Couldn't find or reach %s", u->host);
        return false;
    }

    if (strcmp(u->scheme, "https") == 0)
    {
        if (progress && !progress(user, "Securing connection to", 0))
        {
            conn_close(&c);
            return false;
        }
        c.tls = tls_connect(c.fd, u->host, out->error, sizeof(out->error));
        if (!c.tls)
        {
            conn_close(&c);
            return false;
        }
    }

    char request[URL_MAX + 512];
    int rlen = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " HTTP_USER_AGENT "\r\n"
                        "Accept: text/html,application/xhtml+xml,image/png,*/*;q=0.8\r\n"
                        "Accept-Encoding: gzip, deflate\r\nAccept-Language: en\r\nConnection: close\r\n\r\n",
                        u->path, u->host);
    if (conn_write(&c, request, (size_t)rlen) != 0)
    {
        snprintf(out->error, sizeof(out->error), "Couldn't send the request to %s", u->host);
        conn_close(&c);
        return false;
    }

    size_t cap = 64 * 1024, len = 0;
    uint8_t *data = malloc(cap + 1);
    if (!data)
    {
        conn_close(&c);
        return false;
    }

    size_t header_end = 0, content_length = (size_t)-1;
    for (;;)
    {
        if (len == cap)
        {
            if (cap >= HTTP_MAX_BODY)
                break;
            cap *= 2;
            uint8_t *n = realloc(data, cap + 1);
            if (!n)
                break;
            data = n;
        }
        int n = conn_read(&c, data + len, cap - len);
        if (n <= 0)
            break;
        len += (size_t)n;

        if (!header_end)
        {
            data[len] = 0;
            char *end = strstr((char *)data, "\r\n\r\n");
            if (end)
            {
                header_end = (size_t)(end - (char *)data) + 4;
                char value[32];
                *end = '\0';
                header_copy((char *)data, "Content-Length", value, sizeof(value));
                *end = '\r';
                if (value[0])
                    content_length = (size_t)strtoull(value, NULL, 10);
            }
        }

        if (progress && !progress(user, "Downloading", header_end ? len - header_end : 0))
        {
            free(data);
            conn_close(&c);
            snprintf(out->error, sizeof(out->error), "Stopped");
            return false;
        }

        if (header_end && content_length != (size_t)-1 && len - header_end >= content_length)
            break;
    }
    conn_close(&c);

    if (!header_end)
    {
        free(data);
        snprintf(out->error, sizeof(out->error), "%s sent an invalid response", u->host);
        return false;
    }

    data[header_end - 2] = '\0';
    char *headers = (char *)data;
    if (strncmp(headers, "HTTP/", 5) != 0)
    {
        free(data);
        snprintf(out->error, sizeof(out->error), "%s sent an invalid response", u->host);
        return false;
    }
    const char *sp = strchr(headers, ' ');
    out->status = sp ? atoi(sp + 1) : 0;

    header_copy(headers, "Content-Type", out->content_type, sizeof(out->content_type));
    header_copy(headers, "Location", location, URL_MAX);

    char transfer[32], encoding[32];
    header_copy(headers, "Transfer-Encoding", transfer, sizeof(transfer));
    header_copy(headers, "Content-Encoding", encoding, sizeof(encoding));

    size_t body_len = len - header_end;
    if (content_length != (size_t)-1 && body_len > content_length)
        body_len = content_length;
    uint8_t *body = malloc(body_len + 1);
    if (!body)
    {
        free(data);
        return false;
    }
    memcpy(body, data + header_end, body_len);
    free(data);

    if (strcasestr(transfer, "chunked"))
        dechunk(body, body_len, &body_len);

    size_t decoded_len;
    body = http_decode_body(encoding, body, body_len, &decoded_len);
    uint8_t *final = realloc(body, decoded_len + 1);
    if (final)
        body = final;
    body[decoded_len] = 0;

    out->body = body;
    out->body_len = decoded_len;
    return true;
}

bool http_get(const char *url, HttpResponse *out, HttpProgress progress, void *user)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->url, sizeof(out->url), "%s", url);

    for (int redirect = 0; redirect <= HTTP_MAX_REDIRECT; redirect++)
    {
        Url u;
        if (!url_parse(out->url, &u))
        {
            snprintf(out->error, sizeof(out->error), "That doesn't look like a web address");
            return false;
        }

        if (strcmp(u.scheme, "file") == 0)
            return read_file_url(&u, out);

        if (strcmp(u.scheme, "http") != 0 && strcmp(u.scheme, "https") != 0)
        {
            snprintf(out->error, sizeof(out->error), "Unsupported address type \"%s\"", u.scheme);
            return false;
        }

        char location[URL_MAX];
        if (!http_fetch_once(&u, out, location, progress, user))
            return false;

        bool is_redirect = out->status == 301 || out->status == 302 || out->status == 303 ||
                           out->status == 307 || out->status == 308;
        if (!is_redirect || !location[0])
            return true;

        free(out->body);
        out->body = NULL;
        out->body_len = 0;
        char next[URL_MAX];
        url_resolve(out->url, location, next, sizeof(next));
        snprintf(out->url, sizeof(out->url), "%s", next);
    }

    snprintf(out->error, sizeof(out->error), "Too many redirects");
    return false;
}

void http_response_free(HttpResponse *r)
{
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}
