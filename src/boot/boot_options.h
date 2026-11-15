#pragma once
#include <stdbool.h>

bool boot_syscalls_cpu0(void);
bool boot_console_verbose(void);
const char *boot_cmdline(void);
