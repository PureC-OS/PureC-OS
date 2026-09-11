#pragma once
#include <stdint.h>
#include <stdbool.h>
#define DISPLAY_INI_PATH "/config/display.ini"
#define DISPLAY_INI_DIR "/config"

struct display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

struct display_settings {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

void display_defaults(struct display_settings *s);
bool display_load(struct display_settings *s);
int32_t display_save(const struct display_settings *s);
uint32_t display_mode_count(void);
struct display_mode display_mode_at(uint32_t index);
uint32_t display_mode_index_of(uint32_t width, uint32_t height);
int32_t display_apply_to_boot(const struct display_settings *s);
