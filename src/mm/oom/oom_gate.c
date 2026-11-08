#include "oom_gate.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../io/swap.h"
#include "../../kernel/process/process.h"
#include "../pmm.h"
#include "../vmm.h"
#include "oom.h"
#include "oom_account.h"
#include "oom_pressure.h"
#include "oom_reclaim.h"
#include "oom_slab.h"
#include "oom_victim.h"

#define OOM_KILL_COOLDOWN_MS 1500
#define OOM_CANDIDATE_MAX 64

static uint32_t oom_last_victim;
static uint64_t oom_last_kill_tick;

static bool oom_gate_slab_shrinker(uint64_t want_pages, void *ctx) {
  (void)want_pages;
  (void)ctx;
  return oom_slab_reclaim_empty() > 0;
}
void oom_gate_init(void) {
  oom_init();
  oom_account_init();
  oom_victim_init();
  oom_slab_init();
  oom_reclaim_register(oom_gate_slab_shrinker, NULL);
}
#define OOM_SWAP_BATCH_MAX 16384ULL

static bool oom_gate_swap_reclaim(uint64_t pages) {
  struct swap_info info;
  swap_info_get(&info);
  if (!info.enabled || info.used_pages >= info.total_pages) {
    return false;
  }
  uint64_t free_pages = pmm_free_bytes() / 4096ULL;
  uint64_t target = oom_kernel_reserve_pages() * 2 + pages;
  if (free_pages >= target) {
    return true;
  }
  uint64_t want = target - free_pages;
  if (want > OOM_SWAP_BATCH_MAX) {
    want = OOM_SWAP_BATCH_MAX;
  }
  return vmm_swap_reclaim((uint32_t)want) > 0;
}

static void oom_gate_kill_for(uint32_t requester) {
  uint64_t now = timer_ticks();
  if (oom_last_victim && now - oom_last_kill_tick < OOM_KILL_COOLDOWN_MS) {
    return;
  }
  struct oom_candidate candidates[OOM_CANDIDATE_MAX];
  uint32_t count = process_oom_candidates(candidates, OOM_CANDIDATE_MAX);
  uint32_t victim = oom_victim_select(candidates, count);
  if (!victim) {
    klogf(KLOG_WARN, "oom: pid=%u starved, no killable victim", requester);
    return;
  }
  uint64_t pages = oom_account_used(victim);
  uint32_t killed = oom_victim_kill_one(candidates, count);
  oom_last_victim = killed ? victim : 0;
  oom_last_kill_tick = now;
  klogf(KLOG_WARN, "oom: pid=%u starved, kill victim pid=%u pages=%llu %s",
        requester, victim, (unsigned long long)pages,
        killed ? "SIGKILL sent" : "kill failed");
}
bool oom_gate_try_charge_user(uint32_t pid, uint64_t pages) {
  if (!pid || !pages) {
    return false;
  }
  if (!oom_pressure_user_can_alloc(pages)) {
    (void)oom_gate_swap_reclaim(pages);
    if (!oom_pressure_user_can_alloc(pages)) {
      (void)oom_reclaim_try(pages);
    }
    if (!oom_pressure_user_can_alloc(pages)) {
      oom_gate_kill_for(pid);
      return false;
    }
  }
  return oom_account_charge(pid, pages);
}
void oom_gate_release_user(uint32_t pid, uint64_t pages) {
  oom_account_uncharge(pid, pages);
}
void oom_gate_remove_process(uint32_t pid) { oom_account_remove(pid); }
bool oom_gate_can_spawn(void) { return oom_kernel_allowed(8); }
