#pragma once
// PureC hosted libc: sys/types.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef long off_t;
typedef long ssize_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef unsigned long long dev_t;
typedef unsigned long long ino_t;
typedef unsigned int nlink_t;
typedef long blksize_t;
typedef long blkcnt_t;
typedef int clockid_t;
typedef void *timer_t;
typedef long suseconds_t;
typedef unsigned int useconds_t;
typedef unsigned int id_t;

#include "select.h"
#ifdef __cplusplus
}
#endif
