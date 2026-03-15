#include "vfs.h"

#include <string.h>
#include <memory.h>

#include "memory/heap.h"

static Mount    g_mounts[VFS_MOUNT_MAX];
static OpenFile *g_fds[VFS_FD_MAX];

static volatile int g_lock;
static inline void vfs_lock(void)   { while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)); }
static inline void vfs_unlock(void) { __atomic_clear(&g_lock, __ATOMIC_RELEASE); }

void vnode_ref(VNode *node)
{
    if (node) __atomic_fetch_add(&node->refcount, 1, __ATOMIC_RELAXED);
}

void vnode_unref(VNode *node)
{
    if (!node) return;
    if (__atomic_fetch_sub(&node->refcount, 1, __ATOMIC_ACQ_REL) == 1)
        node->ops->release(node);
}

void vfs_init(void)
{
    memset(g_mounts, 0, sizeof(g_mounts));
    memset(g_fds,    0, sizeof(g_fds));

    /* Reserve 0/1/2 for stdin/stdout/stderr. */
    g_fds[0] = (OpenFile *)(uintptr_t)1;
    g_fds[1] = (OpenFile *)(uintptr_t)1;
    g_fds[2] = (OpenFile *)(uintptr_t)1;
}

/* ---------- mount ---------- */

int vfs_mount(const char *path, VNode *root)
{
    if (!path || !root) return -EINVAL;
    size_t plen = strlen(path);
    if (plen >= VFS_PATH_MAX) return -EINVAL;

    vfs_lock();
    for (int i = 0; i < VFS_MOUNT_MAX; i++) {
        if (g_mounts[i].active) continue;
        memcpy(g_mounts[i].path, path, plen + 1);
        g_mounts[i].root   = root;
        g_mounts[i].active = true;
        vnode_ref(root);
        root->mount = &g_mounts[i];
        vfs_unlock();
        return 0;
    }
    vfs_unlock();
    return -ENOSPC;
}

int vfs_unmount(const char *path)
{
    if (!path) return -EINVAL;
    vfs_lock();
    for (int i = 0; i < VFS_MOUNT_MAX; i++) {
        if (!g_mounts[i].active) continue;
        if (strcmp(g_mounts[i].path, path) != 0) continue;
        vnode_unref(g_mounts[i].root);
        g_mounts[i].active = false;
        vfs_unlock();
        return 0;
    }
    vfs_unlock();
    return -ENOENT;
}

static Mount *find_mount(const char *path)
{
    Mount  *best = NULL;
    size_t  blen = 0;

    for (int i = 0; i < VFS_MOUNT_MAX; i++) {
        if (!g_mounts[i].active) continue;
        size_t mlen = strlen(g_mounts[i].path);
        if (strncmp(g_mounts[i].path, path, mlen) != 0) continue;
        if (path[mlen] != '\0' && path[mlen] != '/' && mlen != 1) continue;
        if (mlen > blen) { best = &g_mounts[i]; blen = mlen; }
    }
    return best;
}

/* ---------- lookup ---------- */

int vfs_lookup(const char *path, VNode **out)
{
    if (!path || path[0] != '/') return -EINVAL;

    vfs_lock();
    Mount *m = find_mount(path);
    if (!m) { vfs_unlock(); return -ENOENT; }

    VNode *cur = m->root;
    vnode_ref(cur);

    const char *rest = path + strlen(m->path);
    vfs_unlock();

    while (*rest) {
        while (*rest == '/') rest++;
        if (*rest == '\0') break;

        const char *start = rest;
        while (*rest && *rest != '/') rest++;
        size_t clen = (size_t)(rest - start);

        if (clen == 0 || clen > VFS_NAME_MAX) { vnode_unref(cur); return -EINVAL; }

        char component[VFS_NAME_MAX + 1];
        memcpy(component, start, clen);
        component[clen] = '\0';

        if (strcmp(component, ".") == 0) continue;

        /* ".." navigation: walk up via mount root (best-effort, no parent pointer) */
        if (strcmp(component, "..") == 0) {
            if (cur != m->root)
                /* Full ".." support would need parent pointers; skip for simplicity. */
                { vnode_unref(cur); return -EINVAL; }
            continue;
        }

        if (cur->type != VNODE_DIR) { vnode_unref(cur); return -ENOTDIR; }

        VNode *child = NULL;
        int err = cur->ops->lookup(cur, component, &child);
        vnode_unref(cur);
        if (err) return err;
        cur = child;
    }

    *out = cur;
    return 0;
}

/* ---------- helpers ---------- */

static int split_path(const char *path, char *parent_buf, size_t buf_sz,
                      const char **base_out)
{
    size_t len = strlen(path);
    if (len == 0 || path[0] != '/') return -EINVAL;

    const char *last_slash = path + len - 1;
    while (last_slash > path && *last_slash != '/') last_slash--;

    size_t parent_len = (size_t)(last_slash - path);
    if (parent_len == 0) parent_len = 1;   /* root */
    if (parent_len >= buf_sz) return -EINVAL;

    memcpy(parent_buf, path, parent_len);
    parent_buf[parent_len] = '\0';

    *base_out = last_slash + 1;
    if (**base_out == '\0') return -EINVAL;
    return 0;
}

static int alloc_fd(OpenFile *f)
{
    for (int i = 0; i < VFS_FD_MAX; i++) {
        if (!g_fds[i]) { g_fds[i] = f; return i; }
    }
    return -EMFILE;
}

static OpenFile *get_fd(int fd)
{
    if (fd < 0 || fd >= VFS_FD_MAX) return NULL;
    return g_fds[fd];
}

/* ---------- open / close ---------- */

int vfs_open(const char *path, int flags)
{
    VNode *node = NULL;
    int err = vfs_lookup(path, &node);

    if (err == -ENOENT && (flags & O_CREAT)) {
        char parent[VFS_PATH_MAX];
        const char *base;
        err = split_path(path, parent, sizeof(parent), &base);
        if (err) return err;

        VNode *dir = NULL;
        err = vfs_lookup(parent, &dir);
        if (err) return err;

        if (dir->type != VNODE_DIR) { vnode_unref(dir); return -ENOTDIR; }

        err = dir->ops->create(dir, base, &node);
        vnode_unref(dir);
        if (err) return err;
    } else if (err) {
        return err;
    }

    if ((flags & O_TRUNC) && node->type == VNODE_FILE &&
        (flags & O_ACCMODE) != O_RDONLY) {
        /* Filesystems that care about truncation must handle this via write(0). */
        node->ops->write(node, NULL, 0, 0);
    }

    OpenFile *f = (OpenFile *)kmalloc(sizeof(OpenFile));
    if (!f) { vnode_unref(node); return -ENOMEM; }

    f->node  = node;
    f->flags = flags;

    if (flags & O_APPEND) {
        VStat st;
        node->ops->stat(node, &st);
        f->offset = st.st_size;
    } else {
        f->offset = 0;
    }

    vfs_lock();
    int fd = alloc_fd(f);
    vfs_unlock();

    if (fd < 0) { kfree(f); vnode_unref(node); return fd; }
    return fd;
}

int vfs_close(int fd)
{
    vfs_lock();
    OpenFile *f = get_fd(fd);
    if (!f) { vfs_unlock(); return -EBADF; }
    g_fds[fd] = NULL;
    vfs_unlock();

    vnode_unref(f->node);
    kfree(f);
    return 0;
}

/* ---------- read / write / seek ---------- */

ssize_t vfs_read(int fd, void *buf, size_t len)
{
    OpenFile *f = get_fd(fd);
    if (!f) return -EBADF;
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -EACCES;
    if (f->node->type == VNODE_DIR) return -EISDIR;

    ssize_t n = f->node->ops->read(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

ssize_t vfs_write(int fd, const void *buf, size_t len)
{
    OpenFile *f = get_fd(fd);
    if (!f) return -EBADF;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EACCES;
    if (f->node->type == VNODE_DIR) return -EISDIR;

    if (f->flags & O_APPEND) {
        VStat st;
        f->node->ops->stat(f->node, &st);
        f->offset = st.st_size;
    }

    ssize_t n = f->node->ops->write(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

int64_t vfs_seek(int fd, int64_t offset, int whence)
{
    OpenFile *f = get_fd(fd);
    if (!f) return -EBADF;

    VStat st;
    f->node->ops->stat(f->node, &st);

    int64_t new_off;
    switch (whence) {
        case SEEK_SET: new_off = offset; break;
        case SEEK_CUR: new_off = (int64_t)f->offset + offset; break;
        case SEEK_END: new_off = (int64_t)st.st_size + offset; break;
        default: return -EINVAL;
    }
    if (new_off < 0) return -EINVAL;
    f->offset = (uint64_t)new_off;
    return new_off;
}

/* ---------- stat ---------- */

int vfs_stat(const char *path, VStat *out)
{
    VNode *node;
    int err = vfs_lookup(path, &node);
    if (err) return err;
    err = node->ops->stat(node, out);
    vnode_unref(node);
    return err;
}

int vfs_fstat(int fd, VStat *out)
{
    OpenFile *f = get_fd(fd);
    if (!f) return -EBADF;
    return f->node->ops->stat(f->node, out);
}

/* ---------- directory operations ---------- */

int vfs_mkdir(const char *path)
{
    char parent[VFS_PATH_MAX];
    const char *base;
    int err = split_path(path, parent, sizeof(parent), &base);
    if (err) return err;

    VNode *dir;
    err = vfs_lookup(parent, &dir);
    if (err) return err;
    if (dir->type != VNODE_DIR) { vnode_unref(dir); return -ENOTDIR; }

    VNode *newdir;
    err = dir->ops->mkdir(dir, base, &newdir);
    vnode_unref(dir);
    if (!err) vnode_unref(newdir);
    return err;
}

int vfs_remove(const char *path)
{
    char parent[VFS_PATH_MAX];
    const char *base;
    int err = split_path(path, parent, sizeof(parent), &base);
    if (err) return err;

    VNode *dir;
    err = vfs_lookup(parent, &dir);
    if (err) return err;
    err = dir->ops->remove(dir, base);
    vnode_unref(dir);
    return err;
}

int vfs_readdir(int fd, uint64_t index, VDirent *out)
{
    OpenFile *f = get_fd(fd);
    if (!f) return -EBADF;
    if (f->node->type != VNODE_DIR) return -ENOTDIR;
    return f->node->ops->readdir(f->node, index, out);
}

int vfs_rename(const char *src, const char *dst)
{
    if (!src || !dst) return -EINVAL;

    /* Trivial self-rename. */
    if (strcmp(src, dst) == 0) return 0;

    /* ---- resolve source parent + base ------------------------------------ */
    char       src_parent_path[VFS_PATH_MAX];
    const char *src_base;
    int err = split_path(src, src_parent_path, sizeof(src_parent_path), &src_base);
    if (err) return err;

    /* ---- resolve destination parent + base ------------------------------- */
    char       dst_parent_path[VFS_PATH_MAX];
    const char *dst_base;
    err = split_path(dst, dst_parent_path, sizeof(dst_parent_path), &dst_base);
    if (err) return err;

    /* ---- look up both parent directories --------------------------------- */
    VNode *src_dir = NULL;
    err = vfs_lookup(src_parent_path, &src_dir);
    if (err) return err;

    VNode *dst_dir = NULL;
    err = vfs_lookup(dst_parent_path, &dst_dir);
    if (err) { vnode_unref(src_dir); return err; }

    /* Both parents must be directories. */
    if (src_dir->type != VNODE_DIR || dst_dir->type != VNODE_DIR) {
        err = -ENOTDIR;
        goto out_dirs;
    }

    /* Reject cross-mount renames. */
    if (src_dir->mount != dst_dir->mount) {
        err = -EINVAL;
        goto out_dirs;
    }

    /* ---- look up the source node ---------------------------------------- */
    VNode *src_node = NULL;
    err = src_dir->ops->lookup(src_dir, src_base, &src_node);
    if (err) goto out_dirs;

    /* ---- handle a pre-existing destination ------------------------------- */
    VNode *dst_node = NULL;
    int dst_exists = dst_dir->ops->lookup(dst_dir, dst_base, &dst_node);

    if (dst_exists == 0) {
        /* src and dst are already the same inode -- nothing to do. */
        if (dst_node->ino == src_node->ino) {
            vnode_unref(dst_node);
            vnode_unref(src_node);
            err = 0;
            goto out_dirs;
        }

        /* Type-compatibility: can't replace a directory with a file or
         * vice-versa (matches Linux rename(2) behaviour). */
        if (dst_node->type != src_node->type) {
            err = (dst_node->type == VNODE_DIR) ? -EISDIR : -ENOTDIR;
            vnode_unref(dst_node);
            vnode_unref(src_node);
            goto out_dirs;
        }

        /* Destination directory must be empty before it can be replaced. */
        if (dst_node->type == VNODE_DIR) {
            VDirent de;
            if (dst_node->ops->readdir(dst_node, 0, &de) == 1) {
                vnode_unref(dst_node);
                vnode_unref(src_node);
                err = -ENOTEMPTY;
                goto out_dirs;
            }
        }

        vnode_unref(dst_node);

        /* Remove the destination entry so the driver can take its place. */
        err = dst_dir->ops->remove(dst_dir, dst_base);
        if (err) { vnode_unref(src_node); goto out_dirs; }
    }
    /* else: -ENOENT -- destination slot is free, nothing to evict. */

    vnode_unref(src_node);

    /* ---- delegate the actual relink to the filesystem driver ------------- */
    if (!src_dir->ops->rename) {
        err = -EINVAL;
        goto out_dirs;
    }

    err = src_dir->ops->rename(src_dir, src_base, dst_dir, dst_base);

out_dirs:
    vnode_unref(dst_dir);
    vnode_unref(src_dir);
    return err;
}