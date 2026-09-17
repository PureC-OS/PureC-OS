#pragma once

#include <stdbool.h>
#include <stdint.h>

struct limine_smp_response;

bool smp_start_cpu(struct limine_smp_response *response, uint32_t logical_id, uint32_t timeout_ms);