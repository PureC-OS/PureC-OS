ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/kernel
KERNEL_DIR := $(BIN_DIR)/kernel
TARGET := $(KERNEL_DIR)/kernel-limine.elf
FALLBACK := $(KERNEL_DIR)/kernel-fallback.elf
LINKER_SCRIPT := $(ROOT_DIR)/linker-limine.ld

# Password-hashing library (separate libxcrypt repo). Its freestanding
# sources are compiled straight into the kernel image; the top-level
# Makefile additionally ships them as a loadable bin/modules/crypt.ko.
CRYPT_DIR := $(ROOT_DIR)/libxcrypt
CRYPT_REPO ?= https://github.com/PureC-OS/libxcrypt.git
CRYPT_SOURCES := $(CRYPT_DIR)/src/sha512.c $(CRYPT_DIR)/src/crypt_sha512.c
CRYPT_OBJECTS := $(OBJ_DIR)/libxcrypt/sha512.o $(OBJ_DIR)/libxcrypt/crypt_sha512.o

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
	arch/x86_64/idt.c \
	arch/x86_64/fpu.c \
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
	drivers/net/e1000_82540em.c \
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
	kernel/gui/window_manager.c \
	kernel/process/scheduler.c \
	drivers/power/power.c \
	drivers/audio/audio_hda.c \
	drivers/audio/audio.c
CPP_SOURCES :=
ASM_SOURCES := \
	arch/x86_64/gdt/asm/gdt.asm \
	arch/x86_64/idt.asm \
	arch/x86_64/scheduler.asm \
	arch/x86_64/user.asm
C_OBJECTS := $(C_SOURCES:%.c=$(OBJ_DIR)/%.o)
CPP_OBJECTS := $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(OBJ_DIR)/%.asm.o)
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS) $(ASM_OBJECTS) $(CRYPT_OBJECTS)

.PHONY: all crypt-fetch
all: crypt-fetch $(TARGET)
	@if [ ! -f $(FALLBACK) ]; then cp $(TARGET) $(FALLBACK); fi

# Fetch libxcrypt on demand so a direct `make -C src/kernel` also works
# on a fresh checkout without the nested repo.
crypt-fetch:
	@if [ ! -f $(CRYPT_DIR)/src/sha512.c ]; then \
		echo "libxcrypt not found, cloning $(CRYPT_REPO)..."; \
		git clone $(CRYPT_REPO) $(CRYPT_DIR); \
	fi

-include $(C_OBJECTS:.o=.d) $(CPP_OBJECTS:.o=.d) $(CRYPT_OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	@if [ -f $@ ]; then cp $@ $(FALLBACK); fi
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(OBJECTS)
	@if [ ! -f $(FALLBACK) ]; then cp $@ $(FALLBACK); fi

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

# libxcrypt lives outside src/, hence the explicit rule (same flags).
$(OBJ_DIR)/libxcrypt/%.o: $(CRYPT_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(CRYPT_DIR)/include $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.asm.o: $(ROOT_DIR)/src/%.asm
	@mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@
