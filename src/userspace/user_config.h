#pragma once

#include <stdbool.h>
#include <stdint.h>

#define USER_CONFIG_SYSTEM_DIR "/config"
#define USER_CONFIG_SUBDIR "/.config/purec"

bool user_config_path(const char *name, char *out, uint32_t capacity,
                      bool for_write);
void user_config_claim(const char *path);
