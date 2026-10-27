#define TASK_MAX 1024

static Task  task_pool[TASK_MAX] ALIGN(16);
static bool  task_pool_used[TASK_MAX];

#define TASK_IDLE_STACK_SIZE (64 * 1024)

static Task *task_run_queue = NULL;

static volatile uint64_t g_tick_count = 0;
static uint64_t next_pid = 1;

__attribute__((naked)) static void task_switch_asm
(
    UNUSED uint64_t *save_rsp_of_prev,
    UNUSED uint64_t new_rsp,
    UNUSED void *prev_fpu_state,
    UNUSED void *next_fpu_state
)
{
    __asm__ volatile(
        "push %rbp\n"
        "push %rbx\n"
        "push %r12\n"
        "push %r13\n"
        "push %r14\n"
        "push %r15\n"

        "fxsave (%rdx)\n"

        "mov %rsp, (%rdi)\n"
        "mov %rsi, %rsp\n"

        "fxrstor (%rcx)\n"

        "pop %r15\n"
        "pop %r14\n"
        "pop %r13\n"
        "pop %r12\n"
        "pop %rbx\n"
        "pop %rbp\n"
        "ret\n"
    );
}

static void task_trampoline(void);

static Task *task_alloc_slot(void)
{
    for (int i = 0; i < TASK_MAX; i++)
    {
        if (!task_pool_used[i])
        {
            task_pool_used[i] = true;
            memset(&task_pool[i], 0, sizeof(Task));
            return &task_pool[i];
        }
    }
    return NULL;
}

static void task_free_slot(Task *t)
{
    for (int i = 0; i < TASK_MAX; i++)
    {
        if (&task_pool[i] == t)
        {
            task_pool_used[i] = false;
            return;
        }
    }
}

static void run_queue_push(Task *t)
{
    if (!task_run_queue)
    {
        task_run_queue = t;
        t->next = t;
        return;
    }

    t->next = task_run_queue->next;
    task_run_queue->next = t;
}

static void run_queue_remove(Task *t)
{
    if (!task_run_queue)
        return;

    if (task_run_queue == t && task_run_queue->next == t)
    {
        task_run_queue = NULL;
        return;
    }

    Task *p = task_run_queue;
    do
    {
        if (p->next == t)
        {
            p->next = t->next;
            if (task_run_queue == t)
                task_run_queue = t->next;
            return;
        }
        p = p->next;
    }
    while (p != task_run_queue);
}

static void task_fpu_init(Task *t)
{
    *(uint16_t *)&t->fpu_state[0]  = 0x037F;
    *(uint32_t *)&t->fpu_state[24] = 0x1F80;
}

void task_init(void)
{
    memset(task_pool, 0, sizeof(task_pool));
    memset(task_pool_used, 0, sizeof(task_pool_used));

    Task *idle = task_alloc_slot();
    strncpy(idle->name, "kernel", TASK_NAME_MAX - 1);
    idle->state = TASK_RUNNING;
    idle->saved_rsp = 0;
    idle->stdio[0] = idle->stdio[1] = idle->stdio[2] = -1;
    task_fpu_init(idle);

    Cpu *cpu = this_cpu();
    cpu->idle = idle;
    cpu->current = idle;
}

Task *task_create_idle(void)
{
    Task *t = task_alloc_slot();
    if (!t)
        return NULL;

    size_t pages = TASK_IDLE_STACK_SIZE / PAGE_SIZE;
    void *stack = pmm_alloc_pages(pages);
    if (!stack)
    {
        task_free_slot(t);
        return NULL;
    }

    t->stack_base = stack;
    t->stack_pages = pages;
    t->stdio[0] = t->stdio[1] = t->stdio[2] = -1;
    task_fpu_init(t);
    t->state = TASK_RUNNING;
    strncpy(t->name, "idle", TASK_NAME_MAX - 1);
    return t;
}

Task *task_current(void)
{
    return this_cpu()->current;
}

void task_bootstrap(TaskEntry entry, void *arg)
{
    bool program = task_current()->owns_image;

    if (program)
        bkl_unlock();
    sti();

    entry(arg);

    if (program)
    {
        cli();
        bkl_lock();
        sti();
    }
    task_exit(0);
}

__attribute__((naked)) static void task_trampoline(void)
{
    __asm__ volatile(
        "pop %rdi\n"
        "pop %rsi\n"
        "call task_bootstrap\n"
    );
}

static TaskArgs *task_args_from_argv(const char *path, char *const argv[])
{
    int argc = 0;
    while (argv && argv[argc])
        argc++;

    TaskArgs *ta = kmalloc(sizeof(TaskArgs));
    if (!ta)
        return NULL;

    size_t backing_size = strlen(path) + 1;
    for (int i = 0; i < argc; i++)
        backing_size += strlen(argv[i]) + 1;

    ta->backing = kmalloc(backing_size);
    if (!ta->backing)
    {
        kfree(ta);
        return NULL;
    }

    ta->argv = kmalloc(sizeof(char *) * (argc + 1));
    if (!ta->argv)
    {
        kfree(ta->backing);
        kfree(ta);
        return NULL;
    }

    char *cursor = ta->backing;

    for (int i = 0; i < argc; i++)
    {
        size_t len = strlen(argv[i]) + 1;
        memcpy(cursor, argv[i], len);
        ta->argv[i] = cursor;
        cursor += len;
    }
    ta->argv[argc] = NULL;
    ta->argc = argc;

    return ta;
}

static void task_args_free(TaskArgs *ta)
{
    if (!ta)
        return;
    kfree(ta->argv);
    kfree(ta->backing);
    kfree(ta);
}

static Task *task_new_common(const char *name)
{
    Task *t = task_alloc_slot();
    if (!t)
    {
        kprintf("task: out of task slots\n");
        return NULL;
    }

    size_t pages = TASK_STACK_SIZE / PAGE_SIZE;
    void *stack = pmm_alloc_pages(pages);
    if (!stack)
    {
        kprintf("task: out of memory for stack\n");
        task_free_slot(t);
        return NULL;
    }

    t->stack_base = stack;
    t->stack_pages = pages;
    t->stdio[0] = t->stdio[1] = t->stdio[2] = -1;
    Task *cur = task_current();
    strcpy(t->cwd, cur ? cur->cwd : "/");
    t->pid = next_pid++;

    task_fpu_init(t);

    t->state = TASK_READY;
    strncpy(t->name, name ? name : "task", TASK_NAME_MAX - 1);

    return t;
}

static void task_prime_stack(Task *t, TaskEntry entry, void *arg)
{
    uint8_t *top = (uint8_t *)t->stack_base + t->stack_pages * PAGE_SIZE;
    uint64_t *sp = (uint64_t *)top;

    *(--sp) = (uint64_t)arg;
    *(--sp) = (uint64_t)entry;
    *(--sp) = (uint64_t)task_trampoline;

    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

    t->saved_rsp = (uint64_t)sp;
}

static Task *task_create_with(const char *name, TaskEntry entry, void *arg, ElfImage *img)
{
    if (!entry)
        return NULL;

    Task *t = task_new_common(name);
    if (!t)
        return NULL;

    if (img)
    {
        t->image = *img;
        t->owns_image = true;
    }

    task_prime_stack(t, entry, arg);

    uint64_t flags = irq_save();
    run_queue_push(t);
    irq_restore(flags);
    return t;
}

Task *task_create(const char *name, TaskEntry entry, void *arg)
{
    return task_create_with(name, entry, arg, NULL);
}

Task *task_create_from_image(const char *name, ElfImage *img, void *arg)
{
    if (!img || !img->entry)
        return NULL;

    return task_create_with(name, (TaskEntry)img->entry, arg, img);
}

static Task *pick_next(void)
{
    if (!task_run_queue)
        return NULL;

    Task *t = task_run_queue;
    do
    {
        t = t->next;
        if (t->state == TASK_READY)
        {
            task_run_queue = t;
            return t;
        }
    }
    while (t != task_run_queue);

    return NULL;
}

static void schedule(TaskState prev_state)
{
    Cpu *cpu = this_cpu();
    Task *prev = cpu->current;

    if (prev->state == TASK_RUNNING)
        prev->state = prev_state;

    Task *next = pick_next();
    if (!next)
        next = (prev != cpu->idle && prev->state == TASK_READY) ? prev : cpu->idle;

    next->state = TASK_RUNNING;
    if (next == prev)
        return;

    cpu->current = next;
    task_switch_asm(&prev->saved_rsp, next->saved_rsp, prev->fpu_state, next->fpu_state);
}

void task_yield(void)
{
    if (!task_current())
        return;

    uint64_t flags = irq_save();
    schedule(TASK_READY);
    irq_restore(flags);
}

static void wake_sleepers(uint64_t now)
{
    if (!task_run_queue)
        return;

    Task *t = task_run_queue;
    do
    {
        if (t->state == TASK_SLEEPING && (int64_t)(now - t->wake_tick) >= 0)
            t->state = TASK_READY;
        t = t->next;
    }
    while (t != task_run_queue);
}

void task_scheduler_tick(void)
{
    Cpu *cpu = this_cpu();
    cpu->ticks++;
    if (cpu->current != cpu->idle)
        cpu->busy_ticks++;

    if (cpu_is_bsp())
    {
        g_tick_count++;
        wake_sleepers(g_tick_count);
    }

    if (!task_current())
        return;

    schedule(TASK_READY);
}

void task_sleep_ms(uint32_t ms)
{
    Task *prev = task_current();
    if (!prev)
        return;

    uint64_t ticks = DIV_CEIL((uint64_t)ms * SCHEDULER_HZ, 1000);
    if (ticks == 0)
        ticks = 1;

    uint64_t flags = irq_save();
    prev->wake_tick = g_tick_count + ticks;
    schedule(TASK_SLEEPING);
    irq_restore(flags);
}

void task_block(uint32_t timeout_ms)
{
    Task *prev = task_current();
    if (!prev)
        return;

    uint64_t ticks = timeout_ms == TASK_BLOCK_FOREVER
        ? (1ULL << 62)
        : DIV_CEIL((uint64_t)timeout_ms * SCHEDULER_HZ, 1000);
    if (ticks == 0)
        ticks = 1;

    uint64_t flags = irq_save();
    prev->wake_tick = g_tick_count + ticks;
    prev->waiting = true;
    schedule(TASK_SLEEPING);
    prev->waiting = false;
    irq_restore(flags);
}

void task_wake_waiters(void)
{
    uint64_t flags = irq_save();

    if (task_run_queue)
    {
        Task *t = task_run_queue;
        do
        {
            if (t->waiting && t->state == TASK_SLEEPING)
                t->state = TASK_READY;
            t = t->next;
        }
        while (t != task_run_queue);
    }

    irq_restore(flags);
}

uint64_t task_uptime_ms(void)
{
    return g_tick_count * 1000 / SCHEDULER_HZ;
}

static void task_release_resources(Task *t)
{
    fs_release_task(t);
    user_pages_release_task(t);
}

NORETURN void task_exit(int code)
{
    Task *t = task_current();
    t->exit_code = code;

    task_release_resources(t);

    cli();
    t->state = TASK_DEAD;

    if (t->args)
    {
        task_args_free(t->args);
        t->args = NULL;
    }

    if (t->owns_image)
    {
        elf_unload_image(&t->image);
        t->owns_image = false;
    }

    if (t->stack_base)
    {
        pmm_free_pages(t->stack_base, t->stack_pages);
        t->stack_base = NULL;
        t->stack_pages = 0;
    }

    kprintf("Exited with code %d\n", code);

    for (;;)
        schedule(TASK_DEAD);
}

void task_check_kill(void)
{
    Task *t = task_current();
    if (t && t->kill_pending)
        task_exit(-1);
}

Task *task_exec_elf(const char *path, char *const argv[])
{
    if (!path)
        return NULL;

    TaskArgs *ta = task_args_from_argv(path, argv);
    if (!ta)
    {
        kprintf("exec: out of memory building args for '%s'\n", path);
        return NULL;
    }

    VfsStat st;
    if (vfs_stat(path, &st) != 0)
    {
        kprintf("exec: %s not found\n", path);
        task_args_free(ta);
        return NULL;
    }

    if (st.type != VNODE_FILE || st.size == 0)
    {
        kprintf("exec: %s is not a regular file\n", path);
        task_args_free(ta);
        return NULL;
    }

    VFile f;
    if (vfs_open(path, VFS_O_RDONLY, &f) != 0)
    {
        kprintf("exec: failed to open %s\n", path);
        task_args_free(ta);
        return NULL;
    }

    void *buf = kmalloc(st.size);
    if (!buf)
    {
        kprintf("exec: out of memory reading %s (%zu bytes)\n", path, st.size);
        vfs_close(&f);
        task_args_free(ta);
        return NULL;
    }

    ssize_t n = vfs_read(&f, buf, st.size);
    vfs_close(&f);

    if (n < 0 || (size_t)n != st.size)
    {
        kprintf("exec: short read on %s\n", path);
        kfree(buf);
        task_args_free(ta);
        return NULL;
    }

    ElfImage img;
    int rc = elf_load_image(buf, st.size, &img);

    kfree(buf);

    if (rc != 0)
    {
        kprintf("exec: %s is not a valid ELF64 image\n", path);
        task_args_free(ta);
        return NULL;
    }

    Task *t = task_create_from_image(path, &img, ta);
    if (!t)
    {
        kprintf("exec: failed to create task for %s\n", path);
        elf_unload_image(&img);
        task_args_free(ta);
        return NULL;
    }

    t->args = ta;

    Task *cur = task_current();
    if (cur && cur != t)
        for (int i = 0; i < 3; i++)
            t->stdio[i] = cur->stdio[i];

    return t;
}

int task_kill(Task *t)
{
    if (!t)
        return false;

    if (t->state == TASK_DEAD)
        return false;

    if (t == task_current())
        task_exit(-1);

    if (t->state == TASK_RUNNING)
    {
        t->kill_pending = true;
        return true;
    }

    task_release_resources(t);

    uint64_t flags = irq_save();
    t->state = TASK_DEAD;

    if (t->owns_image)
    {
        elf_unload_image(&t->image);
        t->owns_image = false;
    }

    if (t->stack_base)
    {
        pmm_free_pages(t->stack_base, t->stack_pages);
        t->stack_base = NULL;
        t->stack_pages = 0;
    }

    run_queue_remove(t);
    task_free_slot(t);
    irq_restore(flags);

    return true;
}

int task_wait(Task *t, uint32_t flags)
{
    if (!t)
        return false;

    if (flags & WNOHANG)
    {
        while (t->state != TASK_DEAD);
        return false;
    }
    else
    {
        return t->state != TASK_DEAD;
    }
}
void task_forget_fd(int fd)
{
    for (int i = 0; i < TASK_MAX; i++)
    {
        if (!task_pool_used[i])
            continue;
        for (int k = 0; k < 3; k++)
            if (task_pool[i].stdio[k] == fd)
                task_pool[i].stdio[k] = -1;
    }
}

static Task *task_find(uint64_t pid)
{
    if (pid == 0)
        return NULL;
    for (int i = 0; i < TASK_MAX; i++)
        if (task_pool_used[i] && task_pool[i].pid == pid)
            return &task_pool[i];
    return NULL;
}

uint64_t sys_spawn(const char *path, char *const argv[])
{
    Task *t = task_exec_elf(path, argv);
    return t ? t->pid : 0;
}

int64_t sys_chdir(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= VFS_PATH_MAX)
        return -1;

    VfsStat st;
    if (vfs_stat(path, &st) != 0 || st.type != VNODE_DIR)
        return -1;

    strcpy(task_current()->cwd, path);
    return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
    Task *cur = task_current();
    size_t len = strlen(cur->cwd);
    if (!buf || size <= len)
        return -1;

    memcpy(buf, cur->cwd, len + 1);
    return (int64_t)len;
}

int64_t sys_kill(uint64_t pid)
{
    Task *t = task_find(pid);
    if (!t || t == &task_pool[0])
        return -1;
    return task_kill(t) ? 0 : -1;
}

int64_t sys_task_status(uint64_t pid, int *code)
{
    Task *t = task_find(pid);
    if (!t)
    {
        if (code)
            *code = -1;
        return 1;
    }
    if (t->state == TASK_DEAD)
    {
        if (code)
            *code = t->exit_code;
        return 1;
    }
    return t->detached ? 2 : 0;
}

uint64_t sys_getpid(void)
{
    Task *cur = task_current();
    return cur ? cur->pid : 0;
}
