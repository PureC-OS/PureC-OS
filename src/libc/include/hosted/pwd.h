#pragma once

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif
struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);
#ifdef __cplusplus
}
#endif
