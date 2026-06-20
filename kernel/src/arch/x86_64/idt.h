#pragma once
#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9,  r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) InterruptFrame;

typedef void (*IRQHandler)(InterruptFrame *frame);

void idt_init(void);
void irq_register(uint8_t irq, IRQHandler handler);
void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);

void interrupt_dispatch(InterruptFrame *frame);