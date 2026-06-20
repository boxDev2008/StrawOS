bits 64
section .text

global setjmp
global _setjmp
global longjmp
global _longjmp

setjmp:
_setjmp:
    mov  [rdi +  0], rbx
    mov  [rdi +  8], rbp
    mov  [rdi + 16], r12
    mov  [rdi + 24], r13
    mov  [rdi + 32], r14
    mov  [rdi + 40], r15

    lea  rax, [rsp + 8]
    mov  [rdi + 48], rax

    mov  rax, [rsp]
    mov  [rdi + 56], rax

    xor  eax, eax
    ret

longjmp:
_longjmp:
    mov  eax, esi
    test eax, eax
    jnz  .nonzero
    mov  eax, 1
.nonzero:
    mov  rbx, [rdi +  0]
    mov  rbp, [rdi +  8]
    mov  r12, [rdi + 16]
    mov  r13, [rdi + 24]
    mov  r14, [rdi + 32]
    mov  r15, [rdi + 40]

    mov  rsp, [rdi + 48]

    jmp  qword [rdi + 56]