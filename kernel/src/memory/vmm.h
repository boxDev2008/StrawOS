#pragma once

#include "common.h"

#define PTE_PRESENT    (1UL << 0)
#define PTE_WRITABLE   (1UL << 1)
#define PTE_USER       (1UL << 2)
#define PTE_WRITETHROUGH (1UL << 3)
#define PTE_WRITECOMBINE PTE_WRITETHROUGH
#define PTE_NOCACHE    (1UL << 4)
#define PTE_ACCESSED   (1UL << 5)
#define PTE_DIRTY      (1UL << 6)
#define PTE_HUGEPAGE   (1UL << 7)
#define PTE_GLOBAL     (1UL << 8)
#define PTE_SHARED     (1UL << 9)
#define PTE_NX         (1UL << 63)

#define VMM_KERNEL_RW  (PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL | PTE_NX)
#define VMM_KERNEL_RO  (PTE_PRESENT | PTE_GLOBAL   | PTE_NX)
#define VMM_KERNEL_EX  (PTE_PRESENT | PTE_GLOBAL)
#define VMM_USER_RW    (PTE_PRESENT | PTE_WRITABLE | PTE_USER  | PTE_NX)
#define VMM_USER_RO    (PTE_PRESENT | PTE_USER     | PTE_NX)
#define VMM_USER_EX    (PTE_PRESENT | PTE_USER)

#define PTE_ADDR_MASK  0x000FFFFFFFFFF000UL

typedef struct
{
    uint64_t  pml4_phys;
}
AddressSpace;

void vmm_init(void);

AddressSpace *vmm_create_aspace(void);
void vmm_destroy_aspace(AddressSpace *as);

void vmm_switch_aspace(AddressSpace *as);

AddressSpace *vmm_kernel_aspace(void);

bool vmm_map(AddressSpace *as, uint64_t virt, uint64_t phys, uint64_t page_count, uint64_t flags);

void vmm_unmap(AddressSpace *as, uint64_t virt, uint64_t page_count);

bool vmm_map_huge(AddressSpace *as, uint64_t virt, uint64_t phys, uint64_t page_count, uint64_t flags);

uint64_t vmm_virt_to_phys(AddressSpace *as, uint64_t virt);

uint64_t vmm_alloc_map(AddressSpace *as, uint64_t virt, uint64_t page_count, uint64_t flags);

void vmm_free_unmap(AddressSpace *as, uint64_t virt, uint64_t page_count);

void vmm_unmap_shared(AddressSpace *as, uint64_t virt, uint64_t page_count);

static inline void vmm_invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}