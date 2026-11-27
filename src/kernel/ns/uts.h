#pragma once

#include <kernel/ns/nsproxy.h>
#include <kernel/sync/spinlock.h>
#include <stdint.h>

#define UTS_FIELD_LENGTH 65
#define UTS_NAME_MAX 64

struct kutsname
{
  char sysname[UTS_FIELD_LENGTH];
  char nodename[UTS_FIELD_LENGTH];
  char release[UTS_FIELD_LENGTH];
  char version[UTS_FIELD_LENGTH];
  char machine[UTS_FIELD_LENGTH];
  char domainname[UTS_FIELD_LENGTH];
};

struct uts_namespace
{
  struct ns_common ns;
  spinlock_t lock;
  struct kutsname name;
};

extern struct uts_namespace init_uts_ns;

void uts_init(void);
struct uts_namespace *uts_ns_copy(struct uts_namespace *old);
void uts_ns_free(struct uts_namespace *uts);
void uts_get_name(struct uts_namespace *uts, struct kutsname *out);
void uts_set_nodename(struct uts_namespace *uts, const char *name,
                      uint32_t length);
void uts_set_domainname(struct uts_namespace *uts, const char *name,
                        uint32_t length);
