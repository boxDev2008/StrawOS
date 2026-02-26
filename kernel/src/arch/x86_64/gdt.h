#pragma once
#include <stdint.h>

#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18   /* must come before user code for sysret */
#define GDT_USER_CODE   0x20
#define GDT_TSS_LOW     0x28   /* TSS occupies two 8-byte slots (0x28, 0x30) */

#define GDT_USER_CODE_RPL3 (GDT_USER_CODE | 3)
#define GDT_USER_DATA_RPL3 (GDT_USER_DATA | 3)

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
} __attribute__((packed)) GDTEntry;

/* 16-byte system segment descriptor for TSS */
typedef struct {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) GDTSystemEntry;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;     /* kernel stack for ring-0 interrupt entry */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];   /* interrupt stack table (we don't use IST) */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed)) TSS;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GDTPointer;

extern TSS g_tss;

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp);