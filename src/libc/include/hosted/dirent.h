#pragma once
// PureC hosted libc: dirent. opendir() snapshots the directory through
// pc_directory_list_long, so readdir() never touches the kernel.
#include <stddef.h>

#define DT_UNKNOWN 0
#define DT_CHR 2
#define DT_LNK 10
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define _DIRENT_HAVE_D_TYPE 1

#ifdef __cplusplus
extern "C" {
#endif
struct dirent {
    unsigned long d_ino;
    unsigned char d_type;
    char d_name[256];
};

typedef struct dir_impl DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
void rewinddir(DIR *directory);

int dirfd(DIR *directory);
int alphasort(const struct dirent **left, const struct dirent **right);
int scandir(const char *path, struct dirent ***result,
            int (*select)(const struct dirent *),
            int (*compare)(const struct dirent **, const struct dirent **));
#ifdef __cplusplus
}
#endif
