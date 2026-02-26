#include "task.h"
#include "elf.h"
#include "filesystem/vfs.h"
#include "memory/heap.h"
#include "memory/vmm.h"
#include "memory/pmm.h"
#include "arch/x86_64/gdt.h"
#include "common.h"
#include "libk/memory.h"
#include <string.h>
#include <stddef.h>

// ── internal state ────────────────────────────────────────────────────────────

static Task      s_tasks[TASK_MAX];
static Task     *s_current = NULL;
static uint32_t  s_next_pid = 0;

extern void task_switch_asm(TaskContext *from, TaskContext *to);
extern void kprintf(const char *fmt, ...);

// ── helpers ───────────────────────────────────────────────────────────────────

static Task *slot_alloc(void)
{
    for (int i = 0; i < TASK_MAX; i++) {
        if (s_tasks[i].state == TASK_UNUSED)
            return &s_tasks[i];
    }
    return NULL;
}

static void task_common_init(Task *t, const char *name)
{
    t->pid   = s_next_pid++;
    t->state = TASK_READY;
    t->aspace = NULL;
    t->ustack = NULL;
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';

    t->ctx.rbx = 0;
    t->ctx.rbp = 0;
    t->ctx.r12 = 0;
    t->ctx.r13 = 0;
    t->ctx.r14 = 0;
    t->ctx.r15 = 0;
    t->ctx.rip = 0;
    t->ctx.rflags = 0x202; // IF=1 (interrupts enabled) + reserved bit 1

    // Insert into circular list after s_current
    t->next         = s_current->next;
    s_current->next = t;
}

// ── public API ────────────────────────────────────────────────────────────────

void task_init(void)
{
    for (int i = 0; i < TASK_MAX; i++)
        s_tasks[i].state = TASK_UNUSED;

    Task *kernel_task  = &s_tasks[0];
    kernel_task->pid   = s_next_pid++;
    kernel_task->state = TASK_RUNNING;
    kernel_task->kstack = NULL;
    kernel_task->ustack = NULL;
    kernel_task->aspace = NULL;
    kernel_task->next  = kernel_task;
    strncpy(kernel_task->name, "kernel", sizeof(kernel_task->name) - 1);

    s_current = kernel_task;
}

// Create a kernel-mode (ring 0) task.
Task *task_create(const char *name, void (*entry)(void))
{
    Task *t = slot_alloc();
    if (!t) return NULL;

    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) return NULL;

    t->kstack = kstack;
    task_common_init(t, name);

    // Build initial kernel stack:
    //   task_switch_asm's final 'ret' pops entry → jumps there.
    //   No exit handler needed: use crt0 / SYS_EXIT from userspace.
    //   For kernel tasks, the entry function should loop forever or call
    //   task_yield() and eventually mark itself dead.
    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);
    *--sp = (uint64_t)entry;

    t->ctx.rsp = (uint64_t)sp;

    kprintf("[task] created kernel task '%s' (pid %u)\r\n", t->name, t->pid);
    return t;
}

// Declared in task_switch.asm
extern void task_enter_userspace(uint64_t entry, uint64_t ustack_top,
                                  uint64_t cs, uint64_t ss);

// User-stack virtual address base (grows down from here)
#define USER_STACK_VA_TOP   0x0000700000000000UL
#define USER_STACK_VA_PAGES (TASK_USTACK_SIZE / PAGE_SIZE)

// Create a userspace (ring 3) task.
Task *task_create_user(const char *name, uint64_t entry_va, void *aspace)
{
    Task *t = slot_alloc();
    if (!t) return NULL;

    // Kernel stack for handling interrupts/syscalls from this task
    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) return NULL;

    // Allocate physical pages for the user stack and map them
    AddressSpace *as = (AddressSpace *)aspace;
    uint64_t ustack_va_base = USER_STACK_VA_TOP - USER_STACK_VA_PAGES * PAGE_SIZE;

    for (uint64_t i = 0; i < USER_STACK_VA_PAGES; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            kfree(kstack);
            return NULL;
        }
        uint64_t va = ustack_va_base + i * PAGE_SIZE;
        vmm_map(as, va, phys, 1, VMM_USER_RW);
    }

    t->kstack = kstack;
    t->ustack = (uint8_t *)ustack_va_base; // store base (bottom) of user stack
    task_common_init(t, name);
    t->aspace = aspace;  // must be set AFTER task_common_init (which zeroes it)

    // The kernel stack for a new user task is set up so that task_switch_asm
    // does 'ret' into task_userspace_trampoline, which then does iretq into ring 3.
    // We push the arguments (entry_va, user_stack_top) onto the kernel stack.
    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);

    // Arguments for task_userspace_trampoline (we call it like a normal C function
    // by pushing them in reverse; but we call via 'ret', so we use a small stub).
    // Simpler: push entry and ustack_top as "parameters" then push the trampoline addr.
    // The trampoline reads them from the stack directly.
    uint64_t ustack_top = USER_STACK_VA_TOP;

    // Stack layout when trampoline is entered via 'ret':
    //   [rsp+0]  = entry_va    (first thing trampoline pops)
    //   [rsp+8]  = ustack_top
    *--sp = ustack_top;
    *--sp = entry_va;

    // The address task_switch_asm's 'ret' jumps to
    extern void task_userspace_trampoline(void);
    *--sp = (uint64_t)task_userspace_trampoline;

    t->ctx.rsp = (uint64_t)sp;

    kprintf("[task] created user task '%s' (pid %u) entry=0x%p\r\n",
            t->name, t->pid, (void *)entry_va);
    return t;
}

void task_destroy(Task *t)
{
    if (!t || t == s_current) return;

    Task *prev = s_current;
    int limit = TASK_MAX;
    while (prev->next != t && prev->next != s_current && limit-- > 0)
        prev = prev->next;
    if (prev->next == t)
        prev->next = t->next;

    if (t->kstack) { kfree(t->kstack); t->kstack = NULL; }
    // Note: ustack is mapped in the task's address space; freeing it properly
    // requires walking the page tables (left as future work).
    t->state = TASK_UNUSED;
    kprintf("[task] destroyed '%s' (pid %u)\r\n", t->name, t->pid);
}

Task *task_current(void) { return s_current; }

Task *task_find(uint32_t pid)
{
    for (int i = 0; i < TASK_MAX; i++) {
        if (s_tasks[i].state != TASK_UNUSED && s_tasks[i].pid == pid)
            return &s_tasks[i];
    }
    return NULL;
}

static void do_switch(Task *to)
{
    Task *from = s_current;
    s_current  = to;
    to->state  = TASK_RUNNING;

    // Update TSS.rsp0 so ring-0 interrupts for the new task use its kernel stack.
    gdt_set_kernel_stack((uint64_t)(to->kstack + TASK_STACK_SIZE));

    // Switch address space if needed
    if (to->aspace && to->aspace != from->aspace) {
        vmm_switch_aspace((AddressSpace *)to->aspace);
    } else if (!to->aspace) {
        // Switch to kernel address space
        vmm_switch_aspace(vmm_kernel_aspace());
    }

    task_switch_asm(&from->ctx, &to->ctx);
    // Returns here when switched back to `from`.
}

void task_yield(void)
{
    Task *next = s_current->next;
    int   laps = 0;
    while (next->state != TASK_READY && laps < TASK_MAX) {
        next = next->next;
        laps++;
    }
    if (next == s_current) return;

    s_current->state = TASK_READY;
    do_switch(next);
}

int task_jump(uint32_t pid)
{
    Task *target = task_find(pid);
    if (!target) {
        kprintf("[task] jump: pid %u not found\r\n", pid);
        return -1;
    }
    if (target == s_current) return 0;
    if (target->state != TASK_READY) {
        kprintf("[task] jump: pid %u not ready (state=%d)\r\n",
                pid, (int)target->state);
        return -1;
    }

    s_current->state = TASK_READY;
    do_switch(target);
    return 0;
}

void task_list(void)
{
    static const char *state_names[] = {
        "unused", "ready", "running", "blocked", "dead"
    };
    kprintf("PID  STATE    NAME\r\n");
    kprintf("---- -------- ----------------\r\n");
    for (int i = 0; i < TASK_MAX; i++) {
        Task *t = &s_tasks[i];
        if (t->state == TASK_UNUSED) continue;
        const char *sn = (t->state <= TASK_DEAD) ? state_names[t->state] : "?";
        kprintf("%u %s %s\r\n", t->pid, sn, t->name);
    }
}

Task *task_exec(const char *path, const char *name)
{
    /* ── 1. Open & stat ─────────────────────────────────────────────────── */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kprintf("[loader] cannot open '%s' (err %d)\r\n", path, fd);
        return NULL;
    }

    VStat st;
    if (vfs_fstat(fd, &st) < 0 || st.st_size == 0) {
        kprintf("[loader] stat failed or empty: '%s'\r\n", path);
        vfs_close(fd);
        return NULL;
    }

    uint64_t size = st.st_size;
    kprintf("[loader] loading ELF '%s' (%u bytes)\r\n", path, (uint32_t)size);

    /* ── 2. Read entire file into a kernel temp buffer ──────────────────── */
    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf) {
        kprintf("[loader] kmalloc(%u) failed\r\n", (uint32_t)size);
        vfs_close(fd);
        return NULL;
    }
    ssize_t got = vfs_read(fd, buf, size);
    vfs_close(fd);
    if (got < 0 || (uint64_t)got != size) {
        kprintf("[loader] vfs_read failed (got %d)\r\n", (int)got);
        kfree(buf);
        return NULL;
    }

    /* ── 3. Validate ELF header ─────────────────────────────────────────── */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E'  ||
        ehdr->e_ident[2] != 'L'  ||
        ehdr->e_ident[3] != 'F') {
        kprintf("[loader] not an ELF file\r\n");
        kfree(buf);
        return NULL;
    }
    if (ehdr->e_machine != EM_X86_64) {
        kprintf("[loader] not x86_64 ELF\r\n");
        kfree(buf);
        return NULL;
    }

    uint64_t entry_va = ehdr->e_entry;
    kprintf("[loader] ELF entry = 0x%p, %u phdrs\r\n",
            (void *)entry_va, (uint32_t)ehdr->e_phnum);

    /* ── 4. Create a fresh address space for this task ──────────────────── */
    AddressSpace *as = vmm_create_aspace();
    if (!as) {
        kprintf("[loader] vmm_create_aspace failed\r\n");
        kfree(buf);
        return NULL;
    }

    /* ── 5. Load PT_LOAD segments ───────────────────────────────────────── */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *ph = (Elf64_Phdr *)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_va    = ph->p_vaddr;
        uint64_t seg_memsz = ph->p_memsz;
        uint64_t seg_filesz= ph->p_filesz;
        uint64_t file_off  = ph->p_offset;

        kprintf("[loader]   PT_LOAD va=0x%p filesz=%u memsz=%u flags=%x\r\n",
                (void *)seg_va, (uint32_t)seg_filesz, (uint32_t)seg_memsz,
                (uint32_t)ph->p_flags);

        // Determine VMM flags from ELF segment flags
        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W) vmm_flags |= PTE_WRITABLE;
        if (!(ph->p_flags & PF_X)) vmm_flags |= PTE_NX;

        // Align to page boundaries
        uint64_t va_start  = seg_va & ~(PAGE_SIZE - 1);
        uint64_t va_end    = DIV_CEIL(seg_va + seg_memsz, PAGE_SIZE) * PAGE_SIZE;
        uint64_t page_cnt  = (va_end - va_start) / PAGE_SIZE;

        for (uint64_t p = 0; p < page_cnt; p++) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                kprintf("[loader] pmm_alloc_page failed for segment %u\r\n", i);
                kfree(buf);
                return NULL;
            }
            uint8_t *hhdm = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(hhdm, 0, PAGE_SIZE);

            // Copy file data for this page
            uint64_t page_va = va_start + p * PAGE_SIZE;
            if (page_va < seg_va + seg_filesz) {
                // This page overlaps with file data
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
                kprintf("[loader] vmm_map failed for segment %u page %u\r\n",
                        i, (uint32_t)p);
                kfree(buf);
                return NULL;
            }
        }
    }

    kfree(buf);

    /* ── 6. Create the userspace task ───────────────────────────────────── */
    Task *t = task_create_user(name, entry_va, as);
    if (!t) {
        kprintf("[loader] task_create_user failed\r\n");
        return NULL;
    }

    return t;
}