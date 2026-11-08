#pragma once
#include <sys/socket.h>

#define UNIX_PATH_MAX 108

#ifdef __cplusplus
extern "C" {
#endif
struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[UNIX_PATH_MAX];
};
#ifdef __cplusplus
}
#endif
