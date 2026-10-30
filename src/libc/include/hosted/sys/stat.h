#pragma once
// PureC hosted libc: sys/stat (size + file/dir kind via pc_file_stat).

#include <stddef.h>
#include <stdint.h>
#include "types.h"

#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

struct stat {
    unsigned long long st_dev;
    unsigned long long st_ino;
    unsigned int st_nlink;
    unsigned long st_blksize;
    unsigned long long st_size;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    long st_atime;
    long st_mtime;
    long st_ctime;
};

int stat(const char *path, struct stat *info);
int fstat(int fd, struct stat *info);
int lstat(const char *path, struct stat *info);
int mkdir(const char *path, unsigned int mode);
mode_t umask(mode_t mask);
