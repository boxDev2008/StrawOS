bits 64

extern kernel_main
extern _bss_start
extern _bss_end

global _start
_start:
    ; Limine gives us a valid stack, but BSS is not zeroed
    ; Zero BSS manually before any C code runs
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi            ; byte count
    shr rcx, 3              ; convert to qwords
    xor eax, eax
    rep stosq               ; memset(bss, 0, size)

    call kernel_main
.hang:
    cli
    hlt
    jmp .hang