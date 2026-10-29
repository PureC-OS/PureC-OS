#pragma once

#include <stdbool.h>
#include <stdint.h>

int32_t pipe_create(int32_t *id);
void pipe_ref(int32_t id, bool write_end);
void pipe_release(int32_t id, bool write_end);
int32_t pipe_read(int32_t id, void *buffer, uint32_t count, bool nonblock);
int32_t pipe_write(int32_t id, const void *buffer, uint32_t count,
                   bool nonblock);
