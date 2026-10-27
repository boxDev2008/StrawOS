static uint8_t *bitmap = NULL;
static uint64_t bitmap_size = 0;
static uint64_t highest_addr = 0;
static uint64_t free_pages = 0;
static uint64_t usable_pages = 0;

static inline void bitmap_set(uint64_t bit)
{
    bitmap[bit / 8] |= (1u << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit)
{
    bitmap[bit / 8] &= ~(1u << (bit % 8));
}

static inline int bitmap_test(uint64_t bit)
{
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

static void memset_local(void *dst, int c, uint64_t n)
{
    uint8_t *p = (uint8_t *)dst;
    for (uint64_t i = 0; i < n; i++) p[i] = (uint8_t)c;
}

static uint64_t hhdm_offset = 0;

void *phys_to_virt(uint64_t phys)
{
    return (void*)(phys + hhdm_offset);
}

uint64_t virt_to_phys(void *virt)
{
    return (uint64_t)virt - hhdm_offset;
}

extern uint8_t __kernel_virt_start[];
extern uint8_t __kernel_virt_end[];

void pmm_init(void)
{
    struct limine_memmap_response *memmap = memmap_request.response;

    hhdm_offset = hhdm_request.response->offset;

    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t top = e->base + e->length;
            kprintf("base: %llu, length: %llu\n", e->base, e->length);
            if (top > highest_addr)
                highest_addr = top;
        }
    }

    uint64_t total_pages = highest_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;

    uint64_t kvirt_base = kaddr_request.response->virtual_base;
    uint64_t kphys_base = kaddr_request.response->physical_base;

    uint64_t kstart = ((uint64_t)__kernel_virt_start - kvirt_base) + kphys_base;
    uint64_t kend = ((uint64_t)__kernel_virt_end   - kvirt_base) + kphys_base;

    uint64_t bitmap_phys = 0;
    uint64_t best_len = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        if (e->length > best_len)
        {
            best_len = e->length;
            bitmap_phys = e->base;
        }
    }

    if (bitmap_phys == 0)
    {
        kprintf("pmm_init: no usable region >= 1MiB found for bitmap!\n");
        panic();
    }

    bitmap = (uint8_t*)phys_to_virt(bitmap_phys);

    memset_local(bitmap, 0xFF, bitmap_size);

    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t start_page = e->base / PAGE_SIZE;
        uint64_t page_count  = e->length / PAGE_SIZE;

        for (uint64_t p = 0; p < page_count; p++)
        {
            bitmap_clear(start_page + p);
            free_pages++;
            usable_pages++;
        }
    }

    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t p = 0; p < bitmap_pages; p++)
    {
        if (!bitmap_test(bitmap_start_page + p))
        {
            bitmap_set(bitmap_start_page + p);
            free_pages--;
        }
    }

    uint64_t kstart_page = kstart / PAGE_SIZE;
    uint64_t kend_page = (kend + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = kstart_page; p < kend_page; p++)
    {
        if (!bitmap_test(p))
        {
            bitmap_set(p);
            free_pages--;
        }
    }
}

uint64_t pmm_total_page_count(void)
{
    return usable_pages;
}

static void *pmm_alloc_pages_locked(uint64_t count)
{
    uint64_t total_pages = highest_addr / PAGE_SIZE;
    uint64_t run_start = 0;
    uint64_t run_len = 0;

    for (uint64_t i = 0; i < total_pages; i++)
    {
        if (!bitmap_test(i))
        {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len == count)
            {
                for (uint64_t p = run_start; p < run_start + count; p++)
                {
                    bitmap_set(p);
                    free_pages--;
                }
                return phys_to_virt(run_start * PAGE_SIZE);
            }
        }
        else
        {
            run_len = 0;
        }
    }
    return NULL;
}

void *pmm_alloc_pages(uint64_t count)
{
    uint64_t flags = irq_save();
    void *p = pmm_alloc_pages_locked(count);
    irq_restore(flags);
    return p;
}

static void *pmm_alloc_page_locked(void)
{
    uint64_t total_pages = highest_addr / PAGE_SIZE;

    for (uint64_t i = 0; i < total_pages; i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            free_pages--;
            return phys_to_virt(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void *pmm_alloc_page(void)
{
    uint64_t flags = irq_save();
    void *p = pmm_alloc_page_locked();
    irq_restore(flags);
    return p;
}

void pmm_free_page(void *page)
{
    uint64_t phys = virt_to_phys(page);
    uint64_t bit = phys / PAGE_SIZE;

    uint64_t flags = irq_save();
    if (bitmap_test(bit))
    {
        bitmap_clear(bit);
        free_pages++;
    }
    irq_restore(flags);
}

void pmm_free_pages(void *pages, uint64_t count)
{
    if (pages == NULL || count == 0)
        return;

    uint64_t phys = virt_to_phys(pages);
    uint64_t start_page = phys / PAGE_SIZE;

    uint64_t total_pages = highest_addr / PAGE_SIZE;

    if (start_page >= total_pages || count > total_pages - start_page)
    {
        kprintf("pmm_free_pages: invalid range\n");
        return;
    }

    uint64_t flags = irq_save();
    for (uint64_t i = 0; i < count; i++)
    {
        uint64_t bit = start_page + i;

        if (bitmap_test(bit))
        {
            bitmap_clear(bit);
            free_pages++;
        }
    }
    irq_restore(flags);
}

uint64_t pmm_free_page_count(void)
{
    return free_pages;
}