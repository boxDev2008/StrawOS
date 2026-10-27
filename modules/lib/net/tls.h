#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct TlsConn TlsConn;

TlsConn *tls_connect(int fd, const char *host, char *err, size_t err_size);
int      tls_write_all(TlsConn *c, const void *data, size_t len);
int      tls_read(TlsConn *c, void *buf, size_t len);
void     tls_close(TlsConn *c);

TlsConn *tls_start(int fd, const char *host, char *err, size_t err_size);
int      tls_pump(TlsConn *c);
bool     tls_ready(TlsConn *c);
int      tls_send(TlsConn *c, const void *data, size_t len);
int      tls_recv(TlsConn *c, void *buf, size_t len);
void     tls_error(TlsConn *c, char *err, size_t err_size);
void     tls_free(TlsConn *c);
void     tls_entropy(unsigned char out[32]);
bool     tls_pending(TlsConn *c);
