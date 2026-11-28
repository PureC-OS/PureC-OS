#pragma once

#include <stdint.h>

#define CLONE_NEWTIME 0x00000080U
#define CLONE_NEWNS 0x00020000U
#define CLONE_NEWCGROUP 0x02000000U
#define CLONE_NEWUTS 0x04000000U
#define CLONE_NEWIPC 0x08000000U
#define CLONE_NEWUSER 0x10000000U
#define CLONE_NEWPID 0x20000000U
#define CLONE_NEWNET 0x40000000U

#define NS_ALL_FLAGS                                                           \
  (CLONE_NEWTIME | CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWUTS |              \
   CLONE_NEWIPC | CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET)
#define NS_SUPPORTED_FLAGS (CLONE_NEWUTS | CLONE_NEWNS)

enum ns_type
{
  NS_TYPE_UTS = 0,
  NS_TYPE_MNT,
  NS_TYPE_COUNT
};

struct ns_common
{
  uint32_t refcount;
  uint32_t type;
  uint64_t id;
};

struct uts_namespace;
struct mnt_namespace;

struct nsproxy
{
  uint32_t refcount;
  struct uts_namespace *uts;
  struct mnt_namespace *mnt;
};

extern struct nsproxy init_nsproxy;

void nsproxy_init(void);
uint64_t ns_id_alloc(void);
uint32_t ns_flag_for_type(uint32_t type);
const char *ns_type_name(uint32_t type);
int32_t ns_type_from_flag(uint32_t nstype);
void ns_get(struct ns_common *ns);
void ns_put(struct ns_common *ns);
struct nsproxy *nsproxy_get(struct nsproxy *proxy);
void nsproxy_put(struct nsproxy *proxy);
int32_t nsproxy_unshare(struct nsproxy *old, uint32_t flags,
                        struct nsproxy **out);
int32_t nsproxy_install(struct nsproxy *old, struct ns_common *ns,
                        struct nsproxy **out);
struct ns_common *nsproxy_ns(struct nsproxy *proxy, uint32_t type);
