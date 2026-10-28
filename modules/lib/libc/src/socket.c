int socket(uint32_t flags)
{
    return (int)kcall(KCALL_SOCKET, flags, 0, 0, 0);
}

int bind(int fd, const char *name)
{
    return (int)kcall(KCALL_BIND, (uint64_t)fd, (uint64_t)name, 0, 0);
}

int listen(int fd)
{
    return (int)kcall(KCALL_LISTEN, (uint64_t)fd, 0, 0, 0);
}

int accept(int fd)
{
    return (int)kcall(KCALL_ACCEPT, (uint64_t)fd, 0, 0, 0);
}

int connect(int fd, const char *name)
{
    return (int)kcall(KCALL_CONNECT, (uint64_t)fd, (uint64_t)name, 0, 0);
}

int poll(struct pollfd *fds, uint64_t count, int64_t timeout_ms)
{
    return (int)kcall(KCALL_POLL, (uint64_t)fds, count, (uint64_t)timeout_ms, 0);
}
