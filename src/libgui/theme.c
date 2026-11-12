#include "include/puregui.h"
#include "../libc/include/purec.h"

const char *pg_version(void) { return PG_VERSION; }

struct pg_theme pg_theme_default(void)
{
    struct pg_theme theme = {
        .desktop = 0x181825,
        .window = 0x1E1E2E,
        .titlebar = 0x313244,
        .border = 0x45475A,
        .text = 0xCDD6F4,
        .muted_text = 0x9399B2,
        .accent = 0x89B4FA,
        .danger = 0xF38BA8,
        .shadow = 0x11111B,
        .surface = 0x2A2B3D,
        .surface_alt = 0x45475A,
        .success = 0xA6E3A1,
        .warning = 0xF9E2AF,
        .info = 0x89DCEB,
        .on_accent = 0x11111B};
    return theme;
}

static const struct {
    const char *name;
    struct pg_theme theme;
} theme_presets[]={
    {"catppuccin-dark", {0x181825,0x1E1E2E,0x313244,0x45475A,0xCDD6F4,0x9399B2,0x89B4FA,0xF38BA8,0x11111B,0x2A2B3D,0x45475A,0xA6E3A1,0xF9E2AF,0x89DCEB,0x11111B}},
    {"nord",            {0x2E3440,0x3B4252,0x434C5E,0x4C566A,0xECEFF4,0x9AA0B0,0x88C0D0,0xBF616A,0x1A1C24,0x434C5E,0x4C566A,0xA3BE8C,0xEBCB8B,0x81A1C1,0x2E3440}},
    {"dracula",         {0x282A36,0x2E3247,0x44475A,0x6272A4,0xF8F8F2,0x9AA0B2,0xBD93F9,0xFF5555,0x1A1B26,0x383A4F,0x44475A,0x50FA7B,0xF1FA8C,0x8BE9FD,0x282A36}},
    {"light",           {0xE6E9EF,0xEFF1F5,0xDCE0E8,0xBCC0CC,0x4C4F69,0x8C8FA1,0x1E66F5,0xD20F39,0x9CA0B0,0xFFFFFF,0xE6E9EF,0x40A02B,0xDF8E1D,0x04A5E5,0xFFFFFF}},
    {"tokyo-night",     {0x1A1B26,0x24283B,0x292E42,0x565F89,0xC0CAF5,0x9AA5CE,0x7AA2F7,0xF7768E,0x101014,0x2F354F,0x3B4261,0x9ECE6A,0xE0AF68,0x7DCFFF,0x1A1B26}},
    {"gruvbox-dark",    {0x1D2021,0x282828,0x3C3836,0x504945,0xEBDBB2,0xA89984,0xFABD2F,0xFB4934,0x0F0F0F,0x32302F,0x504945,0xB8BB26,0xFE8019,0x83A598,0x282828}},
    {"one-dark",        {0x21252B,0x282C34,0x2C313A,0x3E4451,0xABB2BF,0x7F848E,0x61AFEF,0xE06C75,0x15171C,0x2F343F,0x3E4451,0x98C379,0xE5C07B,0x56B6C2,0x21252B}},
    {"rose-pine",       {0x191724,0x1F1D2E,0x26233A,0x403D52,0xE0DEF4,0x908CAA,0xC4A7E7,0xEB6F92,0x0F0E17,0x2A273F,0x403D52,0x9CCFD8,0xF6C177,0xEBBCBA,0x191724}},
    {"everforest",      {0x232A2E,0x2D353B,0x343F44,0x475258,0xD3C6AA,0x9DA9A0,0xA7C080,0xE67E80,0x1A1F22,0x3D484D,0x475258,0xA7C080,0xDBBC7F,0x7FBBB3,0x2D353B}},
    {"solarized-light", {0xEEE8D5,0xFDF6E3,0xEEE8D5,0xD3CBB7,0x586E75,0x93A1A1,0x268BD2,0xDC322F,0xB8B0A0,0xFFFDF6,0xEEE8D5,0x859900,0xB58900,0x2AA198,0xFDF6E3}},
    {"github-dark",     {0x010409,0x0D1117,0x161B22,0x30363D,0xE6EDF3,0x8B949E,0x2F81F7,0xF85149,0x000000,0x161B22,0x21262D,0x3FB950,0xD29922,0x58A6FF,0xFFFFFF}},
};

uint32_t pg_theme_count(void)
{
    return sizeof(theme_presets) / sizeof(theme_presets[0]);
}

const char *pg_theme_name_at(uint32_t index)
{
    if (index >= pg_theme_count())
        return theme_presets[0].name;
    return theme_presets[index].name;
}

struct pg_theme pg_theme_by_name(const char *name)
{
    if (name)
    {
        for (uint32_t i = 0; i < pg_theme_count(); i++)
        {
            if (pc_strcmp(name, theme_presets[i].name) == 0)
                return theme_presets[i].theme;
        }
    }
    return pg_theme_default();
}

struct pg_theme pg_theme_system(void)
{
    static char name[40];
    static bool loaded;
    static uint64_t last;
    struct time_info t;
    uint64_t now = pc_time_get(&t) < 0 ? 0 : t.uptime_ms;
    if (!loaded || now - last >= 1000) {
        last = now;
        loaded = true;
        char cfg[192];
        pc_config_path("appear.ini", cfg, sizeof(cfg), false);
        int32_t fd = pc_file_open(cfg);
        if (fd >= 0) {
            char buf[512];
            int32_t n = pc_file_read(fd, buf, sizeof(buf) - 1);
            (void)pc_file_close(fd);
            if (n > 0) {
                buf[n] = 0;
                for (int32_t i = 0; i < n; i++) {
                    if ((i == 0 || buf[i - 1] == '\n') && buf[i] == 't' && buf[i + 1] == 'h' &&
                        buf[i + 2] == 'e' && buf[i + 3] == 'm' && buf[i + 4] == 'e' && buf[i + 5] == '=') {
                        int k = 0;
                        i += 6;
                        while (i < n && buf[i] != '\n' && buf[i] != '\r' && k < (int)sizeof(name) - 1)
                            name[k++] = buf[i++];
                        name[k] = 0;
                        break;
                    }
                }
            }
        }
    }
    return name[0] ? pg_theme_by_name(name) : pg_theme_default();
}
