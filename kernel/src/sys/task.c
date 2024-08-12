#include "task.h"
#include "mm/liballoc.h"
#include "fs/fatfs/ff.h"

#include "elf.h"

#include <printk.h>

static task_t *running_task;
static task_t main_task;
static task_t otherTask;

static void otherMain(void)
{
    printk("Hello multitasking world!\n");
    task_yield();
}

uint32_t *buff;

void tasking_initialize(void)
{
    asm volatile("movl %%cr3, %%eax; movl %%eax, %0;":"=m"(main_task.regs.cr3)::"%eax");
    asm volatile("pushfl; movl (%%esp), %%eax; movl %%eax, %0; popfl;":"=m"(main_task.regs.eflags)::"%eax");

    FIL fil;
    UINT br;

    f_open(&fil, "test.bin", FA_READ);
    buff = kmalloc(f_size(&fil) * sizeof(uint32_t));
    f_read(&fil, buff, f_size(&fil) * sizeof(uint32_t), &br);
    f_close(&fil);

    task_create(&otherTask, (void*)buff, main_task.regs.eflags, (uint32_t*)main_task.regs.cr3);
    main_task.next = &otherTask;
    otherTask.next = &main_task;

    running_task = &main_task;
}

void task_create(task_t *task, void (*main)(), uint32_t flags, uint32_t *pagedir)
{
    task->regs.eax = 0;
    task->regs.ebx = 0;
    task->regs.ecx = 0;
    task->regs.edx = 0;
    task->regs.esi = 0;
    task->regs.edi = 0;
    task->regs.eflags = flags;
    task->regs.eip = (uint32_t) main;
    task->regs.cr3 = (uint32_t) pagedir;
    task->regs.esp = (uint32_t) kmalloc(0x1000);
    task->next = 0;
}

void task_destroy(task_t *task)
{
    kfree((void*)task->regs.esp);
}
 
void task_yield(void)
{
    task_t *last = running_task;
    running_task = running_task->next;
    switch_task(&last->regs, &running_task->regs);
}