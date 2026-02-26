#include "gdt.h"
#include <memory.h>

/* ── GDT table ────────────────────────────────────────────────── */
/* 7 entries: null, kcode, kdata, udata, ucode, tss_low, tss_high */
static GDTEntry       g_gdt[7];
static GDTSystemEntry g_tss_entry; /* overlaid as g_gdt[5]+g_gdt[6] */
TSS g_tss;

static GDTPointer g_gdtp;

/* Build an 8-byte GDT descriptor */
static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    g_gdt[idx].limit_low       = limit & 0xFFFF;
    g_gdt[idx].base_low        = base  & 0xFFFF;
    g_gdt[idx].base_mid        = (base >> 16) & 0xFF;
    g_gdt[idx].access          = access;
    g_gdt[idx].flags_limit_high = ((flags & 0xF) << 4) | ((limit >> 16) & 0xF);
    g_gdt[idx].base_high       = (base >> 24) & 0xFF;
}

/* Build the 16-byte TSS system descriptor */
static void gdt_set_tss(uint64_t base, uint32_t limit) {
    GDTSystemEntry *e = &g_tss_entry;
    e->length     = (uint16_t)(limit & 0xFFFF);
    e->base_low   = (uint16_t)(base  & 0xFFFF);
    e->base_mid   = (uint8_t)((base  >> 16) & 0xFF);
    e->flags1     = 0x89;  /* present, DPL=0, type=9 (available 64-bit TSS) */
    e->flags2     = 0x00;
    e->base_high  = (uint8_t)((base  >> 24) & 0xFF);
    e->base_upper = (uint32_t)(base  >> 32);
    e->reserved   = 0;
    /* Copy into slots 5 and 6 of the GDT */
    memcpy(&g_gdt[5], e, sizeof(*e));
}

/* Defined in gdt_flush.asm */
extern void gdt_flush(GDTPointer *ptr);
extern void tss_flush(uint16_t selector);

void gdt_init(void)
{
    /*                idx  base  limit     access  flags
     * access byte:  P|DPL|S|Type
     *   0x9A = 1_00_1_1010 = present, DPL0, code, readable
     *   0x92 = 1_00_1_0010 = present, DPL0, data, writable
     *   0xFA = 1_11_1_1010 = present, DPL3, code, readable
     *   0xF2 = 1_11_1_0010 = present, DPL3, data, writable
     * flags nibble (high 4 bits of byte 6):
     *   0xA = G(1) + 64-bit(1) = granularity page, long mode code
     *   0xC = G(1) + D/B(1)    = granularity page, 32-bit data
     */
    gdt_set_entry(0, 0, 0,          0x00, 0x0); /* null          */
    gdt_set_entry(1, 0, 0xFFFFF,   0x9A, 0xA); /* kernel code   */
    gdt_set_entry(2, 0, 0xFFFFF,   0x92, 0xC); /* kernel data   */
    gdt_set_entry(3, 0, 0xFFFFF,   0xF2, 0xC); /* user data     */
    gdt_set_entry(4, 0, 0xFFFFF,   0xFA, 0xA); /* user code     */

    /* TSS */
    memset(&g_tss, 0, sizeof(g_tss));
    g_tss.iopb = sizeof(TSS);           /* IOPB beyond TSS = deny all */
    gdt_set_tss((uint64_t)&g_tss, sizeof(TSS) - 1);

    g_gdtp.limit = sizeof(g_gdt) - 1;
    g_gdtp.base  = (uint64_t)&g_gdt;

    gdt_flush(&g_gdtp);
    tss_flush(GDT_TSS_LOW);

    //kprintf("[GDT] Initialized (GDT=%p TSS=%p)\n", &g_gdt, &g_tss);
}

void gdt_set_kernel_stack(uint64_t rsp)
{
    g_tss.rsp0 = rsp;
}