#include "limine.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "filesystem/ramfs.h"

#include "libk/kprintf.h"

#include "system/syscall.h"
#include "system/task.h"
#include "elf.h"

#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"

#include "devices/ps2mouse.h"

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_executable_address_request exe_addr_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0
};


__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".requests")))
volatile struct limine_framebuffer_request framebuffer_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};

// ── globals ───────────────────────────────────────────────────────────────────

uint64_t g_hhdm_offset;
static struct flanterm_context *ft_ctx;

// ── helpers ───────────────────────────────────────────────────────────────────

static inline void sse_enable(void) {
    uint64_t cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1UL << 2);   // clear EM (bit 2) — no FPU emulation
    cr0 |=  (1UL << 1);   // set   MP (bit 1) — monitor coprocessor
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 9);    // set OSFXSR     (bit 9)  — enable FXSAVE/FXRSTOR + SSE
    cr4 |= (1UL << 10);   // set OSXMMEXCPT (bit 10) — enable SSE exception handling
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");   // initialise x87 FPU to clean state

    // Initialise MXCSR to a known-good state: all SSE exceptions masked,
    // round-to-nearest, no pending flags.  fninit does NOT touch MXCSR,
    // so on real hardware it may contain whatever the firmware left behind,
    // causing spurious #XF (#19) exceptions when userspace code (e.g.
    // Nuklear's font baking) produces denormals or other FP edge cases.
    uint32_t mxcsr = 0x1F80;  // IM|DM|ZM|OM|UM|PM all set, rounding = nearest
    __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));
}

// ── ELF64 userspace loader ────────────────────────────────────────────────────

static void keyboard_handler(InterruptFrame *frame)
{
    uint8_t scancode = inb(0x60);
    //kprintf("%c", scancode);
}

void kputs(const char *str, size_t count)
{
    const char *p = str;
    const char *end = str + count;

    while (p < end)
    {
        const char *newline = memchr(p, '\n', end - p);
        if (!newline)
        {
            flanterm_write(ft_ctx, p, end - p);
            break;
        }
        if (newline > p)
            flanterm_write(ft_ctx, p, newline - p);
        flanterm_write(ft_ctx, "\r\n", 2);
        p = newline + 1;
    }
}

void kernel_main(void)
{
    g_hhdm_offset = hhdm_req.response->offset;
    
    sse_enable();
    gdt_init();
    
    struct limine_framebuffer *framebuffer = framebuffer_req.response->framebuffers[0];
    ft_ctx = flanterm_fb_init(
        NULL, NULL,
        framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch,
        framebuffer->red_mask_size,   framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size,  framebuffer->blue_mask_shift,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        0, 0, 1, 0, 0, 0, 0
    );

    idt_init();
    pmm_init(memmap_req.response);
    vmm_init();
    heap_init();
    vfs_init();

    VNode *root = ramfs_create_root();
    vfs_mount("/", root);
    vfs_mkdir("/modules");
    vfs_mkdir("/modules/bin");

    struct limine_module_response *modules = module_request.response;
    for (uint64_t i = 0; i < modules->module_count; i++)
    {
        struct limine_file *mod = modules->modules[i];

        void    *addr   = mod->address;   // virtual address of module data
        uint64_t size   = mod->size;      // size in bytes
        char    *path   = mod->path;      // path string from config
        char    *string = mod->string;    // MODULE_STRING value

        int fd = vfs_open(path, O_CREAT | O_WRONLY);
        vfs_write(fd, addr, size);
        vfs_close(fd);

        kprintf("Loaded module: %s %u\r\n", path, (uint32_t)size);
    }

    task_init();

    irq_register(1, keyboard_handler);
    ps2mouse_init(framebuffer->width, framebuffer->height);

    __asm__ volatile("sti");

    task_list();
    Task *shell = task_exec("/modules/bin/shell.elf", "shell.elf");
    if (!shell)
    {
        kprintf("[kernel] Failed to load /modules/bin/shell.elf\r\n");
    }

    while (1)
    {
        task_reap_dead();
        task_yield();
    }
}