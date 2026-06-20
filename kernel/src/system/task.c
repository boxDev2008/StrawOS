#include "task.h"
#include "elf.h"
#include "filesystem/vfs.h"
#include "memory/heap.h"
#include "memory/vmm.h"
#include "memory/pmm.h"
#include "arch/x86_64/gdt.h"
#include "common.h"
#include "libk/kprintf.h"
#include <string.h>
#include <stddef.h>

static Task s_tasks[TASK_MAX];
static Task *s_current = NULL;

extern void task_switch_asm(TaskContext *from, TaskContext *to);

static Task *slot_alloc(void)
{
    for (int i = 0; i < TASK_MAX; i++) {
        if (s_tasks[i].state == TASK_UNUSED)
            return &s_tasks[i];
    }
    return NULL;
}

static uint32_t alloc_pid(void)
{
    for (uint32_t candidate = 0; ; candidate++) {
        int taken = 0;
        for (int i = 0; i < TASK_MAX; i++) {
            if (s_tasks[i].state != TASK_UNUSED && s_tasks[i].pid == candidate) {
                taken = 1;
                break;
            }
        }
        if (!taken) return candidate;
    }
}

static const char *path_basename(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') last = p + 1;
    return last;
}

static void task_common_init(Task *t, const char *name)
{
    t->pid   = alloc_pid();
    t->state = TASK_READY;
    t->aspace = NULL;
    t->ustack = NULL;
    t->fpu_used = 0;
    t->brk = 0; t->brk_base = 0; t->mmap_next = 0;
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';

    t->ctx.rbx = 0;
    t->ctx.rbp = 0;
    t->ctx.r12 = 0;
    t->ctx.r13 = 0;
    t->ctx.r14 = 0;
    t->ctx.r15 = 0;
    t->ctx.rflags = 0x202;

    t->cwd[0] = '/';
    t->cwd[1] = '\0';

    Task *prev = s_current;
    while (prev->next != s_current)
        prev = prev->next;
    prev->next = t;
    t->next    = s_current;
}

void task_init(void)
{
    for (int i = 0; i < TASK_MAX; i++)
        s_tasks[i].state = TASK_UNUSED;

    Task *kernel_task  = &s_tasks[0];
    kernel_task->pid   = alloc_pid();
    kernel_task->state = TASK_READY;
    kernel_task->kstack = NULL;
    kernel_task->ustack = NULL;
    kernel_task->aspace = NULL;
    kernel_task->fpu_used = 0;
    kernel_task->next  = kernel_task;
    strncpy(kernel_task->name, "kernel", sizeof(kernel_task->name) - 1);

    s_current = kernel_task;
}

Task *task_create(const char *name, void (*entry)(void))
{
    Task *t = slot_alloc();
    if (!t) return NULL;

    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) return NULL;

    t->kstack = kstack;
    task_common_init(t, name);

    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);
    *--sp = (uint64_t)entry;
    t->ctx.rsp = (uint64_t)sp;

    return t;
}

extern void task_userspace_trampoline(void);

#define USER_STACK_VA_TOP   0x0000700000000000UL
#define USER_STACK_VA_PAGES (TASK_USTACK_SIZE / PAGE_SIZE)

#define ARGV_MAX 64

Task *task_create_user(const char *name, uint64_t entry_va, void *aspace,
                       const char **argv)
{
    Task *t = slot_alloc();
    if (!t) return NULL;

    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) return NULL;

    AddressSpace *as = (AddressSpace *)aspace;
    uint64_t ustack_va_base = USER_STACK_VA_TOP - USER_STACK_VA_PAGES * PAGE_SIZE;

    for (uint64_t i = 0; i < USER_STACK_VA_PAGES; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) { kfree(kstack); return NULL; }
        uint64_t va = ustack_va_base + i * PAGE_SIZE;
        vmm_map(as, va, phys, 1, VMM_USER_RW);
    }

    t->kstack = kstack;
    t->ustack = (uint8_t *)ustack_va_base;
    task_common_init(t, name);
    t->aspace   = aspace;
    t->fpu_used = 1;

    *((uint32_t *)(t->fpu_state.data + 24)) = 0x1F80;

    int argc = 0;
    if (argv) {
        while (argv[argc] && argc < ARGV_MAX)
            argc++;
    }

    uint64_t top_page_va = USER_STACK_VA_TOP - PAGE_SIZE;
    uint64_t top_page_phys = vmm_virt_to_phys(as, top_page_va);
    uint8_t *kva_page = (uint8_t *)PHYS_TO_VIRT(top_page_phys);

    uint8_t *str_cursor = kva_page + PAGE_SIZE;
    uint64_t user_str_ptrs[ARGV_MAX];

    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(argv[i]) + 1;
        str_cursor -= len;
        memcpy(str_cursor, argv[i], len);
        uint64_t offset = (uint64_t)(kva_page + PAGE_SIZE - str_cursor);
        user_str_ptrs[i] = USER_STACK_VA_TOP - offset;
    }

    str_cursor = (uint8_t *)((uintptr_t)str_cursor & ~(uintptr_t)7);

    uint8_t *table_base = str_cursor - (uint64_t)(argc + 2) * sizeof(uint64_t);
    table_base = (uint8_t *)((uintptr_t)table_base & ~(uintptr_t)0xF);

    uint64_t *slot = (uint64_t *)table_base;
    slot[0] = (uint64_t)argc;
    for (int i = 0; i < argc; i++)
        slot[i + 1] = user_str_ptrs[i];
    slot[argc + 1] = 0;

    uint64_t user_rsp = USER_STACK_VA_TOP - (uint64_t)(kva_page + PAGE_SIZE - table_base);

    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);
    *--sp = user_rsp;
    *--sp = entry_va;
    *--sp = (uint64_t)task_userspace_trampoline;

    t->ctx.rsp = (uint64_t)sp;

    return t;
}

Task *task_current(void)
{
    return s_current;
}

Task *task_find(uint32_t pid)
{
    for (int i = 0; i < TASK_MAX; i++)
    {
        if (s_tasks[i].state != TASK_UNUSED && s_tasks[i].pid == pid)
            return &s_tasks[i];
    }
    return NULL;
}

static void do_switch(Task *from, Task *to)
{
    s_current = to;

    if (from->fpu_used)
        __asm__ volatile("fxsave %0" : "=m"(from->fpu_state));

    gdt_set_kernel_stack((uint64_t)(to->kstack + TASK_STACK_SIZE));

    if (to->aspace && to->aspace != from->aspace)
        vmm_switch_aspace((AddressSpace *)to->aspace);
    else if (!to->aspace)
        vmm_switch_aspace(vmm_kernel_aspace());

    if (to->fpu_used)
        __asm__ volatile("fxrstor %0" :: "m"(to->fpu_state));
    else
        __asm__ volatile("fninit");

    uint64_t saved_to_rflags = to->ctx.rflags;
    to->ctx.rflags &= ~(uint64_t)0x200;

    task_switch_asm(&from->ctx, &to->ctx);

    to->ctx.rflags = saved_to_rflags;
}

static Task *find_next_ready(Task *start)
{
    Task *t = start->next;
    int laps = 0;
    while (laps < TASK_MAX) {
        if (t == start) return NULL;
        if (t->state == TASK_READY)  return t;
        t = t->next;
        laps++;
    }
    return NULL;
}

void task_yield(void)
{
    Task *next = find_next_ready(s_current);
    if (!next) return;

    Task *from = s_current;
    do_switch(from, next);
}

void task_exit(void)
{
    Task *dying = s_current;
    dying->state = TASK_DEAD;

    Task *next = find_next_ready(dying);
    if (!next)
    {
        for (;;) __asm__ volatile("hlt");
    }

    s_current = next;

    gdt_set_kernel_stack((uint64_t)(next->kstack + TASK_STACK_SIZE));

    if (next->aspace && next->aspace != dying->aspace)
        vmm_switch_aspace((AddressSpace *)next->aspace);
    else if (!next->aspace)
        vmm_switch_aspace(vmm_kernel_aspace());

    if (next->fpu_used)
        __asm__ volatile("fxrstor %0" :: "m"(next->fpu_state));
    else
        __asm__ volatile("fninit");

    task_switch_asm(&dying->ctx, &next->ctx);

    for (;;) __asm__ volatile("hlt");
}

void task_reap_dead(void)
{
    for (int i = 0; i < TASK_MAX; i++)
    {
        Task *t = &s_tasks[i];
        if (t->state != TASK_DEAD || t == s_current)
            continue;

        Task *prev = s_current;
        int limit = TASK_MAX;
        while (prev->next != t && prev->next != s_current && limit-- > 0)
            prev = prev->next;
        if (prev->next == t)
            prev->next = t->next;

        if (t->kstack)
        {
            kfree(t->kstack);
            t->kstack = NULL;
        }

        if (t->aspace)
        {
            vmm_destroy_aspace((AddressSpace *)t->aspace);
            t->aspace = NULL;
        }

        t->state = TASK_UNUSED;
    }
}

void task_list(void)
{
    kprintf("PID  STATE  NAME\r\n");
    kprintf("---- ------ ----------------\r\n");
    for (int i = 0; i < TASK_MAX; i++) {
        Task *t = &s_tasks[i];
        if (t->state == TASK_UNUSED) continue;
        kprintf("%u ready %s\r\n", t->pid, t->name);
    }
}

Task *task_exec(const char *path, const char **argv)
{
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    VStat st;
    if (vfs_fstat(fd, &st) < 0 || st.st_size == 0)
    {
        vfs_close(fd);
        return NULL;
    }

    uint64_t size = st.st_size;

    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf)
    {
        vfs_close(fd);
        return NULL;
    }
    ssize_t got = vfs_read(fd, buf, size);
    vfs_close(fd);
    if (got < 0 || (uint64_t)got != size)
    {
        kfree(buf);
        return NULL;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E'  ||
        ehdr->e_ident[2] != 'L'  ||
        ehdr->e_ident[3] != 'F')
    {
        kfree(buf);
        return NULL;
    }

    if (ehdr->e_machine != EM_X86_64)
    {
        kfree(buf);
        return NULL;
    }

    uint64_t entry_va = ehdr->e_entry;

    AddressSpace *as = vmm_create_aspace();
    if (!as)
    {
        kfree(buf);
        return NULL;
    }

    for (uint16_t i = 0; i < ehdr->e_phnum; i++)
    {
        Elf64_Phdr *ph = (Elf64_Phdr *)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_va    = ph->p_vaddr;
        uint64_t seg_memsz = ph->p_memsz;
        uint64_t seg_filesz= ph->p_filesz;
        uint64_t file_off  = ph->p_offset;

        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W) vmm_flags |= PTE_WRITABLE;
        if (!(ph->p_flags & PF_X)) vmm_flags |= PTE_NX;

        uint64_t va_start  = seg_va & ~(PAGE_SIZE - 1);
        uint64_t va_end    = DIV_CEIL(seg_va + seg_memsz, PAGE_SIZE) * PAGE_SIZE;
        uint64_t page_cnt  = (va_end - va_start) / PAGE_SIZE;

        for (uint64_t p = 0; p < page_cnt; p++)
        {
            uint64_t phys = pmm_alloc_page();
            if (!phys)
            {
                kfree(buf);
                return NULL;
            }
            uint8_t *hhdm = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(hhdm, 0, PAGE_SIZE);

            uint64_t page_va = va_start + p * PAGE_SIZE;
            if (page_va < seg_va + seg_filesz) {
                int64_t copy_start = (int64_t)page_va - (int64_t)seg_va;
                uint64_t src_off, dst_off, copy_len;
                if (copy_start < 0) {
                    dst_off  = (uint64_t)(-copy_start);
                    src_off  = 0;
                    copy_len = PAGE_SIZE - dst_off;
                } else {
                    dst_off  = 0;
                    src_off  = (uint64_t)copy_start;
                    copy_len = PAGE_SIZE;
                }
                if (src_off + copy_len > seg_filesz)
                    copy_len = (src_off < seg_filesz) ? seg_filesz - src_off : 0;
                if (copy_len > 0)
                    memcpy(hhdm + dst_off, buf + file_off + src_off, copy_len);
            }

            if (!vmm_map(as, page_va, phys, 1, vmm_flags)) {
                kfree(buf);
                return NULL;
            }
        }
    }

    kfree(buf);

    const char *name = path_basename(path);
    Task *t = task_create_user(name, entry_va, as, argv);
    if (!t)
    {
        vmm_destroy_aspace(as);
        return NULL;
    }

    if (s_current && s_current->cwd[0] != '\0')
    {
        strncpy(t->cwd, s_current->cwd, sizeof(t->cwd) - 1);
        t->cwd[sizeof(t->cwd) - 1] = '\0';
    }

    return t;
}

int task_kill(uint32_t pid)
{
    if (pid == 0)
        return -1;

    Task *target = task_find(pid);

    if (!target)
    {
        //kprintf("[task] kill: pid %u not found\r\n", pid);
        return -1;
    }

    if (target->state == TASK_DEAD || target->state == TASK_UNUSED)
    {
        //kprintf("[task] kill: pid %u already dead\r\n", pid);
        return -1;
    }

    if (target == s_current)
    {
        //kprintf("[task] kill: pid %u is current, calling task_exit\r\n", pid);
        task_exit();
    }

    //kprintf("[task] kill: marking '%s' (pid %u) DEAD\r\n", target->name, target->pid);
    target->state = TASK_DEAD;
    return 0;
}