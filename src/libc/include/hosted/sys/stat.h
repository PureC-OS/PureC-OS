#pragma once
// PureC hosted libc: sys/stat (size + file/dir kind via pc_file_stat).

#include <stddef.h>
#include <stdint.h>

#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

struct stat {
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
