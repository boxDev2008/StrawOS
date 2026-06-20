#pragma once

#include <stddef.h>

#define HEAP_SLAB_MIN_SHIFT  3
#define HEAP_SLAB_MAX_SHIFT  11
#define HEAP_SLAB_MIN        (1u << HEAP_SLAB_MIN_SHIFT)
#define HEAP_SLAB_MAX        (1u << HEAP_SLAB_MAX_SHIFT)
#define HEAP_SLAB_CLASSES    (HEAP_SLAB_MAX_SHIFT - HEAP_SLAB_MIN_SHIFT + 1)

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void *kcalloc(size_t count, size_t size);