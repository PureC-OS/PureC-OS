#include "../../libc/include/purec.h"

#define DESKTOP_BG 0x181825U
#define BAR_BG     0x313244U
#define TEXT_FG    0xCDD6F4U
#define ACCENT     0x89B4FAU
#define ICON_BG    0x45475AU
#define BAR_HEIGHT 30U

struct launcher {
    uint32_t x;
    const char *label;
    const char *path;
};

static const struct launcher launchers[] = {
    {20,  "Terminal", "/bin/program/terminal"},
    {140, "Files",    "/bin/program/files"},
    {260, "Settings", "/bin/program/settings"},
};

static bool inside(int32_t px,int32_t py,uint32_t x,uint32_t y,
                   uint32_t w,uint32_t h){
    return px>=(int32_t)x && py>=(int32_t)y
        && px<(int32_t)(x+w) && py<(int32_t)(y+h);
}

static void draw_desktop(const struct pc_display_info *display){
    pc_display_begin_update();
    pc_display_clear(DESKTOP_BG);
    pc_draw_rect(0,0,display->width,BAR_HEIGHT,BAR_BG);
    pc_draw_text(12,9,"PureC OS",ACCENT,BAR_BG);
    for(uint32_t i=0;i<sizeof(launchers)/sizeof(launchers[0]);i++){
        pc_draw_rect(launchers[i].x,58,96,58,ICON_BG);
        pc_draw_text(launchers[i].x+10,82,launchers[i].label,TEXT_FG,ICON_BG);
    }
    pc_display_end_update();
}

static void handle_desktop_click(const struct mouse_state *mouse,
                                 uint32_t wm_result){
    if(wm_result&WM_POINTER_CONSUMED) return;
    for(uint32_t i=0;i<sizeof(launchers)/sizeof(launchers[0]);i++){
        if(inside(mouse->x,mouse->y,launchers[i].x,58,96,58)){
            (void)pc_exec(launchers[i].path);
            return;
        }
    }
}

void _start(void){
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available || !pc_wm_claim()){
        pc_write("window-manager: cannot claim display\n");
        pc_exit(1);
    }
    pc_console_disable();
    draw_desktop(&display);
    pc_write("window-manager: ring 3 event loop started\n");

    uint8_t previous_buttons=0;
    for(;;){
        uint32_t excluded_pid=0;
        if(pc_wm_next_redraw(&excluded_pid)>0){
            (void)pc_display_get_info(&display);
            draw_desktop(&display);
            pc_wm_complete_redraw(excluded_pid);
        }

        struct mouse_state mouse;
        if(pc_mouse_get(&mouse)){
            bool pressed=(mouse.buttons&1U) && !(previous_buttons&1U);
            if(pressed){
                struct wm_pointer_request request={
                    .x=mouse.x,.y=mouse.y,.pressed=1
                };
                uint32_t result=pc_wm_pointer(&request);
                if(result&WM_POINTER_FOCUS_CHANGED){
                    draw_desktop(&display);
                    pc_wm_complete_redraw(0);
                }
                handle_desktop_click(&mouse,result);
            }
            previous_buttons=mouse.buttons;
        }
        (void)pc_syscall(SYS_AUDIO_UPDATE,0,0,0);
        pc_sleep(2);
    }
}
