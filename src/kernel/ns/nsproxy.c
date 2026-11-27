#include <kernel/ns/nsproxy.h>
#include <kernel/ns/uts.h>
#include <kernel/syscall/syscall.h>
#include <lib/string.h>
#include <mm/oom/oom_slab.h>

#define NS_DYNAMIC_ID_BASE 0xF0000000ULL

struct nsproxy init_nsproxy = {1, &init_uts_ns};

static uint64_t next_ns_id = NS_DYNAMIC_ID_BASE;

void nsproxy_init(void)
{
  uts_init();
}

uint64_t ns_id_alloc(void)
{
  return __atomic_fetch_add(&next_ns_id, 1, __ATOMIC_RELAXED);
}

uint32_t ns_flag_for_type(uint32_t type)
{
  switch (type)
  {
  case NS_TYPE_UTS:
    return CLONE_NEWUTS;
  default:
    return 0;
  }
}

const char *ns_type_name(uint32_t type)
{
  switch (type)
  {
  case NS_TYPE_UTS:
    return "uts";
  default:
    return "";
  }
}

int32_t ns_type_from_flag(uint32_t nstype)
{
  for (uint32_t type = 0; type < NS_TYPE_COUNT; type++)
    if (ns_flag_for_type(type) == nstype)
      return (int32_t)type;
  return -1;
}

void ns_get(struct ns_common *ns)
{
  if (ns)
    __atomic_fetch_add(&ns->refcount, 1, __ATOMIC_RELAXED);
}

void ns_put(struct ns_common *ns)
{
  if (!ns)
    return;
  if (__atomic_sub_fetch(&ns->refcount, 1, __ATOMIC_ACQ_REL) != 0)
    return;
  switch (ns->type)
  {
  case NS_TYPE_UTS:
    uts_ns_free((struct uts_namespace *)ns);
    break;
  default:
    break;
  }
}

struct nsproxy *nsproxy_get(struct nsproxy *proxy)
{
  if (proxy)
    __atomic_fetch_add(&proxy->refcount, 1, __ATOMIC_RELAXED);
  return proxy;
}

void nsproxy_put(struct nsproxy *proxy)
{
  if (!proxy)
    return;
  if (__atomic_sub_fetch(&proxy->refcount, 1, __ATOMIC_ACQ_REL) != 0)
    return;
  ns_put(&proxy->uts->ns);
  oom_slab_free(proxy, sizeof(*proxy));
}

static struct nsproxy *nsproxy_alloc(void)
{
  struct nsproxy *proxy = (struct nsproxy *)oom_slab_alloc(sizeof(*proxy));
  if (!proxy)
    return NULL;
  memset(proxy, 0, sizeof(*proxy));
  proxy->refcount = 1;
  return proxy;
}

int32_t nsproxy_unshare(struct nsproxy *old, uint32_t flags,
                        struct nsproxy **out)
{
  if (flags & ~NS_SUPPORTED_FLAGS)
    return -KERR_INVAL;
  struct nsproxy *proxy = nsproxy_alloc();
  if (!proxy)
    return -KERR_NOMEM;
  if (flags & CLONE_NEWUTS)
  {
    proxy->uts = uts_ns_copy(old->uts);
    if (!proxy->uts)
    {
      oom_slab_free(proxy, sizeof(*proxy));
      return -KERR_NOMEM;
    }
  }
  else
  {
    proxy->uts = old->uts;
    ns_get(&proxy->uts->ns);
  }
  *out = proxy;
  return 0;
}

int32_t nsproxy_install(struct nsproxy *old, struct ns_common *ns,
                        struct nsproxy **out)
{
  struct nsproxy *proxy = nsproxy_alloc();
  if (!proxy)
    return -KERR_NOMEM;
  proxy->uts = old->uts;
  switch (ns->type)
  {
  case NS_TYPE_UTS:
    proxy->uts = (struct uts_namespace *)ns;
    break;
  default:
    oom_slab_free(proxy, sizeof(*proxy));
    return -KERR_INVAL;
  }
  ns_get(&proxy->uts->ns);
  *out = proxy;
  return 0;
}

struct ns_common *nsproxy_ns(struct nsproxy *proxy, uint32_t type)
{
  switch (type)
  {
  case NS_TYPE_UTS:
    return &proxy->uts->ns;
  default:
    return NULL;
  }
}
