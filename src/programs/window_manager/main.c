#include "../../libc/include/purec.h"

#define DESKTOP_BG 0x181825U
#define WINDOW_BG  0x1E1E2EU
#define BAR_BG     0x313244U
#define BORDER     0x45475AU
#define TEXT_FG    0xCDD6F4U
#define MUTED_FG   0x9399B2U
#define ACCENT     0x89B4FAU
#define DANGER     0xF38BA8U
#define TOP_H      28U
#define BOTTOM_H   30U
#define ICON_W     58U
#define ICON_H     72U
#define TASK_W     128U

struct desktop_entry {
    const char *name;
    const char *symbol;
    const char *path;
    uint32_t color;
    uint32_t x;
    uint32_t y;
};

static struct desktop_entry entries[] = {
    {"Files",    "FL",  "/bin/program/files",    0xF9E2AFU, 40,  48},
    {"HTOP",     "HT",  "/bin/program/monitor",  0x89B4FAU, 112, 48},
    {"Terminal", ">_",  "/bin/program/terminal", 0x585B70U, 184, 48},
    {"Clock",    "12",  0,                       0x89DCEBU, 256, 48},
    {"Calc",     "+",   0,                       0xA6E3A1U, 328, 48},
    {"Calendar", "28",  0,                       0xF9E2AFU, 400, 48},
    {"Settings", "{}",  "/bin/program/settings", 0x94E2D5U, 40, 130},
    {"Install",  "OS",  "/bin/installer",        0xCBA6F7U, 112,130},
    {"Disks",    "HD",  "/bin/program/disks",    0xF9E2AFU, 184,130},
    {"Devices",  "DV",  "/bin/program/devmgr",   0x89DCEBU, 256,130},
    {"Tetris",   "[]",  "/bin/program/tetris",   0xF38BA8U, 328,130},
    {"Logs",     "LOG", "/bin/program/logview",  0x89B4FAU, 400,130},
    {"HexEdit",  "HX",  "/bin/program/hexedit",  0xF5C2E7U, 472,130},
};

static bool apps_menu;
static bool power_menu;
static bool audio_popup;
static uint8_t previous_buttons;
static int32_t dragged_icon=-1;
static int32_t drag_dx;
static int32_t drag_dy;
static bool drag_moved;

static bool inside(int32_t px,int32_t py,uint32_t x,uint32_t y,
                   uint32_t w,uint32_t h){
    return px>=(int32_t)x && py>=(int32_t)y
        && px<(int32_t)(x+w) && py<(int32_t)(y+h);
}

static void copy_text(char *dst,uint32_t cap,const char *src){
    if(!cap) return;
    uint32_t i=0;
    while(src && src[i] && i+1<cap){ dst[i]=src[i]; i++; }
    dst[i]='\0';
}

static const char *process_name(uint32_t pid,char out[24]){
    struct process_monitor_info list[64];
    int32_t count=pc_process_list(list,64);
    if(count>64) count=64;
    for(int32_t i=0;i<count;i++){
        if(list[i].pid==pid){ copy_text(out,24,list[i].name); return out; }
    }
    copy_text(out,24,"Process");
    return out;
}

static void draw_icon(const struct desktop_entry *entry){
    pc_draw_rect(entry->x,entry->y,ICON_W,50,BAR_BG);
    pc_draw_rect(entry->x+8,entry->y+7,42,34,entry->color);
    pc_draw_text(entry->x+17,entry->y+17,entry->symbol,DESKTOP_BG,entry->color);
    pc_draw_text(entry->x+3,entry->y+55,entry->name,TEXT_FG,DESKTOP_BG);
}

static void draw_audio(uint32_t width){
    uint32_t x=width>138 ? width-138 : 0;
    int32_t volume=pc_audio_get_volume();
    if(volume<0) volume=0;
    char label[12]="Vol 000%";
    label[4]=(char)('0'+(volume/100)%10);
    label[5]=(char)('0'+(volume/10)%10);
    label[6]=(char)('0'+volume%10);
    pc_draw_rect(x,3,92,22,audio_popup ? 0x585B70U : BORDER);
    pc_draw_text(x+8,9,label,TEXT_FG,audio_popup ? 0x585B70U : BORDER);
    if(!audio_popup) return;
    uint32_t px=width>206 ? width-206 : 0;
    pc_draw_rect(px,28,198,116,BORDER);
    pc_draw_rect(px+2,30,194,112,WINDOW_BG);
    pc_draw_text(px+12,42,"Sound",TEXT_FG,WINDOW_BG);
    pc_draw_rect(px+18,70,162,10,0x585B70U);
    pc_draw_rect(px+18,70,(uint32_t)(162*volume/100),10,ACCENT);
    pc_draw_rect(px+12,100,80,26,BORDER);
    pc_draw_text(px+24,109,"Mute",TEXT_FG,BORDER);
    pc_draw_rect(px+104,100,80,26,ACCENT);
    pc_draw_text(px+118,109,"Test",DESKTOP_BG,ACCENT);
}

static void draw_power(uint32_t width){
    uint32_t x=width>38 ? width-38 : 0;
    pc_draw_rect(x,3,30,22,power_menu ? DANGER : BORDER);
    pc_draw_text(x+7,9,"PWR",TEXT_FG,power_menu ? DANGER : BORDER);
    if(!power_menu) return;
    uint32_t mx=width>158 ? width-158 : 0;
    pc_draw_rect(mx,28,150,62,BORDER);
    pc_draw_rect(mx+2,30,146,28,WINDOW_BG);
    pc_draw_rect(mx+2,60,146,28,WINDOW_BG);
    pc_draw_text(mx+12,39,"Restart",TEXT_FG,WINDOW_BG);
    pc_draw_text(mx+12,69,"Power off",DANGER,WINDOW_BG);
}

static uint32_t window_list(struct wm_window_info windows[64]){
    int32_t count=pc_wm_window_list(windows,64);
    if(count<0) return 0;
    return count>64 ? 64U : (uint32_t)count;
}

static void draw_apps_menu(uint32_t height){
    if(!apps_menu) return;
    uint32_t rows=(uint32_t)(sizeof(entries)/sizeof(entries[0]));
    if(rows>12) rows=12;
    uint32_t h=rows*26+62;
    uint32_t y=height>BOTTOM_H+h+4 ? height-BOTTOM_H-h-4 : TOP_H;
    pc_draw_rect(4,y,248,h,BORDER);
    pc_draw_rect(5,y+1,246,h-2,WINDOW_BG);
    for(uint32_t i=0;i<rows;i++){
        uint32_t ry=y+5+i*26;
        pc_draw_rect(9,ry,238,24,BAR_BG);
        pc_draw_rect(15,ry+5,16,14,entries[i].color);
        pc_draw_text(39,ry+7,entries[i].name,TEXT_FG,BAR_BG);
    }
    uint32_t ry=y+5+rows*26;
    pc_draw_rect(9,ry,238,24,BAR_BG);
    pc_draw_text(39,ry+7,"Restart",TEXT_FG,BAR_BG);
    pc_draw_rect(9,ry+26,238,24,BAR_BG);
    pc_draw_text(39,ry+33,"Power off",DANGER,BAR_BG);
}

static void draw_bottom(uint32_t width,uint32_t height){
    uint32_t y=height>BOTTOM_H ? height-BOTTOM_H : 0;
    pc_draw_rect(0,y,width,1,BORDER);
    pc_draw_rect(0,y+1,width,BOTTOM_H-1,BAR_BG);
    pc_draw_rect(4,y+4,86,22,apps_menu ? ACCENT : BORDER);
    pc_draw_rect(8,y+9,12,12,ACCENT);
    pc_draw_text(10,y+11,"P",DESKTOP_BG,ACCENT);
    pc_draw_text(26,y+10,apps_menu ? "Apps v" : "Apps >",
                 apps_menu ? DESKTOP_BG : TEXT_FG,apps_menu ? ACCENT : BORDER);
    static const char *quick[]={"FL","T>","{}","DV"};
    uint32_t qx=98;
    for(uint32_t i=0;i<4;i++,qx+=32){
        pc_draw_rect(qx,y+4,28,22,WINDOW_BG);
        pc_draw_text(qx+6,y+10,quick[i],TEXT_FG,WINDOW_BG);
    }
    struct wm_window_info windows[64];
    uint32_t count=window_list(windows);
    uint32_t focused=pc_wm_focused();
    uint32_t task_x=232;
    uint32_t room=width>task_x+140 ? width-task_x-140 : 0;
    uint32_t max=room/(TASK_W+4);
    if(count>max) count=max;
    for(uint32_t i=0;i<count;i++){
        uint32_t x=task_x+i*(TASK_W+4);
        uint32_t bg=windows[i].pid==focused ? ACCENT : WINDOW_BG;
        char name[24];
        pc_draw_rect(x,y+4,TASK_W,22,bg);
        pc_draw_rect(x+5,y+12,6,6,windows[i].pid==focused ? DESKTOP_BG : ACCENT);
        pc_draw_text(x+15,y+10,process_name(windows[i].pid,name),
                     windows[i].pid==focused ? DESKTOP_BG : TEXT_FG,bg);
    }
    if(!count) pc_draw_text(task_x+4,y+10,"No windows",MUTED_FG,BAR_BG);
    struct cpu_monitor_info cpu;
    uint64_t mins=pc_cpu_info(&cpu) ? cpu.uptime_ms/60000U : 0;
    uint32_t hours=(uint32_t)((mins/60U)%24U);
    char clock[6]={(char)('0'+hours/10U),(char)('0'+hours%10U),':',
                   (char)('0'+(mins%60U)/10U),(char)('0'+mins%10U),'\0'};
    pc_draw_rect(width>128 ? width-128 : 0,y+3,124,24,WINDOW_BG);
    pc_draw_text(width>120 ? width-120 : 0,y+10,clock,TEXT_FG,WINDOW_BG);
    draw_apps_menu(height);
}

static void draw_desktop(const struct pc_display_info *display){
    pc_display_begin_update();
    pc_display_clear(DESKTOP_BG);
    pc_draw_rect(0,0,display->width,TOP_H,BAR_BG);
    pc_draw_text(12,8,"PureC OS",ACCENT,BAR_BG);
    for(uint32_t i=0;i<sizeof(entries)/sizeof(entries[0]);i++) draw_icon(&entries[i]);
    draw_audio(display->width);
    draw_power(display->width);
    draw_bottom(display->width,display->height);
    pc_display_end_update();
}

static void redraw(struct pc_display_info *display,uint32_t excluded_pid){
    (void)pc_display_get_info(display);
    draw_desktop(display);
    pc_wm_complete_redraw(excluded_pid);
}

static void launch(const char *path){ if(path) (void)pc_exec(path); }

static bool handle_bottom(const struct mouse_state *m,
                          const struct pc_display_info *d){
    uint32_t y=d->height>BOTTOM_H ? d->height-BOTTOM_H : 0;
    if(apps_menu){
        uint32_t rows=sizeof(entries)/sizeof(entries[0]);
        if(rows>12) rows=12;
        uint32_t h=rows*26+62;
        uint32_t my=d->height>BOTTOM_H+h+4 ? d->height-BOTTOM_H-h-4 : TOP_H;
        if(inside(m->x,m->y,4,my,248,h)){
            uint32_t row=(uint32_t)(m->y-(int32_t)(my+5))/26;
            if(row<rows) launch(entries[row].path);
            else if(row==rows) pc_reboot();
            else if(row==rows+1) pc_shutdown();
            apps_menu=false;
            return true;
        }
    }
    if(!inside(m->x,m->y,0,y,d->width,BOTTOM_H)) return false;
    if(inside(m->x,m->y,4,y+4,86,22)){ apps_menu=!apps_menu; return true; }
    static const char *quick[]={"/bin/program/files","/bin/program/terminal",
                                "/bin/program/settings","/bin/program/devmgr"};
    for(uint32_t i=0;i<4;i++){
        if(inside(m->x,m->y,98+i*32,y+4,28,22)){ launch(quick[i]); return true; }
    }
    struct wm_window_info windows[64];
    uint32_t count=window_list(windows);
    uint32_t room=d->width>372 ? d->width-372 : 0;
    uint32_t max=room/(TASK_W+4);
    if(count>max) count=max;
    for(uint32_t i=0;i<count;i++){
        if(inside(m->x,m->y,232+i*(TASK_W+4),y+4,TASK_W,22)){
            (void)pc_wm_focus(windows[i].pid);
            return true;
        }
    }
    return true;
}

static bool handle_top(const struct mouse_state *m,
                       const struct pc_display_info *d){
    uint32_t audio_x=d->width>138 ? d->width-138 : 0;
    uint32_t power_x=d->width>38 ? d->width-38 : 0;
    if(inside(m->x,m->y,audio_x,3,92,22)){
        audio_popup=!audio_popup; power_menu=false; return true;
    }
    if(inside(m->x,m->y,power_x,3,30,22)){
        power_menu=!power_menu; audio_popup=false; return true;
    }
    if(power_menu){
        uint32_t x=d->width>158 ? d->width-158 : 0;
        if(inside(m->x,m->y,x,28,150,30)){ pc_reboot(); return true; }
        if(inside(m->x,m->y,x,58,150,32)){ pc_shutdown(); return true; }
        power_menu=false;
    }
    if(audio_popup){
        uint32_t x=d->width>206 ? d->width-206 : 0;
        if(inside(m->x,m->y,x+12,100,80,26)){
            pc_audio_set_muted(!pc_audio_is_muted()); return true;
        }
        if(inside(m->x,m->y,x+104,100,80,26)){
            pc_audio_play_test(); return true;
        }
        audio_popup=false;
    }
    return false;
}

static void reap_children(void){
    struct process_monitor_info list[64];
    int32_t count=pc_process_list(list,64);
    if(count>64) count=64;
    uint32_t self=(uint32_t)pc_getpid();
    for(int32_t i=0;i<count;i++){
        if(list[i].parent_pid==self && list[i].state==PROCESS_MONITOR_STATE_EXITED){
            int32_t status;
            (void)pc_wait((int32_t)list[i].pid,&status,true);
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
    pc_write("window-manager: full ring-3 desktop started\n");

    uint32_t reap_tick=0;
    for(;;){
        uint32_t excluded_pid=0;
        if(pc_wm_next_redraw(&excluded_pid)>0) redraw(&display,excluded_pid);
        struct mouse_state mouse;
        if(pc_mouse_get(&mouse)){
            bool pressed=(mouse.buttons&1U) && !(previous_buttons&1U);
            bool released=!(mouse.buttons&1U) && (previous_buttons&1U);
            bool changed=false;
            if(pressed){
                if(handle_bottom(&mouse,&display) || handle_top(&mouse,&display)) changed=true;
                else{
                    for(uint32_t i=0;i<sizeof(entries)/sizeof(entries[0]);i++){
                        if(inside(mouse.x,mouse.y,entries[i].x,entries[i].y,ICON_W,ICON_H)){
                            dragged_icon=(int32_t)i;
                            drag_dx=mouse.x-(int32_t)entries[i].x;
                            drag_dy=mouse.y-(int32_t)entries[i].y;
                            drag_moved=false;
                            break;
                        }
                    }
                    if(dragged_icon<0){
                        struct wm_pointer_request request={.x=mouse.x,.y=mouse.y,.pressed=1};
                        uint32_t result=pc_wm_pointer(&request);
                        changed=(result&WM_POINTER_FOCUS_CHANGED)!=0;
                    }
                }
            }
            if(dragged_icon>=0 && (mouse.buttons&1U)){
                struct desktop_entry *e=&entries[dragged_icon];
                int32_t x=mouse.x-drag_dx;
                int32_t y=mouse.y-drag_dy;
                if(x<0) x=0;
                if(y<(int32_t)TOP_H) y=TOP_H;
                if(x>(int32_t)(display.width-ICON_W)) x=display.width-ICON_W;
                if(y>(int32_t)(display.height-BOTTOM_H-ICON_H)) y=display.height-BOTTOM_H-ICON_H;
                if(e->x!=(uint32_t)x || e->y!=(uint32_t)y){
                    e->x=(uint32_t)x; e->y=(uint32_t)y;
                    drag_moved=true; changed=true;
                }
            }
            if(released && dragged_icon>=0){
                int32_t icon=dragged_icon;
                dragged_icon=-1;
                if(!drag_moved) launch(entries[icon].path);
                changed=true;
            }
            previous_buttons=mouse.buttons;
            if(changed) redraw(&display,0);
        }
        (void)pc_syscall(SYS_AUDIO_UPDATE,0,0,0);
        if(++reap_tick>=250){ reap_tick=0; reap_children(); }
        pc_sleep(2);
    }
}
