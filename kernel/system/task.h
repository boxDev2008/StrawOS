#define TASK_NAME_MAX     64
#define TASK_STACK_SIZE   (256 * 1024)

#define SCHEDULER_HZ      1000

#define WNOHANG 0x1

#define TASK_BLOCK_FOREVER 0xFFFFFFFFu

#define TASK_NICE_MIN (-20)
#define TASK_NICE_MAX 19

#define IPI_RESCHEDULE 0x40

typedef enum
{
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DEAD
}
TaskState;

typedef void (*TaskEntry)(void *arg);

typedef struct Task Task;

#define FPU_STATE_SIZE 512

typedef struct
{
    int    argc;
    char **argv;
    char  *backing;
}
TaskArgs;

struct Task
{
    char       name[TASK_NAME_MAX];
    TaskState  state;

    uint64_t   saved_rsp;

    // Fair scheduling. vruntime is TSC cycles of CPU time scaled by
    // NICE_0_WEIGHT / weight; the ready task with the smallest one runs next.
    int        nice;
    uint32_t   weight;
    uint64_t   vruntime;
    uint64_t   sum_exec;
    uint64_t   exec_start;
    uint64_t   slice_start;
    bool       runnable;
    bool       on_rq;
    RbNode     rq_node;

    uint64_t   wake_tick;
    bool       on_timer;
    RbNode     timer_node;

    bool       waiting;
    Task      *wait_prev;
    Task      *wait_next;

    int        stdio[3];
    char       cwd[VFS_PATH_MAX];
    uint64_t   pid;
    int        exit_code;
    bool       detached;
    bool       kill_pending;
    bool       in_kcall;

    uint64_t   cpu_ticks;

    void      *stack_base;
    size_t     stack_pages;

    TaskArgs  *args;

    ElfImage   image;
    bool       owns_image;

    uint8_t    fpu_state[FPU_STATE_SIZE] ALIGN(16);

    Task      *list_next;
};

void task_init(void);

Task *task_create(const char *name, TaskEntry entry, void *arg);
Task *task_create_from_image(const char *name, ElfImage *img, void *arg);
Task *task_create_idle(void);

void task_yield(void);

void task_scheduler_tick(void);
void task_sched_start(uint64_t tsc_per_ms);
void task_irq_exit(void);
void task_resched(void);

void task_sleep_ms(uint32_t ms);

void task_block(uint32_t timeout_ms);
void task_wake_waiters(void);

uint64_t task_uptime_ms(void);

NORETURN void task_exit(int code);

int task_kill(Task *t);
int task_wait(Task *t, uint32_t flags);

Task *task_current(void);
void  task_check_kill(void);

Task *task_exec_elf(const char *path, char *const argv[]);

void fs_release_task(Task *t);
void user_pages_release_task(Task *t);
uint64_t user_pages_count(Task *t);
void task_forget_fd(int fd);

uint64_t sys_spawn(const char *path, char *const argv[]);
int64_t  sys_chdir(const char *path);
int64_t  sys_getcwd(char *buf, size_t size);
int64_t  sys_kill(uint64_t pid);
int64_t  sys_task_status(uint64_t pid, int *code);
uint64_t sys_getpid(void);
int64_t  sys_setpriority(uint64_t pid, int64_t nice);
int64_t  sys_getpriority(uint64_t pid, int *nice);

typedef void (*TaskVisitor)(Task *t, void *ctx);
void task_for_each(TaskVisitor visit, void *ctx);