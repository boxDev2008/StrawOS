ENTRY(kmain)

SECTIONS
{
    . = 0xffffffff80000000;

    __kernel_virt_start = .;

    .text : ALIGN(4K)
    {
        /* kernel call table, must stay at the very start of the image */
        KEEP(*(.kcall_table))
        *(.text*)
    } :text

    . = ALIGN(4K);

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    } :rodata

    . = ALIGN(4K);

    .data : ALIGN(4K)
    {
        *(.data*)
    } :data

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
    } :data

    . = ALIGN(4K);

    __kernel_virt_end = .;
}

ASSERT(kcall_table == 0xffffffff80000000, "kcall_table must sit at KCALL_TABLE_ADDR")
