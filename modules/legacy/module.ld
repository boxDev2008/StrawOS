/* module.ld — ELF64 linker script for StrawOS userspace tasks
 * Loads at a canonical user-space virtual address.
 * The kernel ELF loader maps PT_LOAD segments at their p_vaddr.
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

/* User virtual address where this ELF will be loaded.
 * Keep it far from the kernel (which lives at 0xffffffff80000000).
 * All user tasks share the same address (each gets its own page table). */
BASE = 0x0000000000400000;

PHDRS {
    text   PT_LOAD FLAGS(5);   /* R+X */
    rodata PT_LOAD FLAGS(4);   /* R   */
    data   PT_LOAD FLAGS(6);   /* R+W */
}

SECTIONS {
    . = BASE;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
