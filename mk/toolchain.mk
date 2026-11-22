ROOT_DIR ?= $(abspath $(CURDIR))
BIN_DIR ?= $(ROOT_DIR)/bin

ARCH ?= x86_64
include $(ROOT_DIR)/src/arch/$(ARCH)/arch.mk

CROSS ?= $(ARCH_CROSS)
CC := $(CROSS)gcc
CXX := $(CROSS)g++
AR := $(CROSS)ar
LD := $(CROSS)ld
NASM ?= nasm

LIBC_DIR := $(ROOT_DIR)/programs/PureC-OS-Core-Programs/libc
CPPFLAGS := -I$(ROOT_DIR)/src -I$(ROOT_DIR)/programs/PureC-OS-Core-Programs
DEPFLAGS := -MMD -MP
COMMON_CFLAGS := -g -O1 -ffreestanding -fno-stack-protector -fno-pic \
	$(ARCH_COMMON_CFLAGS)
USER_CFLAGS := $(COMMON_CFLAGS) $(ARCH_USER_CFLAGS)
USER_SO_CFLAGS := -g -O2 -ffreestanding -fno-stack-protector -fPIC \
	-m64 -mno-red-zone -mcmodel=small -fno-plt
SO_LINK_FLAGS := -shared --hash-style=sysv -z noexecstack
PIE_LINK_FLAGS := -pie --hash-style=sysv -z noexecstack
DYN_LINK_FLAGS := $(PIE_LINK_FLAGS) --export-dynamic -e _pc_dl_entry -T $(ROOT_DIR)/linker-userspace-pie.ld
DYN_LD_STUB := $(BIN_DIR)/lib/pc-ld.o
INTERP_PATH := /lib/ld-purec.so.1
INTERP_LINK_FLAGS := -shared -Bsymbolic --hash-style=sysv -z noexecstack -z now -z noseparate-code -z max-page-size=4096 -z common-page-size=4096 -e _pc_interp_entry -soname ld-purec.so.1
INTERP_EXE_LINK_FLAGS := $(PIE_LINK_FLAGS) --export-dynamic -dynamic-linker $(INTERP_PATH) -T $(ROOT_DIR)/linker-userspace-pie-interp.ld
KMOD_SO_CFLAGS := -g -O1 -ffreestanding -fno-stack-protector -fPIC \
	-m64 -mno-red-zone -mcmodel=small -mgeneral-regs-only -fno-plt
KMOD_SO_LDSCRIPT := $(ROOT_DIR)/mk/kmod-so.ld
KMOD_SO_LINK := -shared --hash-style=sysv -z noexecstack -T $(ROOT_DIR)/mk/kmod-so.ld
KERNEL_CFLAGS := $(COMMON_CFLAGS) $(ARCH_KERNEL_CFLAGS) \
	-fno-omit-frame-pointer \
	-ffile-prefix-map=$(ROOT_DIR)/src/=src/ \
	-fdebug-prefix-map=$(ROOT_DIR)/src/=src/
KERNEL_CXXFLAGS := $(KERNEL_CFLAGS) -std=c++20 -fno-exceptions -fno-rtti
