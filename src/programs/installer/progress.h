#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../kernel/syscall/syscall.h"

struct installer_progress_view {
    uint32_t last_progress;
    uint32_t poll_count;
    uint8_t spinner;
    char last_stage[INSTALL_STAGE_CAPACITY];
};

void installer_progress_init(struct installer_progress_view *view);
void installer_progress_update(struct installer_progress_view *view,
                               const struct install_status *status,
                               const char *device, bool force);
const char *installer_progress_error_text(int32_t status);
