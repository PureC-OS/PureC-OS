#include "../include/gdt.h"
#include "../../../../kernel/smp/smp.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high_flags;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_table {
    struct gdt_entry entries[5];
    struct tss_descriptor tss;
} __attribute__((packed));

#define IST_STACK_SIZE 16384

static struct gdt_table gdt_tables[SMP_MAX_CPUS];
static struct tss_entry tss_entries[SMP_MAX_CPUS];
static uint8_t ist_stacks[SMP_MAX_CPUS][3][IST_STACK_SIZE]
    __attribute__((aligned(16)));
static struct gdt_ptr gdt_ptrs[SMP_MAX_CPUS];

extern void gdt_flush(uint64_t);

static void gdt_setup(uint32_t cpu){
    struct gdt_table *gdt = &gdt_tables[cpu];
    struct tss_entry *tss = &tss_entries[cpu];
    gdt->entries[0] = (struct gdt_entry){0,0,0,0,0,0};
    gdt->entries[1] = (struct gdt_entry){0,0,0,0x9A,0xA0,0};
    gdt->entries[2] = (struct gdt_entry){0,0,0,0x92,0x00,0};
    gdt->entries[3] = (struct gdt_entry){0,0,0,0xFA,0xA0,0};
    gdt->entries[4] = (struct gdt_entry){0,0,0,0xF2,0x00,0};

    tss->ist1=(uint64_t)(uintptr_t)&ist_stacks[cpu][0][IST_STACK_SIZE];
    tss->ist2=(uint64_t)(uintptr_t)&ist_stacks[cpu][1][IST_STACK_SIZE];
    tss->ist3=(uint64_t)(uintptr_t)&ist_stacks[cpu][2][IST_STACK_SIZE];
    tss->iomap_base=sizeof(*tss);

    uint64_t tss_base=(uint64_t)(uintptr_t)tss;
    uint32_t tss_limit=sizeof(*tss)-1;
    gdt->tss.limit_low=(uint16_t)tss_limit;
    gdt->tss.base_low=(uint16_t)tss_base;
    gdt->tss.base_mid=(uint8_t)(tss_base>>16);
    gdt->tss.access=0x89;
    gdt->tss.limit_high_flags=(uint8_t)((tss_limit>>16)&0x0F);
    gdt->tss.base_high=(uint8_t)(tss_base>>24);
    gdt->tss.base_upper=(uint32_t)(tss_base>>32);
    gdt->tss.reserved=0;

    gdt_ptrs[cpu].limit = sizeof(*gdt) - 1;
    gdt_ptrs[cpu].base  = (uint64_t)gdt;

    gdt_flush((uint64_t)&gdt_ptrs[cpu]);
    __asm__ volatile("mov $0x28, %%ax; ltr %%ax" ::: "rax", "memory");
}

void gdt_init(void) {
    gdt_setup(0);
}

void gdt_install_cpu(uint32_t cpu){
    if(cpu >= SMP_MAX_CPUS) return;
    gdt_setup(cpu);
}

void gdt_set_kernel_stack(uint64_t stack_top){
    uint32_t cpu = smp_cpu_index();
    if(cpu >= SMP_MAX_CPUS) cpu = 0;
    tss_entries[cpu].rsp0=stack_top;
}
