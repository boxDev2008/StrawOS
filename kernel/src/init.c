#include "init.h"

#include "intr/idt.h"
#include "intr/isr.h"
#include "intr/gdt.h"
#include "terminal.h"
#include "mm/pmm.h"
#include "mm/memmap.h"
#include "mm/liballoc.h"
#include "sys/bios32.h"
#include "io.h"
#include "sys/syscall.h"

void kernel_initialize(multiboot_info_t* mboot_info)
{
    gdt_initialize();
    idt_initialize();
    bios32_initialize();

    get_kernel_memory_map(&kmap, mboot_info);
    display_kernel_memory_map(&kmap);

    pmm_initialize(kmap.available.start_addr, kmap.available.size);
	pmm_initialize_region(kmap.available.start_addr, kmap.available.size);

    syscalls_initialize();
}