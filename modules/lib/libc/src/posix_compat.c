intmax_t strtoimax(const char *s, char **end, int base)
{
    return (intmax_t)strtoll(s, end, base);
}

uintmax_t strtoumax(const char *s, char **end, int base)
{
    return (uintmax_t)strtoull(s, end, base);
}

intmax_t imaxabs(intmax_t x)
{
    return x < 0 ? -x : x;
}

sighandler_t signal(int sig, sighandler_t handler)
{
    (void)sig;
    (void)handler;
    return SIG_DFL;
}

int raise(int sig)
{
    if (sig == SIGABRT || sig == SIGKILL || sig == SIGTERM || sig == SIGINT)
        exit(128 + sig);
    return 0;
}

int uname(struct utsname *buf)
{
    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "StrawOS");
    strcpy(buf->nodename, "strawos");
    strcpy(buf->release, "1.0");
    strcpy(buf->version, "1");
    strcpy(buf->machine, "x86_64");
    return 0;
}

uint32_t htonl(uint32_t x)
{
    return __builtin_bswap32(x);
}

uint16_t htons(uint16_t x)
{
    return __builtin_bswap16(x);
}

uint32_t ntohl(uint32_t x)
{
    return __builtin_bswap32(x);
}

uint16_t ntohs(uint16_t x)
{
    return __builtin_bswap16(x);
}

int inet_aton(const char *s, struct in_addr *out)
{
    uint32_t parts[4];
    int count = 0;
    const char *p = s;
    while (count < 4)
    {
        char *end;
        if (!isdigit((unsigned char)*p))
            return 0;
        unsigned long v = strtoul(p, &end, 0);
        parts[count++] = (uint32_t)v;
        p = end;
        if (*p != '.')
            break;
        p++;
    }
    if (*p != '\0')
        return 0;

    uint32_t addr;
    switch (count)
    {
        case 1:
            addr = parts[0];
            break;
        case 2:
            if (parts[0] > 255 || parts[1] > 0xffffff) return 0;
            addr = parts[0] << 24 | parts[1];
            break;
        case 3:
            if (parts[0] > 255 || parts[1] > 255 || parts[2] > 0xffff) return 0;
            addr = parts[0] << 24 | parts[1] << 16 | parts[2];
            break;
        default:
            if (parts[0] > 255 || parts[1] > 255 || parts[2] > 255 || parts[3] > 255) return 0;
            addr = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
            break;
    }
    if (out)
        out->s_addr = htonl(addr);
    return 1;
}

in_addr_t inet_addr(const char *s)
{
    struct in_addr a;
    return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

static int inet_pton4(const char *s, uint8_t out[4])
{
    int count = 0;
    while (count < 4)
    {
        int v = 0, digits = 0;
        while (isdigit((unsigned char)*s) && digits < 4)
        {
            v = v * 10 + (*s++ - '0');
            digits++;
        }
        if (digits == 0 || v > 255)
            return 0;
        out[count++] = (uint8_t)v;
        if (count < 4)
        {
            if (*s != '.')
                return 0;
            s++;
        }
    }
    return *s == '\0';
}

static int inet_pton6(const char *s, uint8_t out[16])
{
    uint16_t words[8] = { 0 };
    int count = 0, gap = -1;
    if (s[0] == ':' && s[1] == ':')
    {
        gap = 0;
        s += 2;
        if (!*s)
            goto done;
    }
    while (*s && count < 8)
    {
        int v = 0, digits = 0;
        while (isxdigit((unsigned char)*s) && digits < 4)
        {
            v = v * 16 + (isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10));
            s++;
            digits++;
        }
        if (digits == 0)
            return 0;
        if (*s == '.')
        {
            return 0;
        }
        words[count++] = (uint16_t)v;
        if (*s == ':')
        {
            s++;
            if (*s == ':')
            {
                if (gap >= 0)
                    return 0;
                gap = count;
                s++;
                if (!*s)
                    break;
            }
            else if (!*s)
            {
                return 0;
            }
        }
        else if (*s)
        {
            return 0;
        }
    }
    if (*s)
        return 0;
done:
    if (gap >= 0)
    {
        int move = count - gap;
        for (int i = 0; i < move; i++)
            words[7 - i] = words[count - 1 - i];
        for (int i = gap; i < 8 - move; i++)
            words[i] = 0;
    }
    else if (count != 8)
    {
        return 0;
    }
    for (int i = 0; i < 8; i++)
    {
        out[i * 2] = (uint8_t)(words[i] >> 8);
        out[i * 2 + 1] = (uint8_t)words[i];
    }
    return 1;
}

int inet_pton(int af, const char *s, void *out)
{
    if (af == AF_INET)
        return inet_pton4(s, out);
    if (af == AF_INET6)
        return inet_pton6(s, out);
    errno = EINVAL;
    return -1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    const uint8_t *b = src;
    if (af == AF_INET)
    {
        if (snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]) >= (int)size)
            return NULL;
        return dst;
    }
    if (af == AF_INET6)
    {
        size_t pos = 0;
        for (int i = 0; i < 8; i++)
        {
            int n = snprintf(dst + pos, size - pos, i ? ":%x" : "%x", b[i * 2] << 8 | b[i * 2 + 1]);
            if (n < 0 || pos + (size_t)n >= size)
                return NULL;
            pos += (size_t)n;
        }
        return dst;
    }
    errno = EINVAL;
    return NULL;
}

int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout)
{
    (void)nfds;
    if (r) FD_ZERO(r);
    if (w) FD_ZERO(w);
    if (e) FD_ZERO(e);
    if (timeout)
    {
        uint64_t ms = (uint64_t)timeout->tv_sec * 1000 + (uint64_t)timeout->tv_usec / 1000;
        if (ms)
            wait((uint32_t)ms);
    }
    return 0;
}

int access(const char *path, int mode)
{
    (void)mode;
    struct stat st;
    if (stat(path, &st) != 0)
    {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int rmdir(const char *path)
{
    return unlink(path);
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    int64_t saved = lseek(fd, 0, SEEK_CUR);
    if (lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    ssize_t n = read(fd, buf, count);
    lseek(fd, saved, SEEK_SET);
    return n;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    int64_t saved = lseek(fd, 0, SEEK_CUR);
    if (lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    ssize_t n = write(fd, buf, count);
    lseek(fd, saved, SEEK_SET);
    return n;
}

int isatty(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 && st.type == STAT_DEVICE;
}

unsigned int sleep(unsigned int seconds)
{
    wait(seconds * 1000);
    return 0;
}

int usleep(useconds_t usec)
{
    if (usec >= 1000)
        wait(usec / 1000);
    else
        yield();
    return 0;
}

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }

int gethostname(char *name, size_t len)
{
    snprintf(name, len, "strawos");
    return 0;
}

int fcntl(int fd, int cmd, ...)
{
    (void)fd;
    (void)cmd;
    return 0;
}

int unlinkat(int dirfd, const char *path, int flags)
{
    (void)dirfd;
    (void)flags;
    return unlink(path);
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    (void)dirfd;
    (void)flags;
    return stat(path, st);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)prot;
    if (length == 0)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    void *mem = (void *)kcall(KCALL_MEM_ALLOC, length, 0, 0, 0);
    if (!mem)
    {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(mem, 0, length);
    if (!(flags & MAP_ANONYMOUS))
    {
        int64_t saved = lseek(fd, 0, SEEK_CUR);
        if (lseek(fd, offset, SEEK_SET) < 0)
        {
            kcall(KCALL_MEM_FREE, (uint64_t)mem, length, 0, 0);
            errno = EBADF;
            return MAP_FAILED;
        }
        size_t done = 0;
        while (done < length)
        {
            ssize_t n = read(fd, (char *)mem + done, length - done);
            if (n <= 0)
                break;
            done += (size_t)n;
        }
        if (saved >= 0)
            lseek(fd, saved, SEEK_SET);
    }
    return mem;
}

int munmap(void *addr, size_t length)
{
    if ((int64_t)kcall(KCALL_MEM_FREE, (uint64_t)addr, length, 0, 0) < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
    (void)addr;
    (void)length;
    (void)prot;
    return 0;
}
