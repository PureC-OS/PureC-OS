#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../fs/types/fs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t fs_open(const char *path);
int32_t fs_read(int32_t descriptor, void *buffer, uint32_t count);
int32_t fs_close(int32_t descriptor);
int32_t fs_list(const char *path, struct fs_directory_entry *entries,
                uint32_t capacity);
int32_t fs_create_file(const char *path);
int32_t fs_write_file(const char *path, const void *buffer, uint32_t count);
int32_t fs_create_directory(const char *path);
int32_t fs_delete(const char *path);
int32_t fs_rename(const char *path, const char *new_name);
int32_t fs_move(const char *path, const char *destination_directory);

#ifdef __cplusplus
}
#endif