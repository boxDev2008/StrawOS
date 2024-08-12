#pragma once

#include "intr/isr.h"

#include <stdint.h>
 
typedef struct task_regs
{
    uint32_t eax, ebx, ecx, edx, esi, edi, esp, ebp, eip, eflags, cr3;
}
task_regs_t;

typedef struct task
{
    task_regs_t regs;
    struct task *next;
}
task_t;
 
void tasking_initialize(void);
void task_create(task_t*, void(*)(), uint32_t, uint32_t*);

void task_yield(void);

extern void switch_task(task_regs_t *old, task_regs_t *new);