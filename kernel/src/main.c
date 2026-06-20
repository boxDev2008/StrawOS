#include "limine.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "filesystem/ramfs.h"

#include "libk/kprintf.h"

#include "system/task.h"

#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"

#include "devices/pit.h"
#include "devices/ps2keyboard.h"
#include "devices/ps2mouse.h"

#include "net/net.h"
#include "net/socket.h"

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".requests")))
volatile struct limine_framebuffer_request framebuffer_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

uint64_t g_hhdm_offset;
struct flanterm_context *ft_ctx;

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
    vfs_mkdir("/tmp");
    vfs_mkdir("/home");
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
    ps2keyboard_init();
    ps2mouse_init(framebuffer->width, framebuffer->height);
    
    pit_init();
    
    __asm__ volatile("sti");

    /* Network — QEMU user-mode networking defaults */
    net_init(
        (10u<<24)|(0u<<16)|(2u<<8)|15u,    /* 10.0.2.15        */
        (255u<<24)|(255u<<16)|(255u<<8)|0u, /* 255.255.255.0    */
        (10u<<24)|(0u<<16)|(2u<<8)|2u       /* 10.0.2.2 gateway */
    );

    /* ---- loopback self-send test ---- */
    int sender   = k_socket(SOCK_UDP);
    int receiver = k_socket(SOCK_UDP);

    k_bind(receiver, 7777);
    k_connect(sender, (10u<<24)|(0u<<16)|(2u<<8)|15u, 7777);

    k_netsend(sender, "ping", 4);

    char buf[32] = {0};
    int n = (int)k_netrecv(receiver, buf, sizeof(buf) - 1);
    if (n > 0)
        kprintf("[loopback test] received %d bytes: '%s'\n", n, buf);
    else
        kprintf("[loopback test] received 0 bytes\n");

    k_sockclose(sender);
    k_sockclose(receiver);

    /* ---- ICMP ping 8.8.8.8 ---- */
    uint32_t ping_ip = (8u<<24)|(8u<<16)|(8u<<8)|8u;
    kprintf("[ping] pinging 8.8.8.8...\n");
    for (uint16_t seq = 1; seq <= 4; seq++) {
        IcmpReply reply;
        if (icmp_ping(ping_ip, seq, 2000, &reply)) {
            kprintf("[ping] reply from 8.8.8.8: seq=%u time=%ums\n",
                    seq, reply.rtt_ticks);
        } else {
            kprintf("[ping] seq=%u timed out\n", seq);
        }
    }

    task_list();
    Task *shell = task_exec("/modules/bin/shell.elf", NULL);
    if (!shell)
    {
        kprintf("[kernel] Failed to load /modules/bin/shell.elf\r\n");
    }

    while (1)
        __asm__ volatile("hlt");
}