#pragma once

// x86-64 FPU/SSE state management (eager save/restore per thread).
// Lets userspace use doubles/SSE (TCC needs it); the kernel itself stays
// -mgeneral-regs-only and never touches FPU registers, so switches only
// need to happen at scheduler context-switch points (all under cli).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FPU_STATE_SIZE 512

void fpu_init(void);
bool fpu_available(void);
// area must be 16-byte aligned, FPU_STATE_SIZE bytes.
void fpu_thread_init(void *area);
void fpu_save(void *area);
void fpu_restore(const void *area);
