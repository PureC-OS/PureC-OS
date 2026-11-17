CROSS ?= x86_64-elf-
KCONFIG             := $(ROOT_DIR)/src/kernel/kconfig
KERNEL_PKG_VERSION  := $(shell sed -n 's/^KERNEL_VERSION=//p' $(KCONFIG))
KERNEL_PKG_RELEASE  := $(shell sed -n 's/^KERNEL_RELEASE=//p' $(KCONFIG))
KERNEL_PKG_FULL     := $(KERNEL_PKG_VERSION)-$(KERNEL_PKG_RELEASE)
KERNEL_PKG_OUT      := $(BIN_DIR)/kernel-pkg
KERNEL_PKG_DIR      := usr/lib/kernel/$(KERNEL_PKG_FULL)
KERNEL_PKG_FILE     := $(KERNEL_PKG_OUT)/kernel-$(KERNEL_PKG_FULL)-x86_64.pkg.tar.gz
KERNEL_PKG_SIGN_KEY ?= $(HOME)/.config/purec-pkg/signing.pem

.PHONY: kernel-package
kernel-package: iso
	@mkdir -p "$(KERNEL_PKG_OUT)/stage"
	rm -f "$(KERNEL_PKG_OUT)"/kernel-*.pkg.tar.gz*
	$(CROSS)strip --strip-debug -o "$(KERNEL_PKG_OUT)/stage/kernel.elf" "$(KERNEL_DIR)/kernel-limine.elf"
	python3 "$(ROOT_DIR)/mk/pkg/mkpkg.py" --name kernel --version $(KERNEL_PKG_FULL) \
		--desc "PureC OS kernel, its modules and initramfs" -o "$(KERNEL_PKG_OUT)" \
		--file $(KERNEL_PKG_DIR)/kernel.elf="$(KERNEL_PKG_OUT)/stage/kernel.elf" \
		--file $(KERNEL_PKG_DIR)/initramfs.cpio="$(BIN_DIR)/staged/initramfs.cpio" \
		--file $(KERNEL_PKG_DIR)/kconfig="$(KCONFIG)" \
		--file $(KERNEL_PKG_DIR)/mkinitcpio="$(PROGRAM_DIR)/mkinitcpio" \
		--tree $(KERNEL_PKG_DIR)/modules="$(BIN_DIR)/modules" \
		--hook post-install="$(ROOT_DIR)/mk/kernel/kernel.hook" \
		--hook post-upgrade="$(ROOT_DIR)/mk/kernel/kernel.hook"
	if [ -f "$(KERNEL_PKG_SIGN_KEY)" ]; then python3 "$(ROOT_DIR)/mk/pkg/pkgsign.py" sign "$(KERNEL_PKG_SIGN_KEY)" "$(KERNEL_PKG_FILE)" "$(KERNEL_PKG_FILE).sig"; fi
	@ls -l "$(KERNEL_PKG_OUT)"/kernel-*
