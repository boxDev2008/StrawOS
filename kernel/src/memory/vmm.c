#include "vmm.h"
#include "pmm.h"
#include <string.h>

#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v) (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

#define ENTRIES_PER_TABLE 512

static AddressSpace g_kernel_aspace;

AddressSpace *vmm_kernel_aspace(void) { return &g_kernel_aspace; }

static uint64_t *get_or_alloc_table(uint64_t *entry, bool alloc)
{
    if (*entry & PTE_PRESENT) {
        return (uint64_t *)PHYS_TO_VIRT(*entry & PTE_ADDR_MASK);
    }
    if (!alloc) return NULL;

    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;

    uint64_t *table = (uint64_t *)PHYS_TO_VIRT(phys);
    memset(table, 0, PAGE_SIZE);

    *entry = phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return table;
}

static uint64_t *walk_to_pte(uint64_t *pml4, uint64_t virt, bool alloc)
{
    uint64_t *pdpt = get_or_alloc_table(&pml4[PML4_IDX(virt)], alloc);
    if (!pdpt) return NULL;
    uint64_t *pd   = get_or_alloc_table(&pdpt[PDPT_IDX(virt)], alloc);
    if (!pd)   return NULL;
    uint64_t *pt   = get_or_alloc_table(&pd[PD_IDX(virt)],     alloc);
    if (!pt)   return NULL;
    return &pt[PT_IDX(virt)];
}

static uint64_t *walk_to_pde(uint64_t *pml4, uint64_t virt, bool alloc)
{
    uint64_t *pdpt = get_or_alloc_table(&pml4[PML4_IDX(virt)], alloc);
    if (!pdpt) return NULL;
    uint64_t *pd   = get_or_alloc_table(&pdpt[PDPT_IDX(virt)], alloc);
    if (!pd)   return NULL;
    return &pd[PD_IDX(virt)];
}

void vmm_init(void)
{
    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys)
        for (;;) __asm__ volatile("hlt");  /* fatal: no memory */

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    uint64_t limine_cr3   = read_cr3() & ~(uint64_t)0xFFF;
    uint64_t *limine_pml4 = (uint64_t *)PHYS_TO_VIRT(limine_cr3);

    memset(pml4,       0,                  PAGE_SIZE / 2);  /* entries   0-255 */
    memcpy(pml4 + 256, limine_pml4 + 256,  PAGE_SIZE / 2); /* entries 256-511 */

    g_kernel_aspace.pml4_phys = pml4_phys;

    write_cr3(pml4_phys);
}

AddressSpace *vmm_create_aspace(void)
{
    AddressSpace *as = (AddressSpace *)PHYS_TO_VIRT(pmm_alloc_page());
    if (!as) return NULL;

    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) { pmm_free_page(VIRT_TO_PHYS(as)); return NULL; }

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);
    uint64_t *kpml4 = (uint64_t *)PHYS_TO_VIRT(g_kernel_aspace.pml4_phys);

    memset(pml4, 0, PAGE_SIZE / 2);
    memcpy(pml4 + 256, kpml4 + 256, PAGE_SIZE / 2);

    as->pml4_phys = pml4_phys;
    return as;
}

static void free_pt(uint64_t *pt)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pt[i] & PTE_PRESENT)) continue;
        if (  pt[i] & PTE_SHARED)   continue;
        pmm_free_page(pt[i] & PTE_ADDR_MASK);
    }
    pmm_free_page(VIRT_TO_PHYS(pt));
}

static void free_pd(uint64_t *pd)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pd[i] & PTE_PRESENT)) continue;
        if (pd[i] & PTE_HUGEPAGE)   continue;
        free_pt((uint64_t *)PHYS_TO_VIRT(pd[i] & PTE_ADDR_MASK));
    }
    pmm_free_page(VIRT_TO_PHYS(pd));
}

static void free_pdpt(uint64_t *pdpt)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pdpt[i] & PTE_PRESENT)) continue;
        if (pdpt[i] & PTE_HUGEPAGE)   continue;
        free_pd((uint64_t *)PHYS_TO_VIRT(pdpt[i] & PTE_ADDR_MASK));
    }
    pmm_free_page(VIRT_TO_PHYS(pdpt));
}

void vmm_destroy_aspace(AddressSpace *as)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PTE_PRESENT)) continue;
        free_pdpt((uint64_t *)PHYS_TO_VIRT(pml4[i] & PTE_ADDR_MASK));
    }
    pmm_free_page(as->pml4_phys);
    pmm_free_page(VIRT_TO_PHYS(as));
}

void vmm_switch_aspace(AddressSpace *as)
{
    write_cr3(as->pml4_phys);
}

bool vmm_map(AddressSpace *as, uint64_t virt, uint64_t phys,
             uint64_t page_count, uint64_t flags)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t p = phys + i * PAGE_SIZE;
        uint64_t *pte = walk_to_pte(pml4, v, true);
        if (!pte) return false;
        *pte = (p & PTE_ADDR_MASK) | flags;
        vmm_invlpg(v);
    }
    return true;
}

void vmm_unmap(AddressSpace *as, uint64_t virt, uint64_t page_count)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pte = walk_to_pte(pml4, v, false);
        if (pte && (*pte & PTE_PRESENT)) {
            *pte = 0;
            vmm_invlpg(v);
        }
    }
}

bool vmm_map_huge(AddressSpace *as, uint64_t virt, uint64_t phys,
                  uint64_t page_count, uint64_t flags)
{
    const uint64_t HUGE_SIZE = 1UL << 21;
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t v = virt + i * HUGE_SIZE;
        uint64_t p = phys + i * HUGE_SIZE;
        uint64_t *pde = walk_to_pde(pml4, v, true);
        if (!pde) return false;
        *pde = (p & PTE_ADDR_MASK) | flags | PTE_HUGEPAGE;
        vmm_invlpg(v);
    }
    return true;
}

uint64_t vmm_virt_to_phys(AddressSpace *as, uint64_t virt)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    uint64_t *pte  = walk_to_pte(pml4, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    return (*pte & PTE_ADDR_MASK) | (virt & 0xFFF);
}

uint64_t vmm_alloc_map(AddressSpace *as, uint64_t virt,
                       uint64_t page_count, uint64_t flags)
{
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            vmm_free_unmap(as, virt, i);
            return 0;
        }
        uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pte = walk_to_pte(pml4, v, true);
        if (!pte) {
            pmm_free_page(phys);
            vmm_free_unmap(as, virt, i);
            return 0;
        }
        *pte = (phys & PTE_ADDR_MASK) | flags;
        vmm_invlpg(v);
    }
    return virt;
}

void vmm_free_unmap(AddressSpace *as, uint64_t virt, uint64_t page_count)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pte = walk_to_pte(pml4, v, false);
        if (!pte || !(*pte & PTE_PRESENT)) continue;
        if (!(*pte & PTE_SHARED))
            pmm_free_page(*pte & PTE_ADDR_MASK);
        *pte = 0;
        vmm_invlpg(v);
    }
}

void vmm_unmap_shared(AddressSpace *as, uint64_t virt, uint64_t page_count)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pte = walk_to_pte(pml4, v, false);
        if (pte && (*pte & PTE_PRESENT)) {
            *pte = 0;
            vmm_invlpg(v);
        }
    }
}