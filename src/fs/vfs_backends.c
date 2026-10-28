#include "vfs_backends.h"
#include "../drivers/storage/block_device.h"
#include "../lib/string.h"
#include "./ext2/include/ext2.h"
#include "./ext2/include/ext2_block.h"
#include "./ext2/include/ext2_dir.h"
#include "./ext2/include/ext2_inode.h"
#include "./ext2/include/ext2_types.h"
#include "initramfs.h"
#include "vfs_path.h"

static bool ext2_ready(void *context) {
  (void)context;
  return ext2_is_mounted();
}

static int32_t ext2_op_open(void *context, const char *path) {
  (void)context;
  return ext2_open(path);
}

static int32_t ext2_op_read(void *context, int32_t descriptor, void *buffer,
                            uint32_t count) {
  (void)context;
  return ext2_read(descriptor, buffer, count);
}

static int64_t ext2_op_seek(void *context, int32_t descriptor, int64_t offset,
                            uint32_t whence) {
  (void)context;
  return ext2_seek(descriptor, offset, whence);
}

static int32_t ext2_op_close(void *context, int32_t descriptor) {
  (void)context;
  return ext2_close(descriptor);
}

static int32_t ext2_op_stat(void *context, const char *path,
                            struct file_stat_info *out) {
  (void)context;
  uint32_t ino;
  int32_t status = ext2_dir_resolve(path, &ino);
  if (status < 0)
    return status;
  uint8_t inode[256];
  if (!ext2_inode_read(ino, inode))
    return FS_ERROR_IO;
  out->size = ext2_read_u32(inode + 4);
  out->is_directory = ((ext2_read_u16(inode) & 0xF000) == EXT2_S_IFDIR) ? 1 : 0;
  out->reserved = 0;
  return 0;
}

static int32_t ext2_op_list(void *context, const char *path,
                            struct fs_directory_entry *entries,
                            uint32_t capacity) {
  (void)context;
  return ext2_list(path, entries, capacity);
}

static int32_t ext2_op_list_long(void *context, const char *path,
                                 struct fs_directory_entry_long *entries,
                                 uint32_t capacity) {
  (void)context;
  return ext2_list_long(path, entries, capacity);
}

static int32_t ext2_op_create_file(void *context, const char *path) {
  (void)context;
  return ext2_create_file(path);
}

static int32_t ext2_op_create_directory(void *context, const char *path) {
  (void)context;
  return ext2_create_directory(path);
}

static int32_t ext2_op_unlink(void *context, const char *path) {
  (void)context;
  return ext2_delete(path);
}

static int32_t ext2_op_rename(void *context, const char *path,
                              const char *new_name) {
  (void)context;
  return ext2_rename(path, new_name);
}

static int32_t ext2_op_move(void *context, const char *path,
                            const char *destination) {
  (void)context;
  return ext2_move(path, destination);
}

static int32_t ext2_op_write_file(void *context, const char *path,
                                  const void *buffer, uint32_t count) {
  (void)context;
  int32_t result = ext2_write_file(path, buffer, count);
  if (result >= 0)
    block_device_flush();
  return result;
}

static int32_t ext2_op_append_file(void *context, const char *path,
                                   const void *buffer, uint32_t count) {
  (void)context;
  int32_t result = ext2_append_file(path, buffer, count);
  if (result >= 0)
    block_device_flush();
  return result;
}

const struct vfs_fs_ops vfs_ext2_ops = {
    .name = "ext2",
    .open = ext2_op_open,
    .read = ext2_op_read,
    .seek = ext2_op_seek,
    .close = ext2_op_close,
    .stat = ext2_op_stat,
    .list = ext2_op_list,
    .list_long = ext2_op_list_long,
    .create_file = ext2_op_create_file,
    .create_directory = ext2_op_create_directory,
    .unlink = ext2_op_unlink,
    .rename = ext2_op_rename,
    .move = ext2_op_move,
    .write_file = ext2_op_write_file,
    .append_file = ext2_op_append_file,
    .is_ready = ext2_ready,
};

static bool initramfs_ready(void *context) {
  (void)context;
  return initramfs_is_mounted();
}

static int32_t initramfs_op_map(void *context, const char *path,
                                const void **data, uint32_t *size) {
  (void)context;
  return initramfs_find(path, data, size) ? 0 : FS_ERROR_NOT_FOUND;
}

static int32_t initramfs_op_stat(void *context, const char *path,
                                 struct file_stat_info *out) {
  (void)context;
  const void *data = 0;
  uint32_t size = 0;
  if (!initramfs_find(path, &data, &size))
    return FS_ERROR_NOT_FOUND;
  out->size = size;
  out->is_directory = 0;
  out->reserved = 0;
  return 0;
}

static int32_t directory_with_slash(const char *path, char *out,
                                    uint32_t capacity) {
  uint32_t length = (uint32_t)strlen(path);
  if (length + 2 > capacity)
    return FS_ERROR_INVALID;
  memcpy(out, path, length);
  if (length && out[length - 1] != '/')
    out[length++] = '/';
  out[length] = '\0';
  return 0;
}

static int32_t initramfs_op_list(void *context, const char *path,
                                 struct fs_directory_entry *entries,
                                 uint32_t capacity) {
  (void)context;
  char directory[VFS_PATH_MAX];
  int32_t status = directory_with_slash(path, directory, sizeof(directory));
  if (status < 0)
    return status;
  return initramfs_list(directory, entries, capacity);
}

static int32_t initramfs_op_list_long(void *context, const char *path,
                                      struct fs_directory_entry_long *entries,
                                      uint32_t capacity) {
  (void)context;
  char directory[VFS_PATH_MAX];
  int32_t status = directory_with_slash(path, directory, sizeof(directory));
  if (status < 0)
    return status;
  return initramfs_list_long(directory, entries, capacity);
}

const struct vfs_fs_ops vfs_initramfs_ops = {
    .name = "initramfs",
    .map = initramfs_op_map,
    .stat = initramfs_op_stat,
    .list = initramfs_op_list,
    .list_long = initramfs_op_list_long,
    .is_ready = initramfs_ready,
};

struct kernelfs_file {
  const char *path;
  const char *name;
  const char *content;
};

static const struct kernelfs_file kernelfs_files[] = {
    {"/version", "version", "PureC OS kernel 1.1.0\n"},
    {"/panic", "panic.cfg", "panic-screan=classic\n #panic-screan=qr\n"},
    {"/init", "init",
     "pid1=/bin/init shell=/bin/program/terminal editor=/bin/program/nano\n"},
    {"/abi", "abi",
     "syscall=int80 process=exec,args,env,wait,exit fd=per-process "
     "vfs=ext2\n"}};

#define KERNELFS_COUNT (sizeof(kernelfs_files) / sizeof(kernelfs_files[0]))

static const struct kernelfs_file *kernelfs_find(const char *path) {
  for (uint32_t i = 0; i < KERNELFS_COUNT; i++)
    if (strcmp(path, kernelfs_files[i].path) == 0)
      return &kernelfs_files[i];
  return 0;
}

static int32_t kernelfs_op_map(void *context, const char *path,
                               const void **data, uint32_t *size) {
  (void)context;
  const struct kernelfs_file *file = kernelfs_find(path);
  if (!file)
    return FS_ERROR_NOT_FOUND;
  *data = file->content;
  *size = (uint32_t)strlen(file->content);
  return 0;
}

static int32_t kernelfs_op_stat(void *context, const char *path,
                                struct file_stat_info *out) {
  (void)context;
  out->reserved = 0;
  if (strcmp(path, "/") == 0) {
    out->size = 0;
    out->is_directory = 1;
    return 0;
  }
  const struct kernelfs_file *file = kernelfs_find(path);
  if (!file)
    return FS_ERROR_NOT_FOUND;
  out->size = (uint64_t)strlen(file->content);
  out->is_directory = 0;
  return 0;
}

static int32_t kernelfs_op_list(void *context, const char *path,
                                struct fs_directory_entry *entries,
                                uint32_t capacity) {
  (void)context;
  if (strcmp(path, "/") != 0)
    return FS_ERROR_NOT_DIR;
  uint32_t count = KERNELFS_COUNT < capacity ? (uint32_t)KERNELFS_COUNT : capacity;
  for (uint32_t i = 0; i < count; i++) {
    strncpy(entries[i].name, kernelfs_files[i].name,
            FS_DIRECTORY_NAME_CAPACITY - 1);
    entries[i].name[FS_DIRECTORY_NAME_CAPACITY - 1] = '\0';
    entries[i].size = (uint32_t)strlen(kernelfs_files[i].content);
    entries[i].attributes = 0;
  }
  return (int32_t)count;
}

static int32_t kernelfs_op_list_long(void *context, const char *path,
                                     struct fs_directory_entry_long *entries,
                                     uint32_t capacity) {
  (void)context;
  if (strcmp(path, "/") != 0)
    return FS_ERROR_NOT_DIR;
  uint32_t count = KERNELFS_COUNT < capacity ? (uint32_t)KERNELFS_COUNT : capacity;
  for (uint32_t i = 0; i < count; i++) {
    strncpy(entries[i].name, kernelfs_files[i].name, FS_LONG_NAME_CAPACITY - 1);
    entries[i].name[FS_LONG_NAME_CAPACITY - 1] = '\0';
    entries[i].size = (uint32_t)strlen(kernelfs_files[i].content);
    entries[i].attributes = 0;
  }
  return (int32_t)count;
}

const struct vfs_fs_ops vfs_kernelfs_ops = {
    .name = "kernelfs",
    .map = kernelfs_op_map,
    .stat = kernelfs_op_stat,
    .list = kernelfs_op_list,
    .list_long = kernelfs_op_list_long,
};
