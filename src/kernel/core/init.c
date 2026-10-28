#include "init.h"
#include "../../drivers/input/i2c_hid_touchpad/i2c_hid_touchpad.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/serial/serial.h"
#include "../../net/core/net_service.h"
#include "../../net/tests/qemu_network_test.h"
#include "../../userspace/userspace.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../smp/cpu.h"
#include "../smp/smp.h"

void init_process_start(void) {
  boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT,
                       "about to start init process");
  klog(KLOG_OK, "Booting init process...");

  scheduler_init();
  klogf(KLOG_INFO, "sched: %u CPUs online; enabling parallel scheduling",
        cpu_online_count());

  int32_t init_pid = process_spawn_module("/bin/init", "");
  if (init_pid != 1) {
    klogf(KLOG_ERROR, "process: /bin/init returned pid=%d; expected 1",
          init_pid);
    kernel_panic(init_pid < 0 ? process_last_spawn_error()
                              : "init started with an invalid PID");
  }
  klog(KLOG_OK, "process: /bin/init started as PID 1");

  serial_write_string("[INIT] PID 1 registered, initializing desktop\n");
  userspace_init();
  boot_diag_checkpoint(BOOT_STAGE_USERSPACE_RUN,
                       "init and desktop ready, starting scheduler");

  int32_t input_pid = process_spawn_service(
      userspace_input_thread, 0, "init-input", 1, -1,
      SCHED_QOS_USER_INTERACTIVE, 0);
  int32_t kbd_pid = process_spawn_service(
      userspace_keyboard_thread, 0, "desktop-keyboard", 1, -1,
      SCHED_QOS_USER_INTERACTIVE, 0);
  if (input_pid < 0 || kbd_pid < 0)
    klog(KLOG_WARN, "sched: failed to start input service processes");
  int32_t log_tid =
      scheduler_create_thread(userspace_log_thread, 0, "kernel-log", 3, -1);
  if (log_tid >= 0)
    scheduler_set_qos(log_tid, SCHED_QOS_BACKGROUND);
  (void)purec_i2c_hid_touchpad_start();
  int32_t net_pid = process_spawn_service(net_service_thread, 0, "net-rx", 2,
                                          -1, SCHED_QOS_UTILITY,
                                          PROCESS_CAP_NET_ADMIN);
  if (net_pid < 0)
    klog(KLOG_WARN, "net: failed to create service process");
  if (qemu_network_test_requested()) {
    int t = scheduler_create_thread(qemu_network_test_thread, 0,
                                    "qemu-net-test", 3, -1);
    if (t < 0)
      klog(KLOG_ERROR, "[NETTEST] RESULT FAIL stage=thread status=-1");
    else
      scheduler_set_qos(t, SCHED_QOS_BACKGROUND);
  }
  klog(KLOG_OK, "sched: init threads created, starting scheduler");
  smp_selftest_start();
  serial_write_string("[SCHED] start\n");
  scheduler_start();

  scheduler_idle_loop();
}
