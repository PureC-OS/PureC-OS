#include "fdtable.h"
#include "../../fs/vfs.h"
#include "../../lib/string.h"
#include "../ipc/pipe.h"
#include "../syscall/syscall.h"
#include "ofd.h"
#include "process.h"

#define O_CLOEXEC_FLAG 02000000U

static int32_t slot_find(const struct process *process, int32_t minimum) {
  for (int32_t fd = minimum < 0 ? 0 : minimum; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0)
      return fd;
  }
  return -1;
}

static int32_t object_get(int32_t fd) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT)
    return -1;
  process_table_lock();
  int32_t object = process->descriptors[fd];
  process_table_unlock();
  return object;
}

int32_t fd_install_handle(int32_t handle) {
  struct process *process = process_current();
  if (!process)
    return handle;
  int32_t object = ofd_create_file(handle, 0);
  if (object < 0)
    return -1;
  process_table_lock();
  for (int32_t fd = 3; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0) {
      process->descriptors[fd] = object;
      process->fd_flags[fd] = 0;
      process_table_unlock();
      return fd;
    }
  }
  process_table_unlock();
  ofd_release(object);
  return -1;
}

int32_t fd_vfs_handle(int32_t fd) {
  struct process *process = process_current();
  if (!process)
    return fd;
  int32_t object = object_get(fd);
  return object < 0 ? -1 : ofd_vfs_handle(object);
}

int32_t fd_open(const char *path, uint32_t flags) {
  struct process *process = process_current();
  if (!process)
    return -KERR_BADF;
  filesystem_syscall_lock();
  int32_t handle = vfs_open_flags(path, flags);
  filesystem_syscall_unlock();
  if (handle < 0)
    return kerr_from_vfs(handle);
  int32_t object = ofd_create_file(handle, flags);
  if (object < 0) {
    (void)vfs_close(handle);
    return object;
  }
  process_table_lock();
  int32_t fd = slot_find(process, 0);
  if (fd < 0) {
    process_table_unlock();
    ofd_release(object);
    return -KERR_MFILE;
  }
  process->descriptors[fd] = object;
  process->fd_flags[fd] = (flags & O_CLOEXEC_FLAG) ? PROCESS_FD_CLOEXEC : 0;
  process_table_unlock();
  return fd;
}

int32_t fd_read(int32_t fd, void *buffer, uint32_t count) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_read(object, buffer, count);
}

int32_t fd_write(int32_t fd, const void *buffer, uint32_t count) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_write(object, buffer, count);
}

int64_t fd_seek(int32_t fd, int64_t offset, uint32_t whence) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_seek(object, offset, whence);
}

int32_t fd_truncate(int32_t fd, uint64_t length) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_truncate(object, length);
}

int32_t fd_close(int32_t fd) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  process_table_lock();
  int32_t object = process->descriptors[fd];
  process->descriptors[fd] = -1;
  process->fd_flags[fd] = 0;
  process_table_unlock();
  if (object < 0)
    return -KERR_BADF;
  ofd_release(object);
  return 0;
}

int32_t fd_dup(int32_t fd, int32_t minimum, bool cloexec) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT || minimum < 0 ||
      minimum >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  process_table_lock();
  int32_t object = process->descriptors[fd];
  int32_t slot = object < 0 ? -1 : slot_find(process, minimum);
  if (object < 0 || slot < 0) {
    process_table_unlock();
    return object < 0 ? -KERR_BADF : -KERR_MFILE;
  }
  ofd_ref(object);
  process->descriptors[slot] = object;
  process->fd_flags[slot] = cloexec ? PROCESS_FD_CLOEXEC : 0;
  process_table_unlock();
  return slot;
}

int32_t fd_dup2(int32_t old_fd, int32_t new_fd) {
  struct process *process = process_current();
  if (!process || old_fd < 0 || old_fd >= PROCESS_FD_COUNT || new_fd < 0 ||
      new_fd >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  process_table_lock();
  int32_t object = process->descriptors[old_fd];
  if (object < 0) {
    process_table_unlock();
    return -KERR_BADF;
  }
  if (old_fd == new_fd) {
    process_table_unlock();
    return new_fd;
  }
  int32_t previous = process->descriptors[new_fd];
  ofd_ref(object);
  process->descriptors[new_fd] = object;
  process->fd_flags[new_fd] = 0;
  process_table_unlock();
  if (previous >= 0)
    ofd_release(previous);
  return new_fd;
}

int32_t fd_pipe(int32_t fds[2], uint32_t flags) {
  struct process *process = process_current();
  if (!process)
    return -KERR_BADF;
  int32_t id;
  int32_t status = pipe_create(&id);
  if (status < 0)
    return status;
  int32_t read_object = ofd_create_pipe(id, false);
  int32_t write_object = ofd_create_pipe(id, true);
  if (read_object < 0 || write_object < 0) {
    if (read_object >= 0)
      ofd_release(read_object);
    else
      pipe_release(id, false);
    if (write_object >= 0)
      ofd_release(write_object);
    else
      pipe_release(id, true);
    return -KERR_NFILE;
  }
  if (flags & 04000U) {
    (void)ofd_flags_set(read_object, 04000U);
    (void)ofd_flags_set(write_object, 04000U);
  }
  process_table_lock();
  int32_t read_fd = slot_find(process, 0);
  int32_t write_fd = -1;
  if (read_fd >= 0) {
    process->descriptors[read_fd] = read_object;
    write_fd = slot_find(process, 0);
    if (write_fd < 0)
      process->descriptors[read_fd] = -1;
  }
  if (read_fd < 0 || write_fd < 0) {
    process_table_unlock();
    ofd_release(read_object);
    ofd_release(write_object);
    return -KERR_MFILE;
  }
  uint8_t cloexec = (flags & O_CLOEXEC_FLAG) ? PROCESS_FD_CLOEXEC : 0;
  process->descriptors[write_fd] = write_object;
  process->fd_flags[read_fd] = cloexec;
  process->fd_flags[write_fd] = cloexec;
  process_table_unlock();
  fds[0] = read_fd;
  fds[1] = write_fd;
  return 0;
}

int32_t fd_ctl(int32_t fd, uint32_t command, uint64_t argument) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  int32_t object = object_get(fd);
  if (object < 0)
    return -KERR_BADF;
  switch (command) {
  case FD_CTL_GETFD:
    return process->fd_flags[fd];
  case FD_CTL_SETFD:
    process->fd_flags[fd] = (uint8_t)(argument & PROCESS_FD_CLOEXEC);
    return 0;
  case FD_CTL_DUPFD:
    return fd_dup(fd, (int32_t)argument, false);
  case FD_CTL_DUPFD_CLOEXEC:
    return fd_dup(fd, (int32_t)argument, true);
  case FD_CTL_KIND:
    return ofd_kind_of(object);
  case FD_CTL_GETFL:
    return ofd_flags_get(object);
  case FD_CTL_SETFL:
    return ofd_flags_set(object, (uint32_t)argument);
  default:
    return -KERR_INVAL;
  }
}

int32_t fd_path(int32_t fd, char *out, uint32_t capacity) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_path(object, out, capacity);
}

int32_t fd_stat(int32_t fd, struct file_stat_info *out) {
  int32_t object = object_get(fd);
  return object < 0 ? -KERR_BADF : ofd_stat(object, out);
}

void fd_close_all(struct process *process) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    int32_t object = process->descriptors[fd];
    process->descriptors[fd] = -1;
    process->fd_flags[fd] = 0;
    if (object >= 0)
      ofd_release(object);
  }
}

void fd_close_on_exec(struct process *process) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0 ||
        !(process->fd_flags[fd] & PROCESS_FD_CLOEXEC))
      continue;
    int32_t object = process->descriptors[fd];
    process->descriptors[fd] = -1;
    process->fd_flags[fd] = 0;
    ofd_release(object);
  }
}

void fd_clone(struct process *child, const struct process *parent) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    int32_t object = parent->descriptors[fd];
    child->descriptors[fd] = object;
    child->fd_flags[fd] = parent->fd_flags[fd];
    if (object >= 0)
      ofd_ref(object);
  }
}
