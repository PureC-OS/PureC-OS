#pragma once

#include <stdbool.h>
#include <stdint.h>

struct file_stat_info;

enum ofd_kind {
  OFD_FREE = 0,
  OFD_CONSOLE_IN,
  OFD_CONSOLE_OUT,
  OFD_FILE,
  OFD_PIPE
};

#define OFD_STATUS_MASK 03003U

int32_t kerr_from_vfs(int32_t code);
void ofd_init(void);
int32_t ofd_create_file(int32_t handle, uint32_t flags);
int32_t ofd_create_pipe(int32_t pipe_id, bool write_end);
void ofd_ref(int32_t ofd);
void ofd_release(int32_t ofd);
int32_t ofd_vfs_handle(int32_t ofd);
int32_t ofd_kind_of(int32_t ofd);
int32_t ofd_read(int32_t ofd, void *buffer, uint32_t count);
int32_t ofd_write(int32_t ofd, const void *buffer, uint32_t count);
int64_t ofd_seek(int32_t ofd, int64_t offset, uint32_t whence);
int32_t ofd_truncate(int32_t ofd, uint64_t length);
int32_t ofd_stat(int32_t ofd, struct file_stat_info *out);
int32_t ofd_path(int32_t ofd, char *out, uint32_t capacity);
int32_t ofd_flags_get(int32_t ofd);
int32_t ofd_flags_set(int32_t ofd, uint32_t flags);
