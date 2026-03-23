#include "pmm.h"
#include <stddef.h>
#include <string.h>

typedef struct FreeBlock
{
    struct FreeBlock *next;
}
FreeBlock;

static uint8_t  *g_bitmap;
static uint64_t  g_total_pages;
static uint64_t  g_free_pages;

static FreeBlock *g_free_lists[PMM_ORDER_COUNT];

static volatile int g_lock;
static inline void lock_acquire(void)  { while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)); }
static inline void lock_release(void)  { __atomic_clear(&g_lock, __ATOMIC_RELEASE); }

static inline void bitmap_set(uint64_t pfn)
{
    g_bitmap[pfn >> 3] |= (uint8_t)(1u << (pfn & 7));
}

static inline void bitmap_clear(uint64_t pfn)
{
    g_bitmap[pfn >> 3] &= (uint8_t)~(1u << (pfn & 7));
}

static inline int bitmap_get(uint64_t pfn)
{
    return (g_bitmap[pfn >> 3] >> (pfn & 7)) & 1;
}

static inline void block_mark_alloc(uint64_t pfn, uint32_t order)
{
    uint64_t pages = (uint64_t)1 << order;
    for (uint64_t i = pfn; i < pfn + pages; i++) bitmap_set(i);
}

static inline void block_mark_free(uint64_t pfn, uint32_t order)
{
    uint64_t pages = (uint64_t)1 << order;
    for (uint64_t i = pfn; i < pfn + pages; i++) bitmap_clear(i);
}

static inline bool buddy_is_free(uint64_t buddy_pfn_val, uint32_t order)
{
    if (buddy_pfn_val + ((uint64_t)1 << order) > g_total_pages) return false;
    return bitmap_get(buddy_pfn_val) == 0;
}

static inline uint64_t buddy_of(uint64_t pfn, uint32_t order)
{
    return pfn ^ ((uint64_t)1 << order);
}

static void freelist_push(uint32_t order, uint64_t pfn)
{
    FreeBlock *blk = (FreeBlock *)PHYS_TO_VIRT(pfn << PAGE_SHIFT);
    blk->next = g_free_lists[order];
    g_free_lists[order] = blk;
}

static uint64_t freelist_pop(uint32_t order)
{
    FreeBlock *blk = g_free_lists[order];
    if (!blk) return 0;
    g_free_lists[order] = blk->next;
    return VIRT_TO_PHYS(blk) >> PAGE_SHIFT;
}

static bool freelist_remove(uint32_t order, uint64_t pfn)
{
    FreeBlock **pp     = &g_free_lists[order];
    FreeBlock  *target = (FreeBlock *)PHYS_TO_VIRT(pfn << PAGE_SHIFT);
    while (*pp) {
        if (*pp == target) { *pp = target->next; return true; }
        pp = &(*pp)->next;
    }
    return false;
}

static void pmm_free_region(uint64_t base_pfn, uint64_t end_pfn)
{
    while (base_pfn < end_pfn)
    {
        uint32_t order = PMM_MAX_ORDER;
        if (base_pfn != 0)
        {
            uint32_t align_order = (uint32_t)__builtin_ctzll(base_pfn);
            if (align_order < order) order = align_order;
        }

        uint64_t remaining = end_pfn - base_pfn;
        while (order > 0 && ((uint64_t)1 << order) > remaining)
            order--;

        block_mark_free(base_pfn, order);
        freelist_push(order, base_pfn);
        g_free_pages += (uint64_t)1 << order;

        base_pfn += (uint64_t)1 << order;
    }
}

void pmm_init(struct limine_memmap_response *memmap)
{
    uint64_t max_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        uint64_t end = e->base + e->length;
        if (end > max_addr) max_addr = end;
    }
    g_total_pages = ALIGN_UP(max_addr, PAGE_SIZE) >> PAGE_SHIFT;

    uint64_t bitmap_bytes = ALIGN_UP(g_total_pages, 8) / 8;
    g_bitmap = NULL;
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->length >= bitmap_bytes) {
            g_bitmap = (uint8_t *)PHYS_TO_VIRT(e->base);
            break;
        }
    }

    memset(g_bitmap, 0xFF, bitmap_bytes);

    uint64_t spare_bits = g_total_pages & 7;
    if (spare_bits)
        g_bitmap[g_total_pages >> 3] = (uint8_t)((1u << spare_bits) - 1);

    uint64_t bitmap_phys_start = VIRT_TO_PHYS(g_bitmap);
    uint64_t bitmap_phys_end   = bitmap_phys_start + bitmap_bytes;

    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t base = ALIGN_UP(e->base,               PAGE_SIZE) >> PAGE_SHIFT;
        uint64_t end  = ALIGN_DOWN(e->base + e->length, PAGE_SIZE) >> PAGE_SHIFT;
        if (base >= end) continue;

        uint64_t bmp_start = bitmap_phys_start >> PAGE_SHIFT;
        uint64_t bmp_end   = ALIGN_UP(bitmap_phys_end, PAGE_SIZE) >> PAGE_SHIFT;

        if (base < bmp_start)
            pmm_free_region(base, MIN(end, bmp_start));

        if (end > bmp_end)
            pmm_free_region(MAX(base, bmp_end), end);
    }
}

uint64_t pmm_alloc(uint32_t order)
{
    if (order > PMM_MAX_ORDER) return 0;
    lock_acquire();

    uint32_t found = PMM_ORDER_COUNT;
    for (uint32_t o = order; o < PMM_ORDER_COUNT; o++)
    {
        if (g_free_lists[o]) { found = o; break; }
    }
    if (found == PMM_ORDER_COUNT) { lock_release(); return 0; }

    uint64_t pfn = freelist_pop(found);

    while (found > order)
    {
        found--;
        uint64_t split_buddy = pfn + ((uint64_t)1 << found);
        freelist_push(found, split_buddy);
        block_mark_free(split_buddy, found);
    }

    block_mark_alloc(pfn, order);
    g_free_pages -= (uint64_t)1 << order;

    lock_release();
    return pfn << PAGE_SHIFT;
}

void pmm_free(uint64_t phys, uint32_t order)
{
    if (!phys || order > PMM_MAX_ORDER) return;
    uint64_t pfn = phys >> PAGE_SHIFT;

    lock_acquire();

    block_mark_free(pfn, order);
    g_free_pages += (uint64_t)1 << order;

    while (order < PMM_MAX_ORDER)
    {
        uint64_t bpfn = buddy_of(pfn, order);
        if (!buddy_is_free(bpfn, order)) break;
        if (!freelist_remove(order, bpfn)) break;

        pfn = MIN(pfn, bpfn);
        order++;
    }

    freelist_push(order, pfn);
    lock_release();
}

PMMStats pmm_stats(void)
{
    return (PMMStats){ .total_pages = g_total_pages, .free_pages = g_free_pages };
}