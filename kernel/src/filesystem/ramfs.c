#include "ramfs.h"
#include "memory/heap.h"

#include <string.h>

typedef struct RamDirEntry
{
    char name[VFS_NAME_MAX + 1];
    VNode *node;
    struct RamDirEntry *next;
}
RamDirEntry;

typedef struct
{
    volatile int  lock;
    uint64_t      ino;

    uint8_t  *buf;
    uint64_t  size;
    uint64_t  cap;

    RamDirEntry *entries;
    uint64_t     entry_count;
}
RamInode;

static inline void inode_lock(RamInode *ri)   { while (__atomic_test_and_set(&ri->lock, __ATOMIC_ACQUIRE)); }
static inline void inode_unlock(RamInode *ri) { __atomic_clear(&ri->lock, __ATOMIC_RELEASE); }

static volatile uint64_t g_next_ino = 1;
static inline uint64_t   alloc_ino(void)
{
    return __atomic_fetch_add(&g_next_ino, 1, __ATOMIC_RELAXED);
}

static VNodeOps g_ramfs_ops;

static VNode *ramfs_alloc_vnode(VNodeType type)
{
    VNode    *vn = (VNode *)kmalloc(sizeof(VNode));
    if (!vn) return NULL;
    RamInode *ri = (RamInode *)kmalloc(sizeof(RamInode));
    if (!ri) { kfree(vn); return NULL; }

    memset(ri, 0, sizeof(RamInode));
    ri->ino = alloc_ino();

    vn->ops      = &g_ramfs_ops;
    vn->type     = type;
    vn->ino      = ri->ino;
    vn->refcount = 1;
    vn->mount    = NULL;
    vn->fs_data  = ri;
    return vn;
}

#define RAMFS_INIT_CAP 256
#define RAMFS_MAX_SIZE (256UL * 1024 * 1024)

static int ensure_cap(RamInode *ri, uint64_t needed)
{
    if (needed <= ri->cap) return 0;
    if (needed > RAMFS_MAX_SIZE) return -ENOSPC;

    uint64_t new_cap = ri->cap ? ri->cap : RAMFS_INIT_CAP;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap > RAMFS_MAX_SIZE) new_cap = RAMFS_MAX_SIZE;

    uint8_t *new_buf = (uint8_t *)krealloc(ri->buf, (size_t)new_cap);
    if (!new_buf) return -ENOMEM;
    ri->buf = new_buf;
    ri->cap = new_cap;
    return 0;
}

static int ramfs_lookup(VNode *dir, const char *name, VNode **out)
{
    RamInode *ri = (RamInode *)dir->fs_data;
    inode_lock(ri);
    for (RamDirEntry *e = ri->entries; e; e = e->next)
    {
        if (strcmp(e->name, name) == 0)
        {
            vnode_ref(e->node);
            *out = e->node;
            inode_unlock(ri);
            return 0;
        }
    }
    inode_unlock(ri);
    return -ENOENT;
}

static int dir_add_entry(RamInode *dri, const char *name, VNode *vn)
{
    RamDirEntry *ent = (RamDirEntry *)kmalloc(sizeof(RamDirEntry));
    if (!ent) return -ENOMEM;

    strncpy(ent->name, name, VFS_NAME_MAX);
    ent->name[VFS_NAME_MAX] = '\0';
    ent->node = vn;
    vnode_ref(vn);

    inode_lock(dri);
    ent->next    = dri->entries;
    dri->entries = ent;
    dri->entry_count++;
    inode_unlock(dri);
    return 0;
}

static int ramfs_create(VNode *dir, const char *name, VNode **out)
{
    RamInode *dri = (RamInode *)dir->fs_data;

    inode_lock(dri);
    for (RamDirEntry *e = dri->entries; e; e = e->next)
        if (strcmp(e->name, name) == 0) { inode_unlock(dri); return -EEXIST; }
    inode_unlock(dri);

    VNode *vn = ramfs_alloc_vnode(VNODE_FILE);
    if (!vn) return -ENOMEM;

    int err = dir_add_entry(dri, name, vn);
    if (err) { kfree(vn->fs_data); kfree(vn); return err; }

    *out = vn;
    return 0;
}

static int ramfs_mkdir(VNode *dir, const char *name, VNode **out)
{
    RamInode *dri = (RamInode *)dir->fs_data;

    inode_lock(dri);
    for (RamDirEntry *e = dri->entries; e; e = e->next)
        if (strcmp(e->name, name) == 0) { inode_unlock(dri); return -EEXIST; }
    inode_unlock(dri);

    VNode *vn = ramfs_alloc_vnode(VNODE_DIR);
    if (!vn) return -ENOMEM;

    int err = dir_add_entry(dri, name, vn);
    if (err) { kfree(vn->fs_data); kfree(vn); return err; }

    *out = vn;
    return 0;
}

static int ramfs_remove(VNode *dir, const char *name)
{
    RamInode *dri = (RamInode *)dir->fs_data;
    inode_lock(dri);

    RamDirEntry **pp = &dri->entries;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0)
        {
            RamDirEntry *dead = *pp;

            if (dead->node->type == VNODE_DIR)
            {
                RamInode *cri = (RamInode *)dead->node->fs_data;
                if (cri->entry_count > 0) { inode_unlock(dri); return -ENOTEMPTY; }
            }

            *pp = dead->next;
            dri->entry_count--;
            inode_unlock(dri);
            vnode_unref(dead->node);
            kfree(dead);
            return 0;
        }
        pp = &(*pp)->next;
    }

    inode_unlock(dri);
    return -ENOENT;
}

static ssize_t ramfs_read(VNode *node, void *buf, size_t len, uint64_t off)
{
    if (node->type != VNODE_FILE) return -EISDIR;
    RamInode *ri = (RamInode *)node->fs_data;

    inode_lock(ri);
    if (off >= ri->size) { inode_unlock(ri); return 0; }

    uint64_t avail = ri->size - off;
    if ((uint64_t)len > avail) len = (size_t)avail;

    memcpy(buf, ri->buf + off, len);
    inode_unlock(ri);
    return (ssize_t)len;
}

static ssize_t ramfs_write(VNode *node, const void *buf, size_t len, uint64_t off)
{
    if (node->type != VNODE_FILE) return -EISDIR;
    RamInode *ri = (RamInode *)node->fs_data;

    if (buf == NULL && len == 0)
    {
        inode_lock(ri);
        ri->size = 0;
        inode_unlock(ri);
        return 0;
    }

    uint64_t end = off + (uint64_t)len;

    inode_lock(ri);
    int err = ensure_cap(ri, end);
    if (err) { inode_unlock(ri); return err; }

    if (off > ri->size)
        memset(ri->buf + ri->size, 0, (size_t)(off - ri->size));

    memcpy(ri->buf + off, buf, len);
    if (end > ri->size) ri->size = end;
    inode_unlock(ri);
    return (ssize_t)len;
}

static int ramfs_stat(VNode *node, VStat *out)
{
    RamInode *ri = (RamInode *)node->fs_data;
    inode_lock(ri);
    out->st_ino  = ri->ino;
    out->st_type = node->type;
    out->st_size = (node->type == VNODE_FILE) ? ri->size : 0;
    inode_unlock(ri);
    return 0;
}

static int ramfs_readdir(VNode *dir, uint64_t index, VDirent *out)
{
    RamInode *ri = (RamInode *)dir->fs_data;
    inode_lock(ri);

    RamDirEntry *e = ri->entries;
    for (uint64_t i = 0; e && i < index; i++) e = e->next;

    if (!e) { inode_unlock(ri); return 0; }

    out->d_type = e->node->type;
    strncpy(out->d_name, e->name, VFS_NAME_MAX);
    out->d_name[VFS_NAME_MAX] = '\0';

    inode_unlock(ri);
    return 1;
}

static void ramfs_release(VNode *node)
{
    RamInode *ri = (RamInode *)node->fs_data;

    if (node->type == VNODE_FILE)
    {
        kfree(ri->buf);
    }
    else if (node->type == VNODE_DIR)
    {
        RamDirEntry *e = ri->entries;
        while (e)
        {
            RamDirEntry *next = e->next;
            vnode_unref(e->node);
            kfree(e);
            e = next;
        }
    }
    kfree(ri);
    kfree(node);
}

/*
 * ramfs_rename -- relink src_name in src_dir as dst_name in dst_dir.
 *
 * vfs_rename guarantees by the time we are called:
 *   - Both directories are on the same mount.
 *   - Any pre-existing entry at (dst_dir, dst_name) has already been removed.
 *   - Same-inode self-renames were short-circuited by the caller.
 *
 * We therefore only need to:
 *   1. Find and detach the entry from src_dir (without dropping the vnode).
 *   2. Rename it and prepend it to dst_dir's entry list.
 *
 * Both directory locks are acquired in pointer order to prevent deadlock
 * in the same-directory case (sri == dri).
 */
static int ramfs_rename(VNode *src_dir, const char *src_name,
                        VNode *dst_dir, const char *dst_name)
{
    RamInode *sri = (RamInode *)src_dir->fs_data;
    RamInode *dri = (RamInode *)dst_dir->fs_data;

    /* Lock in stable pointer order to avoid ABBA deadlock. */
    RamInode *first  = (sri <= dri) ? sri : dri;
    RamInode *second = (sri <= dri) ? dri : sri;

    inode_lock(first);
    if (first != second) inode_lock(second);

    /* Detach the entry from the source directory. */
    RamDirEntry *ent = NULL;
    for (RamDirEntry **pp = &sri->entries; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->name, src_name) == 0) {
            ent = *pp;
            *pp = ent->next;
            sri->entry_count--;
            break;
        }
    }

    if (!ent) {
        if (first != second) inode_unlock(second);
        inode_unlock(first);
        return -ENOENT;
    }

    /* Rewrite the name and link into the destination directory.
     * The existing vnode_ref (held since dir_add_entry) is transferred;
     * no additional ref/unref is required. */
    strncpy(ent->name, dst_name, VFS_NAME_MAX);
    ent->name[VFS_NAME_MAX] = '\0';
    ent->next    = dri->entries;
    dri->entries = ent;
    dri->entry_count++;

    if (first != second) inode_unlock(second);
    inode_unlock(first);
    return 0;
}

static VNodeOps g_ramfs_ops = {
    .lookup  = ramfs_lookup,
    .create  = ramfs_create,
    .mkdir   = ramfs_mkdir,
    .remove  = ramfs_remove,
    .rename  = ramfs_rename,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .stat    = ramfs_stat,
    .readdir = ramfs_readdir,
    .release = ramfs_release,
};

VNode *ramfs_create_root(void)
{
    return ramfs_alloc_vnode(VNODE_DIR);
}