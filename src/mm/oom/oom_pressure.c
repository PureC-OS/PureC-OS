#include "oom_pressure.h"
#include "oom.h"
#include "../pmm.h"
enum oom_pressure oom_pressure_get(void)
{
    uint64_t reserve = oom_kernel_reserve_pages();
    uint64_t free_pages = pmm_free_bytes() / 4096ULL;
    if (free_pages <= reserve) {
        return OOM_PRESSURE_CRITICAL;
    }
    if (free_pages <= reserve * 2) {
        return OOM_PRESSURE_LOW;
    }
    return OOM_PRESSURE_OK;
}
bool oom_pressure_kernel_can_alloc(uint64_t page_count)
{
    return oom_kernel_allowed(page_count);
}
bool oom_pressure_user_can_alloc(uint64_t page_count)
{
    if (!page_count) {
        return true;
    }
    enum oom_pressure p = oom_pressure_get();
    if (p == OOM_PRESSURE_CRITICAL) {
        return false;
    }
    if (p == OOM_PRESSURE_LOW && page_count > 4) {
        return false;
    }
    return oom_user_allowed(page_count);
}
