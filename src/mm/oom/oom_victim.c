#include "oom_victim.h"
#include <stddef.h>
static oom_kill_fn oom_killer;
void oom_victim_init(void)
{
    oom_killer = NULL;
}
void oom_victim_set_killer(oom_kill_fn fn)
{
    oom_killer = fn;
}
uint32_t oom_victim_select(const struct oom_candidate *cands, uint32_t count)
{
    if (!cands || !count) {
        return 0;
    }
    uint32_t best_pid = 0;
    uint64_t best_pages = 0;
    uint64_t best_runtime = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pid = cands[i].pid;
        if (!pid || pid == 1) {
            continue;
        }
        if (cands[i].is_system) {
            continue;
        }
        if (!cands[i].user_pages) {
            continue;
        }
        if (cands[i].user_pages > best_pages ||
            (cands[i].user_pages == best_pages && cands[i].runtime_ms < best_runtime) ||
            best_pid == 0) {
            best_pid = pid;
            best_pages = cands[i].user_pages;
            best_runtime = cands[i].runtime_ms;
        }
    }
    return best_pid;
}
uint32_t oom_victim_kill_one(const struct oom_candidate *cands, uint32_t count)
{
    if (!oom_killer) {
        return 0;
    }
    uint32_t pid = oom_victim_select(cands, count);
    if (!pid) {
        return 0;
    }
    return oom_killer(pid);
}
