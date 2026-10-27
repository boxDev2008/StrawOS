#define CPU_MAX 64

#define IA32_GS_BASE 0xC0000101

typedef struct Task Task;

typedef struct Cpu Cpu;

struct Cpu
{
    Cpu      *self;
    uint64_t  stack_top;
    Task     *current;
    Task     *idle;
    uint32_t  index;
    uint32_t  lapic_id;
    uint64_t  ticks;
    uint64_t  busy_ticks;
};

ALWAYS_INLINE Cpu *this_cpu(void)
{
    Cpu *c;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(c));
    return c;
}

ALWAYS_INLINE bool cpu_is_bsp(void)
{
    return this_cpu()->index == 0;
}

void cpu_init_bsp(void);
void smp_init(void);
uint32_t smp_cpu_count(void);
void smp_cpu_ticks(uint32_t index, uint64_t *total, uint64_t *busy);

NORETURN void cpu_idle_loop(void);

void bkl_lock(void);
void bkl_unlock(void);
bool bkl_held(void);
