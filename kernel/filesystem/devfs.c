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

typedef struct
{
    char  *data;
    size_t cap;
    size_t len;
}
DevfsText;

#define DEVFS_TEXT(name, size) \
    static char name##_data[size]; \
    DevfsText name = { name##_data, sizeof(name##_data), 0 }

static void devfs_text_str(DevfsText *t, const char *s)
{
    while (*s && t->len < t->cap - 1)
        t->data[t->len++] = *s++;
}

static void devfs_text_uint(DevfsText *t, uint64_t v, int min_digits)
{
    char digits[24];
    int n = 0;
    do
    {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (v && n < (int)sizeof(digits));
    while (n < min_digits && n < (int)sizeof(digits))
        digits[n++] = '0';
    while (n > 0 && t->len < t->cap - 1)
        t->data[t->len++] = digits[--n];
}

static ssize_t devfs_text_read(VFile *file, const DevfsText *t, void *buf, size_t count)
{
    if (file->offset >= t->len)
        return 0;
    size_t n = MIN(count, t->len - file->offset);
    memcpy(buf, t->data + file->offset, n);
    file->offset += n;
    return (ssize_t)n;
}

static ssize_t devfs_meminfo_read(VFile *file, void *buf, size_t count)
{
    uint64_t total_kb = pmm_total_page_count() * (PAGE_SIZE / 1024);
    uint64_t free_kb = pmm_free_page_count() * (PAGE_SIZE / 1024);

    DEVFS_TEXT(t, 4096);
    devfs_text_str(&t, "total_kb: ");
    devfs_text_uint(&t, total_kb, 1);
    devfs_text_str(&t, "\nfree_kb: ");
    devfs_text_uint(&t, free_kb, 1);
    devfs_text_str(&t, "\nused_kb: ");
    devfs_text_uint(&t, total_kb - free_kb, 1);
    devfs_text_str(&t, "\npage_size: ");
    devfs_text_uint(&t, PAGE_SIZE, 1);
    devfs_text_str(&t, "\n");
    return devfs_text_read(file, &t, buf, count);
}

static ssize_t devfs_cpuinfo_read(VFile *file, void *buf, size_t count)
{
    uint32_t cpus = smp_cpu_count();

    DEVFS_TEXT(t, 4096);
    devfs_text_str(&t, "cpus: ");
    devfs_text_uint(&t, cpus, 1);
    devfs_text_str(&t, "\nhz: ");
    devfs_text_uint(&t, SCHEDULER_HZ, 1);
    devfs_text_str(&t, "\n");

    for (uint32_t i = 0; i < cpus; i++)
    {
        uint64_t total, busy;
        smp_cpu_ticks(i, &total, &busy);
        devfs_text_str(&t, "cpu");
        devfs_text_uint(&t, i, 1);
        devfs_text_str(&t, "_total: ");
        devfs_text_uint(&t, total, 1);
        devfs_text_str(&t, "\ncpu");
        devfs_text_uint(&t, i, 1);
        devfs_text_str(&t, "_busy: ");
        devfs_text_uint(&t, busy, 1);
        devfs_text_str(&t, "\n");
    }
    return devfs_text_read(file, &t, buf, count);
}

static void devfs_tasks_visit(Task *task, void *ctx)
{
    static const char *states[] = { " Q ", " R ", " S ", " D " };
    DevfsText *t = ctx;

    uint64_t pages = task->stack_pages + user_pages_count(task);
    if (task->owns_image)
        pages += task->image.page_count;

    devfs_text_uint(t, task->pid, 1);
    devfs_text_str(t, states[task->state]);
    devfs_text_str(t, task->owns_image ? "p " : "k ");
    devfs_text_uint(t, task->cpu_ticks, 1);
    devfs_text_str(t, " ");
    devfs_text_uint(t, pages * (PAGE_SIZE / 1024), 1);
    devfs_text_str(t, " ");
    devfs_text_str(t, task->name);
    devfs_text_str(t, "\n");
}

static ssize_t devfs_tasks_read(VFile *file, void *buf, size_t count)
{
    DEVFS_TEXT(t, 128 * 1024);
    task_for_each(devfs_tasks_visit, &t);
    return devfs_text_read(file, &t, buf, count);
}

static const char *devfs_weekdays[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};

static ssize_t devfs_rtc_read(VFile *file, void *buf, size_t count)
{
    RtcTime now;
    if (rtc_read(&now) != 0)
        return -1;

    DEVFS_TEXT(t, 4096);
    devfs_text_uint(&t, now.year, 4);
    devfs_text_str(&t, "-");
    devfs_text_uint(&t, now.month, 2);
    devfs_text_str(&t, "-");
    devfs_text_uint(&t, now.day, 2);
    devfs_text_str(&t, " ");
    devfs_text_uint(&t, now.hour, 2);
    devfs_text_str(&t, ":");
    devfs_text_uint(&t, now.minute, 2);
    devfs_text_str(&t, ":");
    devfs_text_uint(&t, now.second, 2);
    devfs_text_str(&t, " ");
    devfs_text_str(&t, devfs_weekdays[now.weekday % 7]);
    devfs_text_str(&t, "\n");
    return devfs_text_read(file, &t, buf, count);
}

static void devfs_text_ip(DevfsText *t, Ipv4Addr ip)
{
    for (int i = 0; i < 4; i++)
    {
        devfs_text_uint(t, ip.bytes[i], 1);
        if (i < 3)
            devfs_text_str(t, ".");
    }
}

static ssize_t devfs_net_read(VFile *file, void *buf, size_t count)
{
    Ipv4Addr ip, gw, mask, dns;
    net_status(&ip, &gw, &mask, &dns);

    DEVFS_TEXT(t, 4096);
    devfs_text_str(&t, "status: ");
    devfs_text_str(&t, net_is_up() ? "up" : "down");
    devfs_text_str(&t, "\nip: ");
    devfs_text_ip(&t, ip);
    devfs_text_str(&t, "\ngateway: ");
    devfs_text_ip(&t, gw);
    devfs_text_str(&t, "\nnetmask: ");
    devfs_text_ip(&t, mask);
    devfs_text_str(&t, "\ndns: ");
    devfs_text_ip(&t, dns);
    devfs_text_str(&t, "\n");
    return devfs_text_read(file, &t, buf, count);
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