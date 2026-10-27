static Cpu cpus[CPU_MAX];
static uint32_t cpu_count = 0;
static volatile uint32_t cpus_online = 0;

static volatile uint32_t bkl_next_ticket = 0;
static volatile uint32_t bkl_now_serving = 0;
static volatile int32_t  bkl_owner = -1;

_Static_assert(offsetof(Cpu, self) == 0, "ap_entry expects Cpu.self at 0");
_Static_assert(offsetof(Cpu, stack_top) == 8, "ap_entry expects Cpu.stack_top at 8");
_Static_assert(offsetof(struct limine_mp_info, extra_argument) == 24, "ap_entry expects extra_argument at 24");

void bkl_lock(void)
{
    uint32_t ticket = __atomic_fetch_add(&bkl_next_ticket, 1, __ATOMIC_RELAXED);
    while (__atomic_load_n(&bkl_now_serving, __ATOMIC_ACQUIRE) != ticket)
        pause();
    bkl_owner = (int32_t)this_cpu()->index;
}

void bkl_unlock(void)
{
    bkl_owner = -1;
    __atomic_store_n(&bkl_now_serving, bkl_now_serving + 1, __ATOMIC_RELEASE);
}

bool bkl_held(void)
{
    return bkl_owner == (int32_t)this_cpu()->index;
}

uint32_t smp_cpu_count(void)
{
    return cpu_count;
}

void smp_cpu_ticks(uint32_t index, uint64_t *total, uint64_t *busy)
{
    *total = __atomic_load_n(&cpus[index].ticks, __ATOMIC_RELAXED);
    *busy = __atomic_load_n(&cpus[index].busy_ticks, __ATOMIC_RELAXED);
}

void cpu_init_bsp(void)
{
    Cpu *c = &cpus[0];
    c->self = c;
    c->index = 0;
    wrmsr(IA32_GS_BASE, (uint64_t)c);
    cpu_count = 1;
    cpus_online = 1;

    bkl_lock();
}

NORETURN void cpu_idle_loop(void)
{
    for (;;)
    {
        sti();
        hlt();
    }
}

NORETURN void ap_main(Cpu *c)
{
    gdt_load();
    wrmsr(IA32_GS_BASE, (uint64_t)c);
    idt_load();
    fpu_init();
    lapic_enable();
    lapic_timer_start(SCHEDULER_HZ);

    __atomic_fetch_add(&cpus_online, 1, __ATOMIC_RELEASE);

    cpu_idle_loop();
}

__attribute__((naked)) static void ap_entry(UNUSED struct limine_mp_info *info)
{
    __asm__ volatile(
        "mov 24(%rdi), %rdi\n"
        "mov 8(%rdi), %rsp\n"
        "xor %ebp, %ebp\n"
        "call ap_main\n"
        "ud2\n"
    );
}

void smp_init(void)
{
    cpus[0].lapic_id = lapic_id();

    struct limine_mp_response *resp = mp_request.response;
    if (!resp)
    {
        kprintf("SMP: no MP response, running on 1 CPU\n");
        return;
    }

    for (uint64_t i = 0; i < resp->cpu_count; i++)
    {
        struct limine_mp_info *info = resp->cpus[i];
        if (info->lapic_id == resp->bsp_lapic_id)
            continue;

        if (cpu_count == CPU_MAX)
        {
            kprintf("SMP: more than %d CPUs, ignoring the rest\n", CPU_MAX);
            break;
        }

        Task *idle = task_create_idle();
        if (!idle)
        {
            kprintf("SMP: out of memory for CPU %d idle task\n", (int)cpu_count);
            break;
        }

        Cpu *c = &cpus[cpu_count];
        c->self = c;
        c->index = cpu_count;
        c->lapic_id = info->lapic_id;
        c->idle = idle;
        c->current = idle;
        c->stack_top = (uint64_t)idle->stack_base + idle->stack_pages * PAGE_SIZE;
        cpu_count++;

        info->extra_argument = (uint64_t)c;
        __atomic_store_n(&info->goto_address, ap_entry, __ATOMIC_SEQ_CST);
    }

    while (__atomic_load_n(&cpus_online, __ATOMIC_ACQUIRE) != cpu_count)
        pause();

    kprintf("SMP: %d CPU(s) online\n", (int)cpu_count);
}
