// Minimal HTTP server for StrawOS.
// Build and run inside the OS:  tcc http_server_test.c -o http_server_test && ./http_server_test [port]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <socket.h>
#include <net.h>

#define DEFAULT_PORT 8080

static int send_all(int fd, const char *buf, int len)
{
    while (len > 0)
    {
        int n = (int)write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static void respond(int fd, const char *status, const char *type, const char *body)
{
    char head[256];
    int body_len = (int)strlen(body);
    int n = snprintf(head, sizeof(head),
        "HTTP/1.0 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", status, type, body_len);
    if (send_all(fd, head, n) == 0)
        send_all(fd, body, body_len);
}

static void handle(int fd, int hits)
{
    char req[2048];
    int len = 0;

    // read until the end of the request headers
    while (len < (int)sizeof(req) - 1)
    {
        int n = (int)read(fd, req + len, sizeof(req) - 1 - len);
        if (n <= 0)
            break;
        len += n;
        req[len] = '\0';
        if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n"))
            break;
    }
    req[len] = '\0';

    char method[8] = "", path[256] = "";
    sscanf(req, "%7s %255s", method, path);
    printf("%s %s\n", method, path);

    if (strcmp(method, "GET") != 0)
    {
        respond(fd, "405 Method Not Allowed", "text/plain", "method not allowed\n");
        return;
    }

    if (strcmp(path, "/") == 0)
    {
        char body[1024];
        snprintf(body, sizeof(body),
            "<!doctype html>\n"
            "<html><head><meta charset=\"utf-8\"><title>StrawOS</title></head>\n"
            "<body style=\"font-family:sans-serif;max-width:40em;margin:3em auto\">\n"
            "<h1>Hello from StrawOS</h1>\n"
            "<p>This page is served by a tcc-compiled program running inside the OS.</p>\n"
            "<p>Requests served so far: %d</p>\n"
            "</body></html>\n", hits);
        respond(fd, "200 OK", "text/html; charset=utf-8", body);
    }
    else
    {
        respond(fd, "404 Not Found", "text/plain", "not found\n");
    }
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;

    int s = socket(SOCK_TCP);
    if (s < 0)
    {
        printf("socket failed: %d\n", s);
        return 1;
    }

    char addr[32];
    snprintf(addr, sizeof(addr), "0.0.0.0:%d", port);
    int rc = bind(s, addr);
    if (rc < 0)
    {
        printf("bind %s failed: %d\n", addr, rc);
        return 1;
    }
    rc = listen(s);
    if (rc < 0)
    {
        printf("listen failed: %d\n", rc);
        return 1;
    }

    struct net_info info;
    if (net_info(&info) == 0 && info.up)
        printf("listening on http://%d.%d.%d.%d:%d/\n", info.ip[0], info.ip[1], info.ip[2], info.ip[3], port);
    else
        printf("listening on port %d (network is not up yet)\n", port);

    for (int hits = 1;; hits++)
    {
        int c = accept(s);
        if (c < 0)
        {
            printf("accept failed: %d\n", c);
            break;
        }
        handle(c, hits);
        close(c);
    }

    close(s);
    return 0;
}
