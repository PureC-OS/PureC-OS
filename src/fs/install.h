#pragma once

#include "types/fs_types.h"
#include <stdint.h>

typedef void (*install_progress_callback)(uint32_t progress, const char *stage);

int32_t installer_install_device(const char *device_name,
                                 const char *serial_confirmation,
                                 install_progress_callback callback);
int32_t installer_format_custom_device(const char *device, uint32_t partition_count,
                                       const uint64_t *sizes_gb);
