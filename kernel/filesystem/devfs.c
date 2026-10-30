static VNodeOps devfs_dir_ops;

static VNode *devfs_lookup(VNode *dir, const char *name)
{
    for (VNode *c = dir->first_child; c; c = c->next_sibling)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

static int devfs_readdir(VNode *dir, int i, char *name_out)
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

static VFile *devfs_fb_owner;

static int devfs_fb_ioctl(VFile *file, uint64_t request, void *arg)
{
    if (!file || !file->node)
        return -1;

    FbDeviceInfo *info = (FbDeviceInfo *)file->node->private_data;
    if (!info)
        return -1;

    switch (request)
    {
        case DEVFS_IOCTL_FB_GET_INFO:
        {
            if (!arg)
                return -1;
            memcpy(arg, info, sizeof(FbDeviceInfo));
            return 0;
        }
        case DEVFS_IOCTL_FB_ACQUIRE:
        {
            if (devfs_fb_owner && devfs_fb_owner != file)
                return -1;
            devfs_fb_owner = file;
            term_set_fb_enabled(false);
            return 0;
        }
        default:
            return -1;
    }
}

static int devfs_fb_close(VFile *file)
{
    if (devfs_fb_owner == file)
    {
        devfs_fb_owner = NULL;
        term_set_fb_enabled(true);
        term_clear(0);
    }
    return 0;
}

static ssize_t devfs_fb_read(VFile *file, void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    return -1;
}

static ssize_t devfs_fb_write(VFile *file, const void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    return -1;
}

static ssize_t devfs_kbd_read(VFile *file, void *buf, size_t count)
{
    (void)file;
    return (ssize_t)keyboard_read_raw(buf, count);
}

static ssize_t devfs_kbd_write(VFile *file, const void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    return -1;
}

static int devfs_kbd_poll(VFile *file)
{
    (void)file;
    return keyboard_has_data() ? POLLIN : 0;
}

static int devfs_kbd_ioctl(VFile *file, uint64_t request, void *arg)
{
    (void)file; (void)request; (void)arg;
    return -1;
}

static ssize_t devfs_mouse_read(VFile *file, void *buf, size_t count)
{
    (void)file;
    return (ssize_t)mouse_read_raw(buf, count);
}

static ssize_t devfs_mouse_write(VFile *file, const void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    return -1;
}

static int devfs_mouse_poll(VFile *file)
{
    (void)file;
    return mouse_has_data() ? POLLIN : 0;
}

static int devfs_mouse_ioctl(VFile *file, uint64_t request, void *arg)
{
    (void)file; (void)request; (void)arg;
    return -1;
}

_Static_assert(CPU_MAX <= DEVFS_CPUINFO_MAX_CPUS, "CpuInfo cannot hold every CPU");
_Static_assert(TASK_NAME_MAX == DEVFS_TASK_NAME_MAX, "TaskInfo name must match Task name");
_Static_assert(sizeof(TaskInfo) == 96, "TaskInfo layout is shared with libc");
_Static_assert(sizeof(NetInfo) == 17, "NetInfo layout is shared with libc");
_Static_assert(sizeof(RtcTime) == 8, "RtcTime layout is shared with libc");

static ssize_t devfs_record_read(VFile *file, const void *data, size_t size, void *buf, size_t count)
{
    if (file->offset >= size)
        return 0;
    size_t n = MIN(count, size - file->offset);
    memcpy(buf, (const uint8_t *)data + file->offset, n);
    file->offset += n;
    return (ssize_t)n;
}

static ssize_t devfs_meminfo_read(VFile *file, void *buf, size_t count)
{
    MemInfo info = {
        .total_pages = pmm_total_page_count(),
        .free_pages  = pmm_free_page_count(),
        .page_size   = PAGE_SIZE,
    };
    return devfs_record_read(file, &info, sizeof(info), buf, count);
}

static ssize_t devfs_cpuinfo_read(VFile *file, void *buf, size_t count)
{
    CpuInfo info = { .cpus = smp_cpu_count() };

    for (uint32_t i = 0; i < info.cpus; i++)
    {
        smp_cpu_ticks(i, &info.cpu_total_ticks[i], &info.cpu_busy_ticks[i]);
        info.total_ticks += info.cpu_total_ticks[i];
        info.busy_ticks += info.cpu_busy_ticks[i];
    }
    return devfs_record_read(file, &info, sizeof(info), buf, count);
}

typedef struct
{
    TaskInfo *out;
    size_t    skip;
    size_t    max;
    size_t    written;
}
DevfsTaskCursor;

static void devfs_tasks_visit(Task *task, void *ctx)
{
    DevfsTaskCursor *cur = ctx;
    if (cur->skip)
    {
        cur->skip--;
        return;
    }
    if (cur->written == cur->max)
        return;

    uint64_t pages = task->stack_pages + user_pages_count(task);
    if (task->owns_image)
        pages += task->image.page_count;

    TaskInfo *info = &cur->out[cur->written++];
    memset(info, 0, sizeof(*info));
    info->pid = task->pid;
    info->state = (uint8_t)task->state;
    info->kernel = !task->owns_image;
    info->cpu_ticks = task->cpu_ticks;
    info->mem_kb = pages * (PAGE_SIZE / 1024);
    memcpy(info->name, task->name, DEVFS_TASK_NAME_MAX);
}

static ssize_t devfs_tasks_read(VFile *file, void *buf, size_t count)
{
    DevfsTaskCursor cur = {
        .out  = buf,
        .skip = file->offset / sizeof(TaskInfo),
        .max  = count / sizeof(TaskInfo),
    };
    task_for_each(devfs_tasks_visit, &cur);
    file->offset += cur.written * sizeof(TaskInfo);
    return (ssize_t)(cur.written * sizeof(TaskInfo));
}

static ssize_t devfs_rtc_read(VFile *file, void *buf, size_t count)
{
    RtcTime now;
    if (rtc_read(&now) != 0)
        return -1;
    return devfs_record_read(file, &now, sizeof(now), buf, count);
}

static ssize_t devfs_net_read(VFile *file, void *buf, size_t count)
{
    Ipv4Addr ip, gw, mask, dns;
    net_status(&ip, &gw, &mask, &dns);

    NetInfo info = { .up = net_is_up() };
    memcpy(info.ip, ip.bytes, 4);
    memcpy(info.gateway, gw.bytes, 4);
    memcpy(info.netmask, mask.bytes, 4);
    memcpy(info.dns, dns.bytes, 4);
    return devfs_record_read(file, &info, sizeof(info), buf, count);
}

static VNode *devfs_add_child(VNode *dir, const char *name, VFileOps *file_ops, void *private_data)
{
    VNode *n = (VNode *)kmalloc(sizeof(VNode));
    if (!n)
        return NULL;
    memset(n, 0, sizeof(VNode));

    size_t len = strlen(name);
    if (len >= VFS_NAME_MAX)
        len = VFS_NAME_MAX - 1;
    memcpy(n->name, name, len);
    n->name[len] = '\0';

    n->type = VNODE_DEVICE;
    n->ops = &devfs_dir_ops;
    n->file_ops = file_ops;
    n->private_data = private_data;

    n->parent = dir;
    n->next_sibling = dir->first_child;
    dir->first_child = n;
    return n;
}

VNode *devfs_create_root(void)
{
    devfs_dir_ops.lookup  = devfs_lookup;
    devfs_dir_ops.create  = NULL;
    devfs_dir_ops.open    = NULL;
    devfs_dir_ops.read    = NULL;
    devfs_dir_ops.write   = NULL;
    devfs_dir_ops.readdir = devfs_readdir;
    devfs_dir_ops.unlink  = NULL;

    VNode *root = (VNode *)kmalloc(sizeof(VNode));
    if (!root)
        return NULL;
    memset(root, 0, sizeof(VNode));

    root->type = VNODE_DIR;
    root->ops = &devfs_dir_ops;
    root->name[0] = '/';
    root->name[1] = '\0';

    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0)
    {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

        FbDeviceInfo *info = (FbDeviceInfo *)kmalloc(sizeof(FbDeviceInfo));
        if (info)
        {
            static VFileOps devfs_fb_file_ops;
            devfs_fb_file_ops.read  = devfs_fb_read;
            devfs_fb_file_ops.write = devfs_fb_write;
            devfs_fb_file_ops.ioctl = devfs_fb_ioctl;
            devfs_fb_file_ops.close = devfs_fb_close;

            memset(info, 0, sizeof(FbDeviceInfo));
            info->address = (uint64_t)fb->address;
            info->width = (uint32_t)fb->width;
            info->height = (uint32_t)fb->height;
            info->pitch = (uint32_t)fb->pitch;
            info->bpp = fb->bpp;
            info->red_mask_size = fb->red_mask_size;
            info->red_mask_shift = fb->red_mask_shift;
            info->green_mask_size = fb->green_mask_size;
            info->green_mask_shift = fb->green_mask_shift;
            info->blue_mask_size = fb->blue_mask_size;
            info->blue_mask_shift = fb->blue_mask_shift;

            devfs_add_child(root, "fb0", &devfs_fb_file_ops, info);

            kprintf("devfs: /dev/fb0 -> %ux%u, %u bpp, pitch %u\n",
                info->width, info->height, (unsigned)info->bpp, info->pitch);
        }
    }
    else
    {
        kprintf("devfs: no Limine framebuffer response, /dev/fb0 not created\n");
    }

    {
        static VFileOps devfs_kbd_file_ops;
        devfs_kbd_file_ops.read  = devfs_kbd_read;
        devfs_kbd_file_ops.write = devfs_kbd_write;
        devfs_kbd_file_ops.ioctl = devfs_kbd_ioctl;
        devfs_kbd_file_ops.close = NULL;
        devfs_kbd_file_ops.poll  = devfs_kbd_poll;

        devfs_add_child(root, "keyboard", &devfs_kbd_file_ops, NULL);
        kprintf("devfs: /dev/keyboard -> raw scancode stream\n");
    }

    {
        static VFileOps devfs_mouse_file_ops;
        devfs_mouse_file_ops.read  = devfs_mouse_read;
        devfs_mouse_file_ops.write = devfs_mouse_write;
        devfs_mouse_file_ops.ioctl = devfs_mouse_ioctl;
        devfs_mouse_file_ops.close = NULL;
        devfs_mouse_file_ops.poll  = devfs_mouse_poll;

        devfs_add_child(root, "mouse", &devfs_mouse_file_ops, NULL);
        kprintf("devfs: /dev/mouse -> MouseEvent stream\n");
    }

    {
        static VFileOps devfs_meminfo_file_ops;
        devfs_meminfo_file_ops.read = devfs_meminfo_read;
        devfs_add_child(root, "meminfo", &devfs_meminfo_file_ops, NULL);
    }

    {
        static VFileOps devfs_cpuinfo_file_ops;
        devfs_cpuinfo_file_ops.read = devfs_cpuinfo_read;
        devfs_add_child(root, "cpuinfo", &devfs_cpuinfo_file_ops, NULL);
    }

    {
        static VFileOps devfs_tasks_file_ops;
        devfs_tasks_file_ops.read = devfs_tasks_read;
        devfs_add_child(root, "tasks", &devfs_tasks_file_ops, NULL);
    }

    {
        static VFileOps devfs_rtc_file_ops;
        devfs_rtc_file_ops.read = devfs_rtc_read;
        devfs_add_child(root, "rtc", &devfs_rtc_file_ops, NULL);
    }

    {
        static VFileOps devfs_net_file_ops;
        devfs_net_file_ops.read = devfs_net_read;
        devfs_add_child(root, "net", &devfs_net_file_ops, NULL);
    }

    return root;
}