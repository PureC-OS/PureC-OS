#pragma once
#include <stdint.h>

enum boot_stage {
    BOOT_STAGE_ENTRY = 1,
    BOOT_STAGE_BOOT_PROTOCOL,
    BOOT_STAGE_FRAMEBUFFER,
    BOOT_STAGE_DESCRIPTOR_TABLES,
    BOOT_STAGE_INTERRUPTS,
    BOOT_STAGE_KERNEL_MAIN,
    BOOT_STAGE_SYSTEM_INFO,
    BOOT_STAGE_SYSCALLS,
    BOOT_STAGE_USERSPACE_INIT,
    BOOT_STAGE_USERSPACE_RUN
};

void boot_diag_checkpoint(enum boot_stage stage, const char *detail);
enum boot_stage boot_diag_current_stage(void);
const char *boot_diag_current_detail(void);
