#pragma once

#include <kernel/ns/nsproxy.h>
#include <stdint.h>

struct vfs_mount;

struct mnt_namespace
{
  struct ns_common ns;
  struct vfs_mount *mounts;
  uint32_t count;
};

extern struct mnt_namespace init_mnt_ns;

struct mnt_namespace *mnt_ns_copy(struct mnt_namespace *old);
void mnt_ns_free(struct mnt_namespace *mnt);
struct mnt_namespace *mnt_ns_current(void);
