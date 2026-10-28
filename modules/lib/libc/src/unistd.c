#define PATH_BUF 256

static const char *abs_path(const char *path, char *out)
{
    if (!path)
        return NULL;

    char joined[PATH_BUF * 2];
    if (path[0] == '/')
        snprintf(joined, sizeof(joined), "%s", path);
    else
    {
        char cwd[PATH_BUF];
        if (!getcwd(cwd, sizeof(cwd)))
            strcpy(cwd, "/");
        snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }

    size_t len = 0;
    out[len++] = '/';
    const char *p = joined;
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

        if (len > 1)
        {
            if (len + 1 >= PATH_BUF)
                return NULL;
            out[len++] = '/';
        }
        if (len + n >= PATH_BUF)
            return NULL;
        memcpy(out + len, start, n);
        len += n;
    }
    if (len > 1 && out[len - 1] == '/')
        len--;
    out[len] = '\0';
    return out;
}

char *getcwd(char *buf, size_t size)
{
    if ((int64_t)kcall(KCALL_GETCWD, (uint64_t)buf, size, 0, 0) < 0)
        return NULL;
    return buf;
}

int chdir(const char *path)
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;
    return (int)kcall(KCALL_CHDIR, (uint64_t)full, 0, 0, 0);
}

char *realpath(const char *path, char *resolved)
{
    char buf[PATH_BUF];
    if (!abs_path(path, buf))
        return NULL;
    if (resolved)
        return strcpy(resolved, buf);
    return strdup(buf);
}

int dup2(int fd, int target)
{
    return (int)kcall(KCALL_DUP2, (uint64_t)(int64_t)fd, (uint64_t)(int64_t)target, 0, 0);
}

static char *empty_environ[] = { NULL };
char **environ = empty_environ;

int open(const char *path, int flags, ...)
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;

    if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY)
    {
        struct stat st;
        if (stat(full, &st) == 0)
        {
            unlink(full);
            flags |= O_CREAT;
        }
    }
    flags &= ~O_TRUNC;

    return (int)kcall(KCALL_OPEN, (uint64_t)full, (uint64_t)flags, 0, 0);
}

int execvp(const char *file, char *const argv[])
{
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)kcall(KCALL_WRITE, (uint64_t)fd, (uint64_t)buf, count, 0);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)kcall(KCALL_READ, (uint64_t)fd, (uint64_t)buf, count, 0);
}

int close(int fd)
{
    return (int)kcall(KCALL_CLOSE, (uint64_t)fd, 0, 0, 0);
}

int mkdir(const char *path, unsigned int mode)
{
    (void)mode;
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;
    return (int)kcall(KCALL_MKDIR, (uint64_t)full, 0, 0, 0);
}

int readdir(int fd, struct dirent *out)
{
    return (int)kcall(KCALL_READDIR, (uint64_t)fd, (uint64_t)out, 0, 0);
}

int rename(const char *from, const char *to)
{
    char a[PATH_BUF], b[PATH_BUF];
    if (!abs_path(from, a) || !abs_path(to, b))
        return -1;
    return (int)kcall(KCALL_RENAME, (uint64_t)a, (uint64_t)b, 0, 0);
}

int remove(const char *path)
{
    return unlink(path);
}

int unlink(const char *path)
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;
    return (int)kcall(KCALL_UNLINK, (uint64_t)full, 0, 0, 0);
}

int64_t lseek(int fd, int64_t offset, int whence)
{
    return (int64_t)kcall(KCALL_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence, 0);
}

static void stat_fill_posix(struct stat *st)
{
    static const mode_t modes[] = { S_IFREG | 0644, S_IFDIR | 0755, S_IFCHR | 0666, S_IFSOCK | 0666 };
    st->st_mode = (unsigned)st->type < 4 ? modes[st->type] : S_IFREG | 0644;
    st->st_size = (off_t)st->size;
    st->st_nlink = 1;
    st->st_ino = 0;
    st->st_dev = 0;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_blksize = 4096;
    st->st_blocks = (blkcnt_t)((st->size + 511) / 512);
    st->st_atime = st->st_mtime = st->st_ctime = 0;
}

int stat(const char *path, struct stat *statbuf)
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;
    int rc = (int)kcall(KCALL_STAT, (uint64_t)full, (uint64_t)statbuf, 0, 0);
    if (rc < 0)
    {
        errno = ENOENT;
        return -1;
    }
    stat_fill_posix(statbuf);
    return rc;
}

int lstat(const char *path, struct stat *statbuf)
{
    return stat(path, statbuf);
}

int fstat(int fd, struct stat *statbuf)
{
    int rc = (int)kcall(KCALL_FSTAT, (uint64_t)fd, (uint64_t)statbuf, 0, 0);
    if (rc < 0)
    {
        errno = EBADF;
        return -1;
    }
    stat_fill_posix(statbuf);
    return rc;
}

void wait(uint32_t ms)
{
    kcall(KCALL_WAIT, ms, 0, 0, 0);
}

pid_t spawn(const char *path, const char *argv[])
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return 0;
    return (pid_t)kcall(KCALL_SPAWN, (uint64_t)full, (uint64_t)argv, 0, 0);
}

int kill(pid_t pid)
{
    return (int)kcall(KCALL_KILL, (uint64_t)pid, 0, 0, 0);
}

pid_t getpid(void)
{
    return (pid_t)kcall(KCALL_GETPID, 0, 0, 0, 0);
}

int task_status(pid_t pid, int *code)
{
    return (int)kcall(KCALL_WAITPID, (uint64_t)pid, (uint64_t)code, 0, 0);
}

int waitpid(pid_t pid, uint32_t flags)
{
    int code = 0;
    for (;;)
    {
        int st = task_status(pid, &code);
        if (st == 1)
            return code;
        if (flags & WNOHANG)
            return -1;
        wait(10);
    }
}