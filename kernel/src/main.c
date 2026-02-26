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
#include "system/elf.h"

#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"

/*
 * ── QEMU network addresses ────────────────────────────────────────────────
 *
 * These match the QEMU user-mode networking defaults.  Launch QEMU with:
 *
 *   -netdev user,id=n0,hostfwd=tcp::8080-:80 -device rtl8139,netdev=n0
 *
 * Then open http://localhost:8080 in your browser.
 *
 * If you change the QEMU -netdev net= parameter, update MY_IP accordingly.
 */
#define MY_IP      ((10<<24)|(0<<16)|(2<<8)|15)   /* 10.0.2.15  */
#define MY_GW      ((10<<24)|(0<<16)|(2<<8)|2)    /* 10.0.2.2   */
#define MY_MASK    ((255<<24)|(255<<16)|(255<<8)|0) /* 255.255.255.0 */

// ── limine requests ───────────────────────────────────────────────────────────

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
static volatile struct limine_framebuffer_request framebuffer_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
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

    __asm__ volatile("fninit");   // initialise FPU to clean state
}

static inline uint32_t get_rgb(uint8_t r, uint8_t g, uint8_t b) { return (r << 16) | (g << 8) | b; }

// ── ELF64 userspace loader ────────────────────────────────────────────────────

static void keyboard_handler(InterruptFrame *frame)
{
    uint8_t scancode = inb(0x60);
    kprintf("%c", scancode);
}

void kputs(const char *str, size_t count)
{
    flanterm_write(ft_ctx, str, count);
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
    __asm__ volatile("sti");

    const char *gaewrfg = "Hello World\r\n";
    __asm__ volatile(
        "int $0x80"
        :: "a"(SYS_WRITE),              /* rax = syscall number (SYS_WRITE) */
           "D"(1),              /* rdi = fd (stdout)                */
           "S"(gaewrfg),           /* rsi = buf                        */
           "d"(14)               /* rdx = len ("Hello\n" = 6 bytes)  */
        : "memory"
    );


    Task *test_task = task_exec("/modules/test.elf", "test.elf");
    if (!test_task) {
        kprintf("[kernel] Failed to load /modules/test.elf\r\n");
    }

    kprintf("\r\n=== Task list before jump ===\r\n");
    task_list();

    if (test_task) {
        kprintf("\r\n=== Jumping to test.elf (pid %u) ===\r\n", test_task->pid);
        task_yield();
        kprintf("\r\n=== Returned to kernel task ===\r\n");
    }

    kprintf("\r\n=== Final task list ===\r\n");
    task_list();

    int fd = vfs_open("/modules/hello_world.txt", O_RDONLY);
    VStat stat;
    if (vfs_fstat(fd, &stat) < 0) {
        kprintf("File not found\r\n");
        for (;;) __asm__ volatile("hlt");
    }
    uint64_t size = stat.st_size;
    kprintf("Size: %u\r\n", (uint32_t)size);
    uint8_t *buf = (uint8_t*)kmalloc(size);
    vfs_read(fd, buf, size);
    vfs_close(fd);
    kprintf("\r\nKernel halting.\r\n");

    for (;;) __asm__ volatile("hlt");
}