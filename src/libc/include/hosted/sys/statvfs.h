#pragma once

#include <stdint.h>

typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;

#define ST_RDONLY 1
#define ST_NOSUID 2
#define ST_NODEV 4
#define ST_NOEXEC 8
#define ST_SYNCHRONOUS 16
#define ST_NOATIME 1024
#define ST_RELATIME 4096
#define statvfs64 statvfs
#define fstatvfs64 fstatvfs
#define fsblkcnt64_t fsblkcnt_t
#define fsfilcnt64_t fsfilcnt_t

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    fsblkcnt_t f_blocks;
    fsblkcnt_t f_bfree;
    fsblkcnt_t f_bavail;
    fsfilcnt_t f_files;
    fsfilcnt_t f_ffree;
    fsfilcnt_t f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

#ifdef __cplusplus
extern "C" {
#endif

int statvfs(const char *path, struct statvfs *info);
int fstatvfs(int fd, struct statvfs *info);

#ifdef __cplusplus
}
#endif
