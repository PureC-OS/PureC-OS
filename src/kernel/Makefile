ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk
OBJ_DIR := $(BIN_DIR)/obj/kernel
KERNEL_DIR := $(BIN_DIR)/kernel
TARGET := $(KERNEL_DIR)/kernel-limine.elf
FALLBACK := $(KERNEL_DIR)/kernel-fallback.elf
KERNEL_MAP := $(KERNEL_DIR)/kernel.map
KERNEL_LD_MAP := $(KERNEL_DIR)/kernel-limine.ld.map
LINKER_SCRIPT := $(ROOT_DIR)/linker-limine.ld
ACPI_DIR := $(ROOT_DIR)/acpi
ACPI_REPO ?= https://github.com/PureC-OS/PureC-OS-ACPI.git
ACPI_SOURCES := acpi/src/acpi.c acpi/src/acpi_s5.c acpi/src/acpi_power.c acpi/src/uacpi_glue.c acpi/src/uacpi_heap.c acpi/src/acpi_ec.c acpi/src/acpi_battery.c acpi/src/acpi_sensors.c \
	acpi/AML/uACPI/source/tables.c acpi/AML/uACPI/source/types.c \
	acpi/AML/uACPI/source/utilities.c acpi/AML/uACPI/source/stdlib.c \
	acpi/AML/uACPI/source/shareable.c acpi/AML/uACPI/source/uacpi.c \
	acpi/AML/uACPI/source/namespace.c acpi/AML/uACPI/source/interpreter.c \
	acpi/AML/uACPI/source/opcodes.c acpi/AML/uACPI/source/opregion.c \
	acpi/AML/uACPI/source/registers.c acpi/AML/uACPI/source/resources.c \
	acpi/AML/uACPI/source/event.c acpi/AML/uACPI/source/mutex.c \
	acpi/AML/uACPI/source/notify.c acpi/AML/uACPI/source/osi.c \
	acpi/AML/uACPI/source/sleep.c acpi/AML/uACPI/source/io.c \
	acpi/AML/uACPI/source/default_handlers.c
ACPI_OBJECTS := $(ACPI_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPPFLAGS += -I$(ACPI_DIR)/include -I$(ACPI_DIR)/AML/uACPI/include
E1000_DIR := $(ROOT_DIR)/src/drivers/net/e1000
CPPFLAGS += -I$(E1000_DIR)/include
E1000_GC_DIR := $(ROOT_DIR)/src/drivers/net/82543gc
CPPFLAGS += -I$(E1000_GC_DIR)/include
PCNET_DIR := $(ROOT_DIR)/src/drivers/net/pcnet
CPPFLAGS += -I$(PCNET_DIR)/include
ATH9K_DIR := $(ROOT_DIR)/src/drivers/net/ath9k
CPPFLAGS += -I$(ATH9K_DIR)/include
DOT11_DIR := $(ROOT_DIR)/src/net/802.11
CPPFLAGS += -I$(DOT11_DIR)/include -I$(ROOT_DIR)/src

C_SOURCES := \
	boot/boot.c \
	kernel/core/kernel.c \
	kernel/core/init.c \
	kernel/core/system_info.c \
	kernel/smp/cpu.c \
	kernel/smp/smp.c \
	kernel/smp/lapic.c \
	kernel/smp/cpufreq.c \
	kernel/sync/mutex.c \
	kernel/process/process.c \
	kernel/process/program_alias.c \
	kernel/process/elf.c \
	kernel/module/kmod_so.c \
	kernel/random/krandom.c \
	kernel/time/civil.c \
	kernel/time/rtc.c \
	kernel/time/wallclock.c \
	kernel/ipc/ipc.c \
	kernel/ipc/pipe.c \
	kernel/process/signal.c \
	kernel/process/fdtable.c \
	kernel/process/ofd.c \
	mm/pmm.c \
	mm/vmm.c \
	mm/oom/oom.c \
	mm/oom/oom_account.c \
	mm/oom/oom_victim.c \
	mm/oom/oom_pressure.c \
	mm/oom/oom_reclaim.c \
	mm/oom/oom_slab.c \
	mm/oom/oom_gate.c \
	mm/oom/oom_info.c \
	arch/x86_64/gdt/c/gdt.c \
	arch/x86_64/idt/c/idt.c \
	arch/x86_64/fpu/fpu.c \
	arch/x86_64/mmio.c \
	kernel/syscall/syscall.c \
	drivers/serial/serial.c \
	drivers/display/vga.c \
	drivers/interrupts/pic.c \
	drivers/interrupts/timer.c \
	drivers/display/fb.c \
	drivers/display/gop.c \
	gfx/compositor.c \
	gfx/blit.c \
	drivers/display/vbe.c \
	drivers/display/svga.c \
	drivers/mouse/ps2_mouse.c \
	drivers/mouse/usb_mouse.c \
	drivers/input/i2c_hid_touchpad/i2c_hid_touchpad.c \
	lib/string.c \
	kernel/diagnostics/klog.c \
	kernel/diagnostics/boot_diag.c \
	kernel/diagnostics/ksym.c \
	kernel/diagnostics/qr.c \
	kernel/diagnostics/panic_qr.c \
	kernel/diagnostics/panic.c \
	drivers/input/keyboard.c \
	drivers/input/hid_keymap.c \
	drivers/pci/pci.c \
	drivers/net/e1000/e1000_82540em.c \
	drivers/net/82543gc/e1000_82543gc.c \
	drivers/net/pcnet/pcnet_am79c970a.c \
	net/wifi/wifi.c \
	net/802.11/assoc.c \
	net/802.11/data.c \
	net/802.11/b/b_rates.c \
	net/802.11/b/b_chan.c \
	net/802.11/b/b_plcp.c \
	net/802.11/b/b_mgmt.c \
	drivers/net/ath9k/ar9285.c \
	drivers/net/ath9k/ath9k_hw.c \
	drivers/net/ath9k/ath9k_eeprom.c \
	net/core/net_device.c \
	net/link/ethernet.c \
	net/link/arp.c \
	net/network/ipv4.c \
	net/transport/udp.c \
	net/transport/tcp.c \
	net/name/dns.c \
	net/config/dhcp.c \
	net/diagnostics/icmp.c \
	net/api/ping.c \
	net/tests/qemu_network_test.c \
	net/core/net_service.c \
	drivers/storage/ata_pio.c \
	drivers/storage/ahci.c \
	drivers/storage/block_device.c \
	drivers/storage/storage_probe.c \
	drivers/usb/xhci.c \
	drivers/usb/ehci.c \
	fs/install.c \
	fs/ext2/block.c \
	fs/ext2/super.c \
	fs/ext2/inode.c \
	fs/ext2/dir.c \
	fs/ext2/file.c \
	fs/ext2/format.c \
	fs/ext2/meta.c \
	fs/ext2/ext2.c \
	fs/ext2/debug.c \
	fs/vfs.c \
	fs/vfs_path.c \
	fs/vfs_alias.c \
	fs/vfs_perm.c \
	fs/install_accounts.c \
	fs/vfs_mount.c \
	fs/vfs_backends.c \
	fs/devfs.c \
	fs/procfs.c \
	fs/sysfs.c \
	fs/tmpfs.c \
	lib/format.c \
	fs/initramfs.c \
	userspace/display.c \
	userspace/display_mode.c \
	userspace/fs.c \
	userspace/personalization.c \
	userspace/wallpaper.c \
	userspace/audio.c \
	userspace/audio_overlay.c \
	userspace/userspace.c \
	userspace/rs_session.c \
	userspace/window_manager.c \
	userspace/apps/datetime_service.c \
	userspace/apps/desktop_entries.c \
	userspace/apps/clock_app.c \
	userspace/apps/calendar_app.c \
	userspace/apps/calculator_app.c \
	userspace/apps/desktop_apps.c \
	userspace/apps/bottom_panel.c \
	kernel/process/scheduler.c \
	drivers/power/power.c \
	drivers/audio/audio_hda.c \
	drivers/audio/audio.c \
	kernel/devices/device_manager.c
CPP_SOURCES := userspace/apps/audio_panel.cpp
ASM_SOURCES := \
	arch/x86_64/gdt/asm/gdt.asm \
	arch/x86_64/idt/asm/idt.asm \
	arch/x86_64/smp.asm \
	arch/x86_64/scheduler.asm \
	arch/x86_64/user.asm
C_OBJECTS := $(C_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.asm.o)
GEN_DIR := $(OBJ_DIR)/generated
GEN_STAMP := $(GEN_DIR)/build_stamp.c
GEN_STAMP_OBJ := $(GEN_DIR)/build_stamp.o
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS) $(ASM_OBJECTS) $(ACPI_OBJECTS) $(GEN_STAMP_OBJ)

BUILD_UUID := $(shell cat /proc/sys/kernel/random/uuid 2>/dev/null)
BUILD_DATE := $(shell date -u +%Y-%m-%dT%H:%M:%SZ)

.PHONY: all acpi-fetch force
all: acpi-fetch $(TARGET)
	@if [ ! -f $(FALLBACK) ]; then cp $(TARGET) $(FALLBACK); fi

acpi-fetch:
	@if [ ! -f $(ACPI_DIR)/src/acpi.c ]; then \
		echo "acpi module not found, cloning $(ACPI_REPO)..."; \
		git clone $(ACPI_REPO) $(ACPI_DIR); \
	fi

-include $(C_OBJECTS:.o=.d) $(CPP_OBJECTS:.o=.d) $(ACPI_OBJECTS:.o=.d) $(GEN_STAMP_OBJ:.o=.d)

$(TARGET): $(OBJECTS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	@if [ -f $@ ]; then cp $@ $(FALLBACK); fi
	$(LD) -T $(LINKER_SCRIPT) -Map=$(KERNEL_LD_MAP) -o $@ $(OBJECTS)
	@$(CROSS)nm -n --defined-only $@ | python3 $(ROOT_DIR)/mk/ksyms.py $@
	@{ echo "# PureC-OS kernel.map"; echo "# build $(BUILD_UUID) $(BUILD_DATE)"; $(CROSS)nm -n --defined-only $@; } > $(KERNEL_MAP)
	@python3 $(ROOT_DIR)/mk/build_db.py record --db $(ROOT_DIR)/db/builds.db --uuid "$(BUILD_UUID)" --time "$(BUILD_DATE)" --map "$(KERNEL_MAP)"
	@echo "kernel.map: $$(wc -l < $(KERNEL_MAP)) symbols -> $(KERNEL_MAP)"
	@echo "ld map: $(KERNEL_LD_MAP)"
	@if [ ! -f $(FALLBACK) ]; then cp $@ $(FALLBACK); fi

$(GEN_STAMP): force
	@mkdir -p $(@D)
	@printf 'const char kernel_build_uuid[] = "%s";\nconst char kernel_build_time[] = "%s";\n' "$(BUILD_UUID)" "$(BUILD_DATE)" > $@

$(GEN_STAMP_OBJ): $(GEN_STAMP)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/acpi/%.o: $(ACPI_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.asm.o: $(ROOT_DIR)/src/%.asm
	@mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@