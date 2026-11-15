#pragma once
#include <stdbool.h>

bool boot_desktop_rust(void);
bool boot_syscalls_cpu0(void);
bool boot_gui_kernel(void);
bool boot_console_verbose(void);
const char *boot_cmdline(void);
