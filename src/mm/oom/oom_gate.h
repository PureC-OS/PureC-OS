#pragma once
#include <stdbool.h>
#include <stdint.h>
void oom_gate_init(void);
bool oom_gate_try_charge_user(uint32_t pid, uint64_t pages);
void oom_gate_release_user(uint32_t pid, uint64_t pages);
void oom_gate_remove_process(uint32_t pid);
bool oom_gate_can_spawn(void);
