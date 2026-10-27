void  pmm_init(void);
void *pmm_alloc_pages(uint64_t count);
void *pmm_alloc_page(void);
void  pmm_free_page(void *page);
void pmm_free_pages(void *pages, uint64_t count);
uint64_t pmm_free_page_count(void);
uint64_t pmm_total_page_count(void);

void    *phys_to_virt(uint64_t phys);
uint64_t virt_to_phys(void *virt);