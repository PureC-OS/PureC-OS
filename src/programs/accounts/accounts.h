#pragma once

#include "../../libc/include/purec.h"
#include <stdbool.h>
#include <stdint.h>

#define ACCOUNT_PASSWORD_MIN 1

bool account_hash_password(const char *password, char *out, uint32_t capacity);
bool account_login(const char *name, const char *password,
                   struct user_info *info);
