#include "kernel.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/serial/serial.h"
#include "../syscall/syscall.h"
#include "../diagnostics/klog.h"
#include "system_info.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/panic.h"
#include "../../arch/x86_64/mmio.h"
#include "../../arch/x86_64/fpu.h"
#include "../../lib/string.h"
#include "init.h"
#include "../process/process.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../net/core/net_service.h"
extern struct limine_memmap_response *memmap_response_ptr;
extern struct limine_smp_response *smp_response_ptr;
extern uint64_t hhdm_offset_global;
#include <stdint.h>

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "r10","r8","memory");
    return ret;
}

void kernel_main(struct limine_framebuffer *fb) {
    (void)fb;
    klog(KLOG_INFO, "Entering kernel_main...");
    klog(KLOG_INFO, "Kernel: 64-bit long mode, GOP active");

    boot_diag_checkpoint(BOOT_STAGE_SYSTEM_INFO, "probing CPU and memory map");
    system_info_init(memmap_response_ptr);
    if(system_info_usable_ram_bytes()==0)
        kernel_panic("memory map contains no usable RAM");
    klogf(KLOG_OK, "CPU detected: %s", system_info_cpu_name());
    klogf(KLOG_OK, "Usable RAM: %lu MB", system_info_usable_ram_bytes() / (1024 * 1024));
    klogf(KLOG_INFO, "Framebuffer: %dx%d", gop_get_width(), gop_get_height());

    boot_diag_checkpoint(BOOT_STAGE_SYSTEM_INFO,"initializing physical memory");
    pmm_init(memmap_response_ptr,hhdm_offset_global);
    if(!pmm_is_ready()) kernel_panic("physical memory manager initialization failed");
    vmm_init();
    fpu_init();
    process_init();

    // GDT/IDT уже настроены в boot.c, но проверяем инт3 как linux-like selftest
    klog(KLOG_INFO, "Testing IDT: int3 breakpoint...");
    __asm__ volatile("int3");
    klog(KLOG_OK, "int3 handled, IDT working");

    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "initializing syscall layer");
    klog(KLOG_INFO, "Initializing PCI MMIO mapper...");
    mmio_init();
    if(mmio_is_ready()){
        klog(KLOG_OK, "PCI MMIO mapper ready (uncached multi-page mappings)");
    } else {
        klog(KLOG_WARN, "PCI MMIO mapper unavailable; AHCI/xHCI/EHCI disabled");
    }
    syscall_init();
    klog(KLOG_OK, "Syscall int 0x80 ready");
    (void)net_service_init();

    const char *msg="Hello from syscall (Limine)!\n";
    klog(KLOG_INFO, "Testing syscall WRITE...");
    do_syscall(SYS_WRITE, (uint64_t)msg, strlen(msg), 1,0,0);
    klog(KLOG_OK, "syscall WRITE done");

    // GOP тест откладываем в userspace чтобы не перекрывать boot log
    klog(KLOG_INFO, "GOP test deferred to userspace");

    klog(KLOG_INFO, "Boot log complete");
    klogf(KLOG_DEBUG, "Verbose mode: %s (debug visible)", klog_is_verbose() ? "on" : "off");
    init_process_start(smp_response_ptr ? smp_response_ptr->cpu_count : 1);
}
