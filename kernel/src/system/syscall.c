#include "syscall.h"
#include "task.h"
#include "common.h"
#include "memory/vmm.h"
#include "memory/pmm.h"
#include "libk/memory.h"
#include "filesystem/vfs.h"
#include "devices/ps2mouse.h"
#include "arch/x86_64/idt.h"

#include <stdint.h>
#include <stddef.h>

#define STDOUT_FD 1
#define STDERR_FD 2

#define MMAP_PROT_NONE  0
#define MMAP_PROT_READ  1
#define MMAP_PROT_WRITE 2

#define DEVICE_FRAMEBUFFER 0
#define DEVICE_PS2MOUSE 2

#define USER_MMAP_BASE  0x0000400000000000UL
#define USER_MMAP_MAX   0x0000700000000000UL
#define USER_BRK_BASE   0x0000200000000000UL

int64_t k_write(int fd, const char *buf, size_t count)
{
    if (!buf || count == 0) return -1;

    if (fd == STDOUT_FD || fd == STDERR_FD)
    {
        extern void kputs(const char *str, size_t count);
        kputs(buf, count);
        return (int64_t)count;
    }

    return (int64_t)vfs_write(fd, buf, count);
}

int64_t k_read(int fd, char *buf, size_t count)
{
    if (!buf || count == 0) return -1;
    return (int64_t)vfs_read(fd, buf, count);
}

int64_t k_open(const char *path, int flags)
{
    if (!path) return -1;
    return (int64_t)vfs_open(path, flags);
}

int64_t k_close(int fd)
{
    return (int64_t)vfs_close(fd);
}

int64_t k_seek(int fd, int64_t offset, int whence)
{
    return (int64_t)vfs_seek(fd, offset, whence);
}

int64_t k_stat(const char *path, void *statbuf)
{
    return (int64_t)vfs_stat(path, statbuf);
}

int64_t k_fstat(int fd, void *statbuf)
{
    return (int64_t)vfs_fstat(fd, statbuf);
}

int64_t k_getpid(void)
{
    Task *t = task_current();
    return t ? (int64_t)t->pid : 0;
}

int64_t k_exit(int code)
{
    (void)code;
    task_exit();   // marks current task dead and switches away — never returns
    __builtin_unreachable();
}


static void task_mem_init_if_needed(Task *t)
{
    if (t->brk_base == 0)
    {
        t->brk_base  = USER_BRK_BASE;
        t->brk       = USER_BRK_BASE;
        t->mmap_next = USER_MMAP_BASE;
    }
}

int64_t k_mmap(size_t len, int prot)
{
    if (len == 0) return -1;

    Task *t = task_current();
    if (!t || !t->aspace) return -1;

    task_mem_init_if_needed(t);

    /* Determine page flags. */
    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (prot & MMAP_PROT_WRITE)
        flags |= PTE_WRITABLE;
    if (!(prot & (MMAP_PROT_READ | MMAP_PROT_WRITE)))
        flags = 0; /* PROT_NONE — don't map present at all (guard page) */

    uint64_t page_count = DIV_CEIL(len, PAGE_SIZE);
    uint64_t virt       = t->mmap_next;

    /* Make sure we stay within the mmap arena. */
    if (virt + page_count * PAGE_SIZE > USER_MMAP_MAX) return -1;

    AddressSpace *as = (AddressSpace *)t->aspace;

    if (flags != 0) {
        if (!vmm_alloc_map(as, virt, page_count, flags))
            return -1;
    }

    /* Advance the hint past this region (leave one guard page gap). */
    t->mmap_next = virt + (page_count + 1) * PAGE_SIZE;

    return (int64_t)virt;
}

int64_t k_munmap(uint64_t addr, size_t len)
{
    if (!addr || len == 0)               return -1;
    if (addr & (PAGE_SIZE - 1))          return -1; /* must be page-aligned */
    if (addr < USER_MMAP_BASE)           return -1; /* outside mmap arena */
    if (addr >= USER_MMAP_MAX)           return -1;

    Task *t = task_current();
    if (!t || !t->aspace) return -1;

    AddressSpace *as = (AddressSpace *)t->aspace;
    uint64_t page_count = DIV_CEIL(len, PAGE_SIZE);

    vmm_free_unmap(as, addr, page_count);
    return 0;
}

int64_t k_device(int device_id, void *data)
{
    Task *t = task_current();
    if (!t || !t->aspace) return -1;

    switch (device_id)
    {
    case DEVICE_FRAMEBUFFER:
    {
        typedef struct FramebufferDevice
        {
            uint32_t *address;
            uint32_t width, height, pitch;
        }
        FramebufferDevice;
        extern volatile struct limine_framebuffer_request framebuffer_req;
        const struct limine_framebuffer *framebuffer = framebuffer_req.response->framebuffers[0];

        uint64_t fb_phys    = VIRT_TO_PHYS((uint64_t)framebuffer->address);
        uint64_t fb_bytes   = (uint64_t)framebuffer->height * framebuffer->pitch;
        uint64_t page_count = DIV_CEIL(fb_bytes, PAGE_SIZE);

        task_mem_init_if_needed(t);

        uint64_t user_va = t->mmap_next;
        t->mmap_next     = user_va + (page_count + 1) * PAGE_SIZE; /* +1 guard page */

        AddressSpace *as = (AddressSpace *)t->aspace;

        /*
         * PTE_SHARED marks these pages as borrowed (device memory).
         * vmm_free_unmap and vmm_destroy_aspace will clear the PTEs
         * but will NOT call pmm_free_page on them.
         * PTE_NOCACHE ensures writes go straight to the hardware framebuffer.
         */
        if (!vmm_map(as, user_va, fb_phys, page_count,
                     PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_SHARED))
            return -1;

        FramebufferDevice _data = {
            .address = (uint32_t *)user_va,
            .width   = framebuffer->width,
            .height  = framebuffer->height,
            .pitch   = framebuffer->pitch,
        };
        memcpy(data, &_data, sizeof(FramebufferDevice));
        return 0;
    }
    case DEVICE_PS2MOUSE:
    {
        memcpy(data, ps2mouse_state(), sizeof(MouseState));
        return 0;
    }
    default:
        return -1;
    }
}

void syscall_int80_handler(InterruptFrame *frame)
{
    uint64_t nr   = frame->rax;
    uint64_t arg0 = frame->rdi;
    uint64_t arg1 = frame->rsi;
    uint64_t arg2 = frame->rdx;
    uint64_t arg3 = frame->r10;
    uint64_t arg4 = frame->r8;

    int64_t ret;

    switch (nr) {
        case SYS_EXIT:
            k_exit((int)arg0);
            __builtin_unreachable();

        case SYS_READ:
            ret = k_read((int)arg0, (char *)(uintptr_t)arg1, (size_t)arg2);
            break;

        case SYS_WRITE:
            ret = k_write((int)arg0, (const char *)(uintptr_t)arg1, (size_t)arg2);
            break;

        case SYS_OPEN:
            ret = k_open((const char *)(uintptr_t)arg0, (int)arg1);
            break;

        case SYS_CLOSE:
            ret = k_close((int)arg0);
            break;
        
        case SYS_SEEK:
            ret = k_seek((int)arg0, (int64_t)arg1, (int)arg2);
            break;

        case SYS_STAT:
            ret = k_stat((const char *)(uintptr_t)arg0, (void *)arg1);
            break;

        case SYS_FSTAT:
            ret = k_fstat((int)arg0, (void *)arg1);
            break;

        case SYS_GETPID:
            ret = k_getpid();
            break;

        case SYS_YIELD:
            task_yield();
            ret = 0;
            break;

        case SYS_MMAP:
            ret = k_mmap((size_t)arg0, (int)arg1);
            break;

        case SYS_MUNMAP:
            ret = k_munmap(arg0, (size_t)arg1);
            break;
        
        case SYS_DEVICE:
            ret = k_device((int)arg0, (void*)arg1);
            break;
        default:
            ret = -1;
            break;
    }

    frame->rax = (uint64_t)ret;
}