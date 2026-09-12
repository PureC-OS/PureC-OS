#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../types/fs_types.h"

typedef void (*ext2_progress_callback)(uint32_t progress, const char *stage);

bool ext2_init(void);
bool ext2_is_mounted(void);
const char *ext2_device_name(void);
int32_t ext2_open(const char *path);
int32_t ext2_read(int32_t descriptor, void *buffer, uint32_t count);
int64_t ext2_seek(int32_t descriptor, int64_t offset, uint32_t whence);
int32_t ext2_close(int32_t descriptor);
int32_t ext2_delete(const char *path);
int32_t ext2_rename(const char *path, const char *new_name);
int32_t ext2_move(const char *path, const char *destination_directory);
int32_t ext2_list(const char *path, struct fs_directory_entry *entries, uint32_t capacity);
int32_t ext2_list_long(const char *path, struct fs_directory_entry_long *entries, uint32_t capacity);
int32_t ext2_create_file(const char *path);
int32_t ext2_write_file(const char *path, const void *buffer, uint32_t count);
int32_t ext2_append_file(const char *path, const void *buffer, uint32_t count);
int32_t ext2_create_directory(const char *path);
int32_t ext2_format_device(const char *device_name, const char *serial_confirmation, const char *erase_confirmation);
int32_t ext2_format_device_force(const char *device_name, const char *serial_confirmation);
bool ext2_mount_specific(const char *device);

struct ext2_module_ops {
    uint32_t version;
    bool (*init)(void);
    bool (*is_mounted)(void);
    const char *(*device_name)(void);
    int32_t (*open)(const char *path);
    int32_t (*read)(int32_t d, void *b, uint32_t c);
    int32_t (*close)(int32_t d);
    int32_t (*list)(const char *p, struct fs_directory_entry *e, uint32_t cap);
    int32_t (*create_file)(const char *p);
    int32_t (*write_file)(const char *p, const void *b, uint32_t c);
    int32_t (*create_directory)(const char *p);
    int32_t (*format_device)(const char *dev, const char *s, const char *e);
    bool (*mount_specific)(const char *dev);
};

const struct ext2_module_ops *ext2_get_ops(void);
