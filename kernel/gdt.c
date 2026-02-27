#include "gdt.h"

#include "klog.h"
#include "utils.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;
static struct tss_entry g_tss;

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (uint16_t)(base & 0xFFFFU);
    gdt_entries[num].base_mid    = (uint8_t)((base >> 16) & 0xFFU);
    gdt_entries[num].base_high   = (uint8_t)((base >> 24) & 0xFFU);
    gdt_entries[num].limit_low   = (uint16_t)(limit & 0xFFFFU);
    gdt_entries[num].granularity = (uint8_t)(((limit >> 16) & 0x0FU) | (gran & 0xF0U));
    gdt_entries[num].access      = access;
}

static void gdt_write_tss(int num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)(uintptr_t)&g_tss;
    uint32_t limit = sizeof(g_tss) - 1U;

    /* 0x89: present | ring0 | 32-bit available TSS */
    gdt_set_gate(num, base, limit, 0x89, 0x00);
    memset(&g_tss, 0, sizeof(g_tss));

    g_tss.ss0 = ss0;
    g_tss.esp0 = esp0;
    g_tss.cs = GDT_SEL_USER_CODE_R3;
    g_tss.ss = GDT_SEL_USER_DATA_R3;
    g_tss.ds = GDT_SEL_USER_DATA_R3;
    g_tss.es = GDT_SEL_USER_DATA_R3;
    g_tss.fs = GDT_SEL_USER_DATA_R3;
    g_tss.gs = GDT_SEL_USER_DATA_R3;
    g_tss.iomap_base = sizeof(g_tss);
}

void tss_set_kernel_stack(uint32_t esp0) {
    g_tss.esp0 = esp0;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_entries) - 1U;
    gdt_ptr.base = (uint32_t)(uintptr_t)&gdt_entries;

    /* Null descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);
    /* Kernel code/data: flat 4GiB */
    gdt_set_gate(1, 0, 0xFFFFFFFFU, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFFU, 0x92, 0xCF);
    /* User code/data: flat 4GiB, DPL=3 */
    gdt_set_gate(3, 0, 0xFFFFFFFFU, 0xFA, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFFU, 0xF2, 0xCF);
    /* TSS */
    gdt_write_tss(5, GDT_SEL_KERNEL_DATA, 0);

    gdt_flush((uint32_t)(uintptr_t)&gdt_ptr);
    tss_flush();
    KLOGI("gdt: initialized (kernel/user segments + tss)");
}
