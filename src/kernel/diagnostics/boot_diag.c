#include "boot_diag.h"
#include "klog.h"

static volatile enum boot_stage current_stage = BOOT_STAGE_ENTRY;
static const char *current_detail = "kernel entry";

void boot_diag_checkpoint(enum boot_stage stage, const char *detail){
    current_stage = stage;
    current_detail = detail ? detail : "(no detail)";
    klogf(KLOG_INFO, "BOOT[%02u] %s", (unsigned int)stage, current_detail);
}

enum boot_stage boot_diag_current_stage(void){
    return current_stage;
}

const char *boot_diag_current_detail(void){
    return current_detail;
}
