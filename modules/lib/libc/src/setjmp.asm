; setjmp.asm — x86-64 setjmp/longjmp for StrawOS
; Pure NASM, no compiler interference.
;
; jmp_buf layout (8 x uint64_t = 64 bytes):
;   offset  0: rbx
;   offset  8: rbp
;   offset 16: r12
;   offset 24: r13
;   offset 32: r14
;   offset 40: r15
;   offset 48: rsp  (caller's stack pointer — points past the return addr)
;   offset 56: rip  (return address saved by the call to setjmp)

bits 64
section .text

global setjmp
global _setjmp
global longjmp
global _longjmp

; ── int setjmp(jmp_buf env) ──────────────────────────────────────────────
; rdi = env (pointer to jmp_buf)
; Returns 0.  On longjmp restore, returns whatever val longjmp was given.
setjmp:
_setjmp:
    mov  [rdi +  0], rbx
    mov  [rdi +  8], rbp
    mov  [rdi + 16], r12
    mov  [rdi + 24], r13
    mov  [rdi + 32], r14
    mov  [rdi + 40], r15
    ; rsp at the call site = rsp + 8 (account for the return address pushed
    ; by 'call setjmp' — we want the caller's sp, not ours)
    lea  rax, [rsp + 8]
    mov  [rdi + 48], rax
    ; return address = value currently at [rsp]
    mov  rax, [rsp]
    mov  [rdi + 56], rax
    ; return 0
    xor  eax, eax
    ret

; ── void longjmp(jmp_buf env, int val) ──────────────────────────────────
; rdi = env, esi = val
; Restores registers and jumps back to the setjmp call site.
; setjmp will appear to return val (or 1 if val == 0).
longjmp:
_longjmp:
    ; compute return value: val ? val : 1
    mov  eax, esi
    test eax, eax
    jnz  .nonzero
    mov  eax, 1
.nonzero:
    ; restore callee-saved registers
    mov  rbx, [rdi +  0]
    mov  rbp, [rdi +  8]
    mov  r12, [rdi + 16]
    mov  r13, [rdi + 24]
    mov  r14, [rdi + 32]
    mov  r15, [rdi + 40]
    ; restore stack pointer (caller's sp — no return address slot needed,
    ; we jump directly via the saved rip)
    mov  rsp, [rdi + 48]
    ; jump to saved return address
    ; eax already holds the return value for setjmp
    jmp  qword [rdi + 56]