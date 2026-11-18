#pragma once
#include <stdbool.h>
#include <stdint.h>

int32_t pty_create(int32_t *id);
void pty_release(int32_t id, bool master);
int32_t pty_read(int32_t id, bool master, void *buffer, uint32_t count, bool nonblock);
int32_t pty_write(int32_t id, bool master, const void *buffer, uint32_t count, bool nonblock);
int32_t pty_poll(int32_t id, bool master);
uint64_t pty_generation(int32_t id);
int64_t pty_ioctl(int32_t id, bool master, uint32_t request, uint64_t argument);
int32_t pty_create_kernel(int32_t *id, void (*drain)(int32_t id), uint16_t columns, uint16_t rows);
void pty_set_kernel_winsize(int32_t id, uint16_t columns, uint16_t rows);
bool pty_is_kernel(int32_t id);
