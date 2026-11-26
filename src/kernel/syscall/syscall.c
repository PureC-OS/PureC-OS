#include <arch/arch.h>
#include <drivers/audio/audio.h>
#include <io/storage/ahci.h>
#include <io/storage/block_device.h>
#include <io/storage/storage_probe.h>
#include <drivers/usb/ehci.h>
#include <drivers/usb/xhci.h>
#include <fs/vfs.h>
#include <fs/vfs_fat32.h>
#include <kernel/trace/trace.h>
#include <lib/string.h>
#include <kernel/time/wallclock.h>
#include <kernel/diagnostics/boot_diag.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/process/process.h>
#include "sys/common.h"

int64_t syscall_kernel_invoke(uint64_t n, uint64_t a1, uint64_t a2,
                              uint64_t a3, uint64_t a4, uint64_t a5)
{
  struct syscall_regs r;
  memset(&r, 0, sizeof(r));
  r.rax = n;
  r.rdi = a1;
  r.rsi = a2;
  r.rdx = a3;
  r.r10 = a4;
  r.r8 = a5;
  r.vector = 0x80;
  r.cs = ARCH_KERNEL_CS;
  r.ss = ARCH_KERNEL_DS;
  uint64_t flags = arch_irq_save();
  int64_t ret = syscall_handler(&r);
  arch_irq_restore(flags);
  return ret;
}

int64_t syscall_handler(struct syscall_regs *r)
{
  struct process *p = process_current();
  if (!p || !p->trace_slot)
    return do_syscall_64(r);
  uint64_t nr = r->rax;
  uint64_t args[6] = {r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9};
  uint64_t start = trace_syscall_enter(p, nr, args);
  int64_t ret = do_syscall_64(r);
  trace_syscall_exit(process_current(), nr, args, ret, start);
  return ret;
}

void syscall_init(void)
{
  static bool filesystem_checked;
  static bool audio_checked;
  klog(KLOG_DEBUG, "syscall: LSTAR entry ready, int 0x80 removed");
  if (!audio_checked)
  {
    audio_checked = true;
    bool screen_logging = klog_is_screen_enabled();
    klog_set_screen_enabled(false);
    audio_init();
    klog_set_screen_enabled(screen_logging);
    klog(KLOG_INFO, "audio: PC speaker backend ready");
  }
  if (!filesystem_checked)
  {
    filesystem_checked = true;
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage: scanning PCI controllers");
    storage_probe_init();
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage: initializing block transports");
    (void)block_device_init();
    vfs_fat32_mount_boot();
    struct ahci_probe_stats ahci_stats;
    struct xhci_probe_stats xhci_stats;
    struct ehci_probe_stats ehci_stats;
    ahci_get_probe_stats(&ahci_stats);
    xhci_get_probe_stats(&xhci_stats);
    ehci_get_probe_stats(&ehci_stats);
    klogf(KLOG_INFO, "pci: %u storage/USB controller(s) detected",
          storage_controller_count());
    klogf(KLOG_INFO, "storage: %u operational disk(s) detected",
          block_device_count());
    klogf(KLOG_INFO, "ahci: %u SATA disk(s) identified", ahci_device_count());
    klogf(KLOG_DEBUG,
          "ahci: controllers=%u ports=%u sata=%u identify_failures=%u",
          ahci_stats.controllers, ahci_stats.implemented_ports,
          ahci_stats.sata_ports, ahci_stats.identify_failures);
    klogf(KLOG_INFO,
          "xhci: controllers=%u connected=%u addressed=%u usb-storage=%u "
          "failures=%u stage=%u",
          xhci_stats.controllers, xhci_stats.connected_ports,
          xhci_stats.addressed_devices, xhci_stats.mass_storage_devices,
          xhci_stats.failures, xhci_stats.last_stage);
    klogf(KLOG_INFO,
          "ehci: controllers=%u connected=%u high-speed=%u usb-storage=%u "
          "failures=%u stage=%u",
          ehci_stats.controllers, ehci_stats.connected_ports,
          ehci_stats.high_speed_ports, ehci_stats.mass_storage_devices,
          ehci_stats.failures, ehci_stats.last_stage);
    klog(KLOG_DEBUG, "usb stages: 1=pci 2=running 3=port 4=addressed "
                     "5=bulk-endpoints 6=scsi 7=ready");
    if (vfs_mount_root())
    {
      klogf(KLOG_OK, "vfs: root mounted from %s", vfs_root_device_name());
      audio_restore_system_settings();
      wallclock_load_zone();
      {
        int32_t fd = vfs_open("/purec/install.cfg");
        if (fd >= 0)
        {
          char cfg_buf[512] = {0};
          int32_t r = vfs_read(fd, cfg_buf, sizeof(cfg_buf) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            klogf(KLOG_INFO, "config: /purec/install.cfg (%d bytes)", r);
            cfg_buf[r] = 0;
            char *p = cfg_buf;
            while (*p)
            {
              char line[128] = {0};
              uint32_t i = 0;
              while (*p && *p != '\n' && i < 127)
              {
                line[i++] = *p++;
              }
              if (*p == '\n')
                p++;
              if (line[0])
                klogf(KLOG_INFO, "  cfg: %s", line);
            }
          }
        }
        fd = vfs_open("/etc/hostname");
        if (fd >= 0)
        {
          char hn[64] = {0};
          int32_t r = vfs_read(fd, hn, sizeof(hn) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            hn[r] = 0;
            for (int i = 0; i < r; i++)
              if (hn[i] == '\n' || hn[i] == '\r')
              {
                hn[i] = 0;
                break;
              }
            klogf(KLOG_INFO, "config: hostname='%s'", hn);
          }
        }
        fd = vfs_open("/boot/loader.cfg");
        if (fd >= 0)
        {
          char lb[256] = {0};
          int32_t r = vfs_read(fd, lb, sizeof(lb) - 1);
          (void)vfs_close(fd);
          if (r > 0)
          {
            lb[r] = 0;
            klogf(KLOG_DEBUG, "config: /boot/loader.cfg %d bytes", r);
          }
        }
      }
    }
    else
    {
      klog(KLOG_WARN, "vfs: no ext2 root found (use 'install' or "
                      "'mkfs' to create)");
    }
    boot_diag_checkpoint(BOOT_STAGE_SYSCALLS,
                         "storage and filesystem probe complete");
  }
}
