#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/syscall/syscall.h"

void power_init(void);
void power_reboot(void);
void power_shutdown(void);
bool power_battery_get(struct battery_info *out);
bool power_thermal_get(struct thermal_info *out);
