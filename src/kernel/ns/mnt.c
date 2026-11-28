#include <fs/vfs_mount.h>
#include <kernel/ns/mnt.h>
#include <kernel/process/process.h>
#include <lib/string.h>
#include <mm/oom/oom_slab.h>

struct mnt_namespace init_mnt_ns = {
    .ns = {1, NS_TYPE_MNT, 0xEFFFFFFDULL},
};

struct mnt_namespace *mnt_ns_current(void)
{
  struct process *process = process_current();
  if (!process || !process->nsproxy || !process->nsproxy->mnt)
    return &init_mnt_ns;
  return process->nsproxy->mnt;
}

struct mnt_namespace *mnt_ns_copy(struct mnt_namespace *old)
{
  struct mnt_namespace *mnt =
      (struct mnt_namespace *)oom_slab_alloc(sizeof(*mnt));
  if (!mnt)
    return NULL;
  memset(mnt, 0, sizeof(*mnt));
  mnt->ns.refcount = 1;
  mnt->ns.type = NS_TYPE_MNT;
  mnt->ns.id = ns_id_alloc();
  if (vfs_mount_ns_clone(mnt, old) < 0)
  {
    oom_slab_free(mnt, sizeof(*mnt));
    return NULL;
  }
  return mnt;
}

void mnt_ns_free(struct mnt_namespace *mnt)
{
  vfs_mount_ns_release(mnt);
  oom_slab_free(mnt, sizeof(*mnt));
}
