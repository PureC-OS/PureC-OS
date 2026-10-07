#include "oom_gate.h"
#include "oom.h"
#include "oom_account.h"
#include "oom_victim.h"
#include "oom_pressure.h"
#include "oom_slab.h"
void oom_gate_init(void)
{
    oom_init();
    oom_account_init();
    oom_victim_init();
    oom_slab_init();
}
bool oom_gate_try_charge_user(uint32_t pid, uint64_t pages)
{
    if (!pid || !pages) {
        return false;
    }
    if (!oom_pressure_user_can_alloc(pages)) {
        return false;
    }
    return oom_account_charge(pid, pages);
}
void oom_gate_release_user(uint32_t pid, uint64_t pages)
{
    oom_account_uncharge(pid, pages);
}
void oom_gate_remove_process(uint32_t pid)
{
    oom_account_remove(pid);
}
bool oom_gate_can_spawn(void)
{
    return oom_kernel_allowed(8);
}
