#pragma once
#include <stdint.h>

struct pgf_glyph { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; };
struct pgf_font { uint8_t ascent, height; const struct pgf_glyph *glyphs; const uint8_t *bits; };

enum pgf_font_id {
    PGF_SMALL,
    PGF_BODY,
    PGF_MEDIUM,
    PGF_HEAD,
    PGF_TITLE,
    PGF_HERO,
    PGF_DIGITS,
    PGF_COUNT
};
extern const struct pgf_font pgf_fonts[PGF_COUNT];

enum pgf_icon_id {
    PGI_WIFI,
    PGI_WIFI_OFF,
    PGI_WIFI_STRENGTH_1,
    PGI_WIFI_STRENGTH_2,
    PGI_WIFI_STRENGTH_3,
    PGI_WIFI_STRENGTH_4,
    PGI_WIFI_STRENGTH_OUTLINE,
    PGI_VOLUME_HIGH,
    PGI_VOLUME_MEDIUM,
    PGI_VOLUME_LOW,
    PGI_VOLUME_OFF,
    PGI_MONITOR,
    PGI_PALETTE,
    PGI_HARDDISK,
    PGI_INFORMATION_OUTLINE,
    PGI_CLOCK_OUTLINE,
    PGI_POWER,
    PGI_RESTART,
    PGI_BATTERY,
    PGI_BATTERY_CHARGING,
    PGI_BATTERY_OUTLINE,
    PGI_ACCOUNT_CIRCLE,
    PGI_COG,
    PGI_LOCK,
    PGI_CHECK,
    PGI_CHECK_CIRCLE,
    PGI_CHEVRON_RIGHT,
    PGI_CHEVRON_LEFT,
    PGI_ETHERNET,
    PGI_MEMORY,
    PGI_CPU_64_BIT,
    PGI_SPEAKER,
    PGI_REFRESH,
    PGI_WALLPAPER,
    PGI_FORMAT_FONT,
    PGI_THEME_LIGHT_DARK,
    PGI_EARTH,
    PGI_CALENDAR,
    PGI_TIMER_OUTLINE,
    PGI_LAN,
    PGI_CLOSE,
    PGI_ALERT_CIRCLE,
    PGI_MAGNIFY,
    PGI_USB,
    PGI_SERVER,
    PGI_WEB,
    PGI_BRIGHTNESS_6,
    PGI_MONITOR_SCREENSHOT,
    PGI_TEXTURE_BOX,
    PGI_PLAY_CIRCLE_OUTLINE,
    PGI_MUSIC_NOTE,
    PGI_LOCK_OPEN_VARIANT,
    PGI_ACCESS_POINT_NETWORK,
    PGI_SHIELD_CHECK,
    PGI_FLASH,
    PGI_THERMOMETER,
    PGI_FAN,
    PGI_CHIP,
    PGI_ACCOUNT,
    PGI_KEYBOARD,
    PGI_FOLDER,
    PGI_DOWNLOAD,
    PGI_UPLOAD,
    PGI_COUNT
};
#define PGF_ICON_SIZES 3
extern const uint8_t pgf_icon_size[PGF_ICON_SIZES];
extern const uint8_t pgf_icon_bits[];
extern const uint32_t pgf_icon_off[PGF_ICON_SIZES][PGI_COUNT];
