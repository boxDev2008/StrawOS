; task_switch.asm — cooperative context switch + userspace entry for StrawOS
;
; void task_switch_asm(TaskContext *from, TaskContext *to);
;   rdi = from  (save registers here)
;   rsi = to    (restore registers from here)
;
; TaskContext layout (must match task.h):
;   offset  0 : rbx
;   offset  8 : rbp
;   offset 16 : r12
;   offset 24 : r13
;   offset 32 : r14
;   offset 40 : r15
;   offset 48 : rsp
;   offset 56 : rflags

bits 64
section .text

; ── Cooperative kernel context switch ────────────────────────────────────────
global task_switch_asm
task_switch_asm:
    ; Save callee-saved registers + RFLAGS into *from
    mov     [rdi +  0], rbx
    mov     [rdi +  8], rbp
    mov     [rdi + 16], r12
    mov     [rdi + 24], r13
    mov     [rdi + 32], r14
    mov     [rdi + 40], r15
    mov     [rdi + 48], rsp
    pushfq
    pop     qword [rdi + 56]    ; save RFLAGS

    ; Restore registers + RFLAGS from *to
    mov     rbx, [rsi +  0]
    mov     rbp, [rsi +  8]
    mov     r12, [rsi + 16]
    mov     r13, [rsi + 24]
    mov     r14, [rsi + 32]
    mov     r15, [rsi + 40]
    mov     rsp, [rsi + 48]
    push    qword [rsi + 56]    ; push saved RFLAGS
    popfq                        ; restore RFLAGS (re-enables IF if it was set)

    ret

; ── Userspace trampoline ──────────────────────────────────────────────────────
; Called via 'ret' from task_switch_asm for newly created user tasks.
;
; Kernel stack on entry (pushed by task_create_user, low addr first):
;   [rsp+0]  = &task_userspace_trampoline  ← ret lands here
;   [rsp+8]  = entry_va    (user _start virtual address)
;   [rsp+16] = user_rsp    (pre-built user stack pointer — points at argc)
;
; So after the 'ret' that lands us here, rsp points at entry_va.

%define GDT_USER_CODE_RPL3  0x23   ; (0x20 | 3)
%define GDT_USER_DATA_RPL3  0x1B   ; (0x18 | 3)

global task_userspace_trampoline
task_userspace_trampoline:
    pop     rcx         ; entry_va   (rsp now points at user_rsp)
    pop     rdx         ; user_rsp   (pre-built stack with argc/argv on it)

    ; Set ring-3 data segments before iretq
    mov     ax, GDT_USER_DATA_RPL3
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    ; Build iretq frame on the *kernel* stack: SS, RSP, RFLAGS, CS, RIP
    ;
    ; RSP here is still the kernel stack — we push the iretq frame onto it,
    ; then iretq switches to ring 3 and sets RSP = rdx (the user stack).
    push    qword GDT_USER_DATA_RPL3   ; SS
    push    rdx                         ; RSP — user stack, pointing at argc
    pushfq
    pop     rax
    or      rax, 0x200          ; IF=1
    and     rax, ~0x3000        ; IOPL=0
    push    rax                         ; RFLAGS
    push    qword GDT_USER_CODE_RPL3   ; CS
    push    rcx                         ; RIP = entry_va (_start)

    ; Zero all GP registers for a clean, deterministic userspace entry.
    ; rcx/rdx are already consumed above; zero them too.
    xor     rax, rax
    xor     rbx, rbx
    xor     rcx, rcx
    xor     rdx, rdx
    xor     rsi, rsi
    xor     rdi, rdi
    xor     rbp, rbp
    xor     r8,  r8
    xor     r9,  r9
    xor     r10, r10
    xor     r11, r11
    xor     r12, r12
    xor     r13, r13
    xor     r14, r14
    xor     r15, r15

    iretq