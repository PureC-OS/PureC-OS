#pragma once

#include <stdint.h>

#define FS_ERROR_IO          -1
#define FS_ERROR_NOT_FOUND   -2
#define FS_ERROR_INVALID     -3
#define FS_ERROR_NO_SPACE    -4
#define FS_ERROR_EXISTS      -5
#define FS_ERROR_NOT_FILE    -6
#define FS_ERROR_NOT_DIR     -7
#define FS_ERROR_UNSUPPORTED -8
#define FS_ERROR_BUSY        -9
#define FS_ERROR_READ_ONLY  -10
#define FS_ERROR_CONFIRMATION -11
#define FS_ERROR_NOT_BLANK    -12
#define FS_ERROR_TOO_SMALL    -13

#define FS_DIRECTORY_NAME_CAPACITY 13
#define FS_ATTRIBUTE_DIRECTORY     0x10

// Seek origins, POSIX values. Used by fat32_seek/ext2_file_seek/vfs_seek
// and exposed to userspace via purec.h for the TCC port (ar archives,
// object files and headers all need random access).
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct fs_directory_entry {
    char name[FS_DIRECTORY_NAME_CAPACITY];
    uint32_t size;
    uint8_t attributes;
};
