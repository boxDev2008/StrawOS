/*
 * vmm.c — Virtual Memory Manager (x86-64 4-level paging)
 *
 * 4-level paging layout for a 64-bit virtual address:
 *   [63:48] sign-extension (canonical form)
 *   [47:39] PML4 index   (9 bits, 512 entries)
 *   [38:30] PDPT  index  (9 bits, 512 entries)
 *   [29:21] PD    index  (9 bits, 512 entries)
 *   [20:12] PT    index  (9 bits, 512 entries)
 *   [11:0]  page offset  (12 bits)
 *
 * Each page-table page is 4 KiB and holds 512 × 8-byte entries.
 * All page-table pages are allocated from the PMM and accessed via HHDM.
 */

#include "vmm.h"
#include "pmm.h"
#include <string.h>

/* ── page-table index extraction ────────────────────────────────────────── */

#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v) (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

#define ENTRIES_PER_TABLE 512

/* ── kernel address space ───────────────────────────────────────────────── */

static AddressSpace g_kernel_aspace;

AddressSpace *vmm_kernel_aspace(void) { return &g_kernel_aspace; }

/* ── helper: get/create a child table ──────────────────────────────────── */

/*
 * Return a pointer to the 512-entry child table referenced by `entry`.
 * If `entry` is not present and `alloc` is true, allocate a new page for it.
 */
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

    /* Store with present + writable + user (user bit is refined at PT level). */
    *entry = phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return table;
}

/* ── walk to PT entry ───────────────────────────────────────────────────── */

/*
 * Walk (or build) page tables down to the PT level for `virt` in `pml4`.
 * Returns pointer to the PT entry, or NULL on error.
 * `alloc` controls whether intermediate tables are created on the fly.
 */
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

/* Walk down to the PD entry (for 2 MiB huge pages). */
static uint64_t *walk_to_pde(uint64_t *pml4, uint64_t virt, bool alloc)
{
    uint64_t *pdpt = get_or_alloc_table(&pml4[PML4_IDX(virt)], alloc);
    if (!pdpt) return NULL;
    uint64_t *pd   = get_or_alloc_table(&pdpt[PDPT_IDX(virt)], alloc);
    if (!pd)   return NULL;
    return &pd[PD_IDX(virt)];
}

/* ── public API ─────────────────────────────────────────────────────────── */

void vmm_init(void)
{
    /*
     * Strategy: allocate a fresh PML4, then copy the entire upper half
     * (PML4 entries 256-511) from Limine's current page tables.
     *
     * The upper half contains:
     *   - The HHDM window  (Limine maps all physical RAM here)
     *   - The kernel text/data/bss/stack (mapped by Limine)
     *   - Any other kernel-mode mappings Limine set up
     *
     * We share those PDPT pages (shallow copy of the PML4 upper half).
     * The lower half (entries 0-255, user space) starts zeroed.
     *
     * This means write_cr3 is safe: every address the CPU currently uses
     * is still reachable because we kept all the upper-half mappings.
     *
     * Later, vmm_create_aspace() copies the kernel upper half into every
     * new user address space, keeping the kernel always accessible.
     */

    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys)
        for (;;) __asm__ volatile("hlt");  /* fatal: no memory */

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    /* Read Limine's current CR3 to find its PML4. */
    uint64_t limine_cr3   = read_cr3() & ~(uint64_t)0xFFF;
    uint64_t *limine_pml4 = (uint64_t *)PHYS_TO_VIRT(limine_cr3);

    /* Zero the user half; inherit the kernel half verbatim. */
    memset(pml4,       0,                  PAGE_SIZE / 2);  /* entries   0-255 */
    memcpy(pml4 + 256, limine_pml4 + 256,  PAGE_SIZE / 2); /* entries 256-511 */

    g_kernel_aspace.pml4_phys = pml4_phys;

    /* Activate our page tables - safe because the upper half is intact. */
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

    /* Zero user half; copy kernel half (upper 256 PML4 entries). */
    memset(pml4, 0, PAGE_SIZE / 2);
    memcpy(pml4 + 256, kpml4 + 256, PAGE_SIZE / 2);

    as->pml4_phys = pml4_phys;
    return as;
}

static void free_pt(uint64_t *pt)
{
    /* Free only pages the OS owns. PTE_SHARED marks device/borrowed pages. */
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pt[i] & PTE_PRESENT)) continue;
        if (  pt[i] & PTE_SHARED)   continue;  /* not ours — don't free */
        pmm_free_page(pt[i] & PTE_ADDR_MASK);
    }
    pmm_free_page(VIRT_TO_PHYS(pt));
}

static void free_pd(uint64_t *pd)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pd[i] & PTE_PRESENT)) continue;
        if (pd[i] & PTE_HUGEPAGE)   continue;  /* 2 MiB page, no child table */
        free_pt((uint64_t *)PHYS_TO_VIRT(pd[i] & PTE_ADDR_MASK));
    }
    pmm_free_page(VIRT_TO_PHYS(pd));
}

static void free_pdpt(uint64_t *pdpt)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(pdpt[i] & PTE_PRESENT)) continue;
        if (pdpt[i] & PTE_HUGEPAGE)   continue;  /* 1 GiB page */
        free_pd((uint64_t *)PHYS_TO_VIRT(pdpt[i] & PTE_ADDR_MASK));
    }
    pmm_free_page(VIRT_TO_PHYS(pdpt));
}

void vmm_destroy_aspace(AddressSpace *as)
{
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(as->pml4_phys);
    /* Only tear down the user half (first 256 entries). */
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
    /* 2 MiB pages: virt and phys must be 2 MiB aligned. */
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
            /* Rollback already-mapped pages. */
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
        if (!(*pte & PTE_SHARED))           /* only free pages we own */
            pmm_free_page(*pte & PTE_ADDR_MASK);
        *pte = 0;
        vmm_invlpg(v);
    }
}

void vmm_unmap_shared(AddressSpace *as, uint64_t virt, uint64_t page_count)
{
    /* Clears PTEs without freeing the underlying physical pages. */
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