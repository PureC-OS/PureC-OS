#pragma once

#include "../../kernel/syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

#define SETUP_PASSWORD_MAX 64

struct setup_user {
    char name[INSTALL_USER_MAX];
    char gecos[INSTALL_GECOS_MAX];
    char hash[INSTALL_HASH_MAX];
    bool admin;
};

struct install_settings {
    char hostname[INSTALL_HOSTNAME_MAX];
    char root_password[SETUP_PASSWORD_MAX];
    char user_name[INSTALL_USER_MAX];
    char user_gecos[INSTALL_GECOS_MAX];
    char user_password[SETUP_PASSWORD_MAX];
    bool user_admin;
    char timezone[32];
    struct setup_user users[INSTALL_USERS_MAX];
    uint32_t user_count;
};

bool setup_build_request(const struct install_settings *settings,
                         struct install_account_request *request);
void setup_wipe(struct install_settings *settings);
bool setup_add_user(struct install_settings *settings);
void setup_remove_user(struct install_settings *settings, uint32_t index);
bool setup_user_taken(const struct install_settings *settings, const char *name);
