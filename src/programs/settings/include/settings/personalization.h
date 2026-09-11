#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Ring3 mirror of /config/personalization.conf (same key=value format the
 * desktop in src/userspace/personalization.c reads). Theme names match
 * pg_theme_by_name() presets in libgui. */
#define APPEAR_THEME_CAP 32
#define APPEAR_WALLPAPER_CAP 128
#define APPEAR_FONT_CAP 48
#define APPEAR_MIN_FONT_SIZE 8
#define APPEAR_MAX_FONT_SIZE 24

struct personalization_settings {
    char theme[APPEAR_THEME_CAP];
    char wallpaper[APPEAR_WALLPAPER_CAP];
    char font[APPEAR_FONT_CAP];
    uint32_t font_size;
};

void appearance_defaults(struct personalization_settings *s);
bool appearance_load(struct personalization_settings *s);
bool appearance_save(const struct personalization_settings *s);

/* Wallpaper candidates offered in the UI. Index 0 is always "" (= solid
 * theme color); the rest are probed paths, user files last. */
uint32_t appearance_wallpaper_count(void);
const char *appearance_wallpaper_at(uint32_t index);

/* Font faces offered in the UI (bitmap faces today; *.ttf entries are
 * reserved for the future Ring3 TTF rasterizer). */
uint32_t appearance_font_count(void);
const char *appearance_font_name_at(uint32_t index);
