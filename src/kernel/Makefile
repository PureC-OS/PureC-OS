ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk
include $(ROOT_DIR)/src/kernel/kconfig
ifneq ($(KERNEL_ARCH),$(ARCH))
$(error KERNEL_ARCH=$(KERNEL_ARCH) does not match ARCH=$(ARCH))
endif
OBJ_DIR := $(BIN_DIR)/obj/kernel
KERNEL_DIR := $(BIN_DIR)/kernel
TARGET := $(KERNEL_DIR)/kernel-limine.elf
FALLBACK := $(KERNEL_DIR)/kernel-fallback.elf
KERNEL_MAP := $(KERNEL_DIR)/kernel.map
KERNEL_LD_MAP := $(KERNEL_DIR)/kernel-limine.ld.map
LINKER_SCRIPT := $(ARCH_LINKER_SCRIPT)
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
CRYPT_DIR := $(ROOT_DIR)/libxcrypt
CPPFLAGS += -I$(CRYPT_DIR)/include
CRYPT_OBJECTS := $(OBJ_DIR)/xcrypt/sha512.o $(OBJ_DIR)/xcrypt/crypt_sha512.o $(OBJ_DIR)/xcrypt/sha1.o $(OBJ_DIR)/xcrypt/aes.o
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

C_SOURCES := $(ARCH_C_SOURCES) \
	boot/boot.c \
	kernel/core/kernel.c \
	kernel/core/init.c \
	kernel/core/system_info.c \
	kernel/smp/cpu.c \
	kernel/smp/percpu.c \
	kernel/sync/mutex.c \
	kernel/process/process.c \
	kernel/process/program_alias.c \
	kernel/process/elf.c \
	kernel/module/kmod_so.c \
	kernel/random/krandom.c \
	kernel/time/civil.c \
	kernel/time/rtc.c \
	kernel/time/wallclock.c \
	kernel/time/tz.c \
	kernel/ipc/ipc.c \
	kernel/ipc/pipe.c \
	kernel/ipc/pty.c \
	kernel/ipc/evfd.c \
	kernel/ipc/inputdev.c \
	kernel/ipc/unix.c \
	kernel/process/signal.c \
	kernel/process/futex.c \
	kernel/process/vma.c \
	kernel/process/fdtable.c \
	kernel/process/ofd.c \
	kernel/tty/tty.c \
	kernel/tty/vtcon.c \
	mm/pmm.c \
	mm/shm.c \
	mm/vmm.c \
	mm/oom/oom.c \
	mm/oom/oom_account.c \
	mm/oom/oom_victim.c \
	mm/oom/oom_pressure.c \
	mm/oom/oom_reclaim.c \
	mm/oom/oom_slab.c \
	mm/oom/oom_gate.c \
	mm/oom/oom_info.c \
	kernel/syscall/syscall.c \
	kernel/syscall/sys/common.c \
	kernel/syscall/sys/fs/fs.c \
	kernel/syscall/sys/process/process.c \
	kernel/syscall/sys/signal/signal.c \
	kernel/syscall/sys/thread/thread.c \
	kernel/syscall/sys/mm/mm.c \
	kernel/syscall/sys/event/event.c \
	kernel/syscall/sys/net/net.c \
	kernel/syscall/sys/wifi/wifi.c \
	kernel/syscall/sys/user/user.c \
	kernel/syscall/sys/tty/tty.c \
	kernel/syscall/sys/display/display.c \
	kernel/syscall/sys/audio/audio.c \
	kernel/syscall/sys/disk/disk.c \
	kernel/syscall/sys/system/system.c \
	kernel/syscall/sys/user/sudo.c \
	kernel/trace/trace.c \
	drivers/serial/serial.c \
	drivers/display/vga.c \
	drivers/display/fb.c \
	drivers/display/gop.c \
	drivers/display/display_owner.c \
	drivers/display/console_vt.c \
	drivers/display/vt_font.c \
	drivers/display/vbe.c \
	drivers/display/svga.c \
	drivers/display/svga3d.c \
	drivers/mouse/ps2_mouse.c \
	drivers/mouse/usb_mouse.c \
	drivers/mouse/vbox_mouse.c \
	drivers/input/i2c_hid_touchpad/i2c_hid_touchpad.c \
	drivers/net/iwx/iwx.c \
	lib/string.c \
	kernel/diagnostics/klog.c \
	kernel/diagnostics/klog_disk.c \
	kernel/diagnostics/boot_diag.c \
	kernel/diagnostics/ksym.c \
	kernel/diagnostics/qr.c \
	kernel/diagnostics/panic_qr.c \
	kernel/diagnostics/panic_screen.c \
	kernel/diagnostics/panic.c \
	drivers/input/keyboard.c \
	drivers/input/hid_keymap.c \
	drivers/pci/pci.c \
	drivers/net/e1000/e1000_82540em.c \
	drivers/net/82543gc/e1000_82543gc.c \
	drivers/net/pcnet/pcnet_am79c970a.c \
	net/wifi/wifi.c \
	net/wifi/wpa.c \
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
	net/transport/udp_endpoint.c \
	net/core/loopback.c \
	net/transport/tcp.c \
	net/name/dns.c \
	net/config/dhcp.c \
	net/diagnostics/icmp.c \
	net/api/ping.c \
	net/api/socket.c \
	net/api/sockstat.c \
	net/tests/qemu_network_test.c \
	net/core/net_service.c \
	drivers/usb/xhci.c \
	drivers/usb/ehci.c \
	fs/install.c \
	fs/ext2/block.c \
	fs/ext2/bcache.c \
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
	fs/vfs_fat32.c \
	kernel/diagnostics/journal.c \
	lib/format.c \
	fs/initramfs.c \
	kernel/process/scheduler.c \
	drivers/power/power.c \
	drivers/audio/audio_hda.c \
	drivers/audio/audio.c \
	kernel/devices/device_manager.c
CPP_SOURCES :=
ASM_SOURCES := $(ARCH_ASM_SOURCES)
C_OBJECTS := $(C_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.asm.o)
LIBC_DIR := $(ROOT_DIR)/programs/PureC-OS-Core-Programs/libc
GEN_DIR := $(OBJ_DIR)/generated
GEN_STAMP := $(GEN_DIR)/build_stamp.c
GEN_STAMP_OBJ := $(GEN_DIR)/build_stamp.o
IO_OBJECT := $(BIN_DIR)/io/io.o
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS) $(ASM_OBJECTS) $(ACPI_OBJECTS) $(CRYPT_OBJECTS) $(GEN_STAMP_OBJ) $(IO_OBJECT)

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

SYSCALL_TBL := $(ROOT_DIR)/src/arch/$(ARCH)/entry/syscalls/syscall_64.tbl
SYSCALL_GEN := $(ROOT_DIR)/mk/syscalltbl.py
SYSCALL_STAMP := $(OBJ_DIR)/syscalls.stamp

$(SYSCALL_STAMP): $(SYSCALL_TBL) $(SYSCALL_GEN)
	@mkdir -p $(@D)
	python3 $(SYSCALL_GEN) $(SYSCALL_TBL) \
		--nr $(ROOT_DIR)/src/kernel/syscall/syscall_nr.h \
		--nr $(LIBC_DIR)/include/purec/abi/syscall_nr.h \
		--proto $(ROOT_DIR)/src/kernel/syscall/syscall_table.h \
		--table $(ROOT_DIR)/src/arch/$(ARCH)/entry/syscall_table.c
	@touch $@

$(C_OBJECTS) $(ASM_OBJECTS): | $(SYSCALL_STAMP)

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

$(IO_OBJECT): force
	@$(MAKE) -C $(ROOT_DIR)/src/io

$(GEN_STAMP): force
	@mkdir -p $(@D)
	@printf 'const char kernel_build_uuid[] = "%s";\nconst char kernel_build_time[] = "%s";\nconst char kernel_name[] = "%s";\nconst char kernel_version[] = "%s";\n' "$(BUILD_UUID)" "$(BUILD_DATE)" "$(KERNEL_NAME)" "$(KERNEL_VERSION)" > $@

$(GEN_STAMP_OBJ): $(GEN_STAMP)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/xcrypt/%.o: $(CRYPT_DIR)/src/%.c
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
	$(NASM) -f $(ARCH_NASM_FORMAT) $(ARCH_NASM_FLAGS) $< -o $@