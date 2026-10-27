typedef struct HeapBlockHeader HeapBlockHeader;

struct HeapBlockHeader
{
    size_t size;
    size_t reserved;
};

static inline size_t heap_get_block_count(size_t size)
{
    if (size > SIZE_MAX - sizeof(HeapBlockHeader))
        return 0;

    size += sizeof(HeapBlockHeader);
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

void *kmalloc(size_t size)
{
    HeapBlockHeader *ptr = (HeapBlockHeader*)pmm_alloc_pages(heap_get_block_count(size));
    if (!ptr)
        return NULL;

    ptr->size = size;
    return (void*)(ptr + 1);
}

void *kcalloc(size_t nmemb, size_t size)
{
    if (size != 0 && nmemb > SIZE_MAX / size)
        return NULL;

    size_t final_size = nmemb * size;

    void *ptr = kmalloc(final_size);
    if (!ptr)
        return NULL;

    memset(ptr, 0, final_size);
    return ptr;
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return kmalloc(new_size);

    if (new_size == 0)
    {
        kfree(ptr);
        return NULL;
    }

    HeapBlockHeader *header = (HeapBlockHeader *)ptr - 1;

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr)
        return NULL;

    size_t copy_size = header->size < new_size ? header->size : new_size;

    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);

    return new_ptr;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    HeapBlockHeader *header = (HeapBlockHeader*)ptr - 1;
    pmm_free_pages(header, heap_get_block_count(header->size));
}