typedef struct PACKED
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
}
GdtEntry;

typedef struct PACKED
{
    uint16_t limit;
    uint64_t base;
}
GdtDescriptor;

static GdtEntry gdt[3] ALIGN(8);
static GdtDescriptor gdt_desc;

static void gdt_set_entry(int i, uint8_t access, uint8_t granularity)
{
    gdt[i].limit_low = 0;
    gdt[i].base_low = 0;
    gdt[i].base_mid = 0;
    gdt[i].access = access;
    gdt[i].granularity = granularity;
    gdt[i].base_high = 0;
}

void gdt_init(void)
{
    gdt_set_entry(0, 0x00, 0x00);
    gdt_set_entry(1, 0x9A, 0x20);
    gdt_set_entry(2, 0x92, 0x00);

    gdt_desc.limit = sizeof(gdt) - 1;
    gdt_desc.base = (uint64_t)&gdt;

    gdt_load();
}

void gdt_load(void)
{
    __asm__ volatile (
        "lgdt %0\n"
        "push %1\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov %2, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :
        : "m"(gdt_desc), "i"(GDT_CODE_SEL), "i"(GDT_DATA_SEL)
        : "rax", "memory"
    );
}