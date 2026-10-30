#define TASK_NAME_MAX     64
#define TASK_STACK_SIZE   (256 * 1024)

#define SCHEDULER_HZ      1000

#define WNOHANG 0x1

#define TASK_BLOCK_FOREVER 0xFFFFFFFFu

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

    uint64_t   wake_tick;
    bool       waiting;

    int        stdio[3];
    char       cwd[VFS_PATH_MAX];
    uint64_t   pid;
    int        exit_code;
    bool       detached;
    bool       kill_pending;

    uint64_t   cpu_ticks;

    void      *stack_base;
    size_t     stack_pages;

    TaskArgs  *args;

    ElfImage   image;
    bool       owns_image;

    uint8_t    fpu_state[FPU_STATE_SIZE] ALIGN(16);

    Task      *next;
    Task      *list_next;
};

void task_init(void);

Task *task_create(const char *name, TaskEntry entry, void *arg);
Task *task_create_from_image(const char *name, ElfImage *img, void *arg);
Task *task_create_idle(void);

void task_yield(void);

void task_scheduler_tick(void);

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

typedef void (*TaskVisitor)(Task *t, void *ctx);
void task_for_each(TaskVisitor visit, void *ctx);