#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../types/fs_types.h"

typedef void (*fat32_progress_callback)(uint32_t progress,
                                        const char *stage);

bool fat32_init(void);
bool fat32_is_mounted(void);
const char *fat32_device_name(void);
int32_t fat32_open(const char *path);
int32_t fat32_read(int32_t descriptor, void *buffer, uint32_t count);
int64_t fat32_seek(int32_t descriptor, int64_t offset, uint32_t whence);
int32_t fat32_stat(const char *path, uint64_t *size, bool *is_directory);
int32_t fat32_close(int32_t descriptor);
int32_t fat32_delete(const char *path);
int32_t fat32_rename(const char *path, const char *new_name);
int32_t fat32_move(const char *path, const char *destination_directory);
int32_t fat32_list(const char *path, struct fs_directory_entry *entries,
                   uint32_t capacity);
int32_t fat32_create_file(const char *path);
int32_t fat32_write_file(const char *path, const void *buffer, uint32_t count);
int32_t fat32_append_file(const char *path, const void *buffer, uint32_t count);
int32_t fat32_create_directory(const char *path);
int32_t fat32_format_device(const char *device_name,
                            const char *serial_confirmation,
                            const char *erase_confirmation);
int32_t fat32_format_device_force(const char *device_name, const char *serial_confirmation);
int32_t fat32_format_uefi_device(const char *device_name, const char *serial_confirmation);
int32_t fat32_format_uefi_device_progress(
    const char *device_name, const char *serial_confirmation,
    fat32_progress_callback callback);
int32_t fat32_format_uefi_device_progress_ex(
    const char *device_name, const char *serial_confirmation,
    fat32_progress_callback callback, uint8_t fs_type);
int32_t fat32_format_custom_device(const char *device, uint32_t partition_count, const uint64_t *sizes_gb);
bool fat32_mount_specific(const char *device);
