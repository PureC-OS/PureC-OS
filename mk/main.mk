ROOT_DIR := $(CURDIR)
override BIN_DIR    := $(ROOT_DIR)/bin
KERNEL_DIR          := $(BIN_DIR)/kernel
PROGRAM_DIR         := $(BIN_DIR)/programs
ISO_ROOT            := $(BIN_DIR)/iso_root
ISO_IMAGE           := $(BIN_DIR)/purec_limine.iso
LIMINE_CONFIG       := $(BIN_DIR)/staged/limine.conf

CRYPT_DIR           := $(ROOT_DIR)/libxcrypt
TCC_DIR             := $(ROOT_DIR)/tcc
ACPI_DIR            := $(ROOT_DIR)/acpi
NOTEPAD_DIR         := $(ROOT_DIR)/purec-notepad-os

export ROOT_DIR BIN_DIR

.DEFAULT_GOAL := all
.PHONY: all libraries programs kernel iso hexedit notepad clean help \
	test test-cpu test-scheduler-cpu test-pmm-smp test-string test-program-alias test-path \
	test-initramfs test-dot11 test-devman test-qemu-network test-qemu-network-e1000 \
	test-qemu-network-8254xgc test-qemu-network-pcnet

HOST_CC ?= cc
HOST_TEST_FLAGS := -std=c11 -Wall -Wextra -Werror -g -I$(ROOT_DIR)/src

test: test-cpu test-scheduler-cpu test-pmm-smp test-string test-program-alias test-path test-dot11 test-initramfs test-devman
	@echo "All host tests passed"

test-initramfs:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) tests/initramfs_boot_test.c src/fs/initramfs.c \
		-o $(BIN_DIR)/tests/initramfs_boot_test
	$(BIN_DIR)/tests/initramfs_boot_test

test-cpu:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/cpu_topology_test.c src/kernel/smp/cpu.c -o $(BIN_DIR)/tests/cpu_topology_test
	$(BIN_DIR)/tests/cpu_topology_test

test-scheduler-cpu:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST -pthread -ffunction-sections -fdata-sections \
		tests/scheduler_cpu_test.c -Wl,--gc-sections -o $(BIN_DIR)/tests/scheduler_cpu_test
	$(BIN_DIR)/tests/scheduler_cpu_test

test-pmm-smp:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST -pthread \
		tests/pmm_smp_test.c src/mm/pmm.c -o $(BIN_DIR)/tests/pmm_smp_test
	$(BIN_DIR)/tests/pmm_smp_test

test-string:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -fno-builtin \
		tests/kernel_string_test.c src/lib/string.c -o $(BIN_DIR)/tests/kernel_string_test
	$(BIN_DIR)/tests/kernel_string_test

test-program-alias:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -fno-builtin \
		tests/program_alias_test.c src/kernel/process/program_alias.c src/lib/string.c \
		-o $(BIN_DIR)/tests/program_alias_test
	$(BIN_DIR)/tests/program_alias_test

test-path:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/path_test.c src/programs/terminal/path.c src/programs/files/path.c \
		-o $(BIN_DIR)/tests/path_test
	$(BIN_DIR)/tests/path_test

test-dot11:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/dot11_assoc_test.c src/net/802.11/assoc.c -o $(BIN_DIR)/tests/dot11_assoc_test
	$(BIN_DIR)/tests/dot11_assoc_test
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/dot11_b_test.c src/net/802.11/b/b_rates.c src/net/802.11/b/b_chan.c \
		src/net/802.11/b/b_plcp.c src/net/802.11/b/b_mgmt.c -o $(BIN_DIR)/tests/dot11_b_test
	$(BIN_DIR)/tests/dot11_b_test

test-devman:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/devman_match_test.c -o $(BIN_DIR)/tests/devman_match_test
	$(BIN_DIR)/tests/devman_match_test

test-qemu-network: test-qemu-network-e1000 test-qemu-network-8254xgc test-qemu-network-pcnet
	@echo "All QEMU wired-network tests passed"

test-qemu-network-e1000:
	@test -f "$(ISO_IMAGE)" || { echo "Missing $(ISO_IMAGE); run: make iso"; exit 1; }
	python3 tests/qemu_network.py --iso "$(ISO_IMAGE)" --driver e1000

test-qemu-network-8254xgc:
	@test -f "$(ISO_IMAGE)" || { echo "Missing $(ISO_IMAGE); run: make iso"; exit 1; }
	python3 tests/qemu_network.py --iso "$(ISO_IMAGE)" --driver 8254xgc

test-qemu-network-pcnet:
	@test -f "$(ISO_IMAGE)" || { echo "Missing $(ISO_IMAGE); run: make iso"; exit 1; }
	python3 tests/qemu_network.py --iso "$(ISO_IMAGE)" --driver pcnet

all: iso

libraries:
	$(MAKE) -C src/libc
	$(MAKE) -C src/libgui
	$(MAKE) -C src/libfs
	$(MAKE) -C src/libaudio

programs: libraries notepad
	$(MAKE) -C src/programs
	$(MAKE) -C $(ROOT_DIR)/lang ROOT_DIR=$(ROOT_DIR) BIN_DIR=$(BIN_DIR)

notepad:
	@test -f "$(NOTEPAD_DIR)/Makefile" || \
	  { echo "ERROR: purec-notepad-os/ not found. Run: ./purec.py setup"; exit 1; }
	$(MAKE) -C $(NOTEPAD_DIR)
	@mkdir -p $(PROGRAM_DIR)
	cp $(NOTEPAD_DIR)/bin/notepad $(PROGRAM_DIR)/notepad

hexedit: libraries
	$(MAKE) -C src/programs/hexedit


kernel:
	@test -f "$(CRYPT_DIR)/src/sha512.c" || \
	  { echo "ERROR: libxcrypt/ not found. Run: ./purec.py setup"; exit 1; }
	@test -f "$(ACPI_DIR)/src/acpi.c" || \
	  { echo "ERROR: acpi/ not found. Run: ./purec.py setup"; exit 1; }
	$(MAKE) -C src/kernel
	$(MAKE) -C src/fs/ext2
	$(MAKE) -C src/drivers/net/e1000    module
	$(MAKE) -C src/drivers/net/82543gc  module
	$(MAKE) -C src/drivers/net/pcnet    module
	$(MAKE) -C $(CRYPT_DIR)             module
	$(MAKE) -C $(ACPI_DIR)              module

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
		echo "ERROR: Limine not found in /usr or /tmp/limine-pkg" >&2; exit 1; \
	fi; \
	rm -rf "$(ISO_ROOT)"; \
	mkdir -p "$(ISO_ROOT)/boot/limine" "$(ISO_ROOT)/EFI/BOOT" \
		"$(ISO_ROOT)/bin/program" "$(ISO_ROOT)/bin/modules" \
		"$(ISO_ROOT)/src/demo"; \
	if gcc -O2 src/demo/create_demo.c -o /tmp/create_demo 2>/dev/null; then \
		/tmp/create_demo src/demo/screenshot.bmp 2>/dev/null || true; \
	fi; \
	command -v python3 >/dev/null || { echo "python3 required (mk/gen_assets.py)" >&2; exit 1; }; \
	python3 "$(ROOT_DIR)/mk/gen_assets.py" --root "$(ROOT_DIR)" --iso "$(ISO_ROOT)" --staged "$(BIN_DIR)/staged"; \
	cp "$(KERNEL_DIR)/kernel-limine.elf"    "$(ISO_ROOT)/boot/kernel.elf"; \
	cp "$(KERNEL_DIR)/kernel-fallback.elf"  "$(ISO_ROOT)/boot/kernel-fallback.elf"; \
	[ -d "$(ROOT_DIR)/src/demo" ] && cp -r $(ROOT_DIR)/src/demo/* "$(ISO_ROOT)/src/demo/" 2>/dev/null || true; \
	for mod in ext2 crypt acpi e1000 e1000_82543gc pcnet_am79c970a; do \
		for ext in elf ko; do \
			f="$(BIN_DIR)/modules/$$mod.$$ext"; \
			[ -f "$$f" ] && cp "$$f" "$(ISO_ROOT)/bin/modules/$$mod.$$ext" || true; \
		done; \
	done; \
	[ -d "$(BIN_DIR)/modules" ] && cp -r "$(BIN_DIR)/modules/"* "$(ISO_ROOT)/bin/modules/" 2>/dev/null || true; \
	cp "$(LIMINE_CONFIG)"                        "$(ISO_ROOT)/boot/limine/limine.conf"; \
	cp "$(LIMINE_CONFIG)"                        "$(ISO_ROOT)/limine.conf"; \
	cp "$$limine_share/limine-bios.sys"          "$(ISO_ROOT)/boot/limine/"; \
	cp "$$limine_share/limine-bios-cd.bin"       "$(ISO_ROOT)/boot/limine/"; \
	cp "$$limine_share/limine-uefi-cd.bin"       "$(ISO_ROOT)/boot/limine/"; \
	cp "$$limine_share/BOOTX64.EFI"              "$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI"; \
	[ -f "$$limine_share/BOOTIA32.EFI" ] && cp "$$limine_share/BOOTIA32.EFI" "$(ISO_ROOT)/EFI/BOOT/" || true; \
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label "$(ISO_ROOT)" -o "$(ISO_IMAGE)"; \
	"$$limine_bin" bios-install "$(ISO_IMAGE)"; \
	echo "ISO created: $(ISO_IMAGE)"

clean:
	$(MAKE) -C src/kernel      clean 2>/dev/null || true
	$(MAKE) -C src/libc        clean 2>/dev/null || true
	$(MAKE) -C src/libgui      clean 2>/dev/null || true
	$(MAKE) -C src/libfs       clean 2>/dev/null || true
	$(MAKE) -C src/libaudio    clean 2>/dev/null || true
	$(MAKE) -C src/programs    clean 2>/dev/null || true
	$(MAKE) -C src/fs/ext2     clean 2>/dev/null || true
	rm -rf $(BIN_DIR)/iso_root $(BIN_DIR)/staged
	@echo "Clean done."

help:
	@echo ""
	@echo "  ./purec.py          — interactive TUI (recommended)"
	@echo ""
	@echo "  make                build kernel + programs + ISO"
	@echo "  make kernel         build kernel only"
	@echo "  make libraries      build libraries"
	@echo "  make programs       build Ring-3 programs"
	@echo "  make hexedit        build HexEdit"
	@echo "  make notepad        build PureC Notepad"
	@echo "  make iso            assemble ISO image"
	@echo "  make test           run all host tests"
	@echo "  make test-cpu       run host CPU discovery tests"
	@echo "  make test-dot11     run 802.11 association tests"
	@echo "  make test-qemu-network  test each wired NIC in a separate QEMU VM"
	@echo "  make clean          remove build artefacts"
	@echo ""
	@echo "  NOTE: external repos must be fetched first:"
	@echo "        ./purec.py setup"
	@echo ""
