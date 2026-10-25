#include "oom_info.h"
#include "oom.h"
#include "oom_account.h"
#include "oom_pressure.h"
#include "oom_reclaim.h"
#include "oom_slab.h"
#include "../pmm.h"
#include <stddef.h>
void oom_info_get(struct oom_info *out)
{
    if (!out) {
        return;
    }
    out->total_pages = pmm_total_bytes() / 4096ULL;
    out->free_pages = pmm_free_bytes() / 4096ULL;
    out->reserve_pages = oom_kernel_reserve_pages();
    out->budget_pages = oom_user_budget_pages();
    out->proc_limit_pages = oom_user_process_limit_pages();
    out->total_user_pages = oom_account_total_user_pages();
    out->tracked_processes = oom_account_tracked_count();
    out->pressure = (uint32_t)oom_pressure_get();
    out->slab_pages = oom_slab_pages_used();
    out->shrinkers = oom_reclaim_count();
}
