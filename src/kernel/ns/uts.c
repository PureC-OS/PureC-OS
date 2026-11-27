#include <kernel/diagnostics/build_stamp.h>
#include <kernel/ns/uts.h>
#include <lib/format.h>
#include <lib/string.h>
#include <mm/oom/oom_slab.h>

extern const char kernel_version[];

struct uts_namespace init_uts_ns = {
    .ns = {1, NS_TYPE_UTS, 0xEFFFFFFEULL},
    .lock = SPINLOCK_INIT,
    .name = {.sysname = "PureC",
             .nodename = "purec",
             .machine = "x86_64",
             .domainname = "(none)"},
};

void uts_init(void)
{
  strncpy(init_uts_ns.name.release, kernel_version, UTS_NAME_MAX);
  kformat(init_uts_ns.name.version, UTS_FIELD_LENGTH, "#1 %s",
          kernel_build_time);
}

static void store(char *field, const char *name, uint32_t length)
{
  if (length > UTS_NAME_MAX)
    length = UTS_NAME_MAX;
  memset(field, 0, UTS_FIELD_LENGTH);
  memcpy(field, name, length);
}

struct uts_namespace *uts_ns_copy(struct uts_namespace *old)
{
  struct uts_namespace *uts =
      (struct uts_namespace *)oom_slab_alloc(sizeof(*uts));
  if (!uts)
    return NULL;
  memset(uts, 0, sizeof(*uts));
  uts->ns.refcount = 1;
  uts->ns.type = NS_TYPE_UTS;
  uts->ns.id = ns_id_alloc();
  uts_get_name(old, &uts->name);
  return uts;
}

void uts_ns_free(struct uts_namespace *uts)
{
  oom_slab_free(uts, sizeof(*uts));
}

void uts_get_name(struct uts_namespace *uts, struct kutsname *out)
{
  uint64_t flags = spin_lock_irqsave(&uts->lock);
  memcpy(out, &uts->name, sizeof(*out));
  spin_unlock_irqrestore(&uts->lock, flags);
}

void uts_set_nodename(struct uts_namespace *uts, const char *name,
                      uint32_t length)
{
  uint64_t flags = spin_lock_irqsave(&uts->lock);
  store(uts->name.nodename, name, length);
  spin_unlock_irqrestore(&uts->lock, flags);
}

void uts_set_domainname(struct uts_namespace *uts, const char *name,
                        uint32_t length)
{
  uint64_t flags = spin_lock_irqsave(&uts->lock);
  store(uts->name.domainname, name, length);
  spin_unlock_irqrestore(&uts->lock, flags);
}
