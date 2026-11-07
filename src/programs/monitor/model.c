#include "model.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef MON_HOST
#include <dirent.h>
#else
#include "../../libc/include/purec.h"
#endif

#define ENTRY_CAPACITY 320

static char proc_root[160] = "/proc";

void mon_set_root(const char *root) {
  strncpy(proc_root, root, sizeof(proc_root) - 1);
  proc_root[sizeof(proc_root) - 1] = 0;
}

static void full_path(char *out, size_t capacity, const char *relative) {
  if (relative && relative[0])
    snprintf(out, capacity, "%s/%s", proc_root, relative);
  else
    snprintf(out, capacity, "%s", proc_root);
}

int mon_read_proc(const char *relative, char *buffer, int capacity) {
  char path[256];
  full_path(path, sizeof(path), relative);
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  int total = 0;
  while (total < capacity - 1) {
    int got = (int)read(fd, buffer + total, (size_t)(capacity - 1 - total));
    if (got <= 0)
      break;
    total += got;
  }
  close(fd);
  buffer[total] = 0;
  return total;
}

#ifdef MON_HOST
int mon_list_proc(const char *relative, struct mon_entry *out, int capacity) {
  char path[256];
  full_path(path, sizeof(path), relative);
  DIR *directory = opendir(path);
  if (!directory)
    return -1;
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) && count < capacity) {
    if (entry->d_name[0] == '.')
      continue;
    strncpy(out[count].name, entry->d_name, MON_BROWSE_NAME - 1);
    out[count].name[MON_BROWSE_NAME - 1] = 0;
    out[count].is_dir = entry->d_type == DT_DIR;
    count++;
  }
  closedir(directory);
  return count;
}
#else
int mon_list_proc(const char *relative, struct mon_entry *out, int capacity) {
  static struct fs_directory_entry_long entries[ENTRY_CAPACITY];
  char path[256];
  full_path(path, sizeof(path), relative);
  int32_t found = pc_directory_list_long(path, entries, ENTRY_CAPACITY);
  if (found < 0)
    return -1;
  int count = 0;
  for (int32_t i = 0; i < found && count < capacity; i++) {
    if (entries[i].name[0] == '.')
      continue;
    strncpy(out[count].name, entries[i].name, MON_BROWSE_NAME - 1);
    out[count].name[MON_BROWSE_NAME - 1] = 0;
    out[count].is_dir = (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
    count++;
  }
  return count;
}
#endif

static const char *skip_blanks(const char *p) {
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

static const char *next_line(const char *p) {
  while (*p && *p != '\n')
    p++;
  return *p == '\n' ? p + 1 : p;
}

static void copy_token(char *out, size_t capacity, const char **cursor) {
  const char *p = skip_blanks(*cursor);
  size_t n = 0;
  while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
    if (n + 1 < capacity)
      out[n++] = *p;
    p++;
  }
  out[n] = 0;
  *cursor = p;
}

static void copy_rest(char *out, size_t capacity, const char *p) {
  p = skip_blanks(p);
  size_t n = 0;
  while (*p && *p != '\n' && n + 1 < capacity)
    out[n++] = *p++;
  while (n && (out[n - 1] == ' ' || out[n - 1] == '\r'))
    n--;
  out[n] = 0;
}

static uint64_t to_u64(const char *p, const char **end) {
  char *stop;
  uint64_t v = strtoull(p, &stop, 10);
  if (end)
    *end = stop;
  return v;
}

static void parse_stat(struct mon_model *m, const char *text) {
  int count = 0;
  uint64_t prev_total[MON_MAX_CPUS], prev_idle[MON_MAX_CPUS];
  for (int i = 0; i < MON_MAX_CPUS; i++) {
    prev_total[i] = m->cpu[i].total;
    prev_idle[i] = m->cpu[i].idle;
  }
  uint64_t old_sum_total = m->sum_total, old_sum_idle = m->sum_idle;
  bool had_prev = m->have_prev;
  m->cpus_online = 0;
  for (const char *line = text; *line; line = next_line(line)) {
    if (!strncmp(line, "cpu  ", 5)) {
      const char *p = line + 5;
      m->sum_total = to_u64(p, &p);
      m->sum_idle = to_u64(p, &p);
      m->cpus_online = (int)to_u64(p, &p);
    } else if (!strncmp(line, "cpu", 3) && line[3] >= '0' && line[3] <= '9') {
      const char *p = line + 3;
      uint32_t id = (uint32_t)to_u64(p, &p);
      if (id >= MON_MAX_CPUS)
        continue;
      m->cpu[id].total = to_u64(p, &p);
      m->cpu[id].idle = to_u64(p, &p);
      m->cpu[id].online = to_u64(p, &p) != 0;
      uint64_t dt = m->cpu[id].total - prev_total[id];
      uint64_t di = m->cpu[id].idle - prev_idle[id];
      if (had_prev && m->cpu[id].total > prev_total[id] && dt) {
        if (di > dt)
          di = dt;
        m->cpu[id].pct = 100u - (uint32_t)(di * 100 / dt);
      } else if (!had_prev) {
        m->cpu[id].pct = 0;
      }
      if ((int)id + 1 > count)
        count = (int)id + 1;
    } else if (!strncmp(line, "tasks ", 6)) {
      const char *p = line + 6;
      m->tasks_total = (int)to_u64(p, &p);
      m->tasks_running = (int)to_u64(p, &p);
    }
  }
  m->cpu_count = count;
  if (had_prev && m->sum_total > old_sum_total) {
    uint64_t dt = m->sum_total - old_sum_total;
    uint64_t di = m->sum_idle - old_sum_idle;
    if (di > dt)
      di = dt;
    m->cpu_total_pct = 100u - (uint32_t)(di * 100 / dt);
    if (m->hist_len == MON_HIST) {
      memmove(m->hist, m->hist + 1, MON_HIST - 1);
      m->hist_len--;
    }
    m->hist[m->hist_len++] = (uint8_t)m->cpu_total_pct;
  }
  m->have_prev = true;
}

static void parse_meminfo(struct mon_model *m, const char *text) {
  for (const char *line = text; *line; line = next_line(line)) {
    char key[24];
    const char *p = line;
    size_t n = 0;
    while (*p && *p != ':' && *p != '\n' && n + 1 < sizeof(key))
      key[n++] = *p++;
    key[n] = 0;
    if (*p != ':')
      continue;
    uint64_t v = to_u64(p + 1, 0);
    if (!strcmp(key, "MemTotal"))
      m->mem_total_kb = v;
    else if (!strcmp(key, "MemFree"))
      m->mem_free_kb = v;
    else if (!strcmp(key, "MemUsed"))
      m->mem_used_kb = v;
    else if (!strcmp(key, "SwapTotal"))
      m->swap_total_kb = v;
    else if (!strcmp(key, "SwapFree"))
      m->swap_free_kb = v;
  }
  if (!m->mem_used_kb && m->mem_total_kb > m->mem_free_kb)
    m->mem_used_kb = m->mem_total_kb - m->mem_free_kb;
}

static void parse_cpuinfo(struct mon_model *m, const char *text) {
  bool have_model = false;
  for (const char *line = text; *line; line = next_line(line)) {
    if (!strncmp(line, "model name", 10) && !have_model) {
      const char *colon = strchr(line, ':');
      if (colon) {
        copy_rest(m->cpu_model, sizeof(m->cpu_model), colon + 1);
        have_model = true;
      }
    } else if (!strncmp(line, "cpu MHz", 7)) {
      const char *colon = strchr(line, ':');
      if (colon)
        m->cpu_mhz = (uint32_t)to_u64(skip_blanks(colon + 1), 0);
    }
  }
}

static void parse_mounts(struct mon_model *m, const char *text) {
  m->mount_count = 0;
  for (const char *line = text; *line && m->mount_count < MON_MAX_MOUNTS;
       line = next_line(line)) {
    if (*line == '\n')
      continue;
    struct mon_mount *out = &m->mounts[m->mount_count];
    const char *p = line;
    copy_token(out->source, sizeof(out->source), &p);
    copy_token(out->path, sizeof(out->path), &p);
    copy_token(out->type, sizeof(out->type), &p);
    copy_token(out->opts, sizeof(out->opts), &p);
    if (out->path[0])
      m->mount_count++;
  }
}

static void parse_partitions(struct mon_model *m, const char *text) {
  m->disk_count = 0;
  const char *line = next_line(text);
  for (; *line && m->disk_count < MON_MAX_DISKS; line = next_line(line)) {
    if (*line == '\n')
      continue;
    struct mon_disk *out = &m->disks[m->disk_count];
    const char *p = line;
    copy_token(out->name, sizeof(out->name), &p);
    out->sectors = to_u64(skip_blanks(p), &p);
    out->sector_size = (uint32_t)to_u64(skip_blanks(p), &p);
    copy_rest(out->model, sizeof(out->model), p);
    if (out->name[0])
      m->disk_count++;
  }
}

static void parse_interrupts(struct mon_model *m, const char *text) {
  m->irq_count = 0;
  for (const char *line = text; *line; line = next_line(line)) {
    const char *p = skip_blanks(line);
    if (*p < '0' || *p > '9')
      continue;
    uint32_t vector = (uint32_t)to_u64(p, &p);
    if (*p != ':')
      continue;
    uint64_t count = to_u64(p + 1, 0);
    int at = m->irq_count;
    while (at > 0 && m->irqs[at - 1].count < count)
      at--;
    if (at >= MON_TOP_IRQ)
      continue;
    int last = m->irq_count < MON_TOP_IRQ ? m->irq_count : MON_TOP_IRQ - 1;
    for (int i = last; i > at; i--)
      m->irqs[i] = m->irqs[i - 1];
    m->irqs[at].vector = vector;
    m->irqs[at].count = count;
    if (m->irq_count < MON_TOP_IRQ)
      m->irq_count++;
  }
}

static void parse_filesystems(struct mon_model *m, const char *text) {
  size_t n = 0;
  m->filesystems[0] = 0;
  for (const char *line = text; *line; line = next_line(line)) {
    const char *p = skip_blanks(line);
    if (!strncmp(p, "nodev", 5))
      p = skip_blanks(p + 5);
    size_t start = n;
    while (*p && *p != '\n' && n + 3 < sizeof(m->filesystems)) {
      m->filesystems[n++] = *p++;
    }
    if (n > start && n + 3 < sizeof(m->filesystems)) {
      m->filesystems[n++] = ',';
      m->filesystems[n++] = ' ';
    }
  }
  if (n >= 2)
    n -= 2;
  m->filesystems[n] = 0;
}

static char state_letter(const char *word) {
  if (!strncmp(word, "running", 7))
    return 'R';
  if (!strncmp(word, "ready", 5))
    return 'S';
  if (!strncmp(word, "exited", 6))
    return 'Z';
  return '?';
}

static bool read_status(struct mon_proc *p, const char *text) {
  bool have_pid = false;
  for (const char *line = text; *line; line = next_line(line)) {
    const char *colon = strchr(line, ':');
    const char *eol = line;
    while (*eol && *eol != '\n')
      eol++;
    if (!colon || colon > eol)
      continue;
    size_t klen = (size_t)(colon - line);
    const char *value = skip_blanks(colon + 1);
    if (klen == 4 && !strncmp(line, "Name", 4))
      copy_rest(p->name, sizeof(p->name), value);
    else if (klen == 5 && !strncmp(line, "State", 5))
      p->state = state_letter(value);
    else if (klen == 3 && !strncmp(line, "Pid", 3)) {
      p->pid = (uint32_t)to_u64(value, 0);
      have_pid = true;
    } else if (klen == 4 && !strncmp(line, "PPid", 4))
      p->ppid = (uint32_t)to_u64(value, 0);
    else if (klen == 5 && !strncmp(line, "VmRSS", 5))
      p->rss_kb = to_u64(value, 0);
    else if (klen == 6 && !strncmp(line, "VmHeap", 6))
      p->heap_kb = to_u64(value, 0);
    else if (klen == 7 && !strncmp(line, "CpuTime", 7))
      p->time_ms = to_u64(value, 0);
    else if (klen == 3 && !strncmp(line, "Cpu", 3))
      p->cpu = (uint32_t)to_u64(value, 0);
    else if (klen == 3 && !strncmp(line, "Uid", 3))
      p->uid = (uint32_t)to_u64(value, 0);
  }
  return have_pid;
}

static void refresh_processes(struct mon_model *m) {
  static struct mon_entry entries[MON_BROWSE_MAX];
  static struct mon_proc fresh[MON_MAX_PROCS];
  int found = mon_list_proc("", entries, MON_BROWSE_MAX);
  if (found < 0) {
    m->proc_count = 0;
    return;
  }
  int count = 0;
  char text[1024], path[64];
  for (int i = 0; i < found && count < MON_MAX_PROCS; i++) {
    if (!entries[i].is_dir || entries[i].name[0] < '0' || entries[i].name[0] > '9')
      continue;
    snprintf(path, sizeof(path), "%s/status", entries[i].name);
    if (mon_read_proc(path, text, sizeof(text)) <= 0)
      continue;
    struct mon_proc *p = &fresh[count];
    memset(p, 0, sizeof(*p));
    if (!read_status(p, text))
      continue;
    struct mon_proc *old = 0;
    for (int k = 0; k < m->proc_count; k++)
      if (m->procs[k].pid == p->pid) {
        old = &m->procs[k];
        break;
      }
    bool reload = !old || !old->cmd_loaded || (m->refresh_counter % 6) == 0;
    if (old && !reload) {
      memcpy(p->cmd, old->cmd, sizeof(p->cmd));
      p->cmd_loaded = true;
    } else {
      snprintf(path, sizeof(path), "%s/cmdline", entries[i].name);
      int n = mon_read_proc(path, text, sizeof(text));
      if (n > 0) {
        copy_rest(p->cmd, sizeof(p->cmd), text);
        p->cmd_loaded = true;
      } else if (old) {
        memcpy(p->cmd, old->cmd, sizeof(p->cmd));
        p->cmd_loaded = old->cmd_loaded;
      }
    }
    count++;
  }
  memcpy(m->procs, fresh, sizeof(fresh[0]) * (size_t)count);
  m->proc_count = count;
}

void mon_refresh(struct mon_model *m) {
  static char text[8192];
  bool slow = m->refresh_counter % 4 == 0;
  m->ok = false;
  if (mon_read_proc("stat", text, sizeof(text)) > 0) {
    parse_stat(m, text);
    m->ok = true;
  }
  if (mon_read_proc("meminfo", text, sizeof(text)) > 0)
    parse_meminfo(m, text);
  if (mon_read_proc("uptime", text, sizeof(text)) > 0) {
    const char *p = text;
    uint64_t seconds = to_u64(p, &p);
    uint64_t cs = 0;
    if (*p == '.')
      cs = to_u64(p + 1, 0);
    m->uptime_cs = seconds * 100 + cs;
  }
  refresh_processes(m);
  if (slow) {
    if (mon_read_proc("version", text, sizeof(text)) > 0)
      copy_rest(m->version, sizeof(m->version), text);
    if (mon_read_proc("cpuinfo", text, sizeof(text)) > 0)
      parse_cpuinfo(m, text);
    if (mon_read_proc("mounts", text, sizeof(text)) > 0)
      parse_mounts(m, text);
    if (mon_read_proc("partitions", text, sizeof(text)) > 0)
      parse_partitions(m, text);
    if (mon_read_proc("interrupts", text, sizeof(text)) > 0)
      parse_interrupts(m, text);
    if (mon_read_proc("filesystems", text, sizeof(text)) > 0)
      parse_filesystems(m, text);
  }
  m->refresh_counter++;
}
