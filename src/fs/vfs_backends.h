#pragma once

#include "vfs_ops.h"

extern const struct vfs_fs_ops vfs_ext2_ops;
extern const struct vfs_fs_ops vfs_initramfs_ops;
extern const struct vfs_fs_ops vfs_kernelfs_ops;
extern const struct vfs_fs_ops vfs_devfs_ops;
extern const struct vfs_fs_ops vfs_procfs_ops;
extern const struct vfs_fs_ops vfs_sysfs_ops;
extern const struct vfs_fs_ops vfs_tmpfs_ops;
void *vfs_tmpfs_create(void);
