#include "../include/gdt.h"
#include "../../../../kernel/smp/cpu.h"
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

#define EMERGENCY_STACK_SIZE 16384

struct cpu_gdt {
    struct gdt_table gdt;
    struct tss_entry tss;
    struct gdt_ptr gp;
    uint8_t double_fault_stack[EMERGENCY_STACK_SIZE] __attribute__((aligned(16)));
    uint8_t nmi_stack[EMERGENCY_STACK_SIZE] __attribute__((aligned(16)));
    uint8_t machine_check_stack[EMERGENCY_STACK_SIZE] __attribute__((aligned(16)));
};
static struct cpu_gdt cpu_tables[CPU_MAX_COUNT] __attribute__((aligned(64)));

extern void gdt_flush(uint64_t);

bool gdt_init_cpu(uint32_t cpu_id, uint64_t stack_top) {
    if(cpu_id >= CPU_MAX_COUNT) return false;
    struct cpu_gdt *cpu = &cpu_tables[cpu_id];
    struct gdt_table *gdt = &cpu->gdt;
    struct tss_entry *tss = &cpu->tss;
    gdt->entries[0] = (struct gdt_entry){0,0,0,0,0,0};
    gdt->entries[1] = (struct gdt_entry){0,0,0,0x9A,0xA0,0};
    gdt->entries[2] = (struct gdt_entry){0,0,0,0x92,0x00,0};
    gdt->entries[3] = (struct gdt_entry){0,0,0,0xFA,0xA0,0};
    gdt->entries[4] = (struct gdt_entry){0,0,0,0xF2,0x00,0};
    *tss = (struct tss_entry){0};
    tss->rsp0=stack_top;
    tss->ist1=(uint64_t)(uintptr_t)&cpu->double_fault_stack[EMERGENCY_STACK_SIZE];
    tss->ist2=(uint64_t)(uintptr_t)&cpu->nmi_stack[EMERGENCY_STACK_SIZE];
    tss->ist3=(uint64_t)(uintptr_t)&cpu->machine_check_stack[EMERGENCY_STACK_SIZE];
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
    cpu->gp.limit = sizeof(*gdt) - 1;
    cpu->gp.base  = (uint64_t)gdt;
    gdt_flush((uint64_t)&cpu->gp);
    __asm__ volatile("mov $0x28, %%ax; ltr %%ax" ::: "rax", "memory");
    return true;
}
void gdt_init(void) {
    (void)gdt_init_cpu(0, 0);
}
uint32_t gdt_current_cpu_id(void){
    /* GDTR is CPU-local and unchanged by ring transitions. This avoids a GS
       dependency until all user/interrupt entry paths support swapgs. */
    struct gdt_ptr gp;
    __asm__ volatile("sgdt %0" : "=m"(gp));
    for(uint32_t id = 0; id < CPU_MAX_COUNT; id++){
        if(gp.base == (uint64_t)(uintptr_t)&cpu_tables[id].gdt)
            return id;
    }
    return UINT32_MAX;
}

void gdt_set_kernel_stack(uint64_t stack_top){
    uint32_t id = gdt_current_cpu_id();
    if(id < CPU_MAX_COUNT) cpu_tables[id].tss.rsp0=stack_top;
}
