#include <arch/arch.h>
#include <kernel/ipc/pty.h>
#include <io/io_sched.h>
#include <kernel/syscall/syscall.h>
#include <kernel/tty/vtcon.h>
#include <drivers/audio/audio.h>
#include <drivers/display/gop.h>
#include <drivers/display/svga.h>
#include <drivers/display/svga3d.h>
#include <drivers/display/vga.h>
#include <drivers/input/keyboard.h>
#include <drivers/interrupts/timer.h>
#include <drivers/mouse/ps2_mouse.h>
#include <drivers/mouse/usb_mouse.h>
#include <drivers/power/power.h>
#include <drivers/serial/serial.h>
#include <io/storage/ahci.h>
#include <io/storage/block_device.h>
#include <io/storage/storage_probe.h>
#include <drivers/usb/ehci.h>
#include <drivers/usb/xhci.h>
#include <fs/install.h>
#include <fs/install_accounts.h>
#include <fs/vfs.h>
#include <fs/vfs_fat32.h>
#include <fs/vfs_path.h>
#include <drivers/display/display_owner.h>
#include <kernel/ipc/inputdev.h>
#include <kernel/syscall/sys/user/sudo.h>
#include <kernel/trace/trace.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <mm/shm.h>
#include <kernel/random/krandom.h>
#include <kernel/time/wallclock.h>
#include <net/api/ping.h>
#include <net/api/socket.h>
#include <net/api/sockstat.h>
#include <net/config/dhcp.h>
#include <net/core/net_device.h>
#include <net/name/dns.h>
#include <net/network/ipv4.h>
#include <net/transport/tcp.h>
#include <net/wifi/wifi.h>
#include <kernel/core/system_info.h>
#include <kernel/diagnostics/boot_diag.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/diagnostics/panic.h>
#include <kernel/ipc/evfd.h>
#include <kernel/ipc/ipc.h>
#include <kernel/ipc/unix.h>
#include <kernel/process/process.h>
#include <kernel/process/signal.h>
#include <kernel/process/futex.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/ofd.h>
#include <kernel/process/scheduler.h>
#include <kernel/smp/cpu.h>
#include <kernel/tty/tty.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../common.h"

long sys_time_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, sizeof(struct time_info)))
    return -1;
  struct time_info *info = (struct time_info *)(uintptr_t)a1;
  int64_t ms = wallclock_unix_ms();
  info->unix_seconds = ms / 1000;
  info->millis = (uint32_t)(ms % 1000);
  info->tz_minutes = wallclock_tz_minutes();
  info->uptime_ms = system_info_uptime_ms();
  info->source = wallclock_source();
  info->reserved = 0;
  return 0;
}

long sys_time_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (process_current_uid() != 0)
    return -1;
  return wallclock_set((int64_t)a1, (int32_t)a2);
}

long sys_time_zone(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (a1 == TIME_ZONE_GET)
  {
    char *out = (char *)(uintptr_t)a2;
    uint32_t cap = (uint32_t)a3;
    if (cap == 0 || cap > 256 || !writable(out, cap))
      return -1;
    const char *name = wallclock_zone_name();
    uint32_t n = 0;
    while (name[n] && n + 1 < cap)
    {
      out[n] = name[n];
      n++;
    }
    out[n] = 0;
    return (int64_t)n;
  }
  if (a1 != TIME_ZONE_SET || process_current_uid() != 0 ||
      !readable_string((const char *)(uintptr_t)a2))
    return -1;
  char name[64];
  strncpy(name, (const char *)(uintptr_t)a2, sizeof(name) - 1);
  name[sizeof(name) - 1] = 0;
  if (!wallclock_set_zone(name))
    return -1;
  if (!filesystem_syscall_acquire())
    return 0;
  if (vfs_is_root_mounted())
  {
    char line[66];
    uint32_t n = (uint32_t)strlen(name);
    memcpy(line, name, n);
    line[n++] = '\n';
    if (vfs_write_file(WALLCLOCK_ZONE_PATH, line, n) < 0)
      klog(KLOG_WARN, "time: cannot write " WALLCLOCK_ZONE_PATH);
  }
  filesystem_syscall_unlock();
  return 0;
}

long sys_clock_adjust(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if (process_current_uid() != 0 && !process_has_capability(PROCESS_CAP_SYS_TIME))
    return -KERR_PERM;
  int64_t *previous = (int64_t *)(uintptr_t)a3;
  if (previous && !writable(previous, sizeof(*previous)))
    return -KERR_FAULT;
  int64_t old = wallclock_slew_remaining_ms();
  int32_t status = 0;
  switch (a1)
  {
  case CLOCK_ADJ_STEP:
    status = wallclock_step_ms((int64_t)a2);
    if (status == 0)
      klogf(KLOG_OK, "time: clock stepped by %lld ms", (long long)(int64_t)a2);
    break;
  case CLOCK_ADJ_SLEW:
    status = wallclock_slew_ms((int64_t)a2, &old);
    break;
  case CLOCK_ADJ_QUERY:
    break;
  case CLOCK_ADJ_SAVE_RTC:
    status = wallclock_store_rtc() ? 0 : -KERR_IO;
    break;
  default:
    return -KERR_INVAL;
  }
  if (status < 0)
    return status == -1 ? -KERR_INVAL : status;
  if (previous)
    *previous = old;
  return 0;
}

long sys_getrandom(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a2 > 0x100000 || !writable((void *)(uintptr_t)a1, a2))
    return -1;
  krandom_fill((void *)(uintptr_t)a1, (uint32_t)a2);
  return (int64_t)a2;
}

long sys_cpu_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct cpu_monitor_info *info = (struct cpu_monitor_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  memset(info, 0, sizeof(*info));
  strncpy(info->name, system_info_cpu_name(), sizeof(info->name) - 1);
  info->logical_processors = system_info_logical_processors();
  info->usage_percent = system_info_cpu_usage_percent();
  info->frequency_hz = system_info_tsc_frequency_hz();
  info->uptime_ms = system_info_uptime_ms();
  return 0;
}

long sys_cpu_core_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct cpu_core_info *info = (struct cpu_core_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  memset(info, 0, sizeof(*info));
  uint32_t registered = cpu_registered_count();
  if (registered > CPU_CORE_MAX_COUNT)
    registered = CPU_CORE_MAX_COUNT;
  info->count = registered;
  for (uint32_t i = 0; i < registered; i++)
  {
    info->cores[i].id = i;
    info->cores[i].online = cpu_is_online(i) ? 1u : 0u;
    uint64_t total = 0, idle = 0;
    (void)scheduler_cpu_ticks(i, &total, &idle);
    info->cores[i].total_ticks = total;
    info->cores[i].idle_ticks = idle;
  }
  return 0;
}

long sys_memory_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct memory_monitor_info *info =
      (struct memory_monitor_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  info->total_bytes = system_info_total_ram_bytes();
  info->available_bytes = pmm_free_bytes();
  info->used_bytes = info->total_bytes > info->available_bytes
                         ? info->total_bytes - info->available_bytes
                         : 0;
  info->framebuffer_bytes = gop_get_framebuffer_size_bytes();
  return 0;
}

long sys_battery_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct battery_info *out = (struct battery_info *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  return power_battery_get(out) ? 0 : -1;
}

long sys_thermal_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct thermal_info *out = (struct thermal_info *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  return power_thermal_get(out) ? 0 : -1;
}

long sys_reboot(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  power_reboot();
  return 0;
}

long sys_shutdown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  power_shutdown();
  return 0;
}

long sys_klog_write(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  const char *message = (const char *)(uintptr_t)a2;
  if (!readable_string(message))
    return -KERR_FAULT;
  char text[240];
  uint32_t n = 0;
  while (message[n] && n + 1 < sizeof(text))
  {
    char c = message[n];
    text[n++] = (c == '\n' || c == '\r') ? ' ' : c;
  }
  while (n && text[n - 1] == ' ')
    n--;
  text[n] = '\0';
  static const enum klog_level levels[4] = {KLOG_INFO, KLOG_OK, KLOG_WARN, KLOG_ERROR};
  struct process *self = process_current();
  klogf(levels[a1 < 4 ? a1 : 0], "%s[%d]: %s", self ? self->name : "?",
        process_current_pid(), text);
  return 0;
}

long sys_usb_rescan(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint32_t count = block_device_rescan_usb();
  struct xhci_probe_stats xhci_stats;
  struct ehci_probe_stats ehci_stats;
  xhci_get_probe_stats(&xhci_stats);
  ehci_get_probe_stats(&ehci_stats);
  struct usb_scan_status *status = (struct usb_scan_status *)(uintptr_t)a1;
  if (status && !writable(status, sizeof(*status)))
    return -1;
  if (status)
  {
    status->xhci_controllers = xhci_stats.controllers;
    status->xhci_connected_ports = xhci_stats.connected_ports;
    status->xhci_addressed_devices = xhci_stats.addressed_devices;
    status->xhci_disks = xhci_stats.mass_storage_devices;
    status->xhci_hid_mice = xhci_stats.hid_mice;
    status->xhci_hid_interfaces = xhci_stats.hid_interfaces;
    status->xhci_hubs = xhci_stats.hubs;
    status->xhci_mouse_transfer_errors = xhci_stats.mouse_transfer_errors;
    status->xhci_failures = xhci_stats.failures;
    status->xhci_stage = xhci_stats.last_stage;
    status->xhci_error = xhci_stats.last_error;
    status->xhci_last_port = xhci_stats.last_port;
    status->xhci_portsc = xhci_stats.last_portsc;
    status->xhci_completion_code = xhci_stats.last_completion_code;
    status->xhci_max_ports = xhci_stats.max_ports;
    status->xhci_usb_status = xhci_stats.usb_status;
    status->xhci_scratchpad_count = xhci_stats.scratchpad_count;
    status->ehci_connected_ports = ehci_stats.connected_ports;
    status->ehci_high_speed_ports = ehci_stats.high_speed_ports;
    status->ehci_disks = ehci_stats.mass_storage_devices;
    status->ehci_failures = ehci_stats.failures;
    status->ehci_stage = ehci_stats.last_stage;
  }
  klogf(KLOG_INFO,
        "usb-rescan: xhci connected=%u addressed=%u disks=%u failures=%u "
        "stage=%u",
        xhci_stats.connected_ports, xhci_stats.addressed_devices,
        xhci_stats.mass_storage_devices, xhci_stats.failures,
        xhci_stats.last_stage);
  klogf(KLOG_INFO,
        "usb-rescan: xhci error=%u scratchpads=%u port=%u portsc=0x%x "
        "completion=%u usbsts=0x%x",
        xhci_stats.last_error, xhci_stats.scratchpad_count,
        xhci_stats.last_port, xhci_stats.last_portsc,
        xhci_stats.last_completion_code, xhci_stats.usb_status);
  klogf(KLOG_INFO,
        "usb-rescan: ehci connected=%u high-speed=%u disks=%u failures=%u "
        "stage=%u",
        ehci_stats.connected_ports, ehci_stats.high_speed_ports,
        ehci_stats.mass_storage_devices, ehci_stats.failures,
        ehci_stats.last_stage);
  if (count)
  {
    if (!filesystem_syscall_acquire())
    return -1;
    if (!vfs_is_root_mounted() && vfs_mount_root())
      wallclock_load_zone();
    filesystem_syscall_unlock();
  }
  return count;
}
