#pragma once

#include <stdint.h>
#include <stddef.h>

#define TASK_MAX         64
#define TASK_STACK_SIZE  8192
#define TASK_USTACK_SIZE 65536

typedef struct {
    uint8_t data[512];
} __attribute__((aligned(16))) FxsaveBuffer;

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY  = 1,
    TASK_DEAD   = 2
} TaskState;

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

    uint8_t    *kstack;
    uint8_t    *ustack;

    void       *aspace; 

    FxsaveBuffer fpu_state  __attribute__((aligned(16)));
    int          fpu_used;

    uint64_t    brk;
    uint64_t    brk_base;
    uint64_t    mmap_next;

    char        cwd[512];

    struct Task *next;
} __attribute__((aligned(16))) Task;

void  task_init(void);
Task *task_create(const char *name, void (*entry)(void));

Task *task_create_user(const char *name, uint64_t entry_va, void *aspace, const char **argv);

Task *task_current(void);
Task *task_find(uint32_t pid);

void  task_yield(void);

void  task_exit(void);

void  task_reap_dead(void);

void  task_list(void);

Task *task_exec(const char *path, const char **argv);

int   task_kill(uint32_t pid);