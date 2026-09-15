ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk
OBJ_DIR := $(BIN_DIR)/obj/kernel
KERNEL_DIR := $(BIN_DIR)/kernel
TARGET := $(KERNEL_DIR)/kernel-limine.elf
FALLBACK := $(KERNEL_DIR)/kernel-fallback.elf
LINKER_SCRIPT := $(ROOT_DIR)/linker-limine.ld
CRYPT_DIR := $(ROOT_DIR)/libxcrypt
CRYPT_REPO ?= https://github.com/PureC-OS/libxcrypt.git
CRYPT_SOURCES := $(CRYPT_DIR)/src/sha512.c $(CRYPT_DIR)/src/crypt_sha512.c
CRYPT_OBJECTS := $(OBJ_DIR)/libxcrypt/sha512.o $(OBJ_DIR)/libxcrypt/crypt_sha512.o
ACPI_DIR := $(ROOT_DIR)/acpi
ACPI_REPO ?= https://github.com/PureC-OS/PureC-OS-ACPI.git
ACPI_SOURCES := acpi/src/acpi.c acpi/src/acpi_s5.c acpi/src/acpi_power.c
ACPI_OBJECTS := $(ACPI_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPPFLAGS += -I$(ACPI_DIR)/include
E1000_DIR := $(ROOT_DIR)/src/drivers/net/e1000
CPPFLAGS += -I$(E1000_DIR)/include
E1000_GC_DIR := $(ROOT_DIR)/src/drivers/net/82543gc
CPPFLAGS += -I$(E1000_GC_DIR)/include
PCNET_DIR := $(ROOT_DIR)/src/drivers/net/pcnet
CPPFLAGS += -I$(PCNET_DIR)/include
ATH9K_DIR := $(ROOT_DIR)/src/drivers/net/ath9k
CPPFLAGS += -I$(ATH9K_DIR)/include

C_SOURCES := \
	boot/boot.c \
	kernel/core/kernel.c \
	kernel/core/init.c \
	kernel/core/system_info.c \
	kernel/process/process.c \
	kernel/process/program_alias.c \
	kernel/process/elf.c \
	mm/pmm.c \
	mm/vmm.c \
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
	drivers/mouse/ps2_mouse.c \
	drivers/mouse/usb_mouse.c \
	lib/string.c \
	kernel/diagnostics/klog.c \
	kernel/diagnostics/boot_diag.c \
	kernel/diagnostics/panic.c \
	drivers/input/keyboard.c \
	drivers/pci/pci.c \
	drivers/net/e1000/e1000_82540em.c \
	drivers/net/82543gc/e1000_82543gc.c \
	drivers/net/pcnet/pcnet_am79c970a.c \
	drivers/net/ath9k/ar9285.c \
	net/wifi/wifi.c \
	net/core/net_device.c \
	net/link/ethernet.c \
	net/link/arp.c \
	net/network/ipv4.c \
	net/transport/udp.c \
	net/name/dns.c \
	net/config/dhcp.c \
	net/diagnostics/icmp.c \
	net/api/ping.c \
	net/core/net_service.c \
	drivers/storage/ata_pio.c \
	drivers/storage/ahci.c \
	drivers/storage/block_device.c \
	drivers/storage/storage_probe.c \
	drivers/usb/xhci.c \
	drivers/usb/ehci.c \
	fs/fat/fat32.c \
	fs/ext2/block.c \
	fs/ext2/super.c \
	fs/ext2/inode.c \
	fs/ext2/dir.c \
	fs/ext2/file.c \
	fs/ext2/format.c \
	fs/ext2/ext2.c \
	fs/ext2/debug.c \
	fs/vfs.c \
	userspace/display.c \
	userspace/fs.c \
	userspace/personalization.c \
	userspace/wallpaper.c \
	userspace/audio.c \
	userspace/audio_overlay.c \
	userspace/userspace.c \
	userspace/window_manager.c \
	userspace/apps/datetime_service.c \
	userspace/apps/desktop_entries.c \
	userspace/apps/clock_app.c \
	userspace/apps/calendar_app.c \
	userspace/apps/calculator_app.c \
	userspace/apps/desktop_apps.c \
	kernel/process/scheduler.c \
	drivers/power/power.c \
	drivers/audio/audio_hda.c \
	drivers/audio/audio.c
CPP_SOURCES := userspace/apps/audio_panel.cpp
ASM_SOURCES := \
	arch/x86_64/gdt/asm/gdt.asm \
	arch/x86_64/idt/asm/idt.asm \
	arch/x86_64/scheduler.asm \
	arch/x86_64/user.asm
C_OBJECTS := $(C_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.asm.o)
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS) $(ASM_OBJECTS) $(CRYPT_OBJECTS) $(ACPI_OBJECTS)

.PHONY: all crypt-fetch acpi-fetch
all: crypt-fetch acpi-fetch $(TARGET)
	@if [ ! -f $(FALLBACK) ]; then cp $(TARGET) $(FALLBACK); fi

crypt-fetch:
	@if [ ! -f $(CRYPT_DIR)/src/sha512.c ]; then \
		echo "libxcrypt not found, cloning $(CRYPT_REPO)..."; \
		git clone $(CRYPT_REPO) $(CRYPT_DIR); \
	fi

acpi-fetch:
	@if [ ! -f $(ACPI_DIR)/src/acpi.c ]; then \
		echo "acpi module not found, cloning $(ACPI_REPO)..."; \
		git clone $(ACPI_REPO) $(ACPI_DIR); \
	fi

-include $(C_OBJECTS:.o=.d) $(CPP_OBJECTS:.o=.d) $(CRYPT_OBJECTS:.o=.d) $(ACPI_OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	@if [ -f $@ ]; then cp $@ $(FALLBACK); fi
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS)
	@if [ ! -f $(FALLBACK) ]; then cp $@ $(FALLBACK); fi

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/libxcrypt/%.o: $(CRYPT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(CRYPT_DIR)/include $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/acpi/%.o: $(ACPI_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.asm.o: $(ROOT_DIR)/src/%.asm
	@mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@
