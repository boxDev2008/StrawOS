; kernel/arch/x86_64/isr_stubs.asm
; Generates 256 interrupt stubs that push a uniform frame and call interrupt_dispatch()

bits 64
section .text

extern interrupt_dispatch

; ── Macros ────────────────────────────────────────────────────────────────────
; ISR with no error code: push dummy 0 then vector
%macro ISR_NOERR 1
isr_stub_%1:
    push qword 0        ; fake error code
    push qword %1       ; vector number
    jmp  isr_common
%endmacro

; ISR where CPU pushes an error code: just push vector on top
%macro ISR_ERR 1
isr_stub_%1:
    push qword %1       ; CPU already pushed error code before us
    jmp  isr_common
%endmacro

; ── Exception stubs (0–31) ───────────────────────────────────────────────────
ISR_NOERR 0     ; #DE Divide Error
ISR_NOERR 1     ; #DB Debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; #BP Breakpoint
ISR_NOERR 4     ; #OF Overflow
ISR_NOERR 5     ; #BR Bound Range Exceeded
ISR_NOERR 6     ; #UD Invalid Opcode
ISR_NOERR 7     ; #NM Device Not Available
ISR_ERR   8     ; #DF Double Fault (error code = 0 always)
ISR_NOERR 9     ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10    ; #TS Invalid TSS
ISR_ERR   11    ; #NP Segment Not Present
ISR_ERR   12    ; #SS Stack Fault
ISR_ERR   13    ; #GP General Protection
ISR_ERR   14    ; #PF Page Fault
ISR_NOERR 15    ; Reserved
ISR_NOERR 16    ; #MF FPU Error
ISR_ERR   17    ; #AC Alignment Check
ISR_NOERR 18    ; #MC Machine Check
ISR_NOERR 19    ; #XF SIMD FP
ISR_NOERR 20
ISR_ERR   21    ; #CP Control Protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28    ; #HV Hypervisor Injection
ISR_ERR   29    ; #VC VMM Communication
ISR_ERR   30    ; #SX Security Exception
ISR_NOERR 31

; ── Hardware IRQ stubs (32–47, remapped PIC) ─────────────────────────────────
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

; ── Remaining vectors (48–255) ───────────────────────────────────────────────
%assign i 48
%rep 208
ISR_NOERR i
%assign i i+1
%endrep

; ── Common handler ────────────────────────────────────────────────────────────
isr_common:
    ; At this point the stack looks like:
    ;   [cpu] SS, RSP, RFLAGS, CS, RIP
    ;   [us]  error_code, vector    ← RSP points here
    ; Save all general purpose registers (in reverse pop order)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; RDI = pointer to the full InterruptFrame on the stack
    mov  rbp, rsp          ; save frame ptr in rbp — callee-saved, C preserves it
    mov  rdi, rsp          ; pass frame as arg1
    and  rsp, ~0xF         ; align for call
    call interrupt_dispatch ; rbp guaranteed intact when this returns
    mov  rsp, rbp          ; restore from the register we know wasn't touched

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; discard vector + error_code
    iretq

; ── Stub pointer table (exported to C) ───────────────────────────────────────
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep