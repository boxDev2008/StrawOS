#define VFS_NAME_MAX   64
#define VFS_PATH_MAX   256
#define VFS_MAX_MOUNTS 8

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

typedef enum
{
    VNODE_FILE,
    VNODE_DIR,
    VNODE_DEVICE,
    VNODE_SOCKET,
}
VNodeType;

typedef struct VNode VNode;
typedef struct VFile VFile;
typedef struct VfsMount VfsMount;

typedef struct
{
    ssize_t (*read)(VFile *file, void *buf, size_t count);
    ssize_t (*write)(VFile *file, const void *buf, size_t count);
    int     (*ioctl)(VFile *file, uint64_t request, void *arg);
    int     (*close)(VFile *file);
    int     (*poll)(VFile *file);
}
VFileOps;

#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

typedef struct
{
    VNode *(*lookup)(VNode *dir, const char *name);

    VNode *(*create)(VNode *dir, const char *name, VNodeType type);
    int (*open)(VNode *node, VFile *file);

    ssize_t (*read)(VNode *node, void *buf, size_t count, size_t offset);
    ssize_t (*write)(VNode *node, const void *buf, size_t count, size_t offset);

    int (*readdir)(VNode *dir, int i, char *name_out);
    int (*unlink)(VNode *dir, const char *name);
    int (*rename)(VNode *node, VNode *new_dir, const char *new_name);
}
VNodeOps;

struct VNode
{
    char       name[VFS_NAME_MAX];
    VNodeType  type;
    size_t     size;

    VNodeOps  *ops;
    VFileOps  *file_ops;

    void      *private_data;

    VNode     *parent;
    VNode     *first_child;
    VNode     *next_sibling;
};

struct VFile
{
    VNode   *node;
    size_t   offset;
    uint32_t flags;
    void    *private_data;
};

#define VFS_O_RDONLY 0x0
#define VFS_O_WRONLY 0x1
#define VFS_O_RDWR   0x2
#define VFS_O_CREAT  0x4

struct VfsMount
{
    char    path[VFS_NAME_MAX];
    VNode  *root;
    bool    used;
};

typedef struct
{
    VNodeType type;
    size_t    size;
    bool      has_file_ops;
}
VfsStat;

void  vfs_init(void);

int   vfs_mount(const char *path, VNode *root);

VNode *vfs_resolve(const char *path);

int    vfs_open(const char *path, uint32_t flags, VFile *file_out);
ssize_t vfs_read(VFile *file, void *buf, size_t count);
ssize_t vfs_write(VFile *file, const void *buf, size_t count);
int    vfs_ioctl(VFile *file, uint64_t request, void *arg);
int    vfs_close(VFile *file);
int    vfs_poll(VFile *file);

VNode *vfs_mkdir(const char *path);
int    vfs_readdir(VNode *dir, int i, char *name_out);

int    vfs_stat(const char *path, VfsStat *out);
int    vfs_fstat(VFile *file, VfsStat *out);

ssize_t vfs_lseek(VFile *file, int64_t offset, int whence);

int    vfs_unlink(const char *path);
int    vfs_rename(const char *from, const char *to);

VNode *vfs_root(void);