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

// Return the lowest PID not currently assigned to any live task.
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
    t->ctx.rflags = 0x202; // IF=1

    // Append to the end of the circular list (just before s_current)
    // so that scheduling order matches creation order.
    Task *prev = s_current;
    while (prev->next != s_current)
        prev = prev->next;
    prev->next = t;
    t->next    = s_current;
}

// ── public API ────────────────────────────────────────────────────────────────

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
    kernel_task->next  = kernel_task; // circular, points to itself
    strncpy(kernel_task->name, "kernel", sizeof(kernel_task->name) - 1);

    s_current = kernel_task;
}

// Create a ring-0 kernel task.
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

    kprintf("[task] created kernel task '%s' (pid %u)\r\n", t->name, t->pid);
    return t;
}

// Declared in task_switch.asm
extern void task_userspace_trampoline(void);

// User-stack virtual address base (grows down from here)
#define USER_STACK_VA_TOP   0x0000700000000000UL
#define USER_STACK_VA_PAGES (TASK_USTACK_SIZE / PAGE_SIZE)

// Create a ring-3 userspace task.
Task *task_create_user(const char *name, uint64_t entry_va, void *aspace)
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
    t->fpu_used = 1; // userspace always uses SSE

    // The fpu_state buffer is zeroed (task slot is in BSS / zeroed heap),
    // but MXCSR lives at byte offset 24 of the FXSAVE area and a value of
    // 0x0000 means ALL SSE exceptions are UNMASKED — the opposite of safe.
    // Write 0x1F80 (all exceptions masked, round-to-nearest) so the first
    // fxrstor into this task doesn't immediately fire a #XF.
    *((uint32_t *)(t->fpu_state.data + 24)) = 0x1F80;

    // Build kernel stack so task_switch_asm's 'ret' lands in the trampoline.
    // The trampoline pops entry_va and ustack_top off the stack, then iretq.
    uint64_t *sp = (uint64_t *)(kstack + TASK_STACK_SIZE);
    *--sp = USER_STACK_VA_TOP;          // arg2: ustack_top
    *--sp = entry_va;                    // arg1: entry_va
    *--sp = (uint64_t)task_userspace_trampoline;

    t->ctx.rsp = (uint64_t)sp;

    kprintf("[task] created user task '%s' (pid %u) entry=0x%p\r\n",
            t->name, t->pid, (void *)entry_va);
    return t;
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

// Low-level switch: save from, restore to, update global state.
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

    task_switch_asm(&from->ctx, &to->ctx);
    // Returns here when switched back to `from`.
}

// Find the next READY task after `start` in the ring (skipping `start` itself).
// Returns NULL if no other READY task exists.
static Task *find_next_ready(Task *start)
{
    Task *t = start->next;
    int laps = 0;
    while (laps < TASK_MAX) {
        if (t == start) return NULL; // wrapped all the way around
        if (t->state == TASK_READY)  return t;
        t = t->next;
        laps++;
    }
    return NULL;
}


// Free a task that has already been switched away from.
// Safe to call because we are no longer running on its stack.
void task_yield(void)
{
    Task *next = find_next_ready(s_current);
    if (!next) return; // nobody else to run

    Task *from = s_current;
    do_switch(from, next);
    // When we're rescheduled, execution resumes here.
}

// Called by SYS_EXIT. Marks current task DEAD and switches away. Never returns.
// The kstack cannot be freed here (we're still on it), so it is left for
// task_reap_dead() which the kernel task calls after returning from task_yield().
void task_exit(void)
{
    Task *dying = s_current;
    dying->state = TASK_DEAD;

    Task *next = find_next_ready(dying);
    if (!next) {
        // No other task to run — just halt.
        for (;;) __asm__ volatile("hlt");
    }

    s_current = next;

    // Don't save dying's FPU — it will never run again.
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

    for (;;) __asm__ volatile("hlt"); // never reached
}

// Free all DEAD tasks. Must only be called from the kernel task (never from
// a task that might itself be dead). Unlinks each dead task from the ring,
// frees its kernel stack, destroys its address space, and marks the slot UNUSED.
void task_reap_dead(void)
{
    for (int i = 0; i < TASK_MAX; i++) {
        Task *t = &s_tasks[i];
        if (t->state != TASK_DEAD) continue;
        if (t == s_current)        continue; // safety — should never happen

        // Unlink from circular list
        Task *prev = s_current;
        int limit = TASK_MAX;
        while (prev->next != t && prev->next != s_current && limit-- > 0)
            prev = prev->next;
        if (prev->next == t)
            prev->next = t->next;

        // Free kernel stack
        if (t->kstack) { kfree(t->kstack); t->kstack = NULL; }

        // Destroy address space (also frees all user pages including ustack)
        if (t->aspace) { vmm_destroy_aspace((AddressSpace *)t->aspace); t->aspace = NULL; }

        t->state = TASK_UNUSED;
        kprintf("[task] reaped '%s' (pid %u)\r\n", t->name, t->pid);
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

    /* ── 4. Create a fresh address space ────────────────────────────────── */
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

        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W) vmm_flags |= PTE_WRITABLE;
        if (!(ph->p_flags & PF_X)) vmm_flags |= PTE_NX;

        uint64_t va_start  = seg_va & ~(PAGE_SIZE - 1);
        uint64_t va_end    = DIV_CEIL(seg_va + seg_memsz, PAGE_SIZE) * PAGE_SIZE;
        uint64_t page_cnt  = (va_end - va_start) / PAGE_SIZE;

        for (uint64_t p = 0; p < page_cnt; p++) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                kprintf("[loader] pmm_alloc_page failed\r\n");
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
                kprintf("[loader] vmm_map failed\r\n");
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