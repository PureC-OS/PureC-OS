#pragma once

#include <kernel/syscall/syscall.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct install_status;
extern struct install_status install_job;
extern volatile int install_worker_tid;

bool privileged(uint32_t capability);
bool wifi_allowed(void);
bool readable(const void *buffer, uint64_t size);
bool writable(void *buffer, uint64_t size);
bool readable_string(const char *text);
void audio_restore_system_settings(void);
