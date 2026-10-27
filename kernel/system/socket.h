#define SOCK_NONBLOCK 0x1
#define SOCK_TCP      0x10
#define SOCK_UDP      0x20

#define SOCK_NAME_MAX 64
#define SOCK_BUF_SIZE (64 * 1024)
#define SOCK_BACKLOG  32

#define EAGAIN       11
#define EINVAL       22
#define EPIPE        32
#define EADDRINUSE   98
#define ECONNREFUSED 111
#define EMFILE       24
#define ENOMEM       12
#define EBADF        9
#define ENETDOWN     100
#define EINPROGRESS 115
#define ETIMEDOUT    110

int64_t sock_sys_socket(uint32_t flags);
int64_t sock_sys_bind(int fd, const char *name);
int64_t sock_sys_listen(int fd);
int64_t sock_sys_accept(int fd);
int64_t sock_sys_connect(int fd, const char *name);
