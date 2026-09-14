#include "init.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../process/scheduler.h"
#include "../process/process.h"
#include "../../drivers/serial/serial.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../userspace/userspace.h"
#include "../../net/core/net_service.h"

static void boot_log_pause(void){
    volatile uint64_t dummy=0;
    for(uint64_t i=0;i<30000000ULL;i++){
        __asm__ volatile("pause");
        dummy+=i;
    }
    (void)dummy;
}

void init_process_start(uint32_t detected_cpu_count){
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "about to start init process");
    klog(KLOG_OK, "Booting init process...");
    boot_log_pause();

    scheduler_init();
    int core_count=scheduler_get_core_count();
    if(detected_cpu_count>1){
        klogf(KLOG_WARN,
              "sched: Limine detected %u CPUs; 1 CPU active until AP scheduler support is installed",
              detected_cpu_count);
    }
    klogf(KLOG_INFO, "sched: active cores=%d, creating init threads",
          core_count);

    int32_t init_pid=process_spawn_module("/bin/init","");
    if(init_pid!=1) kernel_panic("cannot start /bin/init as PID 1");
    klog(KLOG_OK,"process: /bin/init started as PID 1");

    serial_write_string("[INIT] PID 1 registered, initializing desktop\n");
    userspace_init();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_RUN,
                         "init and desktop ready, starting scheduler");

    scheduler_create_thread(userspace_input_thread, 0, "init-input", 1, 0);
    scheduler_create_thread(userspace_keyboard_thread, 0, "desktop-keyboard", 1, 0);
    scheduler_create_thread(userspace_log_thread, 0, "kernel-log", 3, 0);
    if(scheduler_create_thread(net_service_thread,0,"net-rx",2,0)<0)
        klog(KLOG_WARN,"net: failed to create polling thread");
    klog(KLOG_OK, "sched: init threads created, starting scheduler");
    serial_write_string("[SCHED] start\n");
    scheduler_start();

    for(;;){
        ps2_mouse_poll();
        usb_mouse_poll();
        scheduler_yield();
    }
}
