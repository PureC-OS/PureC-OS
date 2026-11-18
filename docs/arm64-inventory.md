# PureC OS: aarch64 port inventory

Scope: kernel (C + NASM, x86_64, Limine). Branch PureC-OS-ARM64-DEV. Read-only survey; counts are grep hits (rough, `inl`/`inb` matches include some `inline` noise).

## 1. Summary: top 10 blockers

1. **`src/arch/x86_64` is thin.** It holds only GDT/IDT/FPU/MMIO-map plus 3 small `.asm` files (context switch, user entry, AP entry). Several files are empty stubs (`task.c/h`, `isr/*`, `paging/*`, `interrupt.asm`). Most arch code lives elsewhere: paging in `src/mm/vmm.c`, APIC/SMP in `src/kernel/smp/`, TSC/PIT in `drivers/interrupts/`, MSR TLS in `scheduler.c`. There is no arch header, so nothing can be swapped by build flag today.
2. **Build is hard-wired to x86_64.** `CROSS=x86_64-elf-`, `nasm -f elf64`, `-m64 -mno-red-zone -mcmodel=kernel`, `ASM_SOURCES` and `arch/x86_64/...` C files listed literally in `src/kernel/Makefile`. Linker scripts use `0xffffffff80000000`. The ISO step copies `BOOTX64.EFI` and the BIOS stages.
3. **Paging code is x86 PML4 and lives in `src/mm/vmm.c`.** It uses CR0/CR3/EFER (via cpuid/rdmsr/wrmsr), `invlpg`, and x86 PTE bits (`VMM_PAGE_*`, bit 63 NX, software bits 9-11, 52-53). `arch/x86_64/mmio.c` has its own PML4 walker. `smp.c` does TLB shootdown through a LAPIC IPI.
4. **Interrupt/trap model is x86.** IDT, `int 0x80` syscalls (no syscall/sysret, which is simpler to replace with SVC), `struct syscall_regs` with `vector/err/rip/cs/rflags/rsp/ss` shared by scheduler, signals, fork/clone, ELF entry and panic. This struct is the main cross-cutting ABI to abstract.
5. **Interrupt controller is PIC + LAPIC (and ACPI-driven IOAPIC elsewhere).** A GIC (v2/v3) driver is needed, plus the generic timer (CNTP) to replace PIT/LAPIC timer/TSC. The tick, `cpufreq`, `klog` timestamps and `timer.c` all rdtsc/PIT.
6. **Port I/O is everywhere and duplicated.** About 25 files define a private `outb/inb/outw/inw/outl/inl` (no shared header). Used by PIT, PIC, PS/2, serial, CMOS RTC, ATA PIO, PCI config (0xCF8), VGA/VBE/SVGA, pcnet, HDA, VBox mouse, ACPI glue. On aarch64 these are unavailable and need MMIO/ECAM replacements.
7. **PCI config space is port-based only** (`src/drivers/pci/pci.c`, 0xCF8/0xCFC). QEMU virt needs ECAM (from the DTB, or ACPI MCFG with UEFI).
8. **Discovery relies on ACPI** (`acpi/` uACPI glue, RSDP from Limine, EC/battery/S5 power, PM1 ports). On aarch64 use DTB (`LIMINE_DTB_REQUEST`) or ACPI/GTDT/MADT, and PSCI for power-off/SMP.
9. **SMP start is Limine MP request with x86 specifics** (`smp_ap_entry` takes `limine_smp_info` in RDI; per-CPU via `%gs` and MSRs; LAPIC IDs). The aarch64 Limine SMP response has a different struct (MPIDR, GIC ids).
10. **No virtio and no NVMe in the tree.** Storage is AHCI plus ATA PIO only; net is e1000/82543gc/pcnet/ath9k/iwx; display is VGA/VBE/SVGA/GOP. QEMU `virt` needs virtio-blk/net (PCI or MMIO), or AHCI/e1000e via PCI as a stopgap, and a plain framebuffer (GOP via Limine works). Also: ELF loader/`kmod_so`/libc dynamic loader accept only `EM_X86_64` and `R_X86_64_*`; libc has x86 asm (setjmp, fenv, syscall, TLS via FS_BASE).

## 2. Build

- `Makefile` includes `mk/main.mk` and `mk/kernel-package.mk`. The kernel is built by `make -C src/kernel` (`src/kernel/Makefile`, ~260 lines).
- `mk/toolchain.mk`: `CROSS ?= x86_64-elf-`, `CC=$(CROSS)gcc`, `LD`, `AR`, `NASM`. `COMMON_CFLAGS = -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone`. `KERNEL_CFLAGS` adds `-mcmodel=kernel -mgeneral-regs-only -fno-omit-frame-pointer`. User/SO/kmod flags also carry `-m64 -mno-red-zone -mcmodel=small`.
- aarch64 equivalents: `-mgeneral-regs-only` stays; `-mcmodel=small` (kernel in high VA via linker, aarch64 has no `-mcmodel=kernel`); drop `-m64 -mno-red-zone` (aarch64 has no red zone issue in the kernel, but exceptions use SP_EL1, so fine); add `-mno-outline-atomics`, `-mstrict-align` optional.
- `src/kernel/Makefile`: explicit `C_SOURCES` list (about 150 files). x86 entries: `arch/x86_64/{gdt,idt,fpu,mmio}`, `kernel/smp/{lapic,cpufreq,...}`, `drivers/{serial,display/vga,interrupts/pic,interrupts/timer,mouse/ps2_mouse,input/keyboard}`, `drivers/display/{vbe,svga,svga3d}`, ahci/ATA, all NIC drivers, xhci/ehci, HDA. `ASM_SOURCES` = 5 NASM files under `arch/x86_64`, assembled with `nasm -f elf64` (rule `%.asm.o`). aarch64 should use GAS `.S` via `$(CC)` (the repo already has `.S` in libc), so no nasm.
- Link: `ld -T linker-limine.ld`, load via Limine at `0xffffffff80000000` (virt) mapped to phys `0x100000`; sections `.requests`, `.text`, `.rodata`, `.data`, `.bss`, `.ksyms` (reserved 8192*64 bytes for symbols), `_kernel_end`. `linker.ld` is the legacy (multiboot) script. `mk/ksyms.py` and `nm` post-processing use `$(CROSS)nm`, which is fine for any arch.
- Extras compiled into the kernel: uACPI (`acpi/` repo, cloned by `acpi-fetch`), libxcrypt sha512, `io/io.o` (`src/io/Makefile`: storage). Kernel modules (`.so`, `mk/kmod-so.ld`, `KMOD_SO_CFLAGS`): ext2, fat32, e1000, 82543gc, pcnet, ath9k, random, wolfssl; all built with x86 flags.
- ISO/ESP (`mk/main.mk` ~288-330, `mk/make_esp_image.sh`, `build-limine.sh` -> `make iso`): xorriso, `limine-bios-cd.bin`, `limine-bios.sys`, `BOOTX64.EFI`; config from `src/boot/limine.conf.in`, modules `kernel-fallback.elf`, `BOOTX64.EFI` embedded as a boot module (installer copies it to the ESP). aarch64 needs `BOOTAA64.EFI`, no BIOS stages; the ISO can be an EFI-only image.
- kconfig (`src/kernel/kconfig`): `KERNEL_ARCH=x86_64` already exists as a key (used by the kernel-package/mkinitcpio flow). Good hook for `ARCH`.
- Host tests (`make test-*`) compile selected kernel `.c` files natively and are arch-neutral, except `tests/vma_test` etc. which are fine.
- Needs `ARCH=` parametrization: `CROSS`, CFLAGS, `ASM_SOURCES`/arch C sources (use `$(ARCH)`), linker script (`linker-limine-$(ARCH).ld`), nasm vs `.S`, Limine files/EFI name, `-f elf64` rules, `KERNEL_ARCH`, userspace flags in `toolchain.mk`, `mk/sysroot`, ports (tcc, vim, etc.: out of scope), QEMU tests (`tests/qemu_network.py`).

## 3. `src/arch/x86_64` contents (about 940 lines)

| File | Lines | What |
|---|---|---|
| `gdt/c/gdt.c`, `gdt/asm/gdt.asm`, `gdt/include/gdt.h` | 118+17+8 | per-CPU GDT/TSS, `gdt_init`, `gdt_init_cpu(cpu, stack)`, `gdt_set_kernel_stack` (TSS.rsp0), `gdt_current_cpu_id`. Selectors 0x08/0x10 kernel, 0x1B/0x23 user. |
| `idt/c/idt.c`, `idt/asm/idt.asm`, `idt/include/idt.h` | 275+116+13 | 256-gate IDT, ISR stubs with `swapgs` handling, PIC/LAPIC IRQ routing: `idt_set_irq_handler`, `idt_set_vector_handler`, `idt_mask_irq/unmask`, rate checker. `int 0x80` is the syscall gate (DPL3). |
| `fpu/fpu.c`, `fpu/include/fpu.h` | 83+14 | FXSAVE/FXRSTOR 512-byte state: `fpu_init(_cpu)`, `fpu_save/restore`, `fpu_thread_init`. |
| `mmio.c`, `mmio.h` | 188+10 | `mmio_configure/init/map(phys,size)`: separate high-half mapping window with its own PML4 walker, cpuid check; used by AHCI, xhci, ehci, e1000, iwx, ath9k, HDA, vbe/svga, lapic. |
| `user.asm` | 49 | `arch_enter_user(rip,rsp)` (iretq to ring 3) and `arch_enter_user_frame(frame)` (pops a full `syscall_regs`). |
| `scheduler.asm` | 22 | `scheduler_asm_switch(old_rsp*, new_rsp*)`: pushes rbp/rbx/r12-r15, swaps rsp. |
| `smp.asm` | 28 | `smp_ap_entry` (Limine AP entry, reads stack from `limine_smp_info->extra_argument`). |
| `task.c/.h`, `interrupt.asm`, `isr/*`, `paging/*` | 0 | empty stubs. |

Not in arch but arch-specific (should move): `src/mm/vmm.c` (paging), `src/kernel/smp/{lapic,smp,percpu,cpufreq,cpu}.c`, `src/drivers/interrupts/{pic,timer}.c`, TLS/FS_BASE and MSR code in `process/scheduler.c`, `kernel/sync/spinlock.h`, `kernel/diagnostics/panic.h`, `kernel/syscall/syscall.h` (`syscall_regs`).

## 4. x86 code outside arch, by subsystem

Counts: `asm volatile` hits in src (excluding acpi) are about 230 over 40 files; with acpi/ about 60 more.

### src/kernel (core, smp, process, sync, syscall, time, diagnostics)
- `sync/spinlock.h:15-32`: `pushfq; pop; cli`, `sti`, `pause`. Header-level; every lock user depends on it. Replace with `arch_irq_save/restore`, `cpu_relax`.
- `smp/percpu.h:36-45`: `%gs:` per-CPU reads (`movl %%gs:`, `movq %%gs:`). `percpu.c:3-37`: `MSR_GS_BASE/KERNEL_GS_BASE` setup. aarch64: `TPIDR_EL1`.
- `smp/lapic.c` (about 140 lines): xAPIC MMIO (0xFEE00000 via mmio_map) and x2APIC MSRs (0x1B, 0x800+); `lapic_send` IPIs (TLB vector, INIT/SIPI semantics via Limine). `smp/smp.c`: 21 asm sites, TLB shootdown IPI at `smp.c:248`, per-CPU boot context. `smp/cpufreq.c` (34 MSR/cpuid sites: Intel/AMD P-state/HWP): x86-only, stub on aarch64. `smp/cpu.c` is host-tested and mostly neutral (topology struct).
- `process/scheduler.c`: `wrmsr` FS_BASE `:406-412`, `sti` `:468`, `cli;hlt` idle `:1561-1563`, `scheduler_asm_switch` `:1296`; `scheduler.h` holds `rsp`, `fs_base`, `fpu_state[512]` in the task struct. Also fork/clone/ELF entry build `syscall_regs` frames.
- `process/signal.c:15-19`: `USER_CODE_SELECTOR 0x1B`, `USER_DATA_SELECTOR 0x23`, `USER_FLAGS_MASK 0xCD5` (RFLAGS), `USER_RED_ZONE 128`, `USER_TOP_ADDRESS 0x0000800000000000`. Signal frame is built from `syscall_regs` (ucontext layout is x86 GPRs).
- `process/process.c:36-45`: `USER_STACK_TOP 0x00007FFFFFF00000`, `MMAP_BASE 0x0000680000000000`, `MMAP_END 0x00007F0000000000`, `USER_TOP_ADDRESS`. 48-bit VA fits aarch64 (T0SZ=16), so values can stay, but belong in an arch header.
- `process/elf.c:9,131,244`: `ELF_MACHINE_X86_64 (62)`; `elf.h:6-11` user ranges. `module/kmod_so.c`: `R_X86_64_*` relocations (654 lines, apply loop), `kmod` is x86-only. Needs `EM_AARCH64 (183)` and `R_AARCH64_*`.
- `syscall/syscall.c`: `pushfq`/`cli`/`sti` at `:125-145`; `int 0x80` gate setup `:2930`; ABI = rax number, rdi,rsi,rdx,r10,r8,r9; `SYS_SET_FS_BASE (378)` at `:2177` (name is x86; on aarch64 maps to TPIDR_EL0). `syscall.h:1004` `struct syscall_regs`.
- `core/kernel.c:43,79`: `int $0x80` and `int3` self-tests. `core/system_info.c` (9 cpuid/MSR sites: brand string, features). `diagnostics/panic.h:37-41` captures rip/rsp/rbp/rax..; `panic.c`, `panic_qr.c`, `klog.c:51` (private `rdtsc`), `boot_diag.c`. `random/krandom.c:31` `rdrand` (use RNDR or fallback).
- `time/rtc.c`: CMOS ports 0x70/0x71, `wallclock.c` (uses RTC). aarch64: PL031 or `cntpct` plus firmware time.
- `ipc/ipc.c`, `process/futex.c`, `process/vma.c`, `mm/shm.c`, `mm/pmm.c`, `mm/oom/oom_slab.c`: small `outb/inb`-pattern hits are mostly false positives (`inline`); real uses are atomics/spinlock via `spinlock.h`.

### src/mm
- `vmm.c` (about 1000+ lines): `cpuid` `:59-64` (NX/1G page detection), `rdmsr/wrmsr` EFER `:70-72`, CR0 `:81-85`, CR3 read `:92` and switch `:511`, `invlpg` `:140`; 26 shift/mask page-table walk sites (9-bit levels, 4096B pages); constants `USER_TOP`. PTE flags in `vmm.h:6-14` (x86 bit positions). API to keep: `vmm_init`, `vmm_init_cpu`, `vmm_create_address_space`, `vmm_destroy_address_space`, map/unmap/protect, COW (`VMM_PAGE_COW`, bits 9-11 software), `VMM_PAGE_DEVICE` (needs device memory attribute on arm).
- `pmm.c/.h`: takes `limine_memmap_response*` and `hhdm_offset`; Limine-specific but cross-arch.

### src/drivers
- `interrupts/pic.c` (8259), `interrupts/timer.c` (PIT 0x40/0x43, TSC calibration with cpuid 0x15, LAPIC timer, `idle_tsc`), `serial/serial.c` (COM1 0x3F8), `display/vga.c` (text mode at 0xB8000 via HHDM), `display/vbe.c` (Bochs VBE ports 0x1CE/0x1CF), `display/svga.c`/`svga3d.c` (VMware SVGA II I/O ports, VBox), `display/gop.c` (Limine framebuffer; 10 asm sites, likely `rep movs`/`sfence` blits, check), `input/keyboard.c` (PS/2 0x60/0x64, 23 port hits), `mouse/ps2_mouse.c` (25 asm), `mouse/vbox_mouse.c` (VBox guest port I/O), `audio/audio.c` (AC97/SB ports), `audio/audio_hda.c` (MMIO, mostly portable), `power/power.c` (ACPI shutdown, port I/O), `random/random.c` (rdrand/rdtsc seed), `pci/pci.c` (0xCF8/0xCFC; 18 port sites), `input/i2c_hid_touchpad` (ACPI-enumerated, MMIO + IOAPIC GSI, ASUS), `usb/xhci.c` and `ehci.c` (MMIO; xhci has 12 rdtsc/asm sites for delays/barriers).
- NICs: `net/e1000` (MMIO), `net/82543gc` (MMIO), `net/pcnet` (port I/O, 24 sites), `net/ath9k` (MMIO), `net/iwx` (MMIO + PCI cfg).

### src/io (storage)
- `storage/ata_pio.c` (32 port sites; legacy IDE), `storage/ahci.c` (MMIO via `mmio_map`; portable), `block_device.c`, `storage_probe.c` (PCI scan), `limine_stage2.h`/`limine_vbr.h` (x86 BIOS bootsector blobs for installer; BIOS only), `swap.c`, `io_sched.c` (neutral).

### src/boot
- `boot.c` (338 lines): Limine requests (see section 6), `cli; hlt` loops `:234-248`, `cli`/`sti` `:328-330`, `kernel_main` call. `grub_main.c` and `multiboot_header.asm` are the legacy multiboot path (x86-only, not needed on aarch64). `limine.conf.in`: `protocol: limine`, `resolution`, modules.

### src/fs, src/net, src/lib, src/gfx
- `fs/*`: neutral (VFS, ext2, fat32, tmpfs...). `install.c` and the installer write BIOS/EFI boot files (`BOOTX64.EFI`, `limine-bios.sys`) and use x86 disk layout assumptions.
- `net/*`: neutral; only `byteorder.h` has `inl`-like names (no asm). `gfx/text.h`: neutral.
- `lib/string.c`: plain C (check for `rep movsb` inline asm; none found).

### acpi/ (external repo, uACPI glue)
- `uacpi_glue.c`: 14 asm + 26 port I/O sites (uACPI IO space handlers, PCI cfg via 0xCF8, IRQ install, `rdtsc` sleeps). `acpi.c`, `acpi_ec.c` (EC ports 0x62/0x66), `acpi_power.c` (PM1 ports, S5), `acpi_battery.c`, `acpi_sensors.c`. All x86 chipset-bound; on aarch64 ACPI is only available with UEFI firmware (and has no EC/PM1 legacy), so build it optional (`CONFIG_ACPI`).

## 5. Platform devices (x86 assumptions)

| Area | Where | Notes for aarch64 |
|---|---|---|
| IRQ controller | `idt.c`, `pic.c`, `lapic.c`, ioapic code in `acpi/uacpi_glue`, `i2c_hid_touchpad` | Replace with GICv2/v3 (DTB/MADT). IRQ numbers 32+ vector scheme baked into `idt_set_irq_handler`. |
| Timers | `timer.c` PIT/TSC/LAPIC; `cpufreq.c` | ARM generic timer (CNTFRQ, CNTP/CNTV). TSC freq detection becomes `cntfrq_el0`. |
| PCI | `pci.c` ports 0xCF8/0xCFC | ECAM MMIO; BAR/MSI differ (GIC ITS or INTx via DTB). |
| Legacy | PS/2 (0x60/0x64), COM1 (0x3F8), VGA text (0xB8000), CMOS RTC (0x70/0x71), PIT, PIC, 0x80 io_wait, AC97/SB audio | Serial -> PL011 (QEMU virt 0x09000000, from DTB); RTC -> PL031; no PS/2/VGA; keyboard via USB (xhci already) or virtio-input. |
| SMP | `smp.c`, `smp.asm`, `lapic.c` | Limine aarch64 SMP: `limine_smp_info` holds `processor_id`, `gic_iface_no`, `mpidr`, `goto_address`, `extra_argument`. IPIs via GIC SGIs. TLB shootdown could use `tlbi ... is` (inner-shareable broadcast), so no IPI needed. |
| Syscall entry | `int 0x80` through IDT (DPL3), libc uses `int $0x80` (`src/libc/runtime.c:9`, `ld.c:182`) | `svc #0`, vector table (VBAR_EL1); user ABI regs x8 number, x0-x5 args. Syscall numbers stay. |
| Page tables | PML4, 4 levels, 4K pages, 1G/2M optional, NX bit 63, U/S bit 2, RW bit 1 | aarch64 4K granule, 4 levels, AP bits, UXN/PXN, AF, MAIR attrs, TTBR0/TTBR1 split (user vs kernel), ASID. Software bits 9-11/52-53 are within the allowed software range (55-58 on arm; 52-53 are reserved/ignored for PBHA? Use 55-58). |
| Higher half | `0xffffffff80000000` kernel; HHDM from Limine; user < `0x0000800000000000` | Same canonical split works on arm (TTBR1 >= 0xffff...). Limine aarch64 uses same HHDM scheme. |
| TLS | `FS_BASE` MSR; `SYS_SET_FS_BASE`; libc `pthread.c:113`; `scheduler_set_fs_base`; task `fs_base` | `TPIDR_EL0` (TLS variant I layout: libc/tcc/compiler TLS model differs from x86 variant II). |
| Signal frames / ucontext | `signal.c`, `syscall_regs` | New arch-specific frame (x0-x30, sp, pc, pstate, fpsimd). Red zone 128 bytes is x86 only (aarch64 ABI has none below SP... leaf functions may use up to 16 bytes; use 0). |
| fork/clone register frames | `scheduler.h` task create taking `const uint64_t *frame`, `arch_enter_user_frame` | Frame layout must be arch-defined. |
| FPU | FXSAVE 512B, `fpu.c` | FP/SIMD state (32x128-bit + FPCR/FPSR = 528 B); `fpu_state[512]` hard-coded in `scheduler.h:70`. |
| Cache/DMA | x86 coherent DMA assumed in e1000/AHCI/xhci/iwx (no cache maintenance, no barriers other than `asm("":::"memory")`) | Need `dma_wmb/rmb`, coherent DMA allocator (non-cacheable mapping or explicit clean/invalidate). Real risk on real hardware; on QEMU virt it is coherent. |
| Entropy | `rdrand`/`rdtsc` in `krandom.c`, `random.c` | `RNDR` (FEAT_RNG) or counter + virtio-rng. |
| Power | ACPI S5/reset, PM1 ports in `power.c` | PSCI `SYSTEM_OFF/RESET` (HVC/SMC); DTB `psci` node. |

## 6. Limine requests (`src/boot/boot.c:27-87`)

Base revision 0 (`LIMINE_BASE_REVISION(0)`; use 2+ for aarch64 on a recent Limine and re-vendor `src/boot/limine.h` if outdated).

| Request | Use | aarch64 status |
|---|---|---|
| framebuffer | GOP console, fb driver | works (UEFI GOP) |
| hhdm | pmm/mmio/kernel | works |
| kernel_address | phys/virt base for mmio and `vmm` | works |
| memmap | pmm_init | works |
| firmware_type | detect BIOS vs UEFI | on aarch64 always UEFI |
| kernel_file | ksyms / module self-reference | works |
| module | initramfs, fallback kernel, `BOOTX64.EFI` blob, loadable modules | works (module names must change to BOOTAA64.EFI) |
| rsdp | ACPI (uACPI) | present only if firmware provides ACPI (QEMU virt with UEFI: yes; DTB-only boards: no). Treat absent as normal. |
| smp | AP bring-up | works but different `limine_smp_info` fields (MPIDR, GIC iface no); no `lapic_id`, `x2apic` flag. |
| (missing) dtb | needed | `LIMINE_DTB_REQUEST` exists in `limine.h:604`; not requested yet, required for GIC, PL011, ECAM, PSCI, virtio-mmio discovery. |

No `entry_point`/`paging_mode`/`stack_size` requests are used (default stack from Limine).

## 7. Userspace / libc: x86-specific files

- `src/libc/runtime.c`: `pc_syscall` via `int $0x80` (`:9`), `syscall5` helper.
- `src/libc/setjmp.S` (43 lines): x86_64 callee-saved regs.
- `src/libc/ld/ld_entry.S` (71 lines): dynamic loader entry stub; `ld.c:182` `int $0x80`, `:268` `hlt`; `R_X86_64_*` handling (6 hits in ld + kmod).
- `src/libc/fenv.c` (x87/SSE: `stmxcsr`, `fnstcw`, `fldenv`...), `libm.c` and `math.c` (x87/SSE asm inline), `crt0.c` (entry `_start` register convention; 2 asm sites), `pthread.c:113` (`SYS_SET_FS_BASE`; thread struct pointer self at `%fs:0`), `signal.c`, `unistd.c`, `socket.c`, `event.c` (inline asm syscalls/restorer trampolines).
- Linker scripts: `linker-userspace*.ld`, `src/libc/userspace-so.ld`, `mk/kmod-so.ld` (base addresses; `ELF_PIE_DEFAULT_BASE`, `ELF_SO_BASE`).
- Rust userspace (`userspace/`), tcc, vim, SDL, Qt, curl, wolfSSL, etc.: out of scope (the project memory says to focus on the kernel). Only a bare "init + sh" userspace is realistic for the first aarch64 boot; initramfs binaries are x86 now.

## 8. Drivers portability

| Driver | Files | Bus/IO | Portable? |
|---|---|---|---|
| PCI core | `drivers/pci/pci.c` | port I/O 0xCF8 | no; add ECAM backend |
| AHCI | `io/storage/ahci.c` | PCI + MMIO (`mmio_map`) | yes (needs barriers, DMA coherency) |
| ATA PIO | `io/storage/ata_pio.c` | port I/O | no (x86 legacy) |
| xHCI / EHCI | `drivers/usb/xhci.c`, `ehci.c` | PCI + MMIO | yes (rdtsc delays in xhci need generic delay) |
| e1000 (82540EM), 82543GC | `drivers/net/e1000`, `82543gc` | PCI MMIO | yes (QEMU `-device e1000` on virt works over PCI) |
| pcnet | `drivers/net/pcnet` | port I/O | no |
| ath9k, iwx | MMIO | yes in principle (real HW only, low priority) |
| HDA | `audio_hda.c` | MMIO | mostly yes; `audio.c` (AC97/SB ports) no |
| VGA/VBE/SVGA/SVGA3D | `display/*` | I/O ports + x86 only | no; use only `gop.c`/`fb.c` (Limine framebuffer) |
| PS/2 kbd/mouse, VBox mouse | `input/keyboard.c`, `mouse/*` | ports | no (keyboard: USB HID via xhci; usb_mouse exists) |
| I2C HID touchpad | `input/i2c_hid_touchpad` | ACPI + MMIO + IOAPIC | no (ASUS laptop only) |
| serial | `serial/serial.c` | COM1 ports | no; write PL011/16550-MMIO driver |
| PIC/PIT/LAPIC | `interrupts/*`, `smp/lapic.c` | x86 | no |
| RTC | `time/rtc.c` | CMOS ports | no |
| random | `random/*` | `random_core.c` is portable (host-tested); seed uses rdrand/rdtsc | partially |
| **virtio** (blk, net, console, input, gpu, rng) | none in tree | | **missing**: highest-value new drivers for QEMU virt (virtio-pci transport, can reuse PCI + mmio_map) |
| **NVMe** | none | | missing (not needed first) |

Devices usable on QEMU `virt` without new drivers (via PCI ECAM): AHCI (`-device ich9-ahci`), e1000/82543gc (`-device e1000`), xHCI (`-device qemu-xhci`) plus USB keyboard/mouse. Everything else needs new code.

## 9. Proposed arch interface

Create `src/arch/arch.h` (includes `<arch/$(ARCH)/arch_defs.h>` by `-I` path or `#include ARCH_HEADER`), and `src/arch/<arch>/` with the implementation. Target subsystems and function lists (names are suggestions):

1. **CPU control** (`arch/cpu.h`): `arch_cpu_relax()`, `arch_halt()`, `arch_idle()` (sti;hlt / wfi), `arch_cpu_id()`, `arch_irq_save()`, `arch_irq_restore(flags)`, `arch_irq_enable/disable()`, `arch_irqs_enabled()`, `arch_barrier_{full,read,write}`, `arch_cpu_features()` (replaces cpuid users in `system_info.c`, `vmm.c`), `arch_cpu_name()`, `arch_read_cycle_counter()` (rdtsc / `cntvct`), `arch_cycle_freq()`. Move `spinlock.h` asm to it. Provide `arch_read_sp/ip/fp` for `panic.h`.
2. **Per-CPU and TLS**: `arch_percpu_init(cpu, area)`, `arch_percpu_ptr()`, `arch_percpu_read32/64`, `arch_set_user_tls(base)` / get (replaces FS_BASE MSR in scheduler and `SYS_SET_FS_BASE`).
3. **Trap frame** (`arch/regs.h`): define `struct arch_regs` (= today's `syscall_regs`) with accessors `regs_ip/sp/set_ip/set_sp/set_retval/syscall_nr/syscall_arg(n)/is_user(regs)`, user-flags init, `arch_user_regs_init(regs, entry, sp, arg)`, signal frame build/restore (`arch_signal_setup/arch_signal_return`), fork child return setup. Remove direct `regs->rax/rdi...` use from `syscall.c`, `signal.c`, `scheduler.c`, `process.c`.
4. **Context switch**: `arch_context_switch(old_sp*, new_sp*)`, `arch_task_stack_init(stack, entry, arg)`, `arch_fpu_*` (`fpu_init/save/restore/thread_init`, state size via `ARCH_FPU_STATE_SIZE`), `arch_enter_user(ip, sp)`, `arch_enter_user_frame(regs)`, `arch_set_kernel_stack(top)` (TSS.rsp0 / SP_EL1 on entry from EL0 uses SP_EL1 directly, so just per-CPU storage).
5. **Paging** (`arch/mmu.h`): move table walk out of `vmm.c`: `arch_mmu_init/init_cpu`, `arch_pte_make(phys, flags)`, flag mapping from generic `VMM_PROT_*`/`VMM_CACHE_*` to hardware bits (`VMM_PAGE_*` become generic flags translated in arch), `arch_mmu_walk/map/unmap`, `arch_mmu_switch(as)`, `arch_mmu_kernel_root()`, `arch_tlb_flush_page(va)`, `arch_tlb_flush_all()`, `arch_tlb_shootdown(cpumask, va)`, constants `ARCH_USER_TOP`, `ARCH_PAGE_SIZE`, `ARCH_KERNEL_BASE`, `ARCH_USER_STACK_TOP`, `ARCH_MMAP_BASE/END`, `ARCH_PIE_BASE`, `ARCH_SO_BASE`. Merge `mmio.c` walker into the same code (`arch_mmio_map(phys, size)`; with device attribute).
6. **Interrupts** (`arch/irq.h`): `irq_controller_init()`, `irq_register(irq, fn, ctx)`, `irq_mask/unmask(irq)`, `irq_eoi`, `irq_send_ipi(cpu, kind)`, `irq_cpu_init()`. Keep `idt.h` API as the x86 impl of this. IRQ numbering: use a flat "global IRQ number" (GSI/SPI) rather than vectors for drivers.
7. **Timers**: `timer_init(hz)`, `timer_ticks()`, `timer_now_ns()`, `timer_udelay(us)` (fix xhci/rdtsc loops), `timer_set_oneshot(deadline)` (LAPIC timer / CNTP). Isolate `PIT` and `TSC calibration` inside the x86 impl.
8. **Syscall entry**: `arch_syscall_init()` (IDT gate / VBAR_EL1 + SVC vector); generic `syscall_handler(struct arch_regs*)` stays in `kernel/syscall`. Also exception entry: `arch_fault_decode(regs, &fault)` for page faults (`CR2`/`FAR_EL1`, error code/`ESR_EL1`) feeding `vmm` COW/fault handling and `signal_fault`.
9. **SMP**: `arch_smp_boot_aps(entry)` (Limine info wrapped), `arch_smp_ap_entry`, `arch_cpu_id_hw()`, `arch_ipi_send(cpu, type)`; generic `smp.c` keeps scheduling/balance parts.
10. **I/O accessors** (`arch/io.h`): `mmio_read{8,16,32,64}/mmio_write*` (with barriers, replacing ad-hoc volatile derefs); `pio_in{8,16,32}/pio_out*` only on x86, `#if ARCH_HAS_PORT_IO` (drivers needing them are guarded or excluded by Makefile); `dma_wmb/dma_rmb`, `dma_sync_for_device/cpu`, `arch_dma_alloc_coherent`. Replace the ~25 private `outb/inb` copies by one `arch/x86_64/io.h`.
11. **Early console**: `early_console_putc` (COM1 / PL011), used by `serial.c`, panic and klog before everything else; select by arch. `serial.h` API stays.
12. **Firmware/platform discovery**: `platform_init()` that consumes boot info (memmap, hhdm, rsdp or dtb, modules, smp) and exposes `platform_find_uart/gic/timer/ecam/psci`; ACPI optional; DTB parser (new) for aarch64. `power_off/reset` -> ACPI S5 (x86) / PSCI (arm). RTC and entropy through small `platform_*` hooks.
13. **Loader**: `ARCH_ELF_MACHINE` (62 / 183) and `arch_elf_apply_reloc` used by `elf.c`, `kmod_so.c`; libc/ld mirrors for userspace. Kernel module (`.so`) support can be disabled on aarch64 at first (all drivers linked in).

## 10. Suggested order of work

1. **Mechanical refactor on x86 first (no behaviour change, keep booting):** add `ARCH ?= x86_64` to `toolchain.mk`/`kernel/Makefile`, switch `ASM_SOURCES` and arch C files to `arch/$(ARCH)/...`; create `src/arch/arch.h`. Move `spinlock.h`/`percpu.h`/`panic.h` asm behind `arch_*` inlines. Consolidate port I/O into one header. Move `lapic.c`, `cpufreq.c`, `pic.c`, timer x86 bits, TLS/FS_BASE and `smp.asm` into `arch/x86_64` and expose the interfaces from section 9 (items 1, 2, 7).
2. **Trap frame and context** (items 3, 4, 8): abstract `syscall_regs` use in `syscall.c`, `signal.c`, `scheduler.c`, `process.c`, `elf.c`. Largest risk of regressions; verify in VM (and host tests still pass).
3. **Paging split** (item 5): extract table walk + flags out of `vmm.c` and `mmio.c` into `arch/x86_64/mmu.c`; generic `vmm.c` keeps address-space/VMA/COW policy.
4. **aarch64 skeleton that boots to serial:** toolchain (`aarch64-elf-`), linker script `linker-limine-aarch64.ld` (virt `0xffffffff80000000`, same as x86 if Limine allows), `.S` entry, `limine.h` update, DTB request, PL011 early console, build with the heavy drivers excluded (`C_SOURCES` per-arch lists). Boot in QEMU `-M virt -cpu max` with UEFI (edk2) + Limine `BOOTAA64.EFI`; print klog.
5. **Exceptions + MMU + timer:** VBAR_EL1 vectors, GICv2/v3, generic timer tick; arm mmu.c (TTBR0/1, MAIR, ASID optional, `tlbi` broadcast so no IPI needed); page fault path; scheduler context switch; kernel-only threads.
6. **User mode:** SVC entry, `arch_enter_user`, `EM_AARCH64` ELF loader, TPIDR_EL0 TLS, FP/SIMD context, signal frames; build an aarch64 libc subset (setjmp, syscall stub, crt0, fenv optional) and a minimal init/sh in initramfs.
7. **Devices:** PCI ECAM; then use AHCI/e1000/xHCI via PCI on QEMU virt; add virtio-pci (blk, net, rng, input, console) as the preferred drivers; GOP framebuffer console only.
8. **SMP:** Limine aarch64 SMP info, PSCI fallback, GIC SGIs, per-CPU via TPIDR_EL1.
9. **Late:** ACPI optional on arm, PSCI power-off, PL031 RTC, ports/userspace builds, installer (EFI-only, `BOOTAA64.EFI`), kernel package/mkinitcpio flow with `KERNEL_ARCH=aarch64`.

Caveats of this inventory: counts are from grep and slightly inflated by `inline` matches; `gop.c` asm purpose and `lib/string.c` were not read in detail; `acpi/` is a separate repo cloned into the tree (not edited here).
