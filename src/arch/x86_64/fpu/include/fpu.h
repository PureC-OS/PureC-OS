#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FPU_STATE_SIZE 512

void fpu_init(void);
bool fpu_init_cpu(void);
bool fpu_available(void);
void fpu_thread_init(void *area);
void fpu_save(void *area);
void fpu_restore(const void *area);
