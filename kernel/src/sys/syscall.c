#include "intr/isr.h"

#include "task.h"
#include "terminal.h"
#include "mm/liballoc.h"

static void syscall_handler(regs_t *r);

static uintptr_t syscalls[] = {
	(uintptr_t)&task_yield,  /* 0 */
	(uintptr_t)&terminal_write,
	(uintptr_t)&kmalloc,
	(uintptr_t)&kcalloc,
	(uintptr_t)&krealloc,
	(uintptr_t)&kfree,
	0
};
uint32_t num_syscalls;

void syscalls_initialize(void)
{
	for (num_syscalls = 0; syscalls[num_syscalls] != 0; ++num_syscalls);
	isr_register_interrupt_handler(0x80, syscall_handler);
}

void syscall_handler(regs_t *r)
{
	if (r->eax >= num_syscalls)
		return;

	uintptr_t location = syscalls[r->eax];

	if (location == 1)
		return;

	uint32_t ret;
	asm volatile (
			"push %1\n"
			"push %2\n"
			"push %3\n"
			"push %4\n"
			"push %5\n"
			"call *%6\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			"pop %%ebx\n"
			: "=a" (ret) : "r" (r->edi), "r" (r->esi), "r" (r->edx), "r" (r->ecx), "r" (r->ebx), "r" (location));

	r->eax = ret;
}