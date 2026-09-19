#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSONALIZATION_PATH "/config/appear.ini"
#define PERSONALIZATION_THEME_CAP 32
#define PERSONALIZATION_WALLPAPER_CAP 128

struct personalization {
    char theme[PERSONALIZATION_THEME_CAP];
    char wallpaper[PERSONALIZATION_WALLPAPER_CAP];
};

struct personalization_colors {
    uint32_t desktop;
    uint32_t window;
    uint32_t titlebar;
    uint32_t border;
    uint32_t text;
    uint32_t muted_text;
    uint32_t accent;
    uint32_t danger;
    uint32_t shadow;
};

bool personalization_poll(void);
void personalization_current_colors(struct personalization_colors *out);

#ifdef __cplusplus
}
#endif
