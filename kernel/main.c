static void scheduler_timer_irq(void)
{
    if (cpu_is_bsp())
    {
        xhci_poll();
        nic_poll();
        net_loopback_poll();
        inet_tick();
        hda_poll();
    }
    task_scheduler_tick();
}

static void net_task(void *arg)
{
    (void)arg;
    if (!nic_init())
        return;

    if (!net_dhcp_init(5000, NULL, NULL, NULL))
    {
        kprintf("net: DHCP failed, using the QEMU user network defaults\n");
        net_init(ipv4_from_bytes(10, 0, 2, 15), ipv4_from_bytes(10, 0, 2, 2), ipv4_from_bytes(255, 255, 255, 0));
    }
}

void scheduler_timer_init(void)
{
    idt_set_irq_handler(IRQ_TIMER, scheduler_timer_irq);
    lapic_timer_init(SCHEDULER_HZ);
}

static VNode *disk_root_find(void)
{
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++)
    {
        BlockDevice *dev = block_get(i);
        if (!dev || dev->sectors == 0 || !fat32_has_label(dev, "STRAWOS"))
            continue;
        VNode *root = fat32_mount(dev);
        if (root)
        {
            kprintf("vfs: using %s as the root filesystem\n", dev->name);
            return root;
        }
    }
    return NULL;
}

static ssize_t live_read(VFile *file, void *buf, size_t count)
{
    (void)file; (void)buf; (void)count;
    return 0;
}

static void filesystems_init(void)
{
    bool live = ramfs_has_boot_image();
    VNode *root = live ? NULL : disk_root_find();

    if (!root)
    {
        live = true;
        root = ramfs_create_root();
        if (!root)
        {
            kprintf("vfs: failed to create ramfs root\n");
            panic();
        }
        vfs_mount("/", root);
        int n = ramfs_load_limine_modules();
        kprintf("vfs: loaded %d boot module(s)\n", n);
    }
    else
    {
        vfs_mount("/", root);
    }

    VNode *devfs_root = devfs_create_root();
    if (!devfs_root)
    {
        kprintf("vfs: failed to create devfs root\n");
        panic();
    }
    vfs_mkdir("/dev");
    vfs_mount("/dev", devfs_root);
    block_publish_all();

    if (live)
    {
        static VFileOps live_ops;
        live_ops.read = live_read;
        devfs_register("live", &live_ops, NULL);
    }
}

void kmain(void)
{
    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (response == NULL || response->framebuffer_count == 0)
        panic();

    limine_framebuffer = response->framebuffers[0];

    term_init();

    fpu_init();
    kprintf("FPU/SSE Initialized\n");

    gdt_init();
    cpu_init_bsp();
    kprintf("GDT Initialized\n");

    idt_init();
    kprintf("IDT Initialized\n");

    pmm_init();
    kprintf("PMM Initialized\n");

    block_init();
    nvme_init();
    ahci_init();
    ata_init();

    filesystems_init();
    fs_kcalls_init();
    socket_init();
    inet_init();

    lapic_init();
    keyboard_init();
    mouse_init();
    xhci_init();
    ps2_mouse_init();
    hda_init();
    task_init();

    task_create("net", net_task, NULL);

    if (!task_exec_elf("/bin/init.elf", NULL))
        kprintf("kernel: failed to start /bin/init.elf\n");

    scheduler_timer_init();
    smp_init();

    bkl_unlock();
    cpu_idle_loop();
}