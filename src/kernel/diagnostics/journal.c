#include "journal.h"
#include "klog.h"
#include "../../fs/vfs.h"
#include "../../io/storage/block_device.h"
#include "../../lib/string.h"
#include "../process/scheduler.h"
#include "../random/krandom.h"
#include "../sync/mutex.h"
#include "../time/wallclock.h"

#define JOURNAL_DIR "/var/logs"
#define JOURNAL_BASE "journal"
#define JOURNAL_MAX_BYTES (4u * 1024u * 1024u)
#define JOURNAL_KEEP 5
#define JOURNAL_PERIOD_MS 500
#define DRAIN_SIZE 16384
#define OUT_SIZE 32768
#define LINE_MAX 1024
#define TAIL_SIZE 8192
#define PATH_MAX_LEN 64

static mutex_t journal_lock;
static uint64_t cursor;
static uint32_t boot_id;
static bool active;
static bool stopped;
static uint64_t file_size;
static int64_t boot_epoch;
static uint64_t last_uptime_us;
static uint32_t carry_len;
static uint32_t out_len;
static char carry[LINE_MAX];
static char drain[DRAIN_SIZE];
static char out[OUT_SIZE];

struct parsed {
  char level;
  int cpu;
  uint64_t uptime_us;
  const char *message;
  uint32_t message_length;
};

static uint32_t put_dec(char *dst, uint64_t value) {
  char digits[21];
  uint32_t n = 0;
  do {
    digits[n++] = (char)('0' + value % 10);
    value /= 10;
  } while (value);
  for (uint32_t i = 0; i < n; i++)
    dst[i] = digits[n - 1 - i];
  return n;
}

static uint32_t put_hex8(char *dst, uint32_t value) {
  for (int i = 0; i < 8; i++) {
    uint32_t nibble = (value >> (28 - 4 * i)) & 0xF;
    dst[i] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
  }
  return 8;
}

static void build_path(char *dst, int index) {
  const char *prefix = JOURNAL_DIR "/" JOURNAL_BASE;
  uint32_t n = 0;
  while (prefix[n]) {
    dst[n] = prefix[n];
    n++;
  }
  if (index > 0) {
    dst[n++] = '.';
    n += put_dec(dst + n, (uint64_t)index);
  }
  const char *suffix = ".log";
  for (uint32_t i = 0; suffix[i]; i++)
    dst[n++] = suffix[i];
  dst[n] = 0;
}

static void build_leaf(char *dst, int index) {
  char full[PATH_MAX_LEN];
  build_path(full, index);
  const char *leaf = full;
  for (const char *c = full; *c; c++)
    if (*c == '/')
      leaf = c + 1;
  uint32_t n = 0;
  while (leaf[n]) {
    dst[n] = leaf[n];
    n++;
  }
  dst[n] = 0;
}

static void update_epoch(void) {
  if (!wallclock_valid())
    return;
  boot_epoch = (int64_t)wallclock_unix_seconds() -
               (int64_t)(klog_uptime_us() / 1000000ull);
}

static uint32_t put_record(char *dst, uint32_t capacity, char level, int cpu,
                           uint64_t uptime_us, const char *message,
                           uint32_t length) {
  if (capacity < length + 80)
    return 0;
  uint32_t n = 0;
  n += put_hex8(dst + n, boot_id);
  dst[n++] = ' ';
  uint64_t unix_time = 0;
  if (boot_epoch > 0)
    unix_time = (uint64_t)boot_epoch + uptime_us / 1000000ull;
  n += put_dec(dst + n, unix_time);
  dst[n++] = ' ';
  n += put_dec(dst + n, uptime_us);
  dst[n++] = ' ';
  dst[n++] = level;
  dst[n++] = ' ';
  if (cpu < 0)
    dst[n++] = '-';
  else
    n += put_dec(dst + n, (uint64_t)cpu);
  dst[n++] = ' ';
  for (uint32_t i = 0; i < length; i++)
    dst[n++] = message[i] == '\n' ? ' ' : message[i];
  dst[n++] = '\n';
  return n;
}

static void rotate(void) {
  char from[PATH_MAX_LEN];
  char to_leaf[PATH_MAX_LEN];
  build_path(from, JOURNAL_KEEP);
  (void)vfs_delete(from);
  for (int i = JOURNAL_KEEP - 1; i >= 0; i--) {
    build_path(from, i);
    build_leaf(to_leaf, i + 1);
    (void)vfs_rename(from, to_leaf);
  }
  build_path(from, 0);
  (void)vfs_create_file(from);
  file_size = 0;
}

static bool append_store(const char *data, uint32_t length) {
  char path[PATH_MAX_LEN];
  build_path(path, 0);
  if (file_size + length > JOURNAL_MAX_BYTES && file_size > 0)
    rotate();
  int32_t status = vfs_append_file(path, data, length);
  if (status < 0)
    return false;
  file_size += length;
  return true;
}

static void flush_out(void) {
  if (!out_len)
    return;
  (void)append_store(out, out_len);
  out_len = 0;
}

static void emit(char level, int cpu, uint64_t uptime_us, const char *message,
                 uint32_t length) {
  uint32_t n = put_record(out + out_len, OUT_SIZE - out_len, level, cpu,
                          uptime_us, message, length);
  if (!n) {
    flush_out();
    n = put_record(out + out_len, OUT_SIZE - out_len, level, cpu, uptime_us,
                   message, length);
  }
  out_len += n;
  last_uptime_us = uptime_us;
}

static bool parse_klog(const char *line, uint32_t length, struct parsed *p) {
  uint32_t i = 0;
  if (i >= length || line[i] != '[')
    return false;
  i++;
  while (i < length && line[i] == ' ')
    i++;
  uint64_t seconds = 0;
  uint32_t digits = 0;
  while (i < length && line[i] >= '0' && line[i] <= '9') {
    seconds = seconds * 10 + (uint64_t)(line[i] - '0');
    i++;
    digits++;
  }
  if (!digits || i >= length || line[i] != '.')
    return false;
  i++;
  uint64_t micro = 0;
  digits = 0;
  while (i < length && line[i] >= '0' && line[i] <= '9') {
    micro = micro * 10 + (uint64_t)(line[i] - '0');
    i++;
    digits++;
  }
  if (digits != 6 || i + 3 > length || line[i] != ']' || line[i + 1] != ' ' ||
      line[i + 2] != '[')
    return false;
  i += 3;
  while (i < length && line[i] == ' ')
    i++;
  if (i >= length)
    return false;
  char c = line[i];
  char level;
  if (c == 'I')
    level = 'I';
  else if (c == 'O')
    level = 'O';
  else if (c == 'W')
    level = 'W';
  else if (c == 'F')
    level = 'E';
  else if (c == 'D')
    level = 'D';
  else
    return false;
  while (i < length && line[i] != ']')
    i++;
  if (i + 6 > length || line[i + 1] != ' ' || line[i + 2] != '[' ||
      line[i + 3] != 'c' || line[i + 4] != 'p' || line[i + 5] != 'u')
    return false;
  i += 6;
  int cpu = -1;
  if (i < length && line[i] >= '0' && line[i] <= '9') {
    cpu = 0;
    while (i < length && line[i] >= '0' && line[i] <= '9') {
      cpu = cpu * 10 + (line[i] - '0');
      i++;
    }
  } else if (i < length && line[i] == '?') {
    i++;
  }
  if (i + 2 > length || line[i] != ']' || line[i + 1] != ' ')
    return false;
  i += 2;
  p->level = level;
  p->cpu = cpu;
  p->uptime_us = seconds * 1000000ull + micro;
  p->message = line + i;
  p->message_length = length - i;
  return true;
}

static void emit_line(const char *line, uint32_t length) {
  while (length && (line[length - 1] == '\r' || line[length - 1] == '\n'))
    length--;
  if (!length)
    return;
  struct parsed p;
  if (parse_klog(line, length, &p))
    emit(p.level, p.cpu, p.uptime_us, p.message, p.message_length);
  else
    emit('R', -1, last_uptime_us, line, length);
}

static void feed(const char *data, uint32_t length) {
  for (uint32_t i = 0; i < length; i++) {
    char c = data[i];
    if (c == '\n') {
      emit_line(carry, carry_len);
      carry_len = 0;
      continue;
    }
    if (carry_len + 1 >= LINE_MAX) {
      emit_line(carry, carry_len);
      carry_len = 0;
    }
    carry[carry_len++] = c;
  }
}

static void flush_locked(void) {
  update_epoch();
  for (int round = 0; round < 64; round++) {
    bool lost = false;
    uint32_t amount = klog_read_since(&cursor, drain, DRAIN_SIZE, &lost);
    if (lost)
      emit('M', -1, klog_uptime_us(), "ring-overrun", 12);
    if (!amount)
      break;
    feed(drain, amount);
    if (amount < DRAIN_SIZE)
      break;
  }
  flush_out();
}

static bool store_ensure(void) {
  struct file_stat_info info;
  if (vfs_stat(JOURNAL_DIR, &info) < 0) {
    if (vfs_create_directory(JOURNAL_DIR) < 0)
      return false;
  }
  char path[PATH_MAX_LEN];
  build_path(path, 0);
  if (vfs_stat(path, &info) < 0) {
    if (vfs_create_file(path) < 0)
      return false;
    file_size = 0;
    return true;
  }
  file_size = info.size;
  return true;
}

static bool line_has(const char *line, uint32_t length, const char *needle) {
  uint32_t n = 0;
  while (needle[n])
    n++;
  if (length < n)
    return false;
  for (uint32_t i = 0; i + n <= length; i++) {
    uint32_t k = 0;
    while (k < n && line[i + k] == needle[k])
      k++;
    if (k == n)
      return true;
  }
  return false;
}

static bool hex_value(char c, uint32_t *out_value) {
  if (c >= '0' && c <= '9')
    *out_value = (uint32_t)(c - '0');
  else if (c >= 'a' && c <= 'f')
    *out_value = (uint32_t)(c - 'a' + 10);
  else
    return false;
  return true;
}

static void inspect_previous(void) {
  char path[PATH_MAX_LEN];
  build_path(path, 0);
  struct file_stat_info info;
  if (vfs_stat(path, &info) < 0 || info.size == 0)
    return;
  static char tail[TAIL_SIZE];
  uint64_t start = info.size > TAIL_SIZE ? info.size - TAIL_SIZE : 0;
  int32_t fd = vfs_open(path);
  if (fd < 0)
    return;
  uint32_t got = 0;
  if (vfs_seek(fd, (int64_t)start, SEEK_SET) >= 0) {
    int32_t r = vfs_read(fd, tail, TAIL_SIZE);
    if (r > 0)
      got = (uint32_t)r;
  }
  vfs_close(fd);
  if (!got)
    return;
  uint32_t i = 0;
  if (start)
    while (i < got && tail[i] != '\n')
      i++;
  bool clean = false;
  uint32_t previous_id = 0;
  bool have_id = false;
  while (i < got) {
    uint32_t j = i;
    while (j < got && tail[j] != '\n')
      j++;
    uint32_t length = j - i;
    if (length >= 8) {
      uint32_t id = 0;
      bool ok = true;
      for (int k = 0; k < 8; k++) {
        uint32_t nibble;
        if (!hex_value(tail[i + k], &nibble)) {
          ok = false;
          break;
        }
        id = (id << 4) | nibble;
      }
      if (ok) {
        previous_id = id;
        have_id = true;
      }
    }
    if (line_has(tail + i, length, " M - clean-shutdown"))
      clean = true;
    i = j + 1;
  }
  if (!clean && have_id)
    klogf(KLOG_WARN,
          "journal: previous boot %08x ended without a clean shutdown "
          "(crash, reset or power loss)",
          previous_id);
}

static void journal_thread(void *arg) {
  (void)arg;
  while (!vfs_is_root_mounted())
    scheduler_sleep(250);
  const char *root = vfs_root_device_name();
  if (root && strcmp(root, "initramfs") == 0) {
    klog(KLOG_INFO, "journal: live session, persistent journal disabled");
    scheduler_exit();
    return;
  }
  if (!store_ensure()) {
    klog(KLOG_WARN, "journal: cannot prepare " JOURNAL_DIR ", disabled");
    scheduler_exit();
    return;
  }
  mutex_lock(&journal_lock);
  inspect_previous();
  uint32_t id = 0;
  krandom_fill(&id, sizeof(id));
  boot_id = id;
  update_epoch();
  out_len += put_record(out + out_len, OUT_SIZE - out_len, 'M', -1, 0,
                        "boot-start", 10);
  active = true;
  flush_locked();
  mutex_unlock(&journal_lock);
  klogf(KLOG_OK, "journal: boot %08x recording to " JOURNAL_DIR "/journal.log",
        boot_id);
  for (;;) {
    scheduler_sleep(JOURNAL_PERIOD_MS);
    mutex_lock(&journal_lock);
    if (!stopped)
      flush_locked();
    mutex_unlock(&journal_lock);
  }
}

void journal_start(void) {
  int tid = scheduler_create_thread(journal_thread, 0, "journal", 3, -1);
  if (tid >= 0)
    scheduler_set_qos(tid, SCHED_QOS_BACKGROUND);
}

uint32_t journal_boot_id(void) {
  return boot_id;
}

void journal_mark_clean_shutdown(void) {
  if (!active)
    return;
  mutex_lock(&journal_lock);
  if (!stopped) {
    flush_locked();
    out_len += put_record(out + out_len, OUT_SIZE - out_len, 'M', -1,
                          klog_uptime_us(), "clean-shutdown", 14);
    flush_out();
    stopped = true;
    (void)block_device_flush();
  }
  mutex_unlock(&journal_lock);
}
