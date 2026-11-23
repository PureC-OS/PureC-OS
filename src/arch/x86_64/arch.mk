ARCH_NASM_FORMAT := elf64
ARCH_NASM_FLAGS :=
ARCH_CROSS := x86_64-elf-
ARCH_COMMON_CFLAGS := -m64 -mno-red-zone
ARCH_KERNEL_CFLAGS := -mcmodel=kernel -mgeneral-regs-only
ARCH_USER_CFLAGS := -mcmodel=small
ARCH_LINKER_SCRIPT := $(ROOT_DIR)/linker-limine.ld
ARCH_EFI_NAME := BOOTX64.EFI
ARCH_C_SOURCES := \
	arch/x86_64/cpu.c \
	arch/x86_64/mmu.c \
	arch/x86_64/gdt/c/gdt.c \
	arch/x86_64/idt/c/idt.c \
	arch/x86_64/fpu/fpu.c \
	arch/x86_64/mmio.c \
	arch/x86_64/lapic.c \
	arch/x86_64/cpufreq.c \
	arch/x86_64/pat.c \
	arch/x86_64/smp.c \
	arch/x86_64/pic.c \
	arch/x86_64/timer.c
ARCH_ASM_SOURCES := \
	arch/x86_64/gdt/asm/gdt.asm \
	arch/x86_64/idt/asm/idt.asm \
	arch/x86_64/smp.asm \
	arch/x86_64/scheduler.asm \
	arch/x86_64/user.asm \
	arch/x86_64/syscall_entry.asm
