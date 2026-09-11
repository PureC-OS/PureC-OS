ROOT_DIR := $(CURDIR)
override BIN_DIR := $(ROOT_DIR)/bin
KERNEL_DIR := $(BIN_DIR)/kernel
PROGRAM_DIR := $(BIN_DIR)/programs
LIB_DIR := $(BIN_DIR)/lib
ISO_ROOT := $(BIN_DIR)/iso_root
ISO_IMAGE := $(BIN_DIR)/purec_limine.iso
LIMINE_CONFIG := $(BIN_DIR)/staged/limine.conf
CRYPT_DIR := $(ROOT_DIR)/libxcrypt
TCC_DIR := $(ROOT_DIR)/tcc
CRYPT_REPO := https://github.com/PureC-OS/libxcrypt.git
TCC_REPO := https://github.com/PureC-OS/PureC-TCC.git


export ROOT_DIR BIN_DIR

.DEFAULT_GOAL := all
.PHONY: all libraries programs kernel crypt-fetch iso hexedit clean help

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

kernel: crypt-fetch
	$(MAKE) -C src/kernel
	$(MAKE) -C src/fs/ext2
	$(MAKE) -C $(CRYPT_DIR) module

# Fetch the password-hashing sources if they are missing (fresh clone of
# the OS repo does not include the nested libxcrypt checkout).
crypt-fetch:
	@if [ ! -f "$(CRYPT_DIR)/src/sha512.c" ]; then \
		echo "libxcrypt not found, cloning $(CRYPT_REPO)..."; \
		git clone $(CRYPT_REPO) $(CRYPT_DIR); \
	fi

tcc-fetch:
	@if [ ! -f "$(TCC_DIR)/*" ]; then \
		echo "PureC-TCC not found, cloning $(TCC_REPO)..."; \
		git clone $(TCC_REPO) $(TCC_DIR); \
	fi



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
		"$(ISO_ROOT)/bin/program" "$(ISO_ROOT)/bin/modules" \
		"$(ISO_ROOT)/src/demo"; \
	if gcc -O2 src/demo/create_demo.c -o /tmp/create_demo 2>/dev/null; then \
		/tmp/create_demo src/demo/screenshot.bmp 2>/dev/null || true; \
	fi; \
	command -v python3 >/dev/null || { echo "python3 is required (mk/gen_assets.py)" >&2; exit 1; }; \
	python3 "$(ROOT_DIR)/mk/gen_assets.py" --root "$(ROOT_DIR)" --iso "$(ISO_ROOT)" --staged "$(BIN_DIR)/staged"; \
	cp "$(KERNEL_DIR)/kernel-limine.elf" "$(ISO_ROOT)/boot/kernel.elf"; \
	cp "$(KERNEL_DIR)/kernel-fallback.elf" "$(ISO_ROOT)/boot/kernel-fallback.elf"; \
	if [ -d "$(ROOT_DIR)/src/demo" ]; then cp -r $(ROOT_DIR)/src/demo/* "$(ISO_ROOT)/src/demo/" 2>/dev/null || true; fi; \
	if [ -f "$(BIN_DIR)/modules/ext2.elf" ]; then cp "$(BIN_DIR)/modules/ext2.elf" "$(ISO_ROOT)/bin/modules/ext2.elf"; fi; \
	if [ -f "$(BIN_DIR)/modules/ext2.ko" ]; then cp "$(BIN_DIR)/modules/ext2.ko" "$(ISO_ROOT)/bin/modules/ext2.ko"; fi; \
	if [ -f "$(BIN_DIR)/modules/crypt.elf" ]; then cp "$(BIN_DIR)/modules/crypt.elf" "$(ISO_ROOT)/bin/modules/crypt.elf"; fi; \
	if [ -f "$(BIN_DIR)/modules/crypt.ko" ]; then cp "$(BIN_DIR)/modules/crypt.ko" "$(ISO_ROOT)/bin/modules/crypt.ko"; fi; \
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

help:
	@echo "make              собрать ядро, программы, библиотеки и ISO"
	@echo "make kernel       собрать только ядро"
	@echo "make libraries    собрать только библиотеки"
	@echo "make programs     собрать библиотеки и ring-3 программы"
	@echo "make hexedit      собрать только HexEdit (C++)"
	@echo "make iso          собрать итоговый ISO"