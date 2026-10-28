#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#include "limine.h"

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kaddr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = 0
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

static struct limine_framebuffer *limine_framebuffer;

#include "common.h"

#include "term.h"
#include "term.c"

#include "libk/string.h"
#include "libk/kprintf.h"

#include "libk/string.c"
#include "libk/kprintf.c"

#include "system/smp.h"

#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"

#include "memory/pmm.c"
#include "memory/vmm.c"
#include "memory/heap.c"

#include "filesystem/vfs.h"
#include "filesystem/ramfs.h"
#include "filesystem/devfs.h"
#include "net/net.h"

#include "elf.h"
#include "elf.c"

#include "system/task.h"
#include "system/task.c"
#include "system/user_pages.c"

#include "system/kcall.h"
#include "system/socket.h"
#include "devices/rtc.h"

#include "system/fs_kcalls.c"
#include "system/socket.c"
#include "devices/rtc.c"
#include "system/kcall.c"

#include "base/gdt.h"
#include "base/gdt.c"

#include "base/lapic.h"
#include "base/ioapic.h"
#include "base/idt.h"

#include "devices/keyboard.h"
#include "devices/mouse.h"

#include "devices/keyboard.c"
#include "devices/mouse.c"

#include "filesystem/vfs.c"
#include "filesystem/ramfs.c"
#include "filesystem/devfs.c"

#include "base/lapic.c"
#include "base/ioapic.c"
#include "base/idt.c"

#include "system/smp.c"

#include "devices/ps2mouse.c"

#include "drivers/pci.h"
#include "drivers/pci.c"

#include "drivers/usb_hid.h"
#include "drivers/usb_hid.c"

#include "drivers/xhci.h"
#include "drivers/xhci.c"

#include "drivers/rtl8139.h"
#include "drivers/rtl8139.c"

#include "drivers/rtl8169.h"
#include "drivers/rtl8169.c"

#include "drivers/nic.h"
#include "drivers/nic.c"

#include "net/net.c"
#include "net/inet.c"

#include "main.c"