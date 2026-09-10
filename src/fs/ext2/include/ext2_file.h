#pragma once

#include <stdint.h>

int32_t ext2_file_open(const char *path);
int32_t ext2_file_read(int32_t descriptor, void *buffer, uint32_t count);
int64_t ext2_file_seek(int32_t descriptor, int64_t offset, uint32_t whence);
int32_t ext2_file_close(int32_t descriptor);
int32_t ext2_file_create(const char *path);
int32_t ext2_file_write(const char *path, const void *buffer, uint32_t count);
int32_t ext2_file_append(const char *path, const void *buffer, uint32_t count);
int32_t ext2_file_create_dir(const char *path);
void ext2_file_handles_reset(void);
