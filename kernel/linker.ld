ENTRY(kmain)

SECTIONS
{
    . = 0xffffffff80000000;

    __kernel_virt_start = .;

    .text : ALIGN(4K)
    {
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
