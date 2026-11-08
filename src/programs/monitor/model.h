#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MON_MAX_PROCS 256
#define MON_MAX_CPUS 32
#define MON_NAME 32
#define MON_CMD 112
#define MON_HIST 160
#define MON_MAX_MOUNTS 16
#define MON_MAX_DISKS 16
#define MON_TOP_IRQ 10
#define MON_BROWSE_MAX 320
#define MON_BROWSE_NAME 48
#define MON_TEXT_MAX 20000

struct mon_proc {
  uint32_t pid;
  uint32_t ppid;
  uint32_t uid;
  char state;
  uint32_t cpu;
  uint64_t rss_kb;
  uint64_t heap_kb;
  uint64_t time_ms;
  char name[MON_NAME];
  char cmd[MON_CMD];
  bool cmd_loaded;
  int depth;
};

struct mon_cpu {
  bool online;
  uint64_t total;
  uint64_t idle;
  uint32_t pct;
};

struct mon_mount {
  char source[24];
  char path[40];
  char type[16];
  char opts[8];
};

struct mon_disk {
  char name[16];
  uint64_t sectors;
  uint32_t sector_size;
  char model[44];
};

struct mon_irq {
  uint32_t vector;
  uint64_t count;
};

struct mon_entry {
  char name[MON_BROWSE_NAME];
  bool is_dir;
};

struct mon_model {
  struct mon_proc procs[MON_MAX_PROCS];
  int proc_count;

  struct mon_cpu cpu[MON_MAX_CPUS];
  int cpu_count;
  int cpus_online;
  uint32_t cpu_total_pct;
  uint64_t sum_total;
  uint64_t sum_idle;
  bool have_prev;
  uint8_t hist[MON_HIST];
  int hist_len;

  uint64_t mem_total_kb;
  uint64_t mem_free_kb;
  uint64_t mem_used_kb;
  uint64_t swap_total_kb;
  uint64_t swap_free_kb;

  uint64_t uptime_cs;
  char version[96];
  char cpu_model[72];
  uint32_t cpu_mhz;
  int tasks_total;
  int tasks_running;
  char filesystems[96];

  struct mon_mount mounts[MON_MAX_MOUNTS];
  int mount_count;
  struct mon_disk disks[MON_MAX_DISKS];
  int disk_count;
  struct mon_irq irqs[MON_TOP_IRQ];
  int irq_count;

  uint32_t refresh_counter;
  bool ok;
};

void mon_set_root(const char *root);
int mon_read_proc(const char *relative, char *buffer, int capacity);
int mon_list_proc(const char *relative, struct mon_entry *out, int capacity);
void mon_refresh(struct mon_model *model);
