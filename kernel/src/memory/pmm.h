#pragma once

#include "common.h"
#include "limine.h"

#define PMM_MAX_ORDER   18
#define PMM_ORDER_COUNT (PMM_MAX_ORDER + 1)

void pmm_init(struct limine_memmap_response *memmap);

uint64_t pmm_alloc(uint32_t order);

void pmm_free(uint64_t phys, uint32_t order);

static inline uint64_t pmm_alloc_page(void) { return pmm_alloc(0); }
static inline void pmm_free_page(uint64_t phys) { pmm_free(phys, 0);  }

typedef struct { uint64_t total_pages; uint64_t free_pages; } PMMStats;
PMMStats pmm_stats(void);