#include <fs/vfs.h>
#include <kernel/ns/nsproxy.h>
#include <kernel/ns/uts.h>
#include <kernel/process/fdtable.h>
#include <kernel/process/process.h>
#include <kernel/process/signal.h>
#include <lib/string.h>
#include "../common.h"

#define CLONE_EXIT_SIGNAL_MASK 0xffU

static bool ns_admin(void)
{
  return process_current_uid() == 0;
}

long sys_unshare(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint64_t flags = a1;
  if (flags & ~(uint64_t)NS_ALL_FLAGS)
    return -KERR_INVAL;
  if (flags & ~(uint64_t)NS_SUPPORTED_FLAGS)
    return -KERR_INVAL;
  if (!flags)
    return 0;
  if (!ns_admin())
    return -KERR_PERM;
  return process_ns_unshare((uint32_t)flags);
}

long sys_setns(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int32_t handle = process_fd_resolve((int32_t)a1);
  if (handle < 0)
    return -KERR_BADF;
  struct ns_common *ns = vfs_handle_ns(handle);
  if (!ns)
    return -KERR_INVAL;
  uint32_t nstype = (uint32_t)a2;
  long result = 0;
  if (nstype && nstype != ns_flag_for_type(ns->type))
    result = -KERR_INVAL;
  else if (!ns_admin())
    result = -KERR_PERM;
  else
    result = process_ns_install(ns);
  ns_put(ns);
  return result;
}

long sys_clone(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint64_t flags = a1;
  uint64_t signal = flags & CLONE_EXIT_SIGNAL_MASK;
  uint64_t ns_flags = flags & ~(uint64_t)CLONE_EXIT_SIGNAL_MASK;
  if (signal != 0 && signal != SIGCHLD)
    return -KERR_INVAL;
  if (ns_flags & ~(uint64_t)NS_SUPPORTED_FLAGS)
    return -KERR_INVAL;
  if (a2)
    return -KERR_INVAL;
  if (ns_flags && !ns_admin())
    return -KERR_PERM;
  long result = process_fork_ns(syscall_current_regs(), (uint32_t)ns_flags);
  if (result == -1)
    return -KERR_AGAIN;
  return result;
}

long sys_uname(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!writable((void *)(uintptr_t)a1, sizeof(struct kutsname)))
    return -KERR_FAULT;
  struct nsproxy *proxy = process_nsproxy_get_current();
  struct kutsname name;
  uts_get_name(proxy->uts, &name);
  nsproxy_put(proxy);
  memcpy((void *)(uintptr_t)a1, &name, sizeof(name));
  return 0;
}

static long set_uts_string(uint64_t user, uint64_t length, bool domain)
{
  if (!ns_admin())
    return -KERR_PERM;
  if (length > UTS_NAME_MAX)
    return -KERR_INVAL;
  char buffer[UTS_NAME_MAX];
  if (length)
  {
    if (!readable((const void *)(uintptr_t)user, length))
      return -KERR_FAULT;
    memcpy(buffer, (const void *)(uintptr_t)user, length);
  }
  struct nsproxy *proxy = process_nsproxy_get_current();
  if (domain)
    uts_set_domainname(proxy->uts, buffer, (uint32_t)length);
  else
    uts_set_nodename(proxy->uts, buffer, (uint32_t)length);
  nsproxy_put(proxy);
  return 0;
}

long sys_sethostname(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return set_uts_string(a1, a2, false);
}

long sys_setdomainname(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return set_uts_string(a1, a2, true);
}
