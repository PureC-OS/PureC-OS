ROOT_DIR := $(CURDIR)
override BIN_DIR    := $(ROOT_DIR)/bin
KERNEL_DIR          := $(BIN_DIR)/kernel
PROGRAM_DIR         := $(BIN_DIR)/programs
ISO_ROOT            := $(BIN_DIR)/iso_root
ISO_IMAGE           := $(BIN_DIR)/purec_limine.iso
LIMINE_CONFIG       := $(BIN_DIR)/staged/limine.conf

CRYPT_DIR           := $(ROOT_DIR)/libxcrypt
WOLFSSL_DIR         := $(ROOT_DIR)/wolfssl
TCC_DIR             := $(ROOT_DIR)/tcc
ACPI_DIR            := $(ROOT_DIR)/acpi
NOTEPAD_DIR         := $(ROOT_DIR)/purec-notepad-os

export ROOT_DIR BIN_DIR

.DEFAULT_GOAL := all
.PHONY: all libraries programs kernel iso hexedit notepad wolf clean help sdl2 doomgeneric chocolate-doom \
	test test-cpu test-scheduler-cpu test-scheduler-balance test-oom test-pmm-smp test-pmm-refcount test-string test-program-alias test-path test-vfs-mount test-devfs test-format test-procfs test-sysfs test-ext2-append test-ext2-meta test-tmpfs test-vfs-perm test-blit test-hid-keymap test-scanf test-libm test-civil test-install-accounts test-accounts test-hdrbg test-vfs-alias test-tcp \
	test-initramfs test-dot11 test-devman test-ksym test-qr-encode test-qr-decode test-qemu-network test-qemu-network-e1000 \
	test-qemu-network-8254xgc test-qemu-network-pcnet

HOST_CC ?= cc
HOST_TEST_FLAGS := -std=c11 -Wall -Wextra -Werror -g -I$(ROOT_DIR)/src

test: test-cpu test-scheduler-cpu test-scheduler-balance test-oom test-pmm-smp test-string test-program-alias test-path test-vfs-mount test-devfs test-format test-procfs test-sysfs test-ext2-append test-tmpfs test-vfs-alias test-tcp test-hid-keymap test-scanf test-dot11 test-initramfs test-devman test-ipc test-ksym test-qr-encode test-wolfcrypt
	@echo "All host tests passed"

test-wolfcrypt:
	@test -f "$(WOLFSSL_DIR)/wolfcrypt/src/aes.c" || \
	  { echo "ERROR: wolfssl/ not found. Run: ./purec.py setup"; exit 1; }
	$(MAKE) -C $(WOLFSSL_DIR) check-crypt

test-ipc:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST \
		tests/ipc_mailbox_test.c -o $(BIN_DIR)/tests/ipc_mailbox_test
	$(BIN_DIR)/tests/ipc_mailbox_test

test-tcp:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST \
		tests/tcp_test.c src/net/transport/tcp.c -o $(BIN_DIR)/tests/tcp_test
	$(BIN_DIR)/tests/tcp_test

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
		tests/scheduler_cpu_test.c src/mm/oom/oom_slab.c -Wl,--gc-sections -o $(BIN_DIR)/tests/scheduler_cpu_test
	$(BIN_DIR)/tests/scheduler_cpu_test

test-scheduler-balance:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST -pthread -ffunction-sections -fdata-sections \
		tests/scheduler_balance_test.c src/mm/oom/oom_slab.c -Wl,--gc-sections -o $(BIN_DIR)/tests/scheduler_balance_test
	$(BIN_DIR)/tests/scheduler_balance_test

test-oom:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST \
		tests/oom_policy_test.c src/mm/oom/oom.c src/mm/oom/oom_account.c -o $(BIN_DIR)/tests/oom_policy_test
	$(BIN_DIR)/tests/oom_policy_test

test-pmm-refcount:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -DPUREC_HOST_TEST \
		tests/pmm_refcount_test.c src/mm/pmm.c -o $(BIN_DIR)/tests/pmm_refcount_test
	$(BIN_DIR)/tests/pmm_refcount_test

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

test-vfs-mount:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/vfs_mount_test.c src/fs/vfs_path.c src/fs/vfs_mount.c \
		-o $(BIN_DIR)/tests/vfs_mount_test
	$(BIN_DIR)/tests/vfs_mount_test

test-devfs:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src -I$(ROOT_DIR)/src/lib \
		tests/devfs_test.c src/fs/devfs.c -o $(BIN_DIR)/tests/devfs_test
	$(BIN_DIR)/tests/devfs_test

test-format:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/format_test.c src/lib/format.c -o $(BIN_DIR)/tests/format_test
	$(BIN_DIR)/tests/format_test

test-ext2-append:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src \
		tests/ext2_append_test.c src/fs/ext2/block.c src/fs/ext2/dir.c \
		src/fs/ext2/ext2.c src/fs/ext2/file.c src/fs/ext2/format.c \
		src/fs/ext2/inode.c src/fs/ext2/super.c src/fs/ext2/debug.c \
		src/fs/ext2/meta.c \
		-o $(BIN_DIR)/tests/ext2_append_test
	$(BIN_DIR)/tests/ext2_append_test

test-ext2-meta:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src \
		tests/ext2_meta_test.c src/fs/ext2/block.c src/fs/ext2/dir.c \
		src/fs/ext2/ext2.c src/fs/ext2/file.c src/fs/ext2/format.c \
		src/fs/ext2/inode.c src/fs/ext2/super.c src/fs/ext2/debug.c \
		src/fs/ext2/meta.c \
		-o $(BIN_DIR)/tests/ext2_meta_test
	$(BIN_DIR)/tests/ext2_meta_test

test-sysfs:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src \
		tests/sysfs_test.c src/fs/sysfs.c src/lib/format.c \
		-o $(BIN_DIR)/tests/sysfs_test
	$(BIN_DIR)/tests/sysfs_test

test-procfs:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src \
		tests/procfs_test.c src/fs/procfs.c src/lib/format.c \
		-o $(BIN_DIR)/tests/procfs_test
	$(BIN_DIR)/tests/procfs_test

test-tmpfs:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -I$(ROOT_DIR)/src \
		tests/tmpfs_test.c src/fs/tmpfs.c -o $(BIN_DIR)/tests/tmpfs_test
	$(BIN_DIR)/tests/tmpfs_test

test-vfs-perm:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) tests/vfs_perm_test.c src/fs/vfs_perm.c \
		-o $(BIN_DIR)/tests/vfs_perm_test
	$(BIN_DIR)/tests/vfs_perm_test

test-blit:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -g -I$(ROOT_DIR)/src -I$(ROOT_DIR)/src/lib tests/blit_test.c \
		src/gfx/blit.c -o $(BIN_DIR)/tests/blit_test
	$(BIN_DIR)/tests/blit_test

test-hid-keymap:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -g -I$(ROOT_DIR)/src tests/hid_keymap_test.c \
		src/drivers/input/hid_keymap.c -o $(BIN_DIR)/tests/hid_keymap_test
	$(BIN_DIR)/tests/hid_keymap_test

test-scanf:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -Dsscanf=pc_sscanf -Dvsscanf=pc_vsscanf -Dfscanf=pc_fscanf -Dvfscanf=pc_vfscanf -c src/libc/scanf.c -o $(BIN_DIR)/tests/scanf_pc.o
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g tests/scanf_test.c $(BIN_DIR)/tests/scanf_pc.o -o $(BIN_DIR)/tests/scanf_test
	$(BIN_DIR)/tests/scanf_test

test-libm:
	@mkdir -p $(BIN_DIR)/tests
	cc -std=gnu11 -O1 -Wall -Wextra -ffreestanding -fno-builtin -include tests/libm_rename.h -c src/libc/math.c -o $(BIN_DIR)/tests/libm_math.o
	cc -std=gnu11 -O1 -Wall -Wextra -ffreestanding -fno-builtin -include tests/libm_rename.h -c src/libc/libm.c -o $(BIN_DIR)/tests/libm_libm.o
	cc -std=gnu11 -O1 -Wall -Wextra -c tests/libm_test.c -o $(BIN_DIR)/tests/libm_test.o
	cc $(BIN_DIR)/tests/libm_test.o $(BIN_DIR)/tests/libm_math.o $(BIN_DIR)/tests/libm_libm.o -lm -o $(BIN_DIR)/tests/libm_test
	$(BIN_DIR)/tests/libm_test

test-civil:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -g -I$(ROOT_DIR)/src tests/civil_test.c \
		src/kernel/time/civil.c src/kernel/time/rtc.c -o $(BIN_DIR)/tests/civil_test
	$(BIN_DIR)/tests/civil_test

test-install-accounts:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) tests/install_accounts_test.c src/fs/install_accounts.c \
		src/fs/vfs_perm.c -o $(BIN_DIR)/tests/install_accounts_test
	$(BIN_DIR)/tests/install_accounts_test

test-accounts:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -I$(ROOT_DIR)/libxcrypt/include tests/accounts_test.c \
		src/programs/accounts/accounts_core.c src/fs/install_accounts.c \
		libxcrypt/src/sha512.c libxcrypt/src/crypt_sha512.c -o $(BIN_DIR)/tests/accounts_test
	$(BIN_DIR)/tests/accounts_test

test-hdrbg:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -I$(ROOT_DIR)/libxcrypt/include tests/hdrbg_test.c \
		src/drivers/random/random_core.c libxcrypt/src/sha512.c -o $(BIN_DIR)/tests/hdrbg_test
	$(BIN_DIR)/tests/hdrbg_test

test-vfs-alias:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) \
		tests/vfs_alias_test.c src/fs/vfs_alias.c src/fs/vfs_path.c \
		-o $(BIN_DIR)/tests/vfs_alias_test
	$(BIN_DIR)/tests/vfs_alias_test

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

test-ksym:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -I$(ROOT_DIR)/src/kernel/diagnostics \
		tests/ksym_resolve_test.c src/kernel/diagnostics/ksym.c \
		-o $(BIN_DIR)/tests/ksym_resolve_test
	$(BIN_DIR)/tests/ksym_resolve_test

test-qr-encode:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -I$(ROOT_DIR)/src/kernel/diagnostics \
		tests/qr_encode_test.c src/kernel/diagnostics/qr.c \
		-o $(BIN_DIR)/tests/qr_encode_test
	$(BIN_DIR)/tests/qr_encode_test

test-qr-decode:
	@mkdir -p $(BIN_DIR)/tests
	$(HOST_CC) $(HOST_TEST_FLAGS) -I$(ROOT_DIR)/src \
		tests/qr_dump.c src/kernel/diagnostics/qr.c \
		-o $(BIN_DIR)/tests/qr_dump
	python3 tests/qr_decode_test.py --tool $(BIN_DIR)/tests/qr_dump $(QR_DECODE_ARGS)

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

test-qemu-panic:
	@test -f "$(ISO_IMAGE)" || { echo "Missing $(ISO_IMAGE); run: make iso"; exit 1; }
	python3 tests/qemu_panic_test.py --iso "$(ISO_IMAGE)" \
		--log "$(BIN_DIR)/tests/qemu-panic.log" \
		--shot "$(BIN_DIR)/tests/qemu-panic-shot.ppm"

all: iso

libraries:
	$(MAKE) -C src/libc
	$(MAKE) -C src/libgui
	$(MAKE) -C src/libfs
	$(MAKE) -C src/libaudio

programs: libraries notepad
	$(MAKE) -C src/programs
	$(MAKE) -C src/programs/micropython
	@if test -f SDL/src/SDL.c; then $(MAKE) -C src/libsdl2; else echo "SDL/ not found, skipping SDL2 port"; fi
	$(MAKE) -C src/doom
	$(MAKE) -C src/chocolate
	$(MAKE) -C $(ROOT_DIR)/lang ROOT_DIR=$(ROOT_DIR) BIN_DIR=$(BIN_DIR)

sdl2: libraries
	$(MAKE) -C src/libsdl2

doomgeneric: libraries
	$(MAKE) -C src/doom

chocolate-doom: sdl2
	$(MAKE) -C src/chocolate

notepad:
	@test -f "$(NOTEPAD_DIR)/Makefile" || \
	  { echo "ERROR: purec-notepad-os/ not found. Run: ./purec.py setup"; exit 1; }
	$(MAKE) -C $(NOTEPAD_DIR)
	@mkdir -p $(PROGRAM_DIR)
	cp $(NOTEPAD_DIR)/bin/notepad $(PROGRAM_DIR)/notepad

hexedit: libraries
	$(MAKE) -C src/programs/hexedit

wolf: $(WOLFSSL_DIR)/wolfcrypt/src/aes.c
	$(MAKE) -C $(WOLFSSL_DIR) module
	$(MAKE) -C $(WOLFSSL_DIR) lib


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
	$(MAKE) -C src/drivers/net/ath9k    module
	$(MAKE) -C $(CRYPT_DIR)             module
	$(MAKE) -C $(CRYPT_DIR)             BIN_DIR=$(BIN_DIR)
	@mkdir -p $(BIN_DIR)/lib
	cp $(CRYPT_DIR)/build/libpurecrypt.a $(BIN_DIR)/lib/libpurecrypt.a
	$(MAKE) -C $(ACPI_DIR)              module
	$(MAKE) -C $(WOLFSSL_DIR)           module lib
	$(MAKE) -C src/drivers/random       module

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
		"$(ISO_ROOT)/bin/program" "$(ISO_ROOT)/lib/modules" \
		"$(ISO_ROOT)/lib/firmware" \
		"$(ISO_ROOT)/src/demo"; \
	if gcc -O2 src/demo/create_demo.c -o /tmp/create_demo 2>/dev/null; then \
		/tmp/create_demo src/demo/screenshot.bmp 2>/dev/null || true; \
	fi; \
	command -v python3 >/dev/null || { echo "python3 required (mk/gen_assets.py)" >&2; exit 1; }; \
	python3 "$(ROOT_DIR)/mk/gen_assets.py" --root "$(ROOT_DIR)" --iso "$(ISO_ROOT)" --staged "$(BIN_DIR)/staged" --modules "$(BIN_DIR)/modules"; \
	cp "$(KERNEL_DIR)/kernel-limine.elf"    "$(ISO_ROOT)/boot/kernel.elf"; \
	cp "$(KERNEL_DIR)/kernel-fallback.elf"  "$(ISO_ROOT)/boot/kernel-fallback.elf"; \
	[ -f "$(KERNEL_DIR)/kernel.map" ] && cp "$(KERNEL_DIR)/kernel.map" "$(ISO_ROOT)/boot/kernel.map" || true; \
	[ -d "$(ROOT_DIR)/src/demo" ] && cp -r $(ROOT_DIR)/src/demo/* "$(ISO_ROOT)/src/demo/" 2>/dev/null || true; \
	for mod in ext2 crypt acpi wolf random e1000 e1000_82543gc pcnet_am79c970a ar9285; do \
		for ext in elf ko; do \
			f="$(BIN_DIR)/modules/$$mod.$$ext"; \
			[ -f "$$f" ] && cp "$$f" "$(ISO_ROOT)/lib/modules/$$mod.$$ext" || true; \
		done; \
	done; \
	[ -d "$(BIN_DIR)/modules" ] && cp -r "$(BIN_DIR)/modules/"* "$(ISO_ROOT)/lib/modules/" 2>/dev/null || true; \
	cp "$$limine_share/BOOTX64.EFI"              "$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI"; \
	sh "$(ROOT_DIR)/mk/make_esp_image.sh" "$(ISO_ROOT)" "$(ISO_ROOT)/boot/esp.img"; \
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
	$(MAKE) -C src/libsdl2     clean 2>/dev/null || true
	$(MAKE) -C src/doom        clean 2>/dev/null || true
	$(MAKE) -C src/chocolate   clean 2>/dev/null || true
	$(MAKE) -C src/libsdl2_mixer clean 2>/dev/null || true
	$(MAKE) -C src/programs    clean 2>/dev/null || true
	$(MAKE) -C src/fs/ext2     clean 2>/dev/null || true
	$(MAKE) -C $(WOLFSSL_DIR)  clean 2>/dev/null || true
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
	@echo "  make wolf           build libwolf.a + wolf.ko (wolfSSL port)"
	@echo "  make iso            assemble ISO image"
	@echo "  make test           run all host tests"
	@echo "  make test-cpu       run host CPU discovery tests"
	@echo "  make test-dot11     run 802.11 association tests"
	@echo "  make test-ksym       run kernel symbol resolver tests"
	@echo "  make test-qr-encode  run QR encoder unit tests"
	@echo "  make test-qr-decode  run QR encode/decode round-trip test"
	@echo "  make test-qemu-network  test each wired NIC in a separate QEMU VM"
	@echo "  make clean          remove build artefacts"
	@echo ""
	@echo "  NOTE: external repos must be fetched first:"
	@echo "        ./purec.py setup"
	@echo ""
