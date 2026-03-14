; crt0.asm — C runtime startup for StrawOS userspace ELF tasks
; Linked first; the ELF entry point is _start.
; After main() returns we issue a SYS_EXIT syscall so the task dies cleanly.

bits 64
section .text
global _start
extern main          ; provided by the user's C file

%define SYS_EXIT 1

_start:
    ; The stack is already 16-byte aligned by the kernel's task launcher.
    ; Clear the frame pointer so stack unwinding stops here.
    xor     rbp, rbp

    call    main

    ; main() returned — call SYS_EXIT with main's return value (rax).
    mov     rdi, rax        ; exit code = return value of main
    mov     rax, SYS_EXIT
    int     0x80

    ; Should never reach here, but just in case:
.hang:
    hlt
    jmp     .hang
