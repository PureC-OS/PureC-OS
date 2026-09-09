ROOT_DIR := $(CURDIR)
override BIN_DIR := $(ROOT_DIR)/bin
KERNEL_DIR := $(BIN_DIR)/kernel
PROGRAM_DIR := $(BIN_DIR)/programs
LIB_DIR := $(BIN_DIR)/lib
ISO_ROOT := $(BIN_DIR)/iso_root
ISO_IMAGE := $(BIN_DIR)/purec_limine.iso
LIMINE_CONFIG := $(ROOT_DIR)/src/boot/limine.conf

export ROOT_DIR BIN_DIR

.DEFAULT_GOAL := all
.PHONY: all libraries programs kernel iso hexedit clean help

all: iso

libraries:
	$(MAKE) -C src/libc
	$(MAKE) -C src/libgui
	$(MAKE) -C src/libfs
	$(MAKE) -C src/libaudio

programs: libraries
	$(MAKE) -C src/programs

hexedit: libraries
	$(MAKE) -C src/programs/hexedit

kernel:
	$(MAKE) -C src/kernel
	$(MAKE) -C src/fs/ext2

iso: kernel programs
	@set -eu; \
	limine_share=""; \
	for candidate in /usr/share/limine /tmp/limine-pkg/usr/share/limine; do \
		if [ -f "$$candidate/limine-bios-cd.bin" ]; then limine_share="$$candidate"; break; fi; \
	done; \
	limine_bin=""; \
	for candidate in /usr/bin/limine /tmp/limine-pkg/usr/bin/limine; do \
		if [ -x "$$candidate" ]; then limine_bin="$$candidate"; break; fi; \
	done; \
	if [ -z "$$limine_share" ] || [ -z "$$limine_bin" ]; then \
		echo "Limine не найден в /usr или /tmp/limine-pkg" >&2; \
		exit 1; \
	fi; \
	rm -rf "$(ISO_ROOT)"; \
	mkdir -p "$(ISO_ROOT)/boot/limine" "$(ISO_ROOT)/EFI/BOOT" \
		"$(ISO_ROOT)/bin/program" "$(ISO_ROOT)/bin/modules" "$(ISO_ROOT)/lib" "$(ISO_ROOT)/include" \
		"$(ISO_ROOT)/src/demo" "$(ISO_ROOT)/demo"; \
	if gcc -O2 src/demo/create_demo.c -o /tmp/create_demo 2>/dev/null; then \
		/tmp/create_demo src/demo/screenshot.bmp 2>/dev/null || true; \
	fi; \
	cp "$(KERNEL_DIR)/kernel-limine.elf" "$(ISO_ROOT)/boot/kernel.elf"; \
	cp "$(KERNEL_DIR)/kernel-fallback.elf" "$(ISO_ROOT)/boot/kernel-fallback.elf"; \
	cp "$(PROGRAM_DIR)/init" "$(ISO_ROOT)/bin/init"; \
	cp "$(PROGRAM_DIR)/installer" "$(ISO_ROOT)/bin/installer"; \
	cp "$(PROGRAM_DIR)/snake" "$(ISO_ROOT)/bin/snake"; \
	cp "$(PROGRAM_DIR)/tetris" "$(ISO_ROOT)/bin/tetris"; \
	cp "$(PROGRAM_DIR)/terminal" "$(ISO_ROOT)/bin/program/terminal"; \
	cp "$(PROGRAM_DIR)/nano" "$(ISO_ROOT)/bin/program/nano"; \
	cp "$(PROGRAM_DIR)/system" "$(ISO_ROOT)/bin/program/system"; \
	cp "$(PROGRAM_DIR)/files" "$(ISO_ROOT)/bin/program/files"; \
	cp "$(PROGRAM_DIR)/gui-demo" "$(ISO_ROOT)/bin/gui-demo"; \
	cp "$(PROGRAM_DIR)/settings" "$(ISO_ROOT)/bin/program/settings"; \
	cp "$(PROGRAM_DIR)/monitor" "$(ISO_ROOT)/bin/program/monitor"; \
	cp "$(PROGRAM_DIR)/disks" "$(ISO_ROOT)/bin/program/disks"; \
	cp "$(PROGRAM_DIR)/logview" "$(ISO_ROOT)/bin/program/logview"; \
	cp "$(PROGRAM_DIR)/tetris" "$(ISO_ROOT)/bin/program/tetris"; \
	cp "$(PROGRAM_DIR)/hexedit" "$(ISO_ROOT)/bin/program/hexedit"; \
	if [ -f "$(PROGRAM_DIR)/imgview" ]; then cp "$(PROGRAM_DIR)/imgview" "$(ISO_ROOT)/bin/program/imgview"; fi; \
	cp "$(PROGRAM_DIR)/hello" "$(ISO_ROOT)/bin/program/hello"; \
	cp "$(LIB_DIR)/libpurec.a" "$(ISO_ROOT)/lib/libpurec.a"; \
	cp "$(LIB_DIR)/libpuregui.a" "$(ISO_ROOT)/lib/libpuregui.a"; \
	cp "$(LIB_DIR)/libpguiw.a" "$(ISO_ROOT)/lib/libpguiw.a"; \
	cp "$(LIB_DIR)/libpurefs.a" "$(ISO_ROOT)/lib/libpurefs.a"; \
	cp "$(LIB_DIR)/libpureaudio.a" "$(ISO_ROOT)/lib/libpureaudio.a"; \
	cp "$(ROOT_DIR)/src/libgui/include/puregui.h" "$(ISO_ROOT)/include/puregui.h"; \
	cp "$(ROOT_DIR)/src/libgui/include/pguiw.h" "$(ISO_ROOT)/include/pguiw.h"; \
	cp "$(ROOT_DIR)/src/libfs/include/purefs.h" "$(ISO_ROOT)/include/purefs.h"; \
	cp "$(ROOT_DIR)/src/libaudio/include/pureaudio.h" "$(ISO_ROOT)/include/pureaudio.h"; \
	if [ -d "$(ROOT_DIR)/src/demo" ]; then cp -r $(ROOT_DIR)/src/demo/* "$(ISO_ROOT)/src/demo/" 2>/dev/null || true; fi; \
	if [ -f "$(ROOT_DIR)/src/demo/screenshot.bmp" ]; then cp "$(ROOT_DIR)/src/demo/screenshot.bmp" "$(ISO_ROOT)/demo/screenshot.bmp"; fi; \
	if [ -f "$(BIN_DIR)/modules/ext2.elf" ]; then cp "$(BIN_DIR)/modules/ext2.elf" "$(ISO_ROOT)/bin/modules/ext2.elf"; fi; \
	if [ -f "$(BIN_DIR)/modules/ext2.ko" ]; then cp "$(BIN_DIR)/modules/ext2.ko" "$(ISO_ROOT)/bin/modules/ext2.ko"; fi; \
	cp "$(LIMINE_CONFIG)" "$(ISO_ROOT)/boot/limine/limine.conf"; \
	cp "$(LIMINE_CONFIG)" "$(ISO_ROOT)/limine.conf"; \
	cp "$$limine_share/limine-bios.sys" "$(ISO_ROOT)/boot/limine/limine-bios.sys"; \
	cp "$$limine_share/limine-bios-cd.bin" "$(ISO_ROOT)/boot/limine/limine-bios-cd.bin"; \
	cp "$$limine_share/limine-uefi-cd.bin" "$(ISO_ROOT)/boot/limine/limine-uefi-cd.bin"; \
	cp "$$limine_share/BOOTX64.EFI" "$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI"; \
	if [ -f "$$limine_share/BOOTIA32.EFI" ]; then \
		cp "$$limine_share/BOOTIA32.EFI" "$(ISO_ROOT)/EFI/BOOT/BOOTIA32.EFI"; \
	fi; \
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label "$(ISO_ROOT)" -o "$(ISO_IMAGE)"; \
	"$$limine_bin" bios-install "$(ISO_IMAGE)"; \
	echo "Готово: $(ISO_IMAGE)"

clean:
	@printf '%s\n' 'Ты чё, долбоёб ёбаный? Нахуй ты это делаешь, блять?' > /dev/null
	@echo "make clean заблокирован, иди нахуй."

help:
	@echo "make              собрать ядро, программы, библиотеки и ISO"
	@echo "make kernel       собрать только ядро"
	@echo "make libraries    собрать только библиотеки"
	@echo "make programs     собрать библиотеки и ring-3 программы"
	@echo "make hexedit      собрать только HexEdit (C++)"
	@echo "make iso          собрать итоговый ISO"