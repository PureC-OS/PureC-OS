#include "kernel.h"
#include "../../arch/x86_64/fpu/include/fpu.h"
#include "../../arch/x86_64/mmio.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/display/svga.h"
#include "../../drivers/display/vbe.h"
#include "../../drivers/power/power.h"
#include "../../drivers/serial/serial.h"
#include "../../fs/vfs.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_gate.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../net/core/net_service.h"
#include "../devices/device_manager.h"
#include "../diagnostics/boot_diag.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../process/process.h"
#include "../smp/cpu.h"
#include "../smp/smp.h"
#include "../syscall/syscall.h"
#include "init.h"
#include "../random/krandom.h"
#include "system_info.h"
extern struct limine_memmap_response *memmap_response_ptr;
extern struct limine_smp_response *smp_response_ptr;
extern uint64_t hhdm_offset_global;
#include <stdint.h>

static uint16_t g_vbe_probe_id = 0xFFFFu;

static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4, uint64_t a5) {
  int64_t ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                   : "r10", "r8", "memory");
  return ret;
}

void kernel_main(struct limine_framebuffer *fb) {
  (void)fb;
  klog(KLOG_INFO, "Entering kernel_main...");
  klog(KLOG_INFO, "Kernel: 64-bit long mode, GOP active");

  boot_diag_checkpoint(BOOT_STAGE_SYSTEM_INFO, "probing CPU and memory map");
  cpu_topology_init(smp_response_ptr);
  system_info_init(memmap_response_ptr);
  if (system_info_usable_ram_bytes() == 0)
    kernel_panic("memory map contains no usable RAM");
  klogf(KLOG_OK, "CPU detected: %s", system_info_cpu_name());
  klogf(KLOG_OK, "Usable RAM: %lu MB",
        system_info_usable_ram_bytes() / (1024 * 1024));
  klogf(KLOG_INFO, "Framebuffer: %dx%d", gop_get_width(), gop_get_height());

  boot_diag_checkpoint(BOOT_STAGE_SYSTEM_INFO, "initializing physical memory");
  pmm_init(memmap_response_ptr, hhdm_offset_global);
  if (!pmm_is_ready())
    kernel_panic("physical memory manager initialization failed");
  oom_gate_init();
  vmm_init();
  if (!vfs_mount_initramfs())
    kernel_panic("cannot mount /boot/initramfs.cpio");
  vfs_selftest();
  fpu_init();
  power_init();
  process_init();
  klog(KLOG_INFO, "Testing IDT: int3 breakpoint...");
  __asm__ volatile("int3");
  klog(KLOG_OK, "int3 handled, IDT working");

  boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "initializing syscall layer");
  klog(KLOG_INFO, "Initializing PCI MMIO mapper...");
  mmio_init();
  if (mmio_is_ready()) {
    klog(KLOG_OK, "PCI MMIO mapper ready (uncached multi-page mappings)");
  } else {
    klog(KLOG_WARN, "PCI MMIO mapper unavailable; AHCI/xHCI/EHCI disabled");
  }
  {
    extern int boot_video_pref_get(void);
    int pref = boot_video_pref_get();
    {
        extern uint16_t vbe_debug_id(void);
        g_vbe_probe_id = vbe_debug_id();
        klogf(KLOG_WARN, "vbe: dispi probe id=0x%x %s", g_vbe_probe_id,
              (g_vbe_probe_id >= 0xB0C0 && g_vbe_probe_id <= 0xB0C5) ? "VBE_ALIVE" : "VBE_SILENT");
    }
    if (pref != 1 && mmio_is_ready()) {
        if (svga_init(1024, 768, 32)) {
            if (gop_apply_live(svga_fb_virt(), svga_width(), svga_height(),
                               svga_pitch_pixels(), svga_bpp()))
                klog(KLOG_OK, "video: VMware SVGA II active (FIFO UPDATE)");
            else
                klog(KLOG_WARN, "video: svga mapped but gop_apply_live failed, keep GOP");
        } else if (pref == 2) {
            klog(KLOG_WARN, "video: SVGA requested but failed, keep GOP (safe)");
        }
    } else {
        klog(KLOG_INFO, "video: GOP forced by cmdline (video=gop)");
    }
  }
  smp_start_all(smp_response_ptr);
  syscall_init();
  klog(KLOG_OK, "Syscall int 0x80 ready");
  (void)net_service_init();
  const char *msg = "Hello from syscall (Limine)!\n";
  klog(KLOG_INFO, "Testing syscall WRITE...");
  do_syscall(SYS_WRITE, (uint64_t)msg, strlen(msg), 1, 0, 0);
  klog(KLOG_OK, "syscall WRITE done");
  klog(KLOG_INFO, "GOP test deferred to userspace");

  klog(KLOG_INFO, "Boot log complete");
  klogf(KLOG_INFO, "Verbose mode: %s (debug visible)",
        klog_is_verbose() ? "on" : "off");

  klog(KLOG_INFO, "Initializing device manager...");
  devman_init();
  devman_enumerate();
  devman_register_builtin_drivers();
  devman_autoload();
  krandom_init();
  klogf(KLOG_INFO, "random: %s backend",
        krandom_hardened() ? "purecrypt hash-drbg" : "fallback");
  devman_dump();
  klogf(KLOG_OK, "devman: %u devices registered", devman_get_device_count());
  {
      extern const char *svga_status_line(void);
      klogf(KLOG_WARN, "video late: %s vbe=0x%x", svga_status_line(), g_vbe_probe_id);
  }

  init_process_start();
}