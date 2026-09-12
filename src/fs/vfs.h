#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "types/fs_types.h"
#include "./fat/include/fat32.h"
#include "./ext2/include/ext2.h"
#include "../kernel/syscall/syscall.h"

#define VFS_FD_STDIN 0
#define VFS_FD_STDOUT 1
#define VFS_FD_STDERR 2
#define VFS_FD_BASE 3

#define VFS_FS_FAT32 0
#define VFS_FS_EXT2 1
#define VFS_FS_AUTO 255

bool vfs_mount_root(void);
bool vfs_mount_root_with_fs(uint8_t fs_type);
bool vfs_is_root_mounted(void);
const char *vfs_root_device_name(void);
uint8_t vfs_root_fs_type(void);
void vfs_set_active_fs(uint8_t fs_type);
const char *vfs_fs_type_name(uint8_t fs_type);
int32_t vfs_open(const char *path);
int32_t vfs_read(int32_t descriptor, void *buffer, uint32_t count);
int64_t vfs_seek(int32_t descriptor, int64_t offset, uint32_t whence);
int32_t vfs_stat(const char *path, struct file_stat_info *out);
int32_t vfs_close(int32_t descriptor);
int32_t vfs_delete(const char *path);
int32_t vfs_rename(const char *path, const char *new_name);
int32_t vfs_move(const char *path, const char *destination_directory);
int32_t vfs_list(const char *path, struct fs_directory_entry *entries, uint32_t capacity);
int32_t vfs_list_long(const char *path, struct fs_directory_entry_long *entries, uint32_t capacity);
int32_t vfs_create_file(const char *path);
int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count);
int32_t vfs_append_file(const char *path, const void *buffer, uint32_t count);
int32_t vfs_create_directory(const char *path);
int32_t vfs_format_device(const char *device_name, const char *serial_confirmation, const char *erase_confirmation);
int32_t vfs_format_device_force(const char *device_name, const char *serial_confirmation);
int32_t vfs_format_device_ex(const char *device_name, const char *serial_confirmation, const char *erase_confirmation, uint8_t fs_type);
int32_t vfs_format_uefi_device(const char *device_name, const char *serial_confirmation);
int32_t vfs_format_uefi_device_progress(const char *device_name, const char *serial_confirmation, fat32_progress_callback callback);
int32_t vfs_save_klog_to_device(const char *device, const char *path);
int32_t vfs_ext2_stat(const char *path, struct ext2_stat_info *out);
int32_t vfs_ext2_inode(uint32_t ino, struct ext2_stat_info *out);
int32_t vfs_ext2_super(struct ext2_super_info *out);
int32_t vfs_ext2_blocks(const char *path, struct ext2_blocks_info *out);
int32_t vfs_ext2_inode_blocks(uint32_t ino, struct ext2_blocks_info *out);
