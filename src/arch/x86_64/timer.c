#include <arch/x86_64/include/portio.h>
#include <arch/arch.h>
#include <drivers/interrupts/timer.h>
#include <kernel/process/scheduler.h>
#include <kernel/diagnostics/klog.h>

#define PIT_INPUT_HZ 1193182U
#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40

static volatile uint64_t tick_count;
static volatile uint64_t idle_tsc;
static uint32_t timer_frequency_hz=1000;
static uint64_t tsc_frequency_hz=3000000000ULL;
static uint64_t tsc_start;

static uint64_t tsc_milliseconds(void){
    uint64_t elapsed=arch_read_timestamp()-tsc_start;
    uint64_t seconds=elapsed/tsc_frequency_hz;
    uint64_t remainder=elapsed%tsc_frequency_hz;
    return seconds*1000+(remainder*1000)/tsc_frequency_hz;
}

void timer_init(uint32_t frequency_hz){
    {
        uint64_t hz=arch_cpuid_tsc_hz();
        if(hz) tsc_frequency_hz=hz;
    }
    tsc_start=arch_read_timestamp();
    if(frequency_hz<100) frequency_hz=100;
    if(frequency_hz>2000) frequency_hz=2000;
    uint32_t divisor=(PIT_INPUT_HZ+frequency_hz/2)/frequency_hz;
    if(divisor<1) divisor=1;
    if(divisor>0xFFFF) divisor=0xFFFF;
    timer_frequency_hz=PIT_INPUT_HZ/divisor;

    outb(PIT_COMMAND,0x36);
    outb(PIT_CHANNEL0,(uint8_t)divisor);
    outb(PIT_CHANNEL0,(uint8_t)(divisor>>8));
    uint8_t mask=inb(0x21);
    outb(0x21,(uint8_t)(mask&~1U));
    klogf(KLOG_OK,"timer: PIT=%u Hz TSC=%llu Hz monotonic fallback ready",
          timer_frequency_hz,tsc_frequency_hz);
}

void timer_tick(void){ tick_count++; }

void timer_sleep(uint32_t milliseconds){
    if(!milliseconds) return;
    bool irq_on=arch_irq_enabled();
    struct thread *thread=scheduler_current_thread();
    if(irq_on && scheduler_is_running() && thread && !thread->idle){
        scheduler_sleep(milliseconds);
        return;
    }
    uint64_t start=tsc_milliseconds();
    while(tsc_milliseconds()-start<milliseconds){
        uint64_t before=arch_read_timestamp();
        if(irq_on) arch_cpu_halt();
        else arch_cpu_relax();
        __atomic_fetch_add(&idle_tsc,arch_read_timestamp()-before,__ATOMIC_RELAXED);
    }
}

uint64_t timer_ticks(void){ return tsc_milliseconds(); }

uint64_t timer_idle_tsc(void){ return __atomic_load_n(&idle_tsc,__ATOMIC_RELAXED); }
