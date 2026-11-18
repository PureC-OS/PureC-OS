#include "../../fs/vfs.h"
#include <acpi/acpi.h>
#include "../../drivers/net/iwx/iwx.h"
#include "../../io/swap.h"
#include "init.h"
#include "../../drivers/input/i2c_hid_touchpad/i2c_hid_touchpad.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/serial/serial.h"
#include "../../net/core/net_service.h"
#include "../../net/tests/qemu_network_test.h"
#include "../../boot/boot_options.h"
#include "../diagnostics/klog_disk.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/journal.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../smp/cpu.h"
#include "../smp/smp.h"
#include "../tty/vtcon.h"

void init_process_start(void)
{
  boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT,
                       "about to start init process");
  klog(KLOG_OK, "Booting init process...");

  scheduler_init();
  klogf(KLOG_INFO, "sched: %u CPUs online; enabling parallel scheduling",
        cpu_online_count());

  int32_t init_pid = process_spawn_module("/bin/init", "");
  if (init_pid != 1)
  {
    klogf(KLOG_ERROR, "process: /bin/init returned pid=%d; expected 1",
          init_pid);
    kernel_panic(init_pid < 0 ? process_last_spawn_error()
                              : "init started with an invalid PID");
  }
  klog(KLOG_OK, "process: /bin/init started as PID 1");

  if (!vtcon_ready())
    klog(KLOG_WARN, "vtcon: no framebuffer text console");
  vtcon_start_thread();
  boot_diag_checkpoint(BOOT_STAGE_USERSPACE_RUN,
                       "init and consoles ready, starting scheduler");

  int32_t log_tid =
      scheduler_create_thread(klog_disk_thread, 0, "kernel-log", 3, -1);
  if (log_tid >= 0)
    scheduler_set_qos(log_tid, SCHED_QOS_BACKGROUND);
  journal_start();
  vfs_writeback_start();
  (void)purec_i2c_hid_touchpad_start();
  acpi_ec_start_event_thread();
  (void)iwx_start();
  int32_t net_pid = process_spawn_service(net_service_thread, 0, "net-rx", 2,
                                          -1, SCHED_QOS_UTILITY,
                                          PROCESS_CAP_NET_ADMIN);
  if (net_pid < 0)
    klog(KLOG_WARN, "net: failed to create service process");
  if (qemu_network_test_requested())
  {
    int t = scheduler_create_thread(qemu_network_test_thread, 0,
                                    "qemu-net-test", 3, -1);
    if (t < 0)
      klog(KLOG_ERROR, "[NETTEST] RESULT FAIL stage=thread status=-1");
    else
      scheduler_set_qos(t, SCHED_QOS_BACKGROUND);
  }
  swap_daemon_start();
  klog(KLOG_OK, "sched: init threads created, starting scheduler");
  serial_write_string("[SCHED] start\n");
  scheduler_start();

  scheduler_idle_loop();
}
