#include "../include/idt.h"
#include "../../../../drivers/serial/serial.h"
#include "../../../../drivers/display/fb.h"
#include "../../../../kernel/syscall/syscall.h"
#include "../../../../kernel/diagnostics/panic.h"
#include "../../../../drivers/interrupts/timer.h"
#include "../../../../kernel/process/scheduler.h"
#include "../../../../kernel/process/process.h"
#include "../../../../kernel/diagnostics/klog.h"
#include <stdint.h>
#include "../../../../kernel/smp/smp.h"
#include "../../../../kernel/smp/lapic.h"
#include "../../../../kernel/smp/cpu.h"
#include "../../gdt/include/gdt.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;
static struct idt_entry ap_idt[CPU_MAX_COUNT][256] __attribute__((aligned(16)));
static struct idt_ptr ap_idtp[CPU_MAX_COUNT];

extern void *isr_stub_table[256];
extern void idt_load(uint64_t);

void idt_set_gate(int n, uint64_t handler, uint8_t flags) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].ist         = 0;
    idt[n].type_attr   = flags;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

static void idt_set_ist(int n, uint8_t ist){
    idt[n].ist=ist&0x07;
}

struct isr_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
};

static inline void pic_eoi(uint8_t irq){
    if(irq>=8) __asm__ volatile("outb %0,%1"::"a"((uint8_t)0x20),"Nd"((uint16_t)0xA0));
    __asm__ volatile("outb %0,%1"::"a"((uint8_t)0x20),"Nd"((uint16_t)0x20));
}

static inline uint8_t pic_imr_read(uint16_t port){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));
    return v;
}

static inline void pic_imr_write(uint16_t port, uint8_t v){
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}

static void *irq_handler_fn[16] = {0};
static void *irq_handler_ctx[16] = {0};

void idt_set_irq_handler(uint8_t irq, void *fn, void *ctx){
    if(irq>=16) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    irq_handler_fn[irq]=fn;
    irq_handler_ctx[irq]=ctx;
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void idt_clear_irq_handler(uint8_t irq){
    if(irq>=16) return;
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    irq_handler_fn[irq]=0;
    irq_handler_ctx[irq]=0;
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void idt_unmask_irq(uint8_t irq){
    if(irq>=16) return;
    uint16_t port = irq<8 ? 0x21 : 0xA1;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    pic_imr_write(port, (uint8_t)(pic_imr_read(port) & ~bit));
}

void idt_mask_irq(uint8_t irq){
    if(irq>=16) return;
    uint16_t port = irq<8 ? 0x21 : 0xA1;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    pic_imr_write(port, (uint8_t)(pic_imr_read(port) | bit));
}
extern void ps2_mouse_handler(void);

void isr_handler(uint64_t vector, uint64_t err, uint64_t rip, uint64_t cs, uint64_t rflags, struct isr_regs *regs) {
    if(vector==2 && smp_handle_nmi()) return;
    if(vector==SMP_SPURIOUS_VECTOR) return;
    if(vector==SMP_RESCHEDULE_VECTOR || vector==SMP_TIMER_VECTOR){
        lapic_eoi();
        if(vector==SMP_TIMER_VECTOR) scheduler_on_timer_interrupt();
        else scheduler_on_reschedule_interrupt();
        return;
    }
    if (vector == 0x80) {
        scheduler_enter_kernel();
        int64_t ret = syscall_handler((struct syscall_regs*)regs);
        regs->rax = (uint64_t)ret;
        scheduler_leave_kernel();
        return;
    }
    if (vector == 44) {
        ps2_mouse_handler();
        pic_eoi(12);
        return;
    }
    if (vector == 32) {
        timer_tick();
        pic_eoi(0);
        scheduler_on_timer_interrupt();
        return;
    }
    if (vector >= 32 && vector < 48) {
        uint8_t irq = (uint8_t)(vector-32);
        if (irq < 16 && irq_handler_fn[irq]) {
            int (*fn)(void*) = (int(*)(void*))irq_handler_fn[irq];
            fn(irq_handler_ctx[irq]);
        }
        pic_eoi(irq);
        return;
    }
    if (vector == 3) {
        serial_write_string("[IDT] #BP self-test handled\n");
        return;
    }
    uint64_t cr2 = 0;
    if(vector == 14) __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    if((cs&3)==3 && process_current_is_user()){
        scheduler_enter_kernel();
        klogf(KLOG_ERROR,
              "process: pid=%d exception=%u rip=0x%llx cr2=0x%llx",
              process_current_pid(),(uint32_t)vector,rip,cr2);
        process_exit_current(128+(int32_t)vector);
    }
    kernel_panic_exception(vector, err, rip, cs, rflags, cr2,
                           (const struct panic_registers*)regs);
}

void idt_init(void) {
    for(int i=0;i<256;i++) {
        idt[i].offset_low=0; idt[i].selector=0; idt[i].ist=0;
        idt[i].type_attr=0; idt[i].offset_mid=0; idt[i].offset_high=0; idt[i].zero=0;
    }
    for(int i=0;i<256;i++) idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x8E);
    idt_set_ist(2,2);
    idt_set_ist(8,1);
    idt_set_ist(18,3);
    idt_set_gate(0x80, (uint64_t)isr_stub_table[0x80], 0xEE);
    idtp.limit = sizeof(idt)-1;
    idtp.base  = (uint64_t)&idt;
    idt_load((uint64_t)&idtp);
}

void idt_init_cpu(void) {
    uint32_t id=gdt_current_cpu_id();
    if(id==0 || id>=CPU_MAX_COUNT){ idt_load((uint64_t)&idtp); return; }
    for(unsigned n=0;n<256;n++) ap_idt[id][n]=idt[n];
    ap_idtp[id].limit=sizeof(ap_idt[id])-1;
    ap_idtp[id].base=(uint64_t)ap_idt[id];
    idt_load((uint64_t)&ap_idtp[id]);
}
