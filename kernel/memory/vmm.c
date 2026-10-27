#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void flush_tlb_page(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

static uint64_t *table_get_or_alloc(uint64_t *table, uint64_t index)
{
    if (!(table[index] & PTE_PRESENT))
    {
        void *new_table = pmm_alloc_page();
        if (!new_table)
        {
            kprintf("vmm: out of memory allocating page table\n");
            panic();
        }

        uint8_t *p = (uint8_t *)new_table;
        for (uint64_t i = 0; i < PAGE_SIZE; i++)
            p[i] = 0;

        uint64_t phys = virt_to_phys(new_table);
        table[index] = (phys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }

    uint64_t next_phys = table[index] & PTE_ADDR_MASK;
    return (uint64_t *)phys_to_virt(next_phys);
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)phys_to_virt(read_cr3() & PTE_ADDR_MASK);

    uint64_t *pdpt = table_get_or_alloc(pml4, pml4_idx);
    uint64_t *pd   = table_get_or_alloc(pdpt, pdpt_idx);
    uint64_t *pt   = table_get_or_alloc(pd, pd_idx);

    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    flush_tlb_page(virt);
}

void *vmm_map_mmio(uint64_t phys, uint64_t size)
{
    uint64_t phys_start = ALIGN_DOWN(phys, PAGE_SIZE);
    uint64_t phys_end   = ALIGN_UP(phys + size, PAGE_SIZE);
    uint64_t virt_start = (uint64_t)phys_to_virt(phys_start);

    for (uint64_t off = 0; off < (phys_end - phys_start); off += PAGE_SIZE)
    {
        vmm_map_page(virt_start + off, phys_start + off, PTE_WRITABLE | PTE_NOCACHE | PTE_WRITETHRU);
    }

    return (void *)((uint8_t *)virt_start + (phys - phys_start));
}