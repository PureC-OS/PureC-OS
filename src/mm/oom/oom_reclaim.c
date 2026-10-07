#include "oom_reclaim.h"
#include <stddef.h>
#define OOM_RECLAIM_MAX 16
struct oom_shrinker {
    bool used;
    oom_shrinker_fn fn;
    void *ctx;
};
static struct oom_shrinker oom_shrinkers[OOM_RECLAIM_MAX];
int oom_reclaim_register(oom_shrinker_fn fn, void *ctx)
{
    if (!fn) {
        return -1;
    }
    for (int i = 0; i < OOM_RECLAIM_MAX; i++) {
        if (!oom_shrinkers[i].used) {
            oom_shrinkers[i].used = true;
            oom_shrinkers[i].fn = fn;
            oom_shrinkers[i].ctx = ctx;
            return i;
        }
    }
    return -1;
}
void oom_reclaim_unregister(int id)
{
    if (id < 0 || id >= OOM_RECLAIM_MAX) {
        return;
    }
    oom_shrinkers[id].used = false;
    oom_shrinkers[id].fn = NULL;
    oom_shrinkers[id].ctx = NULL;
}
uint64_t oom_reclaim_try(uint64_t want_pages)
{
    uint64_t done = 0;
    for (int i = 0; i < OOM_RECLAIM_MAX; i++) {
        if (!oom_shrinkers[i].used || !oom_shrinkers[i].fn) {
            continue;
        }
        uint64_t need = want_pages > done ? want_pages - done : 0;
        if (oom_shrinkers[i].fn(need, oom_shrinkers[i].ctx)) {
            done++;
        }
        if (done >= want_pages) {
            break;
        }
    }
    return done;
}
uint32_t oom_reclaim_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < OOM_RECLAIM_MAX; i++) {
        if (oom_shrinkers[i].used) {
            n++;
        }
    }
    return n;
}
