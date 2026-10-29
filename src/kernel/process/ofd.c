#include "ofd.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/serial/serial.h"
#include "../../fs/vfs.h"
#include "../../lib/string.h"
#include "../ipc/pipe.h"
#include "../sync/mutex.h"
#include "../syscall/syscall.h"

#define OFD_MAX 4096
#define OFD_CONSOLE_COUNT 3
#define OFD_O_ACCMODE 03U
#define OFD_O_WRONLY 01U
#define OFD_O_RDONLY 00U
#define OFD_O_APPEND 02000U
#define OFD_O_NONBLOCK 04000U

struct ofd {
  uint8_t kind;
  bool write_end;
  uint32_t flags;
  uint32_t refs;
  int32_t object;
};

static mutex_t ofd_mutex;
static struct ofd table[OFD_MAX];
static bool initialized;

int32_t kerr_from_vfs(int32_t code) {
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

void ofd_init(void) {
  mutex_lock(&ofd_mutex);
  if (!initialized) {
    initialized = true;
    table[0] = (struct ofd){OFD_CONSOLE_IN, false, 0, UINT32_MAX / 2, -1};
    table[1] = (struct ofd){OFD_CONSOLE_OUT, false, 1, UINT32_MAX / 2, -1};
    table[2] = (struct ofd){OFD_CONSOLE_OUT, false, 1, UINT32_MAX / 2, -1};
  }
  mutex_unlock(&ofd_mutex);
}

static int32_t allocate(uint8_t kind, uint32_t flags, int32_t object,
                        bool write_end) {
  ofd_init();
  mutex_lock(&ofd_mutex);
  for (int32_t i = OFD_CONSOLE_COUNT; i < OFD_MAX; i++) {
    if (table[i].kind != OFD_FREE)
      continue;
    table[i] = (struct ofd){kind, write_end, flags, 1, object};
    mutex_unlock(&ofd_mutex);
    return i;
  }
  mutex_unlock(&ofd_mutex);
  return -KERR_NFILE;
}

int32_t ofd_create_file(int32_t handle, uint32_t flags) {
  return allocate(OFD_FILE, flags & OFD_STATUS_MASK, handle, false);
}

int32_t ofd_create_pipe(int32_t pipe_id, bool write_end) {
  return allocate(OFD_PIPE, write_end ? OFD_O_WRONLY : OFD_O_RDONLY, pipe_id,
                  write_end);
}

static bool valid(int32_t index) {
  return index >= 0 && index < OFD_MAX && table[index].kind != OFD_FREE;
}

void ofd_ref(int32_t index) {
  mutex_lock(&ofd_mutex);
  if (valid(index) && index >= OFD_CONSOLE_COUNT && table[index].refs < UINT32_MAX)
    table[index].refs++;
  mutex_unlock(&ofd_mutex);
}

void ofd_release(int32_t index) {
  struct ofd dropped = {0};
  mutex_lock(&ofd_mutex);
  if (valid(index) && index >= OFD_CONSOLE_COUNT) {
    if (--table[index].refs == 0) {
      dropped = table[index];
      table[index].kind = OFD_FREE;
    }
  }
  mutex_unlock(&ofd_mutex);
  if (dropped.kind == OFD_FILE)
    (void)vfs_close(dropped.object);
  else if (dropped.kind == OFD_PIPE)
    pipe_release(dropped.object, dropped.write_end);
}

static bool snapshot(int32_t index, struct ofd *out) {
  mutex_lock(&ofd_mutex);
  bool ok = valid(index);
  if (ok)
    *out = table[index];
  mutex_unlock(&ofd_mutex);
  return ok;
}

int32_t ofd_vfs_handle(int32_t index) {
  struct ofd o;
  if (!snapshot(index, &o) || o.kind != OFD_FILE)
    return -1;
  return o.object;
}

int32_t ofd_kind_of(int32_t index) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -1;
  if (o.kind == OFD_CONSOLE_IN || o.kind == OFD_CONSOLE_OUT)
    return FD_KIND_CONSOLE;
  return o.kind == OFD_PIPE ? FD_KIND_PIPE : FD_KIND_FILE;
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

int32_t ofd_read(int32_t index, void *buffer, uint32_t count) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -KERR_BADF;
  switch (o.kind) {
  case OFD_CONSOLE_IN:
    return -KERR_CONSOLE;
  case OFD_PIPE:
    if (o.write_end)
      return -KERR_BADF;
    return pipe_read(o.object, buffer, count, (o.flags & OFD_O_NONBLOCK) != 0);
  case OFD_FILE: {
    if ((o.flags & OFD_O_ACCMODE) == OFD_O_WRONLY)
      return -KERR_BADF;
    filesystem_syscall_lock();
    int32_t result = vfs_read(o.object, buffer, count);
    filesystem_syscall_unlock();
    return result < 0 ? kerr_from_vfs(result) : result;
  }
  default:
    return -KERR_BADF;
  }
}

int32_t ofd_write(int32_t index, const void *buffer, uint32_t count) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -KERR_BADF;
  switch (o.kind) {
  case OFD_CONSOLE_OUT:
    console_write((const char *)buffer, count);
    return (int32_t)count;
  case OFD_PIPE:
    if (!o.write_end)
      return -KERR_BADF;
    return pipe_write(o.object, buffer, count, (o.flags & OFD_O_NONBLOCK) != 0);
  case OFD_FILE: {
    if ((o.flags & OFD_O_ACCMODE) == OFD_O_RDONLY)
      return -KERR_BADF;
    filesystem_syscall_lock();
    int32_t result = vfs_write(o.object, buffer, count);
    filesystem_syscall_unlock();
    return result < 0 ? kerr_from_vfs(result) : result;
  }
  default:
    return -KERR_BADF;
  }
}

int64_t ofd_seek(int32_t index, int64_t offset, uint32_t whence) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -KERR_BADF;
  if (o.kind != OFD_FILE)
    return -KERR_SPIPE;
  filesystem_syscall_lock();
  int64_t result = vfs_seek(o.object, offset, whence);
  filesystem_syscall_unlock();
  return result < 0 ? kerr_from_vfs((int32_t)result) : result;
}

int32_t ofd_truncate(int32_t index, uint64_t length) {
  struct ofd o;
  if (!snapshot(index, &o) || o.kind != OFD_FILE)
    return -KERR_BADF;
  if ((o.flags & OFD_O_ACCMODE) == OFD_O_RDONLY)
    return -KERR_INVAL;
  filesystem_syscall_lock();
  int32_t result = vfs_handle_truncate(o.object, length);
  filesystem_syscall_unlock();
  return result < 0 ? kerr_from_vfs(result) : 0;
}

int32_t ofd_stat(int32_t index, struct file_stat_info *out) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -KERR_BADF;
  if (o.kind != OFD_FILE) {
    memset(out, 0, sizeof(*out));
    return 0;
  }
  filesystem_syscall_lock();
  int32_t result = vfs_handle_stat(o.object, out);
  filesystem_syscall_unlock();
  return result < 0 ? kerr_from_vfs(result) : 0;
}

int32_t ofd_path(int32_t index, char *out, uint32_t capacity) {
  struct ofd o;
  if (!snapshot(index, &o) || o.kind != OFD_FILE)
    return -KERR_INVAL;
  int32_t result = vfs_handle_path(o.object, out, capacity);
  return result < 0 ? kerr_from_vfs(result) : result;
}

int32_t ofd_flags_get(int32_t index) {
  struct ofd o;
  if (!snapshot(index, &o))
    return -KERR_BADF;
  return (int32_t)o.flags;
}

int32_t ofd_flags_set(int32_t index, uint32_t flags) {
  mutex_lock(&ofd_mutex);
  bool ok = valid(index) && index >= OFD_CONSOLE_COUNT;
  if (ok)
    table[index].flags = (table[index].flags & ~(OFD_O_APPEND | OFD_O_NONBLOCK)) |
                         (flags & (OFD_O_APPEND | OFD_O_NONBLOCK));
  mutex_unlock(&ofd_mutex);
  return ok ? 0 : -KERR_BADF;
}
