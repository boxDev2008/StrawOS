static void scheduler_timer_irq(void)
{
    if (cpu_is_bsp())
    {
        xhci_poll();
        nic_poll();
        inet_tick();
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

static void filesystems_init(void)
{
    VNode *ramfs_root = ramfs_create_root();
    if (!ramfs_root)
    {
        kprintf("vfs: failed to create ramfs root\n");
        panic();
    }
    vfs_mount("/", ramfs_root);
    int n = ramfs_load_limine_modules();
    kprintf("vfs: loaded %d boot module(s)\n", n);

    VNode *devfs_root = devfs_create_root();
    if (!devfs_root)
    {
        kprintf("vfs: failed to create devfs root\n");
        panic();
    }
    vfs_mkdir("/dev");
    vfs_mount("/dev", devfs_root);
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

    filesystems_init();
    fs_kcalls_init();
    socket_init();
    inet_init();

    lapic_init();
    keyboard_init();
    mouse_init();
    xhci_init();
    ps2_mouse_init();
    task_init();

    task_create("net", net_task, NULL);

    if (!task_exec_elf("/bin/init.elf", NULL))
        kprintf("kernel: failed to start /bin/init.elf\n");

    scheduler_timer_init();
    smp_init();

    bkl_unlock();
    cpu_idle_loop();
}