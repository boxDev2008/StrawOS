#pragma once
#include "common.h"

typedef struct VNode    VNode;
typedef struct VNodeOps VNodeOps;
typedef struct OpenFile OpenFile;
typedef struct Mount    Mount;

typedef enum {
    VNODE_FILE = 1,
    VNODE_DIR  = 2,
} VNodeType;

/* Open flags */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_ACCMODE 0x003
#define O_CREAT   0x040
#define O_TRUNC   0x200
#define O_APPEND  0x400

/* Stat */
typedef struct {
    uint64_t  st_ino;
    VNodeType st_type;
    uint64_t  st_size;
} VStat;

/* Directory entry */
#define VFS_NAME_MAX 255
typedef struct {
    VNodeType d_type;
    char      d_name[VFS_NAME_MAX + 1];
} VDirent;

/* VNode operations */
struct VNodeOps {
    int     (*lookup) (VNode *dir,  const char *name, VNode **out);
    int     (*create) (VNode *dir,  const char *name, VNode **out);
    int     (*mkdir)  (VNode *dir,  const char *name, VNode **out);
    int     (*remove) (VNode *dir,  const char *name);   /* works for files and dirs */
    /* rename: relink src_name in src_dir as dst_name in dst_dir.
     * Called only after vfs_rename has validated types, checked for
     * cross-mount moves, and already evicted any pre-existing dst entry.
     * May be NULL; vfs_rename returns -EINVAL for drivers that omit it. */
    int     (*rename) (VNode *src_dir, const char *src_name,
                       VNode *dst_dir, const char *dst_name);
    ssize_t (*read)   (VNode *node, void *buf,       size_t len, uint64_t off);
    ssize_t (*write)  (VNode *node, const void *buf, size_t len, uint64_t off);
    int     (*stat)   (VNode *node, VStat *out);
    int     (*readdir)(VNode *dir,  uint64_t index,  VDirent *out);
    void    (*release)(VNode *node);
};

struct VNode {
    VNodeOps *ops;
    VNodeType type;
    uint64_t  ino;
    uint32_t  refcount;
    Mount    *mount;
    void     *fs_data;
};

struct OpenFile {
    VNode    *node;
    uint64_t  offset;
    int       flags;
};

/* Limits */
#define VFS_MOUNT_MAX 16
#define VFS_PATH_MAX  512
#define VFS_FD_MAX    1024

struct Mount {
    char   path[VFS_PATH_MAX];
    VNode *root;
    bool   active;
};

/* Errors */
#define ENOENT    2
#define EBADF     9
#define ENOMEM    12
#define EACCES    13
#define EEXIST    17
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define EMFILE    24
#define ENOSPC    28
#define ENOTEMPTY 39

/* Seek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* API */
void    vfs_init(void);
void    vnode_ref(VNode *node);
void    vnode_unref(VNode *node);

int     vfs_mount  (const char *path, VNode *root);
int     vfs_unmount(const char *path);
int     vfs_lookup (const char *path, VNode **out);

int     vfs_open   (const char *path, int flags);
int     vfs_close  (int fd);
ssize_t vfs_read   (int fd, void *buf, size_t len);
ssize_t vfs_write  (int fd, const void *buf, size_t len);
int64_t vfs_seek   (int fd, int64_t offset, int whence);

int     vfs_stat   (const char *path, VStat *out);
int     vfs_fstat  (int fd, VStat *out);

int     vfs_mkdir  (const char *path);
int     vfs_remove (const char *path);
int     vfs_readdir(int fd, uint64_t index, VDirent *out);
int     vfs_rename (const char *src, const char *dst);
int     vfs_chdir  (const char *path);
int     vfs_getcwd (char *buf, size_t size);