#include <dirent.h>
#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* ── opendir ──────────────────────────────────────────────────────────── */

DIR *opendir(const char *name)
{
    int fd = open(name, O_DIRECTORY);
    if (fd < 0) return NULL;

    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd    = fd;
    dir->index = 0;
    return dir;
}

/* ── rewinddir ────────────────────────────────────────────────────────── */

void rewinddir(DIR *dir)
{
    if (dir) dir->index = 0;
}

/* ── readdir_r (low-level indexed read) ──────────────────────────────── */
/*
 * Returns 1 if an entry was read, 0 at end-of-directory, <0 on error.
 * Matches the kernel's ramfs_readdir convention exactly.
 */
int readdir_r(DIR *dir, uint64_t index, struct dirent *out)
{
    if (!dir || !out) return -1;
    return (int)syscall3(SYS_READDIR, (uint64_t)dir->fd, index, (uint64_t)out);
}

/* ── readdir (POSIX-style stateful iteration) ─────────────────────────── */

struct dirent *readdir(DIR *dir)
{
    if (!dir) return NULL;

    /* We keep a single static buffer per DIR — caller must not free it. */
    static struct dirent _ent;

    int rc = (int)syscall3(SYS_READDIR,
                           (uint64_t)dir->fd,
                           dir->index,
                           (uint64_t)&_ent);
    if (rc <= 0) return NULL;  /* 0 = end of directory, <0 = error */

    dir->index++;
    return &_ent;
}

/* ── closedir ─────────────────────────────────────────────────────────── */

int closedir(DIR *dir)
{
    if (!dir) return -1;
    int rc = close(dir->fd);
    free(dir);
    return rc;
}