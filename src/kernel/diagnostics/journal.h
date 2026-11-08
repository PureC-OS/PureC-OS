#pragma once

#include <stdint.h>

void journal_start(void);
void journal_mark_clean_shutdown(void);
uint32_t journal_boot_id(void);
