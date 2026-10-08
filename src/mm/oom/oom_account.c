#include "oom_account.h"
#include "oom.h"
#define OOM_ACCOUNT_MAX 256
struct oom_account_entry {
  bool used;
  uint32_t pid;
  uint64_t pages;
};
static struct oom_account_entry oom_entries[OOM_ACCOUNT_MAX];
static uint64_t oom_total_user_pages;
static int oom_account_find(uint32_t pid) {
  for (int i = 0; i < OOM_ACCOUNT_MAX; i++) {
    if (oom_entries[i].used && oom_entries[i].pid == pid) {
      return i;
    }
  }
  return -1;
}
static int oom_account_find_free(void) {
  for (int i = 0; i < OOM_ACCOUNT_MAX; i++) {
    if (!oom_entries[i].used) {
      return i;
    }
  }
  return -1;
}
void oom_account_init(void) {
  for (int i = 0; i < OOM_ACCOUNT_MAX; i++) {
    oom_entries[i].used = false;
    oom_entries[i].pid = 0;
    oom_entries[i].pages = 0;
  }
  oom_total_user_pages = 0;
}
bool oom_account_charge(uint32_t pid, uint64_t pages) {
  if (!pid || !pages) {
    return false;
  }
  int idx = oom_account_find(pid);
  uint64_t cur = idx >= 0 ? oom_entries[idx].pages : 0;
  if (!oom_user_process_allowed(cur, pages)) {
    return false;
  }
  if (!oom_user_allowed(pages)) {
    return false;
  }
  if (idx >= 0) {
    oom_entries[idx].pages = cur + pages;
    oom_total_user_pages += pages;
    return true;
  }
  int free_idx = oom_account_find_free();
  if (free_idx < 0) {
    return false;
  }
  oom_entries[free_idx].used = true;
  oom_entries[free_idx].pid = pid;
  oom_entries[free_idx].pages = pages;
  oom_total_user_pages += pages;
  return true;
}
void oom_account_uncharge(uint32_t pid, uint64_t pages) {
  if (!pid || !pages) {
    return;
  }
  int idx = oom_account_find(pid);
  if (idx < 0) {
    return;
  }
  uint64_t cur = oom_entries[idx].pages;
  uint64_t drop = pages > cur ? cur : pages;
  oom_entries[idx].pages = cur - drop;
  oom_total_user_pages -= drop;
  if (oom_entries[idx].pages == 0) {
    oom_entries[idx].used = false;
    oom_entries[idx].pid = 0;
  }
}
void oom_account_remove(uint32_t pid) {
  if (!pid) {
    return;
  }
  int idx = oom_account_find(pid);
  if (idx < 0) {
    return;
  }
  oom_total_user_pages -= oom_entries[idx].pages;
  oom_entries[idx].used = false;
  oom_entries[idx].pid = 0;
  oom_entries[idx].pages = 0;
}
uint64_t oom_account_used(uint32_t pid) {
  int idx = oom_account_find(pid);
  return idx >= 0 ? oom_entries[idx].pages : 0;
}
uint64_t oom_account_total_user_pages(void) { return oom_total_user_pages; }
uint32_t oom_account_tracked_count(void) {
  uint32_t n = 0;
  for (int i = 0; i < OOM_ACCOUNT_MAX; i++) {
    if (oom_entries[i].used) {
      n++;
    }
  }
  return n;
}
