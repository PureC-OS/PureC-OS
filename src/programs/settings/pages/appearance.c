#include "settings/appearance_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"

#define PAGE_LEFT 198
#define PAGE_TOP 80
#define CARD_H 96
#define CARD_GAP 12
static int32_t g_last_save_rc=0;
static bool g_have_save_rc=false;

static char *append_u32(char *out, uint32_t value){
    char reverse[12]; uint32_t count=0;
    do{ reverse[count++]=(char)('0'+value%10U); value/=10U; }
    while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0'; return out;
}

static void commit(struct personalization_settings *appearance){
    g_last_save_rc=appearance_save(appearance);
    g_have_save_rc=true;
}

static uint32_t theme_index_of(const char *name){
    for(uint32_t i=0;i<pg_theme_count();i++){
        if(pc_strcmp(name,pg_theme_name_at(i))==0) return i;
    }
    return 0;
}

static uint32_t wallpaper_index_of(const char *path){
    for(uint32_t i=0;i<appearance_wallpaper_count();i++){
        if(pc_strcmp(path,appearance_wallpaper_at(i))==0) return i;
    }
    return 0;
}

static uint32_t font_index_of(const char *name){
    for(uint32_t i=0;i<appearance_font_count();i++){
        if(pc_strcmp(name,appearance_font_name_at(i))==0) return i;
    }
    return 0;
}

static void draw_cycle_card(struct pg_window *window,
                            struct personalization_settings *appearance,
                            const struct pg_event *event,
                            uint32_t card_y, uint32_t width,
                            const char *title, const char *value,
                            const char *hint,
                            bool *prev, bool *next){
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,card_y,width,CARD_H},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,card_y+14,title,window->theme.text);
    pg_window_text(window,PAGE_LEFT+18,card_y+36,
                   value[0] ? value : "(solid color)",window->theme.accent);
    pg_window_text(window,PAGE_LEFT+18,card_y+58,hint,
                   window->theme.muted_text);
    *prev=pg_button(window,
        (struct pg_rect){PAGE_LEFT+width-206,card_y+CARD_H-38,92,28},
        "Previous",event);
    *next=pg_button(window,
        (struct pg_rect){PAGE_LEFT+width-106,card_y+CARD_H-38,92,28},
        "Next",event);
}

void appearance_page_draw(struct pg_window *window,
                          struct personalization_settings *appearance,
                          const struct pg_event *event){
    if(!window || !appearance) return;
    uint32_t width=window->client.width-PAGE_LEFT-22;
    pg_window_text(window,PAGE_LEFT,PAGE_TOP-2,"Appearance",
                   window->theme.text);
    pg_window_text(window,PAGE_LEFT,PAGE_TOP+12,
                   "Themes, wallpaper, fonts. Applied live from",
                   window->theme.muted_text);

    bool prev=false, next=false;
    uint32_t card_y=PAGE_TOP+38;

    /* Theme */
    draw_cycle_card(window,appearance,event,card_y,width,
        "Color theme",appearance->theme,
        "Window + desktop palette", &prev, &next);
    if(prev || next){
        uint32_t count=pg_theme_count();
        uint32_t index=theme_index_of(appearance->theme);
        index=(prev ? (index+count-1u)%count : (index+1u)%count);
        pc_copy(appearance->theme,pg_theme_name_at(index),
                sizeof(appearance->theme));
        commit(appearance);
    }
    card_y+=CARD_H+CARD_GAP;

    /* Wallpaper */
    const char *wallpaper_label=appearance->wallpaper[0]
        ? appearance->wallpaper : "(solid color)";
    draw_cycle_card(window,appearance,event,card_y,width,
        "Wallpaper",wallpaper_label,
        "BMP / PNG, stretched. Own: /config/wp.bmp, /config/wp.png",
        &prev,&next);
    if(prev || next){
        uint32_t count=appearance_wallpaper_count();
        uint32_t index=wallpaper_index_of(appearance->wallpaper);
        index=(prev ? (index+count-1u)%count : (index+1u)%count);
        pc_copy(appearance->wallpaper,appearance_wallpaper_at(index),
                sizeof(appearance->wallpaper));
        commit(appearance);
    }
    card_y+=CARD_H+CARD_GAP;

    /* Font face */
    draw_cycle_card(window,appearance,event,card_y,width,
        "Font",appearance->font,
        "Bitmap faces. TTF files in /config/fonts are reserved",
        &prev,&next);
    if(prev || next){
        uint32_t count=appearance_font_count();
        uint32_t index=font_index_of(appearance->font);
        index=(prev ? (index+count-1u)%count : (index+1u)%count);
        pc_copy(appearance->font,appearance_font_name_at(index),
                sizeof(appearance->font));
        commit(appearance);
    }
    card_y+=CARD_H+CARD_GAP;

    /* Font size */
    char size_text[16];
    char *end=append_u32(size_text,appearance->font_size);
    *end++=' '; *end++='p'; *end++='x'; *end='\0';
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,card_y,width,CARD_H},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,card_y+14,"Font size",
                   window->theme.text);
    pg_window_text_sized(window,PAGE_LEFT+18,card_y+34,
                         "Ag PureC OS",window->theme.accent,
                         appearance->font_size);
    pg_window_text(window,PAGE_LEFT+18,card_y+58,
                   "Desktop labels use up to 12 px to fit tiles",
                   window->theme.muted_text);
    pg_window_text(window,PAGE_LEFT+width-160,card_y+36,size_text,
                   window->theme.text);
    if(pg_button(window,
            (struct pg_rect){PAGE_LEFT+width-206,card_y+CARD_H-38,92,28},
            "Smaller",event)
        && appearance->font_size>APPEAR_MIN_FONT_SIZE){
        appearance->font_size--;
        commit(appearance);
    }
    if(pg_button(window,
            (struct pg_rect){PAGE_LEFT+width-106,card_y+CARD_H-38,92,28},
            "Bigger",event)
        && appearance->font_size<APPEAR_MAX_FONT_SIZE){
        appearance->font_size++;
        commit(appearance);
    }

    /* Save status line: never fail silently (live CD = read-only). */
    char status[96];
    uint32_t status_color=window->theme.muted_text;
    if(!g_have_save_rc){
        pc_copy(status,"Pick a setting above, it saves to /config/appear.ini",
                sizeof(status));
    } else if(g_last_save_rc>=0){
        pc_copy(status,"Saved to /config/appear.ini, desktop applies it live",
                sizeof(status));
        status_color=window->theme.accent;
    } else {
        const char *reason=pf_strerror(g_last_save_rc);
        char *out=status;
        const char *prefix="SAVE FAILED: ";
        while(*prefix) *out++=*prefix++;
        while(*reason && (uint32_t)(out-status)<sizeof(status)-32)
            *out++=*reason++;
        const char *suffix=" (read-only disk? install OS?)";
        while(*suffix && (uint32_t)(out-status)<sizeof(status)-1)
            *out++=*suffix++;
        *out='\0';
        status_color=window->theme.danger;
    }
    pg_window_text(window,PAGE_LEFT,card_y+CARD_H+10,status,status_color);
}
