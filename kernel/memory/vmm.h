#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_NOCACHE   (1ULL << 4)
#define PTE_WRITETHRU (1ULL << 3)

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void *vmm_map_mmio(uint64_t phys, uint64_t size);