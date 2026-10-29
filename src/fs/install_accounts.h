#pragma once

#include "../kernel/syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

#define INSTALL_ACCOUNT_TEXT_MAX 1024
#define INSTALL_SHELL_DEFAULT "/usr/bin/terminal"
#define INSTALL_USERS_GID 100

struct install_account_files {
  char passwd[INSTALL_ACCOUNT_TEXT_MAX];
  char shadow[INSTALL_ACCOUNT_TEXT_MAX];
  char group[INSTALL_ACCOUNT_TEXT_MAX];
  char hostname[INSTALL_HOSTNAME_MAX + 2];
  char home[INSTALL_USER_MAX + 8];
};

bool install_account_name_valid(const char *name);
bool install_account_hostname_valid(const char *name);
bool install_account_hash_valid(const char *hash);
bool install_account_gecos_valid(const char *text);
bool install_accounts_valid(const struct install_account_request *request);
int32_t install_accounts_render(const struct install_account_request *request,
                                struct install_account_files *out);
