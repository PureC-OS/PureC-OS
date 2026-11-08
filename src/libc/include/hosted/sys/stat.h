#pragma once
// PureC hosted libc: sys/stat (size + file/dir kind via pc_file_stat).

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "../time.h"

#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_IFBLK 0060000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)

#ifdef __cplusplus
extern "C" {
#endif
struct stat {
    unsigned long long st_dev;
    unsigned long long st_ino;
    unsigned int st_nlink;
    unsigned long st_blksize;
    unsigned long long st_size;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long long st_rdev;
    long long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

int stat(const char *path, struct stat *info);
int fstat(int fd, struct stat *info);
int lstat(const char *path, struct stat *info);
int fstatat(int dirfd, const char *path, struct stat *info, int flags);

#define UTIME_NOW ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
struct timespec;
int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags);
int futimens(int fd, const struct timespec times[2]);
int mkdir(const char *path, unsigned int mode);
int chmod(const char *path, unsigned int mode);
int fchmod(int fd, unsigned int mode);
mode_t umask(mode_t mask);
#ifdef __cplusplus
}
#endif
