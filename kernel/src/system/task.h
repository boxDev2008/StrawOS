#pragma once

#include <stdint.h>
#include <stddef.h>

// Maximum number of tasks the kernel can track simultaneously
#define TASK_MAX         16
#define TASK_STACK_SIZE  8192   // 8 KiB kernel stack per task
#define TASK_USTACK_SIZE 8192   // 8 KiB user stack per task

typedef enum {
    TASK_UNUSED  = 0,   // slot is free
    TASK_READY   = 1,   // ready to run
    TASK_RUNNING = 2,   // currently executing
    TASK_BLOCKED = 3,   // waiting (sleeping, I/O, etc.)
    TASK_DEAD    = 4,   // terminated, not yet reaped
} TaskState;

// Saved callee-saved registers for a kernel-side cooperative switch.
// On a syscall/interrupt entry the full register set is on the kernel stack
// (as InterruptFrame); for the kernel task itself we only save callee-saved.
typedef struct {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;    // kernel RSP at point of switch
    uint64_t rip;    // unused after first switch; resume encoded in rsp
    uint64_t rflags; // saved RFLAGS — preserves IF across context switches
} TaskContext;

typedef struct Task {
    uint32_t    pid;
    TaskState   state;
    TaskContext ctx;
    char        name[32];

    uint8_t    *kstack;         // base of kernel stack (ring 0)
    uint8_t    *ustack;         // base of user stack   (ring 3), NULL for kernel tasks

    // Address space — NULL means share kernel page tables
    void       *aspace;         // AddressSpace*, NULL = kernel

    // Plan 9-style memory segment tracking
    uint64_t    brk;            // current program break (end of data segment)
    uint64_t    brk_base;       // base address of data segment (set at load time)
    uint64_t    mmap_next;      // next hint address for anonymous mmap regions

    struct Task *next;          // intrusive circular list
} Task;

// ── API ───────────────────────────────────────────────────────────────────────

void  task_init(void);

/**
 * Create a kernel-mode task (runs in ring 0).
 * Entry is a plain C function pointer.
 */
Task *task_create(const char *name, void (*entry)(void));

/**
 * Create a userspace task (ring 3).
 * entry_va  : virtual address of _start in the user address space.
 * aspace    : the AddressSpace the task lives in.
 */
Task *task_create_user(const char *name, uint64_t entry_va, void *aspace);

void  task_destroy(Task *t);
Task *task_current(void);
Task *task_find(uint32_t pid);
void  task_yield(void);
int   task_jump(uint32_t pid);
void  task_list(void);

Task *task_exec(const char *path, const char *name);