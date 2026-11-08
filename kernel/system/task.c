static Task *task_list = NULL;
static Task *kernel_task = NULL;

#define TASK_IDLE_STACK_SIZE (64 * 1024)

#define NICE_0_WEIGHT    1024
#define SCHED_NR_LATENCY 8

static const uint32_t nice_weights[40] = {
    88761, 71755, 56483, 46273, 36291,
    29154, 23254, 18705, 14949, 11916,
     9548,  7620,  6100,  4904,  3906,
     3121,  2501,  1991,  1586,  1277,
     1024,   820,   655,   526,   423,
      335,   272,   215,   172,   137,
      110,    87,    70,    56,    45,
       36,    29,    23,    18,    15,
};

static RbTree   rq_tree;
static RbTree   timer_tree;
static Task    *wait_list;
static uint64_t rq_min_vruntime;
static uint64_t rq_load;
static uint64_t rq_nr_running;

static uint64_t sched_latency;
static uint64_t sched_min_gran;
static uint64_t sched_wakeup_gran;

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

// fxsave/fxrstor fault unless fpu_state is 16-byte aligned, which kmalloc gives us
// only while its header keeps the returned pointer on a 16-byte boundary.
_Static_assert(sizeof(HeapBlockHeader) % 16 == 0, "kmalloc must return 16-byte aligned tasks");

static Task *task_alloc(void)
{
    Task *t = kmalloc(sizeof(Task));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(Task));
    t->weight = NICE_0_WEIGHT;

    uint64_t flags = irq_save();
    t->list_next = task_list;
    task_list = t;
    irq_restore(flags);
    return t;
}

static void task_free(Task *t)
{
    uint64_t flags = irq_save();
    for (Task **link = &task_list; *link; link = &(*link)->list_next)
    {
        if (*link == t)
        {
            *link = t->list_next;
            break;
        }
    }
    irq_restore(flags);
    kfree(t);
}

static bool vruntime_before(uint64_t a, uint64_t b)
{
    return (int64_t)(a - b) < 0;
}

static bool rq_less(const RbNode *a, const RbNode *b)
{
    return vruntime_before(RB_ENTRY(a, Task, rq_node)->vruntime, RB_ENTRY(b, Task, rq_node)->vruntime);
}

static bool timer_less(const RbNode *a, const RbNode *b)
{
    return (int64_t)(RB_ENTRY(a, Task, timer_node)->wake_tick - RB_ENTRY(b, Task, timer_node)->wake_tick) < 0;
}

static Task *rq_first(void)
{
    RbNode *n = rb_first(&rq_tree);
    return n ? RB_ENTRY(n, Task, rq_node) : NULL;
}

static void rq_enqueue(Task *t)
{
    rb_insert(&rq_tree, &t->rq_node, rq_less);
    t->on_rq = true;
}

static void rq_dequeue(Task *t)
{
    if (!t->on_rq)
        return;
    rb_erase(&rq_tree, &t->rq_node);
    t->on_rq = false;
}

static void set_runnable(Task *t, bool runnable)
{
    if (t->runnable == runnable)
        return;
    t->runnable = runnable;
    if (runnable)
    {
        rq_load += t->weight;
        rq_nr_running++;
    }
    else
    {
        rq_load -= t->weight;
        rq_nr_running--;
    }
}

static void timer_arm(Task *t, uint64_t wake_tick)
{
    t->wake_tick = wake_tick;
    rb_insert(&timer_tree, &t->timer_node, timer_less);
    t->on_timer = true;
}

static void timer_cancel(Task *t)
{
    if (!t->on_timer)
        return;
    rb_erase(&timer_tree, &t->timer_node);
    t->on_timer = false;
}

static void wait_list_add(Task *t)
{
    t->wait_prev = NULL;
    t->wait_next = wait_list;
    if (wait_list)
        wait_list->wait_prev = t;
    wait_list = t;
    t->waiting = true;
}

static void wait_list_remove(Task *t)
{
    if (!t->waiting)
        return;
    if (t->wait_prev)
        t->wait_prev->wait_next = t->wait_next;
    else
        wait_list = t->wait_next;
    if (t->wait_next)
        t->wait_next->wait_prev = t->wait_prev;
    t->wait_prev = t->wait_next = NULL;
    t->waiting = false;
}

static void sched_detach(Task *t)
{
    rq_dequeue(t);
    timer_cancel(t);
    wait_list_remove(t);
    set_runnable(t, false);
}

static uint64_t calc_delta_fair(uint64_t delta, const Task *t)
{
    if (t->weight == NICE_0_WEIGHT)
        return delta;
    if (delta > (1ULL << 40))
        delta = 1ULL << 40;
    return delta * NICE_0_WEIGHT / t->weight;
}

static uint64_t sched_slice(const Task *t)
{
    uint64_t nr = rq_nr_running + (t->runnable ? 0 : 1);
    uint64_t load = rq_load + (t->runnable ? 0 : t->weight);
    uint64_t period = nr > SCHED_NR_LATENCY ? nr * sched_min_gran : sched_latency;

    uint64_t slice = period * t->weight * smp_cpu_count() / load;
    if (slice > period)
        slice = period;
    if (slice < sched_min_gran)
        slice = sched_min_gran;
    return slice;
}

static void update_min_vruntime(Task *curr)
{
    Task *first = rq_first();
    uint64_t v;

    if (curr)
        v = curr->vruntime;
    else if (first)
        v = first->vruntime;
    else
        return;

    if (first && vruntime_before(first->vruntime, v))
        v = first->vruntime;
    if (vruntime_before(rq_min_vruntime, v))
        rq_min_vruntime = v;
}

static void update_curr(Cpu *cpu)
{
    Task *t = cpu->current;
    if (!t || t == cpu->idle)
        return;

    uint64_t now = rdtsc();
    uint64_t delta = now - t->exec_start;
    t->exec_start = now;
    if ((int64_t)delta <= 0)
        return;

    t->sum_exec += delta;
    t->vruntime += calc_delta_fair(delta, t);
    update_min_vruntime(t);
}

static void place_task(Task *t, bool initial)
{
    if (initial)
    {
        t->vruntime = rq_min_vruntime + calc_delta_fair(sched_slice(t), t);
        return;
    }

    uint64_t floor = rq_min_vruntime - sched_latency / 2;
    if (vruntime_before(t->vruntime, floor))
        t->vruntime = floor;
}

static bool wakeup_preempts(Task *curr, Task *t)
{
    int64_t lead = (int64_t)(curr->vruntime - t->vruntime);
    return lead > (int64_t)calc_delta_fair(sched_wakeup_gran, t);
}

static void activate(Task *t, bool initial)
{
    timer_cancel(t);
    wait_list_remove(t);
    place_task(t, initial);
    t->state = TASK_READY;
    set_runnable(t, true);
    rq_enqueue(t);

    Cpu *cpu = this_cpu();
    if (cpu->current == cpu->idle)
    {
        cpu->need_resched = true;
        return;
    }

    if (smp_kick_idle_cpu())
        return;

    update_curr(cpu);
    if (wakeup_preempts(cpu->current, t))
        cpu->need_resched = true;
}

static void wake_up(Task *t)
{
    if (t->state == TASK_SLEEPING)
        activate(t, false);
}

static void task_set_nice(Task *t, int nice)
{
    if (nice < TASK_NICE_MIN)
        nice = TASK_NICE_MIN;
    if (nice > TASK_NICE_MAX)
        nice = TASK_NICE_MAX;

    uint64_t flags = irq_save();

    bool queued = t->on_rq;
    bool was_runnable = t->runnable;
    rq_dequeue(t);
    set_runnable(t, false);

    t->nice = nice;
    t->weight = nice_weights[nice - TASK_NICE_MIN];

    set_runnable(t, was_runnable);
    if (queued)
        rq_enqueue(t);

    irq_restore(flags);
}

static void task_fpu_init(Task *t)
{
    *(uint16_t *)&t->fpu_state[0]  = 0x037F;
    *(uint32_t *)&t->fpu_state[24] = 0x1F80;
}

void task_init(void)
{
    Task *idle = task_alloc();
    kernel_task = idle;
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
    Task *t = task_alloc();
    if (!t)
        return NULL;

    size_t pages = TASK_IDLE_STACK_SIZE / PAGE_SIZE;
    void *stack = pmm_alloc_pages(pages);
    if (!stack)
    {
        task_free(t);
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
    Task *t = task_alloc();
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
        task_free(t);
        return NULL;
    }

    t->stack_base = stack;
    t->stack_pages = pages;
    t->stdio[0] = t->stdio[1] = t->stdio[2] = -1;
    Task *cur = task_current();
    strcpy(t->cwd, cur ? cur->cwd : "/");
    t->pid = next_pid++;
    t->nice = cur ? cur->nice : 0;
    t->weight = nice_weights[t->nice - TASK_NICE_MIN];

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
    activate(t, true);
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

static void schedule(TaskState prev_state)
{
    Cpu *cpu = this_cpu();
    Task *prev = cpu->current;

    update_curr(cpu);
    cpu->need_resched = false;
    cpu->kick_pending = false;

    if (prev->state == TASK_RUNNING)
        prev->state = prev_state;

    if (prev != cpu->idle)
    {
        if (prev->state == TASK_READY)
            rq_enqueue(prev);
        else
            set_runnable(prev, false);
    }

    Task *next = rq_first();
    if (next)
        rq_dequeue(next);
    else
        next = cpu->idle;

    next->state = TASK_RUNNING;
    if (next == prev)
        return;

    next->exec_start = rdtsc();
    next->slice_start = next->sum_exec;
    cpu->current = next;
    task_switch_asm(&prev->saved_rsp, next->saved_rsp, prev->fpu_state, next->fpu_state);
}

void task_sched_start(uint64_t tsc_per_ms)
{
    if (tsc_per_ms == 0)
        tsc_per_ms = 1000000;

    sched_latency     = 6 * tsc_per_ms;
    sched_min_gran    = tsc_per_ms * 3 / 4;
    sched_wakeup_gran = tsc_per_ms;
}

void task_yield(void)
{
    Task *t = task_current();
    if (!t)
        return;

    uint64_t flags = irq_save();
    Cpu *cpu = this_cpu();
    if (t != cpu->idle)
    {
        update_curr(cpu);

        Task *first = rq_first();
        if (first && !vruntime_before(first->vruntime, t->vruntime))
            t->vruntime = first->vruntime;
    }
    schedule(TASK_READY);
    irq_restore(flags);
}

void task_resched(void)
{
    Cpu *cpu = this_cpu();
    if (cpu->current && cpu->need_resched)
        schedule(TASK_READY);
}

void task_irq_exit(void)
{
    Cpu *cpu = this_cpu();
    Task *t = cpu->current;
    if (!t || !cpu->need_resched || t->in_kcall)
        return;
    schedule(TASK_READY);
}

static void wake_timers(uint64_t now)
{
    RbNode *n;
    while ((n = rb_first(&timer_tree)))
    {
        Task *t = RB_ENTRY(n, Task, timer_node);
        if ((int64_t)(now - t->wake_tick) < 0)
            break;
        timer_cancel(t);
        wake_up(t);
    }
}

void task_scheduler_tick(void)
{
    Cpu *cpu = this_cpu();
    cpu->ticks++;
    if (cpu->current != cpu->idle)
    {
        cpu->busy_ticks++;
        cpu->current->cpu_ticks++;
    }

    if (cpu_is_bsp())
    {
        g_tick_count++;
        wake_timers(g_tick_count);
    }

    Task *t = cpu->current;
    if (!t)
        return;

    if (t == cpu->idle)
    {
        if (rq_first())
            cpu->need_resched = true;
        return;
    }

    update_curr(cpu);

    Task *first = rq_first();
    if (!first)
        return;

    uint64_t ran = t->sum_exec - t->slice_start;
    uint64_t ideal = sched_slice(t);
    if (ran >= ideal)
        cpu->need_resched = true;
    else if (ran >= sched_min_gran && (int64_t)(t->vruntime - first->vruntime) > (int64_t)ideal)
        cpu->need_resched = true;
}

static uint64_t ms_to_ticks(uint32_t ms)
{
    uint64_t ticks = DIV_CEIL((uint64_t)ms * SCHEDULER_HZ, 1000);
    return ticks ? ticks : 1;
}

void task_sleep_ms(uint32_t ms)
{
    Task *prev = task_current();
    if (!prev)
        return;

    uint64_t flags = irq_save();
    if (prev == this_cpu()->idle)
    {
        schedule(TASK_READY);
    }
    else
    {
        timer_arm(prev, g_tick_count + ms_to_ticks(ms));
        schedule(TASK_SLEEPING);
    }
    irq_restore(flags);
}

void task_block(uint32_t timeout_ms)
{
    Task *prev = task_current();
    if (!prev)
        return;

    uint64_t flags = irq_save();
    if (prev == this_cpu()->idle)
    {
        schedule(TASK_READY);
    }
    else
    {
        if (timeout_ms != TASK_BLOCK_FOREVER)
            timer_arm(prev, g_tick_count + ms_to_ticks(timeout_ms));
        wait_list_add(prev);
        schedule(TASK_SLEEPING);
        wait_list_remove(prev);
    }
    irq_restore(flags);
}

void task_wake_waiters(void)
{
    uint64_t flags = irq_save();

    Task *t = wait_list;
    while (t)
    {
        Task *next = t->wait_next;
        wake_up(t);
        t = next;
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

    sched_detach(t);
    task_free(t);
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
    for (Task *t = task_list; t; t = t->list_next)
        for (int k = 0; k < 3; k++)
            if (t->stdio[k] == fd)
                t->stdio[k] = -1;
}

void task_for_each(TaskVisitor visit, void *ctx)
{
    for (Task *t = task_list; t; t = t->list_next)
        if (t->pid != 0 && t->state != TASK_DEAD)
            visit(t, ctx);
}

static Task *task_find(uint64_t pid)
{
    if (pid == 0)
        return NULL;
    for (Task *t = task_list; t; t = t->list_next)
        if (t->pid == pid)
            return t;
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
    if (!t || t == kernel_task)
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

static Task *task_find_live(uint64_t pid)
{
    Task *t = pid == 0 ? task_current() : task_find(pid);
    if (!t || t->pid == 0 || t->state == TASK_DEAD)
        return NULL;
    return t;
}

int64_t sys_setpriority(uint64_t pid, int64_t nice)
{
    Task *t = task_find_live(pid);
    if (!t)
        return -1;
    task_set_nice(t, (int)MAX(MIN(nice, TASK_NICE_MAX), TASK_NICE_MIN));
    return 0;
}

int64_t sys_getpriority(uint64_t pid, int *nice)
{
    Task *t = task_find_live(pid);
    if (!t || !nice)
        return -1;
    *nice = t->nice;
    return 0;
}
