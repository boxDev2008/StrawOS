typedef int64_t ssize_t;

#define PACKED        __attribute__((packed))
#define NORETURN      __attribute__((noreturn))
#define UNUSED        __attribute__((unused))
#define ALWAYS_INLINE __attribute__((always_inline)) static inline
#define ALIGN(n)      __attribute__((aligned(n)))

#define MIN(a, b)          ((a) < (b) ? (a) : (b))
#define MAX(a, b)          ((a) > (b) ? (a) : (b))
#define ALIGN_UP(x, a)     (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)   ((x) & ~((a) - 1))
#define ARRAY_LEN(a)       (sizeof(a) / sizeof((a)[0]))
#define DIV_CEIL(a, b)     (((a) + (b) - 1) / (b))

#define PAGE_SIZE   0x1000UL

#define KERNEL_BASE 0xFFFFFFFF80000000UL

ALWAYS_INLINE uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

ALWAYS_INLINE void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

ALWAYS_INLINE void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

ALWAYS_INLINE void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

ALWAYS_INLINE uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

ALWAYS_INLINE uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

ALWAYS_INLINE void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port) : "memory");
}

ALWAYS_INLINE uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

ALWAYS_INLINE void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

ALWAYS_INLINE void sti(void)    { __asm__ volatile("sti" ::: "memory"); }
ALWAYS_INLINE void cli(void)    { __asm__ volatile("cli" ::: "memory"); }
ALWAYS_INLINE void hlt(void)    { __asm__ volatile("hlt"); }
ALWAYS_INLINE void pause(void)  { __asm__ volatile("pause"); }
ALWAYS_INLINE void panic(void)  { cli(); for(;;) hlt(); }

ALWAYS_INLINE uint64_t irq_save(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq\n pop %0\n cli" : "=r"(flags) :: "memory");
    return flags;
}

ALWAYS_INLINE void irq_restore(uint64_t flags)
{
    if (flags & (1UL << 9))
        sti();
}

ALWAYS_INLINE uint64_t read_cr0(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

ALWAYS_INLINE void write_cr0(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory");
}

ALWAYS_INLINE uint64_t read_cr4(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

ALWAYS_INLINE void write_cr4(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr4" :: "r"(v) : "memory");
}

#define CR0_EM (1UL << 2)
#define CR0_MP (1UL << 1)
#define CR0_TS (1UL << 3)

#define CR4_OSFXSR     (1UL << 9)
#define CR4_OSXMMEXCPT (1UL << 10)

ALWAYS_INLINE void fpu_init(void)
{
    uint64_t cr0 = read_cr0();
    cr0 &= ~CR0_EM;
    cr0 |= CR0_MP;
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    write_cr4(cr4);
}