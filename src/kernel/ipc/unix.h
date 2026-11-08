#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UNIX_TYPE_STREAM 1
#define UNIX_PATH_MAX 108
#define UNIX_FDS_PER_MESSAGE 16

#define UNIX_MSG_PEEK 0x2U
#define UNIX_MSG_DONTWAIT 0x40U
#define UNIX_MSG_NOSIGNAL 0x4000U

struct unix_iov {
  uint64_t base;
  uint64_t length;
};

struct unix_cred {
  int32_t pid;
  uint32_t uid;
  uint32_t gid;
};

int32_t unix_create(int32_t type);
int32_t unix_pair(int32_t type, int32_t ids[2]);
void unix_release(int32_t id);
int32_t unix_bind(int32_t id, const uint8_t *name, uint32_t length);
int32_t unix_connect(int32_t id, const uint8_t *name, uint32_t length,
                     bool nonblock);
int32_t unix_listen(int32_t id, int32_t backlog);
int32_t unix_accept(int32_t id, bool nonblock, int32_t *new_id);
int32_t unix_name(int32_t id, bool peer, uint8_t *out, uint32_t capacity);
int32_t unix_shutdown(int32_t id, int32_t how);
int32_t unix_peer_cred(int32_t id, struct unix_cred *out);
int32_t unix_send(int32_t id, const struct unix_iov *iov, uint32_t count,
                  const int32_t *ofds, uint32_t ofd_count, bool nonblock,
                  uint32_t flags);
int32_t unix_recv(int32_t id, const struct unix_iov *iov, uint32_t count,
                  int32_t *ofds, uint32_t *ofd_count, bool *truncated,
                  uint32_t ofd_capacity, bool nonblock, uint32_t flags);
int32_t unix_poll(int32_t id);
