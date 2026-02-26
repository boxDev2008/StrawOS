; kernel/arch/x86_64/gdt_flush.asm
; void gdt_flush(GDTPointer *ptr)  -- rdi = ptr
; void tss_flush(uint16_t sel)     -- di  = selector

bits 64
section .text

global gdt_flush
gdt_flush:
    lgdt [rdi]

    ; Reload CS via a far return (can't mov to CS directly)
    push 0x08               ; kernel code selector
    lea  rax, [rel .reload]
    push rax
    retfq

.reload:
    ; Reload all data segment registers to kernel data (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; FS and GS are used for per-CPU / TLS — zero them for now
    xor ax, ax
    mov fs, ax
    mov gs, ax
    ret

global tss_flush
tss_flush:
    ltr di
    ret