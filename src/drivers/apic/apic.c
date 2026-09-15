#include "apic.h"
#include "arch/x86_64/mmio.h"
#include "drivers/interrupts/timer.h"
#include "kernel/diagnostics/klog.h"
#include <stddef.h>
#include <stdint.h>

#define APIC_MSR_BASE 0x1B
#define APIC_MSR_ENABLE (1ULL << 11)
#define APIC_MSR_X2APIC (1ULL << 10)

#define LAPIC_REG_ID 0x20
#define LAPIC_REG_EOI 0xB0
#define LAPIC_REG_SPURIOUS 0xF0
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_INITIAL_COUNT 0x380
#define LAPIC_REG_CURRENT_COUNT 0x390
#define LAPIC_REG_DIVIDE 0x3E0

#define LAPIC_SPURIOUS_ENABLE 0x100
#define LAPIC_LVT_PERIODIC (1U << 17)
#define LAPIC_LVT_MASKED (1U << 16)
#define LAPIC_DIVIDE_16 0x3

static volatile uint32_t *lapic = NULL;
static uint32_t timer_ticks_1ms = 0;
static bool ready = false;
static volatile uint64_t test_ticks = 0;

void apic_test_tick(void){
    test_ticks++;
}

uint64_t apic_test_ticks(void){
    return test_ticks;
}

bool apic_wait_tick(uint32_t timeout_ms){
    uint64_t deadline = timer_ticks() + timeout_ms;
    while(test_ticks == 0 && timer_ticks() < deadline)
        __asm__ volatile("pause");
    return test_ticks != 0;
}

static uint64_t read_msr(uint32_t msr){
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value){
    __asm__ volatile("wrmsr" :: "c"(msr),
                     "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t lapic_read(uint32_t offset){
    return lapic[offset / 4];
}

static void lapic_write(uint32_t offset, uint32_t value){
    lapic[offset / 4] = value;
}

void apic_disable(void){
    if(lapic){
        lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);
        lapic_write(LAPIC_REG_INITIAL_COUNT, 0);
    }
    ready = false;
}

bool apic_present(void){
    return ready;
}

uint32_t apic_lapic_id(void){
    if(!lapic) return 0;
    return lapic_read(LAPIC_REG_ID) >> 24;
}

uint32_t apic_raw_lapic_id(void){
    if(!lapic) return 0xFFFFFFFFU;
    return lapic[LAPIC_REG_ID / 4] >> 24;
}

void apic_eoi(void){
    if(lapic) lapic_write(LAPIC_REG_EOI, 0);
}

void apic_mask_pit_timer(void){
    outb(0x21, (uint8_t)(inb(0x21) | 1U));
}

static void timer_start(void){
    lapic_write(LAPIC_REG_DIVIDE, LAPIC_DIVIDE_16);
    lapic_write(LAPIC_REG_LVT_TIMER,
                LAPIC_TIMER_VECTOR | LAPIC_LVT_PERIODIC);
    lapic_write(LAPIC_REG_INITIAL_COUNT, timer_ticks_1ms);
}

void apic_cpu_enable(void){
    if(!lapic) return;
    lapic_write(LAPIC_REG_SPURIOUS,
                LAPIC_SPURIOUS_VECTOR | LAPIC_SPURIOUS_ENABLE);
    if(timer_ticks_1ms) timer_start();
}

bool apic_init(void){
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    if(!(edx & (1U << 9))){
        klog(KLOG_WARN, "apic: no local APIC, staying single-CPU");
        return false;
    }
    uint64_t base = read_msr(APIC_MSR_BASE);
    if(base & APIC_MSR_X2APIC){
        write_msr(APIC_MSR_BASE, base & ~(APIC_MSR_ENABLE | APIC_MSR_X2APIC));
        base = read_msr(APIC_MSR_BASE);
    }
    base |= APIC_MSR_ENABLE;
    write_msr(APIC_MSR_BASE, base);
    uint64_t lapic_physical = base & 0xFFFFFF000ULL;
    lapic = mmio_map(lapic_physical, 0x1000);
    if(!lapic){
        klog(KLOG_ERROR, "apic: cannot map LAPIC registers");
        return false;
    }
    if(lapic_read(LAPIC_REG_SPURIOUS) == 0xFFFFFFFFU){
        klog(KLOG_ERROR, "apic: LAPIC MMIO readback failed");
        lapic = NULL;
        return false;
    }
    lapic_write(LAPIC_REG_SPURIOUS,
                LAPIC_SPURIOUS_VECTOR | LAPIC_SPURIOUS_ENABLE);
    lapic_write(LAPIC_REG_DIVIDE, LAPIC_DIVIDE_16);
    lapic_write(LAPIC_REG_LVT_TIMER,
                LAPIC_TIMER_VECTOR | LAPIC_LVT_MASKED);
    lapic_write(LAPIC_REG_INITIAL_COUNT, 0xFFFFFFFFU);
    timer_sleep(10);
    uint32_t elapsed = 0xFFFFFFFFU - lapic_read(LAPIC_REG_CURRENT_COUNT);
    timer_ticks_1ms = elapsed / 10;
    if(!timer_ticks_1ms){
        klog(KLOG_ERROR, "apic: timer calibration failed");
        return false;
    }
    timer_start();
    ready = true;
    klogf(KLOG_OK, "apic: LAPIC id=%u timer=%u ticks/ms",
          apic_lapic_id(), timer_ticks_1ms);
    return true;
}
