#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSONALIZATION_PATH "/config/personalization.conf"
#define PERSONALIZATION_THEME_CAP 32
#define PERSONALIZATION_WALLPAPER_CAP 128
#define PERSONALIZATION_FONT_CAP 48

struct personalization {
    char theme[PERSONALIZATION_THEME_CAP];
    char wallpaper[PERSONALIZATION_WALLPAPER_CAP];
    char font[PERSONALIZATION_FONT_CAP];
    uint32_t font_size;
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

void personalization_defaults(struct personalization *p);
bool personalization_load(struct personalization *p);
bool personalization_theme_colors(const char *name,
                                  struct personalization_colors *out);
uint32_t personalization_theme_count(void);
const char *personalization_theme_name_at(uint32_t index);
uint32_t personalization_font_face(const char *font);
uint32_t personalization_font_size_clamped(uint32_t size);
void personalization_apply(const struct personalization *p);
bool personalization_poll(void);
const struct personalization *personalization_current(void);
void personalization_current_colors(struct personalization_colors *out);

#ifdef __cplusplus
}
#endif
