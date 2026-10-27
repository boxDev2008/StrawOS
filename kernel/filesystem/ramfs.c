typedef struct
{
    uint8_t *data;
    size_t capacity;
}
RamfsFile;

static VNodeOps ramfs_ops;

static VNode *ramfs_lookup(VNode *dir, const char *name)
{
    for (VNode *c = dir->first_child; c; c = c->next_sibling)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

static VNode *ramfs_create(VNode *dir, const char *name, VNodeType type)
{
    if (ramfs_lookup(dir, name))
        return NULL;

    VNode *n = (VNode *)kmalloc(sizeof(VNode));
    if (!n)
        return NULL;
    memset(n, 0, sizeof(VNode));

    size_t len = strlen(name);
    if (len >= VFS_NAME_MAX)
        len = VFS_NAME_MAX - 1;
    memcpy(n->name, name, len);
    n->name[len] = '\0';

    n->type = type;
    n->ops = &ramfs_ops;
    n->parent = dir;

    if (type == VNODE_FILE)
    {
        RamfsFile *rf = (RamfsFile *)kmalloc(sizeof(RamfsFile));
        if (!rf)
        {
            kfree(n);
            return NULL;
        }
        rf->data = NULL;
        rf->capacity = 0;
        n->private_data = rf;
        n->size = 0;
    }

    n->next_sibling = dir->first_child;
    dir->first_child = n;
    return n;
}

static int ramfs_ensure_capacity(RamfsFile *rf, size_t needed)
{
    if (needed <= rf->capacity)
        return 0;

    size_t new_cap = rf->capacity ? rf->capacity : 256;
    while (new_cap < needed)
        new_cap *= 2;

    uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
    if (!new_data)
        return -1;

    if (rf->data)
    {
        memcpy(new_data, rf->data, rf->capacity);
        kfree(rf->data);
    }
    memset(new_data + rf->capacity, 0, new_cap - rf->capacity);

    rf->data = new_data;
    rf->capacity = new_cap;
    return 0;
}

static ssize_t ramfs_read(VNode *node, void *buf, size_t count, size_t offset)
{
    RamfsFile *rf = (RamfsFile *)node->private_data;
    if (!rf || offset >= node->size)
        return 0;

    size_t avail = node->size - offset;
    size_t n = (count < avail) ? count : avail;

    memcpy(buf, rf->data + offset, n);
    return (ssize_t)n;
}

static ssize_t ramfs_write(VNode *node, const void *buf, size_t count, size_t offset)
{
    RamfsFile *rf = (RamfsFile *)node->private_data;
    if (!rf)
        return -1;

    if (ramfs_ensure_capacity(rf, offset + count) != 0)
        return -1;

    memcpy(rf->data + offset, buf, count);

    if (offset + count > node->size)
        node->size = offset + count;

    return (ssize_t)count;
}

static int ramfs_readdir(VNode *dir, int i, char *name_out)
{
    int idx = 0;
    for (VNode *c = dir->first_child; c; c = c->next_sibling, idx++)
    {
        if (idx == i)
        {
            size_t len = strlen(c->name);
            if (len >= VFS_NAME_MAX)
                len = VFS_NAME_MAX - 1;
            memcpy(name_out, c->name, len);
            name_out[len] = '\0';
            return 0;
        }
    }
    return -1;
}

static int ramfs_rename(VNode *node, VNode *new_dir, const char *new_name)
{
    VNode *old_dir = node->parent;
    VNode **pp = &old_dir->first_child;
    while (*pp && *pp != node)
        pp = &(*pp)->next_sibling;
    if (!*pp)
        return -1;
    *pp = node->next_sibling;

    size_t len = strlen(new_name);
    if (len >= VFS_NAME_MAX)
        len = VFS_NAME_MAX - 1;
    memcpy(node->name, new_name, len);
    node->name[len] = '\0';

    node->parent = new_dir;
    node->next_sibling = new_dir->first_child;
    new_dir->first_child = node;
    return 0;
}

static int ramfs_unlink(VNode *dir, const char *name)
{
    VNode *prev = NULL;
    VNode *cur = dir->first_child;

    while (cur)
    {
        if (strcmp(cur->name, name) == 0)
            break;
        prev = cur;
        cur = cur->next_sibling;
    }

    if (!cur)
        return -1;

    if (cur->type == VNODE_DIR && cur->first_child != NULL)
        return -1;

    if (prev)
        prev->next_sibling = cur->next_sibling;
    else
        dir->first_child = cur->next_sibling;

    if (cur->type == VNODE_FILE && cur->private_data)
    {
        RamfsFile *rf = (RamfsFile *)cur->private_data;
        if (rf->data)
            kfree(rf->data);
        kfree(rf);
    }

    kfree(cur);
    return 0;
}

VNode *ramfs_create_root(void)
{
    ramfs_ops.lookup  = ramfs_lookup;
    ramfs_ops.create  = ramfs_create;
    ramfs_ops.open    = NULL;
    ramfs_ops.read    = ramfs_read;
    ramfs_ops.write   = ramfs_write;
    ramfs_ops.readdir = ramfs_readdir;
    ramfs_ops.unlink  = ramfs_unlink;
    ramfs_ops.rename  = ramfs_rename;

    VNode *root = (VNode *)kmalloc(sizeof(VNode));
    if (!root)
        return NULL;
    memset(root, 0, sizeof(VNode));

    root->type = VNODE_DIR;
    root->ops = &ramfs_ops;
    root->name[0] = '/';
    root->name[1] = '\0';

    return root;
}

static const char *basename_of(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            last = p + 1;
    return last;
}

static uint64_t tar_octal(const uint8_t *s, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(s[i] - '0');
    return v;
}

static void ramfs_mkdirs(const char *path)
{
    char buf[VFS_PATH_MAX];
    size_t len = strlen(path);
    if (len >= VFS_PATH_MAX)
        return;

    for (size_t i = 1; i <= len; i++)
    {
        if (path[i] != '/' && path[i] != '\0')
            continue;
        memcpy(buf, path, i);
        buf[i] = '\0';
        if (!vfs_resolve(buf))
            vfs_mkdir(buf);
    }
}

static bool tar_entry_path(const uint8_t *hdr, char *out)
{
    char raw[VFS_PATH_MAX];
    size_t n = 0;

    if (memcmp(hdr + 257, "ustar", 5) == 0 && hdr[345])
    {
        for (size_t i = 0; i < 155 && hdr[345 + i] && n < sizeof(raw) - 1; i++)
            raw[n++] = (char)hdr[345 + i];
        if (n < sizeof(raw) - 1)
            raw[n++] = '/';
    }
    for (size_t i = 0; i < 100 && hdr[i] && n < sizeof(raw) - 1; i++)
        raw[n++] = (char)hdr[i];
    raw[n] = '\0';

    const char *p = raw;
    while (*p == '.' && p[1] == '/')
        p += 2;
    while (*p == '/')
        p++;

    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/')
        len--;
    if (len == 0 || len + 2 > VFS_PATH_MAX || (len == 1 && p[0] == '.'))
        return false;

    out[0] = '/';
    memcpy(out + 1, p, len);
    out[len + 1] = '\0';
    return true;
}

static int ramfs_extract_tar(const uint8_t *data, size_t size)
{
    size_t off = 0;
    int files = 0;

    while (off + 512 <= size)
    {
        const uint8_t *hdr = data + off;
        if (hdr[0] == 0)
            break;

        uint64_t file_size = tar_octal(hdr + 124, 12);
        char type = (char)hdr[156];
        off += 512;

        char path[VFS_PATH_MAX];
        if (tar_entry_path(hdr, path) && off + file_size <= size)
        {
            if (type == '5')
            {
                ramfs_mkdirs(path);
            }
            else if (type == '0' || type == '\0')
            {
                char parent[VFS_PATH_MAX];
                strcpy(parent, path);
                char *slash = parent;
                for (char *q = parent; *q; q++)
                    if (*q == '/')
                        slash = q;
                *slash = '\0';
                if (parent[0])
                    ramfs_mkdirs(parent);

                VFile f;
                if (vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT, &f) == 0)
                {
                    vfs_write(&f, data + off, (size_t)file_size);
                    vfs_close(&f);
                    files++;
                }
            }
        }

        off += ALIGN_UP(file_size, 512);
    }

    return files;
}

int ramfs_load_limine_modules(void)
{
    if (module_request.response == NULL)
    {
        kprintf("ramfs: no Limine module response (bootloader didn't answer)\n");
        return -1;
    }

    struct limine_module_response *resp = module_request.response;
    int loaded = 0;

    for (uint64_t i = 0; i < resp->module_count; i++)
    {
        struct limine_file *mod = resp->modules[i];
        if (!mod || !mod->path)
            continue;

        const char *base = basename_of(mod->path);

        size_t name_len = strlen(base);
        if (name_len > 4 && strcmp(base + name_len - 4, ".tar") == 0)
        {
            int n = ramfs_extract_tar((const uint8_t *)mod->address, (size_t)mod->size);
            kprintf("ramfs: unpacked %d file(s) from %s\n", n, base);
            loaded++;
            continue;
        }

        char full_path[VFS_PATH_MAX];
        ramfs_mkdirs("/modules");
        const char *prefix = "/modules/";
        size_t prefix_len = strlen(prefix);
        size_t base_len = strlen(base);

        if (prefix_len + base_len >= VFS_PATH_MAX)
        {
            kprintf("ramfs: module name too long, skipping: %s\n", base);
            continue;
        }

        memcpy(full_path, prefix, prefix_len);
        memcpy(full_path + prefix_len, base, base_len);
        full_path[prefix_len + base_len] = '\0';

        VFile f;
        if (vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT, &f) != 0)
        {
            kprintf("ramfs: failed to create %s\n", full_path);
            continue;
        }

        ssize_t written = vfs_write(&f, mod->address, (size_t)mod->size);
        vfs_close(&f);

        if (written < 0 || (uint64_t)written != mod->size)
        {
            kprintf("ramfs: short write loading %s (%d/%llu bytes)\n",
                    full_path, (int)written, (unsigned long long)mod->size);
            continue;
        }

        kprintf("ramfs: loaded module %s (%llu bytes)\n",
                full_path, (unsigned long long)mod->size);
        loaded++;
    }

    return loaded;
}