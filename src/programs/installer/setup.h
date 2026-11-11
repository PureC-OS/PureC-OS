#pragma once

#include "../../kernel/syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

#define SETUP_PASSWORD_MAX 64

struct install_settings {
    char hostname[INSTALL_HOSTNAME_MAX];
    char root_password[SETUP_PASSWORD_MAX];
    char user_name[INSTALL_USER_MAX];
    char user_gecos[INSTALL_GECOS_MAX];
    char user_password[SETUP_PASSWORD_MAX];
    char timezone[32];
};

bool setup_build_request(const struct install_settings *settings,
                         struct install_account_request *request);
void setup_wipe(struct install_settings *settings);
