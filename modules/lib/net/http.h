#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define URL_MAX 1024

typedef struct
{
    char     scheme[8];
    char     host[256];
    uint16_t port;
    char     path[URL_MAX];
}
Url;

typedef struct
{
    int     status;
    char    url[URL_MAX];
    char    content_type[128];
    uint8_t *body;
    size_t  body_len;
    char    error[160];
}
HttpResponse;

typedef bool (*HttpProgress)(void *user, const char *stage, size_t received);

bool url_parse(const char *text, Url *out);
void url_resolve(const char *base, const char *ref, char *out, size_t size);

bool http_get(const char *url, HttpResponse *out, HttpProgress progress, void *user);
void http_response_free(HttpResponse *r);

uint8_t *http_decode_body(const char *encoding, uint8_t *body, size_t len, size_t *out_len);
