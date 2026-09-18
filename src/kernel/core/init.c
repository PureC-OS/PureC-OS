#include "init.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../process/scheduler.h"
#include "../process/process.h"
#include "../smp/cpu.h"
#include "../smp/smp.h"
#include "../../drivers/serial/serial.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../userspace/userspace.h"
#include "../../net/core/net_service.h"
#include "../../net/tests/qemu_network_test.h"

void init_process_start(void){
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "about to start init process");
    klog(KLOG_OK, "Booting init process...");

    scheduler_init();
    klogf(KLOG_INFO,"sched: %u CPUs online; enabling parallel scheduling",
          cpu_online_count());

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
    if(qemu_network_test_requested()
       && scheduler_create_thread(qemu_network_test_thread,0,
                                  "qemu-net-test",3,0)<0)
        klog(KLOG_ERROR,"[NETTEST] RESULT FAIL stage=thread status=-1");
    klog(KLOG_OK, "sched: init threads created, starting scheduler");
    smp_selftest_start();
    serial_write_string("[SCHED] start\n");
    scheduler_start();

    scheduler_idle_loop();
}