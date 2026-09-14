ROOT_DIR ?= $(abspath $(CURDIR))
BIN_DIR ?= $(ROOT_DIR)/bin

CROSS ?= x86_64-elf-
CC := $(CROSS)gcc
CXX := $(CROSS)g++
AR := $(CROSS)ar
LD := $(CROSS)ld
NASM ?= nasm

CPPFLAGS := -I$(ROOT_DIR)/src
DEPFLAGS := -MMD -MP
COMMON_CFLAGS := -g -O1 -ffreestanding -fno-stack-protector -fno-pic \
	-m64 -mno-red-zone
USER_CFLAGS := $(COMMON_CFLAGS) -mcmodel=small
KERNEL_CFLAGS := $(COMMON_CFLAGS) -mcmodel=kernel -mgeneral-regs-only
KERNEL_CXXFLAGS := $(KERNEL_CFLAGS) -std=c++20 -fno-exceptions -fno-rtti
