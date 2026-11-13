#pragma once

#include <stdbool.h>
#include <stdint.h>

struct input_record;

int32_t inputdev_open(void);
void inputdev_release(void);
bool inputdev_active(void);
void inputdev_push(const struct input_record *record);
int32_t inputdev_read(void *buffer, uint32_t count, bool nonblock);
int32_t inputdev_poll(void);
uint64_t inputdev_generation(void);
