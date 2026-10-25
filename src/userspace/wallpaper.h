#pragma once

#include <stdint.h>
#include <stdbool.h>
void wallpaper_set_path(const char *path);
bool wallpaper_active(void);
bool wallpaper_draw(void);
