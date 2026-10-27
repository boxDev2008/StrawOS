static VfsMount mounts[VFS_MAX_MOUNTS] = { 0 };

VNode *vfs_root(void)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (mounts[i].used && mounts[i].path[0] == '/' && mounts[i].path[1] == '\0')
            return mounts[i].root;
    return NULL;
}

int vfs_mount(const char *path, VNode *fs_root)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
    {
        if (!mounts[i].used)
        {
            size_t len = strlen(path);
            if (len >= VFS_NAME_MAX)
                return -1;
            memcpy(mounts[i].path, path, len);
            mounts[i].path[len] = '\0';
            mounts[i].root = fs_root;
            mounts[i].used = true;
            return 0;
        }
    }
    return -1;
}

static VfsMount *find_mount_for(const char *path, const char **remainder_out)
{
    VfsMount *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
    {
        if (!mounts[i].used)
            continue;

        size_t mlen = strlen(mounts[i].path);
        bool is_root_mount = (mlen == 1 && mounts[i].path[0] == '/');

        if (is_root_mount)
        {
            if (mlen > best_len || best == NULL)
            {
                best = &mounts[i];
                best_len = mlen;
            }
            continue;
        }

        if (strncmp(path, mounts[i].path, mlen) == 0 &&
            (path[mlen] == '/' || path[mlen] == '\0'))
        {
            if (mlen > best_len)
            {
                best = &mounts[i];
                best_len = mlen;
            }
        }
    }

    if (!best)
        return NULL;

    size_t mlen = strlen(best->path);
    bool is_root_mount = (mlen == 1 && best->path[0] == '/');

    if (is_root_mount)
        *remainder_out = path;
    else
        *remainder_out = path + mlen;

    return best;
}

VNode *vfs_resolve(const char *path)
{
    if (!path || path[0] != '/')
        return NULL;

    const char *remainder = NULL;
    VfsMount *mnt = find_mount_for(path, &remainder);
    if (!mnt)
        return NULL;

    VNode *cur = mnt->root;

    if (*remainder == '/')
        remainder++;

    if (*remainder == '\0')
        return cur;

    char component[VFS_NAME_MAX];
    const char *p = remainder;

    while (*p)
    {
        size_t i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX - 1)
            component[i++] = *p++;
        component[i] = '\0';

        while (*p == '/')
            p++;

        if (i == 0)
            continue;

        if (!cur->ops || !cur->ops->lookup)
            return NULL;

        VNode *next = cur->ops->lookup(cur, component);
        if (!next)
            return NULL;

        cur = next;
    }

    return cur;
}

static VNode *resolve_parent(const char *path, char *leaf_out)
{
    size_t len = strlen(path);
    if (len == 0 || path[0] != '/')
        return NULL;

    size_t last_slash = 0;
    for (size_t i = 0; i < len; i++)
        if (path[i] == '/')
            last_slash = i;

    size_t leaf_len = len - (last_slash + 1);
    if (leaf_len == 0 || leaf_len >= VFS_NAME_MAX)
        return NULL;

    memcpy(leaf_out, path + last_slash + 1, leaf_len);
    leaf_out[leaf_len] = '\0';

    if (last_slash == 0)
        return vfs_resolve("/");

    char parent_path[VFS_PATH_MAX];
    if (last_slash >= VFS_PATH_MAX)
        return NULL;
    memcpy(parent_path, path, last_slash);
    parent_path[last_slash] = '\0';

    return vfs_resolve(parent_path);
}

int vfs_open(const char *path, uint32_t flags, VFile *file_out)
{
    VNode *node = vfs_resolve(path);

    if (!node && (flags & VFS_O_CREAT))
    {
        char leaf[VFS_NAME_MAX];
        VNode *parent = resolve_parent(path, leaf);
        if (!parent || parent->type != VNODE_DIR || !parent->ops || !parent->ops->create)
            return -1;

        node = parent->ops->create(parent, leaf, VNODE_FILE);
        if (!node)
            return -1;
    }

    if (!node)
        return -1;

    memset(file_out, 0, sizeof(VFile));
    file_out->node = node;
    file_out->offset = 0;
    file_out->flags = flags;

    if (node->ops && node->ops->open)
    {
        int rc = node->ops->open(node, file_out);
        if (rc != 0)
            return rc;
    }

    return 0;
}

ssize_t vfs_read(VFile *file, void *buf, size_t count)
{
    if (!file || !file->node)
        return -1;

    if (file->node->file_ops && file->node->file_ops->read)
        return file->node->file_ops->read(file, buf, count);

    if (!file->node->ops || !file->node->ops->read)
        return -1;

    ssize_t n = file->node->ops->read(file->node, buf, count, file->offset);
    if (n > 0)
        file->offset += (size_t)n;
    return n;
}

ssize_t vfs_write(VFile *file, const void *buf, size_t count)
{
    if (!file || !file->node)
        return -1;

    if (file->node->file_ops && file->node->file_ops->write)
        return file->node->file_ops->write(file, buf, count);

    if (!file->node->ops || !file->node->ops->write)
        return -1;

    ssize_t n = file->node->ops->write(file->node, buf, count, file->offset);
    if (n > 0)
    {
        file->offset += (size_t)n;
        if (file->offset > file->node->size)
            file->node->size = file->offset;
    }
    return n;
}

int vfs_ioctl(VFile *file, uint64_t request, void *arg)
{
    if (!file || !file->node)
        return -1;

    if (file->node->file_ops && file->node->file_ops->ioctl)
        return file->node->file_ops->ioctl(file, request, arg);

    return -1;
}

int vfs_close(VFile *file)
{
    if (!file || !file->node)
        return -1;

    if (file->node->file_ops && file->node->file_ops->close)
        return file->node->file_ops->close(file);

    return 0;
}

int vfs_poll(VFile *file)
{
    if (!file || !file->node)
        return POLLNVAL;

    if (file->node->file_ops && file->node->file_ops->poll)
        return file->node->file_ops->poll(file);

    return POLLIN | POLLOUT;
}

VNode *vfs_mkdir(const char *path)
{
    char leaf[VFS_NAME_MAX];
    VNode *parent = resolve_parent(path, leaf);
    if (!parent || parent->type != VNODE_DIR || !parent->ops || !parent->ops->create)
        return NULL;

    if (parent->ops->lookup && parent->ops->lookup(parent, leaf))
        return NULL;

    return parent->ops->create(parent, leaf, VNODE_DIR);
}

int vfs_readdir(VNode *dir, int i, char *name_out)
{
    if (!dir || dir->type != VNODE_DIR || !dir->ops || !dir->ops->readdir)
        return -1;
    return dir->ops->readdir(dir, i, name_out);
}

int vfs_stat(const char *path, VfsStat *out)
{
    VNode *node = vfs_resolve(path);
    if (!node || !out)
        return -1;

    out->type = node->type;
    out->size = node->size;
    out->has_file_ops = (node->file_ops != NULL);
    return 0;
}

int vfs_fstat(VFile *file, VfsStat *out)
{
    if (!file || !file->node || !out)
        return -1;

    VNode *node = file->node;
    out->type = node->type;
    out->size = node->size;
    out->has_file_ops = (node->file_ops != NULL);
    return 0;
}

ssize_t vfs_lseek(VFile *file, int64_t offset, int whence)
{
    if (!file || !file->node)
        return -1;

    if (file->node->file_ops)
        return -1;

    int64_t base;
    switch (whence)
    {
        case VFS_SEEK_SET:
            base = 0;
            break;
        case VFS_SEEK_CUR:
            base = (int64_t)file->offset;
            break;
        case VFS_SEEK_END:
            base = (int64_t)file->node->size;
            break;
        default:
            return -1;
    }

    int64_t result = base + offset;
    if (result < 0)
        return -1;

    file->offset = (size_t)result;
    return (ssize_t)file->offset;
}

int vfs_unlink(const char *path)
{
    char leaf[VFS_NAME_MAX];
    VNode *parent = resolve_parent(path, leaf);
    if (!parent || parent->type != VNODE_DIR || !parent->ops || !parent->ops->unlink)
        return -1;

    return parent->ops->unlink(parent, leaf);
}
int vfs_rename(const char *from, const char *to)
{
    VNode *node = vfs_resolve(from);
    if (!node || !node->parent || vfs_resolve(to))
        return -1;

    char leaf[VFS_NAME_MAX];
    VNode *dir = resolve_parent(to, leaf);
    if (!dir || dir->type != VNODE_DIR || dir->ops != node->ops || !node->ops || !node->ops->rename)
        return -1;

    for (VNode *p = dir; p; p = p->parent)
        if (p == node)
            return -1;

    return node->ops->rename(node, dir, leaf);
}
