#pragma once

#include <stddef.h>

#define HEAP_SLAB_MIN_SHIFT  3           /* smallest slab: 2^3  =   8 bytes  */
#define HEAP_SLAB_MAX_SHIFT  11          /* largest  slab: 2^11 = 2048 bytes */
#define HEAP_SLAB_MIN        (1u << HEAP_SLAB_MIN_SHIFT)
#define HEAP_SLAB_MAX        (1u << HEAP_SLAB_MAX_SHIFT)
#define HEAP_SLAB_CLASSES    (HEAP_SLAB_MAX_SHIFT - HEAP_SLAB_MIN_SHIFT + 1)

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void *kcalloc(size_t count, size_t size);