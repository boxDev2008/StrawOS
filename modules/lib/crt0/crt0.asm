; crt0.asm — C runtime startup for StrawOS userspace ELF tasks
; Linked first; the ELF entry point is _start.
; After main() returns we issue a SYS_EXIT syscall so the task dies cleanly.
;
; Stack layout on entry (set up by kernel's task_create_user):
;
;   [rsp+0 ]  argc          (int64_t)
;   [rsp+8 ]  argv[0]       (char*)   ← rsp+8 == &argv[0]
;   ...
;   [rsp+?]   NULL          (argv sentinel)
;
; We pop argc into rdi and point rsi at argv[0], matching the
; System V AMD64 ABI so that main(int argc, char **argv) just works.
; After the pop, rsp is 8-byte aligned; 'call main' pushes the 8-byte
; return address making rsp 16-byte aligned at main's entry — correct.

bits 64
section .text
global _start
extern main

%define SYS_EXIT 1

_start:
    xor     rbp, rbp            ; clear frame pointer — stack unwinds stop here

    pop     rdi                 ; rdi = argc  (RSP now points at argv[0])
    mov     rsi, rsp            ; rsi = argv  (save before we move RSP)

    ; The kernel aligns user_rsp to 16 bytes, so after `pop rdi` RSP is
    ; 8 mod 16.  The SysV ABI requires RSP to be 16-byte aligned at the
    ; CALL site so that, after CALL pushes the return address, RSP inside
    ; the callee is 8 mod 16.  Without this, the compiler's SSE spills
    ; (movaps) will fault with #GP because they assume 16-byte alignment.
    and     rsp, -16            ; force RSP to 16-byte boundary before call

    call    main

    ; main() returned — exit with its return value (rax).
    mov     rdi, rax
    mov     rax, SYS_EXIT
    int     0x80

    ; Should never reach here.
.hang:
    hlt
    jmp     .hang