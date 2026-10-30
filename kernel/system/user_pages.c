typedef struct
{
    void     *addr;
    uint64_t  count;
    Task     *owner;
}
UserPageRun;

static UserPageRun *user_runs = NULL;
static uint64_t     user_run_count = 0;
static uint64_t     user_run_cap = 0;

static inline uint64_t user_page_count(uint64_t size)
{
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

void *sys_mem_alloc(uint64_t size)
{
    if (size == 0 || size > UINT64_MAX - PAGE_SIZE)
        return NULL;

    uint64_t count = user_page_count(size);

    uint64_t flags = irq_save();

    if (user_run_count == user_run_cap)
    {
        uint64_t cap = user_run_cap ? user_run_cap * 2 : 64;
        UserPageRun *n = krealloc(user_runs, cap * sizeof(UserPageRun));
        if (!n)
        {
            irq_restore(flags);
            return NULL;
        }
        user_runs = n;
        user_run_cap = cap;
    }

    void *p = pmm_alloc_pages(count);
    if (p)
        user_runs[user_run_count++] = (UserPageRun){ p, count, task_current() };

    irq_restore(flags);
    return p;
}

int64_t sys_mem_free(void *addr, uint64_t size)
{
    if (!addr)
        return 0;

    uint64_t count = user_page_count(size);

    uint64_t flags = irq_save();
    for (uint64_t i = 0; i < user_run_count; i++)
    {
        if (user_runs[i].addr != addr)
            continue;

        if (user_runs[i].count != count)
        {
            irq_restore(flags);
            kprintf("mem_free: size mismatch for %p (%llu pages != %llu)\n",
                    addr, count, user_runs[i].count);
            return -1;
        }

        pmm_free_pages(addr, count);
        user_runs[i] = user_runs[--user_run_count];
        irq_restore(flags);
        return 0;
    }
    irq_restore(flags);

    kprintf("mem_free: %p was not allocated with mem_alloc\n", addr);
    return -1;
}

void user_pages_release_task(Task *t)
{
    uint64_t flags = irq_save();

    for (uint64_t i = 0; i < user_run_count;)
    {
        if (user_runs[i].owner == t)
        {
            pmm_free_pages(user_runs[i].addr, user_runs[i].count);
            user_runs[i] = user_runs[--user_run_count];
        }
        else
            i++;
    }

    irq_restore(flags);
}

uint64_t user_pages_count(Task *t)
{
    uint64_t flags = irq_save();
    uint64_t pages = 0;
    for (uint64_t i = 0; i < user_run_count; i++)
        if (user_runs[i].owner == t)
            pages += user_runs[i].count;
    irq_restore(flags);
    return pages;
}
