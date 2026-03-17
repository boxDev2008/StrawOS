#pragma once

#include <stdint.h>
#include <stddef.h>

#define TASK_MAX         64
#define TASK_STACK_SIZE  8192    // 8 KiB kernel stack per task
#define TASK_USTACK_SIZE 65536   // 64 KiB user stack per task

// 512-byte, 16-byte aligned buffer for FXSAVE/FXRSTOR
typedef struct {
    uint8_t data[512];
} __attribute__((aligned(16))) FxsaveBuffer;

// Simplified task states
typedef enum {
    TASK_UNUSED = 0,    // slot is free
    TASK_READY  = 1,    // ready to run (or currently running)
    TASK_DEAD   = 2
} TaskState;

// Callee-saved register context for cooperative switch
typedef struct {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rflags;
} TaskContext;

typedef struct Task {
    uint32_t    pid;
    TaskState   state;
    TaskContext ctx;
    char        name[32];

    uint8_t    *kstack;     // base of kernel stack (ring 0)
    uint8_t    *ustack;     // base of user stack (ring 3), NULL for kernel tasks

    void       *aspace;     // AddressSpace*, NULL = kernel page tables

    FxsaveBuffer fpu_state  __attribute__((aligned(16)));
    int          fpu_used;

    // Memory segment tracking
    uint64_t    brk;
    uint64_t    brk_base;
    uint64_t    mmap_next;

    // Working directory (absolute path, always starts with '/')
    char        cwd[512];

    struct Task *next;      // intrusive circular linked list
} __attribute__((aligned(16))) Task;

// ── API ───────────────────────────────────────────────────────────────────────

void  task_init(void);
Task *task_create(const char *name, void (*entry)(void));

// argv is a NULL-terminated array of argument strings, or NULL for no args.
// The kernel writes argc/argv onto the top of the user stack before entry.
Task *task_create_user(const char *name, uint64_t entry_va, void *aspace,
                       const char **argv);

Task *task_current(void);
Task *task_find(uint32_t pid);

// Yield to the next READY task in the ring. Returns when scheduled back.
void  task_yield(void);

// Mark the current task dead and switch away — never returns.
void  task_exit(void);

// Free all DEAD tasks. Call from the kernel task after task_yield() returns.
void  task_reap_dead(void);

void  task_list(void);

// Load an ELF from path, derive name from filename, pass argv to the new task.
// argv may be NULL. Returns the new Task* on success, NULL on failure.
// t->pid is safe to return to userspace.
Task *task_exec(const char *path, const char **argv);

int   task_kill(uint32_t pid);