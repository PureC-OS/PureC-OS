#include "settings/display_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"

#define PAGE_LEFT 198
#define PAGE_TOP 80
#define CARD_H 118
#define CARD_GAP 12

static int32_t g_last_apply_rc=0;
static bool g_have_apply_rc=false;

static char *append_text(char *out, const char *text){
    while(*text) *out++=*text++;
    *out='\0';
    return out;
}

static char *append_u32(char *out, uint32_t value){
    char reverse[12];
    uint32_t count=0;
    do{
        reverse[count++]=(char)('0'+value%10U);
        value/=10U;
    }while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0';
    return out;
}

static void commit(struct display_settings *display){
    g_last_apply_rc=display_apply_live(display);
    g_have_apply_rc=true;
    if(g_last_apply_rc==0){
        int32_t saved=display_save(display);
        if(saved<0) g_last_apply_rc=saved;
    } else {
        /* Revert the pending selection so the UI matches the screen. */
        (void)display_load(display);
    }
}

void display_page_draw(struct pg_window *window,
                       struct display_settings *display,
                       const struct pg_event *event){
    if(!window || !display) return;
    uint32_t width=window->client.width-PAGE_LEFT-22;
    pg_window_text(window,PAGE_LEFT,PAGE_TOP-2,"Display",
                   window->theme.text);
    pg_window_text(window,PAGE_LEFT,PAGE_TOP+12,
                   "Resolution applies immediately, no reboot",
                   window->theme.muted_text);

    char current[48];
    char *out=current;
    struct pc_display_info info;
    if(pc_display_get_info(&info) && info.available){
        out=append_text(out,"Current: ");
        out=append_u32(out,info.width);
        out=append_text(out,"x");
        out=append_u32(out,info.height);
        out=append_text(out,"x");
        out=append_u32(out,info.bpp);
    } else {
        out=append_text(out,"Current: unknown");
    }

    char selected[48];
    out=selected;
    out=append_text(out,"Selected: ");
    out=append_u32(out,display->width);
    out=append_text(out,"x");
    out=append_u32(out,display->height);
    out=append_text(out,"x");
    out=append_u32(out,display->bpp);

    uint32_t card_y=PAGE_TOP+38;
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,card_y,width,CARD_H},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,card_y+14,"Screen resolution",
                   window->theme.text);
    pg_window_text(window,PAGE_LEFT+18,card_y+36,current,
                   window->theme.accent);
    pg_window_text(window,PAGE_LEFT+18,card_y+58,selected,
                   window->theme.text);
    pg_window_text(window,PAGE_LEFT+18,card_y+80,
                   "QEMU / VirtualBox only; bare-metal UEFI keeps boot mode",
                   window->theme.muted_text);
    bool prev=pg_button(window,
        (struct pg_rect){PAGE_LEFT+width-206,card_y+CARD_H-38,92,28},
        "Previous",event);
    bool next=pg_button(window,
        (struct pg_rect){PAGE_LEFT+width-106,card_y+CARD_H-38,92,28},
        "Next",event);
    if(prev || next){
        uint32_t count=display_mode_count();
        uint32_t index=display_mode_index_of(display->width,
                                             display->height);
        index=(prev ? (index+count-1u)%count : (index+1u)%count);
        struct display_mode mode=display_mode_at(index);
        display->width=mode.width;
        display->height=mode.height;
        display->bpp=mode.bpp;
        commit(display);
    }

    char status[112];
    uint32_t status_color=window->theme.muted_text;
    if(!g_have_apply_rc){
        pc_copy(status,
            "Pick a resolution above, it saves to /config/display.ini",
            sizeof(status));
    } else if(g_last_apply_rc==0){
        pc_copy(status,
            "Applied live and saved to /config/display.ini",
            sizeof(status));
        status_color=window->theme.accent;
    } else if(g_last_apply_rc==-2){
        pc_copy(status,
            "Not supported on this machine (no VBE); boot mode kept",
            sizeof(status));
        status_color=window->theme.danger;
    } else {
        const char *reason=pf_strerror(g_last_apply_rc);
        out=status;
        const char *prefix="APPLY FAILED: ";
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
