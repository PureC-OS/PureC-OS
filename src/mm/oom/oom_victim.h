#pragma once
#include <stdbool.h>
#include <stdint.h>
struct oom_candidate {
    uint32_t pid;
    uint64_t user_pages;
    uint64_t runtime_ms;
    bool is_system;
};
typedef uint32_t (*oom_kill_fn)(uint32_t pid);
void oom_victim_init(void);
void oom_victim_set_killer(oom_kill_fn fn);
uint32_t oom_victim_select(const struct oom_candidate *cands, uint32_t count);
uint32_t oom_victim_kill_one(const struct oom_candidate *cands, uint32_t count);
