#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include <memory.h>

#define LARGE_MAGIC  0xDEADBEEFCAFEBABEUL

typedef struct SlabHdr {
    struct SlabHdr *next_slab;
    uint32_t        chunk_size;
    uint16_t        free_count;
    uint16_t        total_count;
    void           *freelist;
} SlabHdr;

typedef struct
{
    uint64_t magic;
    uint64_t page_count;
}
LargeHdr;

typedef struct ChunkNode { struct ChunkNode *next; } ChunkNode;

static SlabHdr *g_slabs[HEAP_SLAB_CLASSES];

static uint64_t g_heap_virt_top;

static volatile int g_heap_lock;
static inline void heap_lock(void)   { while (__atomic_test_and_set(&g_heap_lock, __ATOMIC_ACQUIRE)); }
static inline void heap_unlock(void) { __atomic_clear(&g_heap_lock, __ATOMIC_RELEASE); }

static void *heap_va_alloc(uint64_t pages)
{
    uint64_t size = pages << PAGE_SHIFT;
    if (g_heap_virt_top + size > HEAP_MAX) return NULL;

    uint64_t virt = g_heap_virt_top;
    g_heap_virt_top += size;

    if (!vmm_alloc_map(vmm_kernel_aspace(), virt, pages, VMM_KERNEL_RW))
        return NULL;

    return (void *)virt;
}

static uint32_t size_to_chunk(size_t size)
{
    size += sizeof(void *);
    if (size < HEAP_SLAB_MIN) size = HEAP_SLAB_MIN;
    size--;
    size |= size >> 1; size |= size >> 2; size |= size >> 4;
    size |= size >> 8; size |= size >> 16; size |= size >> 32;
    size++;
    return (uint32_t)size;
}

static int chunk_to_class(uint32_t chunk_size)
{
    int shift = 0;
    uint32_t v = chunk_size;
    while (v > 1) { v >>= 1; shift++; }
    return shift - HEAP_SLAB_MIN_SHIFT;
}

static SlabHdr *slab_new(uint32_t chunk_size)
{
    SlabHdr *slab = (SlabHdr *)heap_va_alloc(1);
    if (!slab) return NULL;

    slab->next_slab  = NULL;
    slab->chunk_size = chunk_size;
    slab->freelist   = NULL;

    uint8_t *base = (uint8_t *)slab;
    uint64_t hdr_end = ALIGN_UP(sizeof(SlabHdr), chunk_size);
    uint64_t usable  = PAGE_SIZE - hdr_end;
    uint16_t count   = (uint16_t)(usable / chunk_size);

    slab->total_count = count;
    slab->free_count  = count;

    ChunkNode *prev = NULL;
    for (int i = count - 1; i >= 0; i--)
    {
        ChunkNode *node = (ChunkNode *)(base + hdr_end + (uint64_t)i * chunk_size);
        node->next = prev;
        prev = node;
    }
    slab->freelist = prev;
    return slab;
}

void heap_init(void)
{
    g_heap_virt_top = HEAP_BASE;
    for (int i = 0; i < HEAP_SLAB_CLASSES; i++) g_slabs[i] = NULL;
}

void *kmalloc(size_t size)
{
    if (!size) return NULL;
    heap_lock();

    if (size + sizeof(LargeHdr) > HEAP_SLAB_MAX)
    {
        uint64_t total = size + sizeof(LargeHdr);
        uint64_t pages = DIV_CEIL(total, PAGE_SIZE);
        void *base = heap_va_alloc(pages);
        heap_unlock();
        if (!base) return NULL;
        LargeHdr *hdr = (LargeHdr *)base;
        hdr->magic      = LARGE_MAGIC;
        hdr->page_count = pages;
        return (uint8_t *)base + sizeof(LargeHdr);
    }

    uint32_t chunk_size = size_to_chunk(size);
    if (chunk_size > HEAP_SLAB_MAX) chunk_size = HEAP_SLAB_MAX;
    int cls = chunk_to_class(chunk_size);
    if (cls < 0 || cls >= HEAP_SLAB_CLASSES) { heap_unlock(); return NULL; }

    SlabHdr *slab = g_slabs[cls];
    while (slab && slab->free_count == 0) slab = slab->next_slab;

    if (!slab) {
        slab = slab_new(chunk_size);
        if (!slab) { heap_unlock(); return NULL; }
        slab->next_slab = g_slabs[cls];
        g_slabs[cls] = slab;
    }

    uint8_t *chunk = (uint8_t *)slab->freelist;
    slab->freelist = ((ChunkNode *)chunk)->next;
    slab->free_count--;

    *(SlabHdr **)chunk = slab;

    heap_unlock();
    return chunk + sizeof(void *);
}

void kfree(void *ptr)
{
    if (!ptr) return;

    LargeHdr *lhdr = (LargeHdr *)((uint8_t *)ptr - sizeof(LargeHdr));
    if (lhdr->magic == LARGE_MAGIC)
    {
        heap_lock();
        uint64_t pages = lhdr->page_count;
        vmm_free_unmap(vmm_kernel_aspace(), (uint64_t)lhdr, pages);
        heap_unlock();
        return;
    }

    uint8_t *chunk = (uint8_t *)ptr - sizeof(void *);
    SlabHdr *slab  = *(SlabHdr **)chunk;

    heap_lock();
    ((ChunkNode *)chunk)->next = (ChunkNode *)slab->freelist;
    slab->freelist = chunk;
    slab->free_count++;
    heap_unlock();
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)     return kmalloc(new_size);
    if (!new_size){ kfree(ptr); return NULL; }

    size_t old_size;
    LargeHdr *lhdr = (LargeHdr *)((uint8_t *)ptr - sizeof(LargeHdr));
    if (lhdr->magic == LARGE_MAGIC)
    {
        old_size = (lhdr->page_count << PAGE_SHIFT) - sizeof(LargeHdr);
    }
    else
    {
        uint8_t *chunk = (uint8_t *)ptr - sizeof(void *);
        SlabHdr *slab  = *(SlabHdr **)chunk;
        old_size = slab->chunk_size - sizeof(void *);
    }

    void *newptr = kmalloc(new_size);
    if (!newptr) return NULL;
    memcpy(newptr, ptr, old_size < new_size ? old_size : new_size);
    kfree(ptr);
    return newptr;
}

void *kcalloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}