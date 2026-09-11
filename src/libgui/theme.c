#include "include/puregui.h"
#include "../libc/include/purec.h"

const char *pg_version(void){ return PG_VERSION; }

struct pg_theme pg_theme_default(void){
    struct pg_theme theme={
        .desktop=0x181825,
        .window=0x1E1E2E,
        .titlebar=0x313244,
        .border=0x45475A,
        .text=0xCDD6F4,
        .muted_text=0x9399B2,
        .accent=0x89B4FA,
        .danger=0xF38BA8,
        .shadow=0x11111B
    };
    return theme;
}

static const struct {
    const char *name;
    struct pg_theme theme;
} theme_presets[]={
    {"catppuccin-dark", {0x181825,0x1E1E2E,0x313244,0x45475A,0xCDD6F4,0x9399B2,0x89B4FA,0xF38BA8,0x11111B}},
    {"nord",            {0x2E3440,0x3B4252,0x434C5E,0x4C566A,0xECEFF4,0x9AA0B0,0x88C0D0,0xBF616A,0x1A1C24}},
    {"dracula",         {0x282A36,0x2E3247,0x44475A,0x6272A4,0xF8F8F2,0x9AA0B2,0xBD93F9,0xFF5555,0x1A1B26}},
    {"light",           {0xE6E9EF,0xEFF1F5,0xDCE0E8,0xBCC0CC,0x4C4F69,0x8C8FA1,0x1E66F5,0xD20F39,0x9CA0B0}},
    {"tokyo-night",     {0x1A1B26,0x24283B,0x292E42,0x565F89,0xC0CAF5,0x9AA5CE,0x7AA2F7,0xF7768E,0x101014}},
};

uint32_t pg_theme_count(void){
    return sizeof(theme_presets)/sizeof(theme_presets[0]);
}

const char *pg_theme_name_at(uint32_t index){
    if(index>=pg_theme_count()) return theme_presets[0].name;
    return theme_presets[index].name;
}

struct pg_theme pg_theme_by_name(const char *name){
    if(name){
        for(uint32_t i=0;i<pg_theme_count();i++){
            if(pc_strcmp(name,theme_presets[i].name)==0)
                return theme_presets[i].theme;
        }
    }
    return pg_theme_default();
}
