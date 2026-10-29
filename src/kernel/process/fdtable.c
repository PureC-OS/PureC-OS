#include "fdtable.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/serial/serial.h"
#include "../../lib/string.h"
#include "../../fs/vfs.h"
#include "../ipc/pipe.h"
#include "../syscall/syscall.h"
#include "process.h"

#define O_CLOEXEC_FLAG 02000000U
#define O_NONBLOCK_FLAG 04000U

static bool value_is_pipe(int32_t value) { return value >= FD_PIPE_BASE; }
static int32_t pipe_id(int32_t value) { return (value - FD_PIPE_BASE) >> 1; }
static bool pipe_is_write(int32_t value) { return (value & 1) != 0; }

static int32_t vfs_error(int32_t code) {
  switch (code) {
  case FS_ERROR_NOT_FOUND:
    return -KERR_NOENT;
  case FS_ERROR_EXISTS:
    return -KERR_EXIST;
  case FS_ERROR_INVALID:
    return -KERR_INVAL;
  case FS_ERROR_NO_SPACE:
    return -KERR_NFILE;
  case FS_ERROR_NOT_DIR:
    return -KERR_NOTDIR;
  case FS_ERROR_DENIED:
  case FS_ERROR_READ_ONLY:
    return -KERR_ACCES;
  case FS_ERROR_UNSUPPORTED:
    return -KERR_BADF;
  default:
    return -KERR_IO;
  }
}

static void value_ref(int32_t value) {
  if (value >= VFS_FD_BASE && !value_is_pipe(value))
    (void)vfs_ref(value);
  else if (value_is_pipe(value))
    pipe_ref(pipe_id(value), pipe_is_write(value));
}

static void value_release(int32_t value) {
  if (value >= VFS_FD_BASE && !value_is_pipe(value))
    (void)vfs_close(value);
  else if (value_is_pipe(value))
    pipe_release(pipe_id(value), pipe_is_write(value));
}

static int32_t slot_find(const struct process *process, int32_t minimum) {
  for (int32_t fd = minimum < 0 ? 0 : minimum; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0)
      return fd;
  }
  return -1;
}

static int32_t value_get(int32_t fd) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT)
    return -1;
  process_table_lock();
  int32_t value = process->descriptors[fd];
  process_table_unlock();
  return value;
}

static void console_write(const char *text, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    serial_putc(text[i]);
    if (gop_console_is_active())
      gop_console_putc(text[i]);
    else
      gop_putc(text[i]);
  }
}

int32_t fd_open(const char *path, uint32_t flags) {
  struct process *process = process_current();
  if (!process)
    return -KERR_BADF;
  int32_t handle = vfs_open_flags(path, flags);
  if (handle < 0)
    return vfs_error(handle);
  process_table_lock();
  int32_t fd = slot_find(process, 0);
  if (fd < 0) {
    process_table_unlock();
    (void)vfs_close(handle);
    return -KERR_MFILE;
  }
  process->descriptors[fd] = handle;
  process->fd_flags[fd] = (flags & O_CLOEXEC_FLAG) ? PROCESS_FD_CLOEXEC : 0;
  process_table_unlock();
  return fd;
}

int32_t fd_read(int32_t fd, void *buffer, uint32_t count) {
  int32_t value = value_get(fd);
  if (value < 0)
    return -KERR_BADF;
  if (value == VFS_FD_STDIN)
    return -KERR_CONSOLE;
  if (value <= VFS_FD_STDERR)
    return -KERR_BADF;
  if (value_is_pipe(value)) {
    if (pipe_is_write(value))
      return -KERR_BADF;
    return pipe_read(pipe_id(value), buffer, count);
  }
  filesystem_syscall_lock();
  int32_t result = vfs_read(value, buffer, count);
  filesystem_syscall_unlock();
  return result < 0 ? vfs_error(result) : result;
}

int32_t fd_write(int32_t fd, const void *buffer, uint32_t count) {
  int32_t value = value_get(fd);
  if (value < 0 || value == VFS_FD_STDIN)
    return -KERR_BADF;
  if (value <= VFS_FD_STDERR) {
    console_write((const char *)buffer, count);
    return (int32_t)count;
  }
  if (value_is_pipe(value)) {
    if (!pipe_is_write(value))
      return -KERR_BADF;
    return pipe_write(pipe_id(value), buffer, count);
  }
  filesystem_syscall_lock();
  int32_t result = vfs_write(value, buffer, count);
  filesystem_syscall_unlock();
  return result < 0 ? vfs_error(result) : result;
}

int32_t fd_close(int32_t fd) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  process_table_lock();
  int32_t value = process->descriptors[fd];
  process->descriptors[fd] = -1;
  process->fd_flags[fd] = 0;
  process_table_unlock();
  if (value < 0)
    return -KERR_BADF;
  value_release(value);
  return 0;
}

int32_t fd_dup(int32_t fd, int32_t minimum, bool cloexec) {
  struct process *process = process_current();
  if (!process || fd < 0 || fd >= PROCESS_FD_COUNT || minimum < 0 ||
      minimum >= PROCESS_FD_COUNT)
    return -KERR_BADF;
  process_table_lock();
  int32_t value = process->descriptors[fd];
  int32_t slot = value < 0 ? -1 : slot_find(process, minimum);
  if (value < 0 || slot < 0) {
    process_table_unlock();
    return value < 0 ? -KERR_BADF : -KERR_MFILE;
  }
  value_ref(value);
  process->descriptors[slot] = value;
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
  int32_t value = process->descriptors[old_fd];
  if (value < 0) {
    process_table_unlock();
    return -KERR_BADF;
  }
  if (old_fd == new_fd) {
    process_table_unlock();
    return new_fd;
  }
  int32_t previous = process->descriptors[new_fd];
  value_ref(value);
  process->descriptors[new_fd] = value;
  process->fd_flags[new_fd] = 0;
  process_table_unlock();
  if (previous >= 0)
    value_release(previous);
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
  process_table_lock();
  int32_t read_fd = slot_find(process, 0);
  int32_t write_fd = -1;
  if (read_fd >= 0) {
    process->descriptors[read_fd] = FD_PIPE_BASE + id * 2;
    write_fd = slot_find(process, 0);
    if (write_fd < 0)
      process->descriptors[read_fd] = -1;
  }
  if (read_fd < 0 || write_fd < 0) {
    process_table_unlock();
    pipe_release(id, false);
    pipe_release(id, true);
    return -KERR_MFILE;
  }
  uint8_t cloexec = (flags & O_CLOEXEC_FLAG) ? PROCESS_FD_CLOEXEC : 0;
  process->descriptors[write_fd] = FD_PIPE_BASE + id * 2 + 1;
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
  process_table_lock();
  int32_t value = process->descriptors[fd];
  process_table_unlock();
  if (value < 0)
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
    if (value <= VFS_FD_STDERR)
      return FD_KIND_CONSOLE;
    return value_is_pipe(value) ? FD_KIND_PIPE : FD_KIND_FILE;
  default:
    return -KERR_INVAL;
  }
}

int32_t fd_path(int32_t fd, char *out, uint32_t capacity) {
  int32_t value = value_get(fd);
  if (value < 0)
    return -KERR_BADF;
  if (value <= VFS_FD_STDERR || value_is_pipe(value))
    return -KERR_INVAL;
  int32_t result = vfs_handle_path(value, out, capacity);
  return result < 0 ? vfs_error(result) : result;
}

int32_t fd_stat(int32_t fd, struct file_stat_info *out) {
  int32_t value = value_get(fd);
  if (value < 0)
    return -KERR_BADF;
  if (value <= VFS_FD_STDERR || value_is_pipe(value)) {
    memset(out, 0, sizeof(*out));
    return 0;
  }
  filesystem_syscall_lock();
  int32_t result = vfs_handle_stat(value, out);
  filesystem_syscall_unlock();
  return result < 0 ? vfs_error(result) : 0;
}

void fd_close_all(struct process *process) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    int32_t value = process->descriptors[fd];
    process->descriptors[fd] = -1;
    process->fd_flags[fd] = 0;
    if (value >= VFS_FD_BASE)
      value_release(value);
  }
}

void fd_close_on_exec(struct process *process) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0 ||
        !(process->fd_flags[fd] & PROCESS_FD_CLOEXEC))
      continue;
    int32_t value = process->descriptors[fd];
    process->descriptors[fd] = -1;
    process->fd_flags[fd] = 0;
    if (value >= VFS_FD_BASE)
      value_release(value);
  }
}

void fd_clone(struct process *child, const struct process *parent) {
  for (int32_t fd = 0; fd < PROCESS_FD_COUNT; fd++) {
    int32_t value = parent->descriptors[fd];
    child->descriptors[fd] = value;
    child->fd_flags[fd] = parent->fd_flags[fd];
    if (value >= VFS_FD_BASE)
      value_ref(value);
  }
}
