#include "userspace.h"
#include "personalization.h"
#include "wallpaper.h"
#include "apps/desktop_apps.h"
#include "apps/desktop_entries.h"
#include "apps/audio_panel.h"
#include "window_manager.h"
#include "syscall.h"
#include "audio.h"
#include "display.h"
#include "../drivers/display/gop.h"
#include "../drivers/input/keyboard.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/mouse/usb_mouse.h"
#include "../drivers/interrupts/timer.h"
#include "../drivers/storage/block_device.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/diagnostics/boot_diag.h"
#include "../kernel/diagnostics/panic.h"
#include "../kernel/process/process.h"
#include "../kernel/process/scheduler.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stdbool.h>

#define DESKTOP_BG       0x181825
#define TOPBAR_BG        0x313244
#define TOPBAR_FG        0xCDD6F4
#define TOPBAR_MUTED     0x9399B2
#define TOPBAR_ACCENT    0x89B4FA
#define TOPBAR_HEIGHT    28
#define MOUSE_DEBUG_X    12
#define MOUSE_DEBUG_Y    38
#define MOUSE_DEBUG_W    380
#define MOUSE_DEBUG_H    84
#define ICON_Y           48
#define ICON_W           58
#define ICON_H           72
#define PERSISTENT_LOG_CHUNK (64 * 1024)
#define PERSISTENT_LOG_MAX_BYTES 0xFFFFFFFFULL
#define PERSISTENT_LOG_PATH "/kernel.log"

static void desktop_theme(struct personalization_colors *out){
    personalization_current_colors(out);
}

static uint32_t desktop_label_size(void){
    const struct personalization *p=personalization_current();
    uint32_t size=personalization_font_size_clamped(p ? p->font_size : 12);
    return size>12 ? 12 : size;
}

static uint32_t desktop_width;
static uint32_t desktop_height;
static bool installer_icon_visible=true;
static bool external_program_active;
static uint32_t explorer_icon_x=348;
static uint32_t htop_icon_x=420;
static uint32_t terminal_icon_x=500;
static uint32_t explorer_icon_y=ICON_Y;
static uint32_t htop_icon_y=ICON_Y;
static uint32_t terminal_icon_y=ICON_Y;
static uint32_t clock_icon_x=40,calculator_icon_x=112,calendar_icon_x=184;
static uint32_t clock_icon_y=ICON_Y,calculator_icon_y=ICON_Y,calendar_icon_y=ICON_Y;
static uint32_t settings_icon_x=256,settings_icon_y=ICON_Y;
static uint32_t installer_icon_x=328,installer_icon_y=ICON_Y;
static uint32_t disks_icon_x=400,disks_icon_y=130;
static uint32_t tetris_icon_x=472,tetris_icon_y=130;
static uint32_t logview_icon_x=544,logview_icon_y=130;
static uint32_t hexedit_icon_x=40,hexedit_icon_y=210;
static uint32_t desktop_redraw_requested;
static uint32_t desktop_redraw_completed;
static uint32_t desktop_redraw_requester;
static bool desktop_redraw_busy;
static int32_t detached_programs[WINDOW_MANAGER_CAPACITY];
static uint8_t previous_mouse_buttons;
static bool power_menu_visible;
static int8_t dragged_icon=-1;
static int32_t icon_drag_offset_x;
static int32_t icon_drag_offset_y;
static bool icon_drag_moved;
static bool icon_layout_ready;
static char persistent_log_buffer[PERSISTENT_LOG_CHUNK];
static void redraw_scene(void);
static void redraw_managed_scene(uint32_t excluded_pid);
static int32_t userspace_run_detached(const char *path,
                                      const char *arguments);
static int32_t userspace_run_program_with_args(const char *path,
                                               const char *arguments);

static bool external_program_has_input_focus(void){
    return __atomic_load_n(&external_program_active,__ATOMIC_ACQUIRE);
}

static void set_external_program_input_focus(bool active){
    __atomic_store_n(&external_program_active,active,__ATOMIC_RELEASE);
}

static bool point_inside(int32_t x, int32_t y, uint32_t left, uint32_t top,
                         uint32_t width, uint32_t height){
    return x>=(int32_t)left && y>=(int32_t)top
        && x<(int32_t)(left+width) && y<(int32_t)(top+height);
}

static void draw_htop_icon(void){
    struct personalization_colors th;
    desktop_theme(&th);
    uint32_t y=htop_icon_y;
    display_draw_rect(htop_icon_x,y,ICON_W,50,th.titlebar);
    display_draw_rect(htop_icon_x+7,y+8,44,30,th.window);
    display_draw_line(htop_icon_x+11,y+31,htop_icon_x+19,y+21,th.accent);
    display_draw_line(htop_icon_x+19,y+21,htop_icon_x+28,y+27,th.accent);
    display_draw_line(htop_icon_x+28,y+27,htop_icon_x+39,y+14,0xA6E3A1);
    display_draw_line(htop_icon_x+39,y+14,htop_icon_x+47,y+19,0xA6E3A1);
    display_draw_text_sized_at(htop_icon_x+9,y+55,"HTOP",th.text,th.desktop,
                               desktop_label_size());
}

static void draw_explorer_icon(void){
    struct personalization_colors th;
    desktop_theme(&th);
    uint32_t y=explorer_icon_y;
    display_draw_rect(explorer_icon_x,y,ICON_W,50,th.titlebar);
    display_draw_rect(explorer_icon_x+7,y+13,44,27,0xF9E2AF);
    display_draw_rect(explorer_icon_x+10,y+9,20,8,0xF9E2AF);
    display_draw_rect(explorer_icon_x+10,y+18,38,4,0xFAB387);
    display_draw_text_sized_at(explorer_icon_x+7,y+55,"Files",th.text,
                               th.desktop,desktop_label_size());
}

static void draw_terminal_icon(void){
    struct personalization_colors th;
    desktop_theme(&th);
    uint32_t y=terminal_icon_y;
    display_draw_rect(terminal_icon_x,y,ICON_W,50,th.titlebar);
    display_draw_rect(terminal_icon_x+7,y+8,44,30,th.window);
    display_draw_text_at(terminal_icon_x+13,y+18,">_",0xA6E3A1,th.window);
    display_draw_text_sized_at(terminal_icon_x,y+55,"Terminal",th.text,
                               th.desktop,desktop_label_size());
}

static void draw_app_icon(
    uint32_t ix,
    uint32_t iy,
    const char *symbol,
    const char *label,
    uint32_t color
){
    struct personalization_colors th;
    desktop_theme(&th);
    display_draw_rect(ix,iy,ICON_W,50,th.titlebar);
    display_draw_rect(ix+8,iy+7,42,34,color);
    display_draw_text_sized_at(ix+17,iy+17,symbol,0x1E1E2E,color,12);
    display_draw_text_sized_at(ix+4,iy+55,label,th.text,th.desktop,
                               desktop_label_size());
}

static void draw_desktop_icons(void){
    uint32_t count = desktop_entries_count();
    for(uint32_t i=0; i<count; i++){
        const struct desktop_entry *e = desktop_entries_get(i);
        if(e->hidden) continue;
        draw_app_icon(e->x, e->y, e->icon_text, e->name, e->icon_color);
    }
}

static bool installation_present(void){
    int64_t descriptor=userspace_syscall(
        SYS_OPEN,(uint64_t)"/purec/install.cfg",0,0);
    if(descriptor<0) return false;
    (void)userspace_syscall(SYS_CLOSE,(uint64_t)descriptor,0,0);
    return true;
}

static void launch_installer(void){
    (void)userspace_run_detached("/bin/installer",0);
}

static bool installer_requires_restart(int32_t status){
    if(status>=128) return true;
    struct install_status install={0};
    return userspace_syscall(SYS_INSTALL_STATUS,
                             (uint64_t)&install,0,0)>=0
        && install.state!=0;
}

static int32_t detached_program_slot(void){
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        if(detached_programs[index]<=0) return (int32_t)index;
    }
    return -1;
}

static int32_t userspace_run_detached(const char *path,
                                      const char *arguments){
    int32_t slot=detached_program_slot();
    if(slot<0) return -1;
    int32_t pid=(int32_t)userspace_syscall(
        SYS_EXEC,(uint64_t)path,(uint64_t)arguments,0);
    if(pid>=0) detached_programs[slot]=pid;
    else klogf(KLOG_ERROR,"desktop: exec failed path='%s' rc=%d",path,pid);
    return pid;
}

static void reap_detached_programs(void){
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        if(detached_programs[index]<=0) continue;
        int32_t status=0;
        int64_t result=userspace_syscall(SYS_WAIT,
            (uint64_t)detached_programs[index],(uint64_t)&status,1);
        if(result>0){
            if(status!=0){
                klogf(KLOG_ERROR,"desktop: detached pid=%d exited status=%d",
                      detached_programs[index],status);
            }
            detached_programs[index]=0;
            installer_icon_visible=!installation_present();
        }
    }
}

static int32_t userspace_run_program_with_args(const char *path,
                                               const char *arguments){
    if(!path) return -1;
    bool supervise_installer=strcmp(path,"/bin/installer")==0;
    if(!supervise_installer) return userspace_run_detached(path,arguments);
    if(external_program_has_input_focus()) return -1;
    set_external_program_input_focus(true);
    window_manager_set_suspended(true);
    (void)userspace_syscall(SYS_CONSOLE_DISABLE,0,0,0);
    bool installer_pinned=false;
    int32_t status=-1;
    for(;;){
        int64_t pid=userspace_syscall(
            SYS_EXEC,(uint64_t)path,(uint64_t)arguments,0);
        if(pid<0){
            if(supervise_installer
               && (installer_pinned || installer_requires_restart(-1))){
                scheduler_sleep(20);
                continue;
            }
            break;
        }
        status=0;
        (void)userspace_syscall(SYS_WAIT,(uint64_t)pid,
                                (uint64_t)&status,0);
        if(supervise_installer && status>=128)
            install_report_ui_crash(status);
        if(!supervise_installer || !installer_requires_restart(status)) break;
        installer_pinned=true;
        klogf(KLOG_ERROR,
              "installer: process crashed status=%d; restarting without desktop redraw",
              status);
        scheduler_sleep(20);
    }
    set_external_program_input_focus(false);
    window_manager_set_suspended(false);
    redraw_managed_scene(0);
    return status;
}

int32_t userspace_run_program(const char *path){
    return userspace_run_program_with_args(path,0);
}

static void draw_power_button(void){
    struct personalization_colors th;
    desktop_theme(&th);
    uint32_t x=desktop_width-38;
    display_draw_rect(x,3,30,22,th.border);
    display_draw_text_at(x+7,9,"PWR",th.text,th.border);
}

static void draw_power_menu(void){
    if(!power_menu_visible) return;
    struct personalization_colors th;
    desktop_theme(&th);
    uint32_t x=desktop_width-158;
    display_draw_rect(x,28,150,62,th.border);
    display_draw_rect(x+2,30,146,28,th.window);
    display_draw_rect(x+2,60,146,28,th.window);
    display_draw_text_at(x+12,39,"Restart",th.text,th.window);
    display_draw_text_at(x+12,69,"Power off",th.danger,th.window);
}

static void draw_desktop(void){
    desktop_width=display_get_width();
    desktop_height=display_get_height();    if(desktop_width==0) desktop_width=1280;
    if(desktop_height==0) desktop_height=800;
    if(!icon_layout_ready){
        explorer_icon_x=desktop_width>700 ? 420 : desktop_width-212;
        htop_icon_x=explorer_icon_x+72;
        terminal_icon_x=htop_icon_x+72;
        if(desktop_width<=700){
            settings_icon_y=130;
            installer_icon_y=130;
        }
        if(desktop_width<=560){
            clock_icon_y=130;
            calculator_icon_y=130;
            calendar_icon_y=130;
        }
        icon_layout_ready=true;
    }

    struct personalization_colors dth;
    desktop_theme(&dth);
    if(!wallpaper_draw())
        display_clear(dth.desktop);
    display_draw_rect(0,0,desktop_width,TOPBAR_HEIGHT,dth.titlebar);
    display_draw_text_at(12,8,"PureC OS",dth.accent,dth.titlebar);
    audio_panel_draw(desktop_width);
    draw_desktop_icons();
    draw_power_button();
}

static void redraw_scene(void){
    mouse_begin_framebuffer_update();
    draw_desktop();
    if(desktop_apps_is_visible()) desktop_apps_draw();
    audio_panel_draw(desktop_width);
    draw_power_menu();
    mouse_end_framebuffer_update();
}

static void wait_for_managed_repaint(void){
    for(uint32_t attempt=0;
        attempt<250 && window_manager_repaint_pending();attempt++)
        scheduler_sleep(1);
    if(window_manager_repaint_pending()) window_manager_cancel_repaint();
}

static void redraw_managed_scene(uint32_t excluded_pid){
    mouse_begin_framebuffer_update();
    gop_begin_compose();
    redraw_scene();
    window_manager_request_repaint(excluded_pid);
    wait_for_managed_repaint();
    gop_end_compose();
    mouse_end_framebuffer_update();
}

static bool service_desktop_redraw(void){
    uint32_t requested=__atomic_load_n(&desktop_redraw_requested,
                                       __ATOMIC_ACQUIRE);
    uint32_t completed=__atomic_load_n(&desktop_redraw_completed,
                                       __ATOMIC_RELAXED);
    if(requested==completed) return false;
    redraw_managed_scene(desktop_redraw_requester);
    __atomic_store_n(&desktop_redraw_completed,requested,__ATOMIC_RELEASE);
    return true;
}

void userspace_redraw_desktop(void){
    while(__atomic_test_and_set(&desktop_redraw_busy,__ATOMIC_ACQUIRE))
        scheduler_sleep(1);
    desktop_redraw_requester=(uint32_t)process_current_pid();
    uint32_t ticket=__atomic_add_fetch(&desktop_redraw_requested,1,
                                       __ATOMIC_ACQ_REL);
    for(;;){
        uint32_t completed=__atomic_load_n(&desktop_redraw_completed,
                                           __ATOMIC_ACQUIRE);
        if((int32_t)(completed-ticket)>=0) break;
        scheduler_sleep(1);
    }
    __atomic_clear(&desktop_redraw_busy,__ATOMIC_RELEASE);
}

static void redraw_icon_move(void){
    redraw_managed_scene(0);
}

static void handle_desktop_mouse(void){
    struct mouse_state mouse=mouse_get_state();
    bool pressed=(mouse.buttons&1) && !(previous_mouse_buttons&1);
    bool released=!(mouse.buttons&1) && (previous_mouse_buttons&1);
    bool redraw=false;
    bool consumed=false;

    if(pressed && point_inside(mouse.x,mouse.y,desktop_width-38,3,30,22)){
        power_menu_visible=!power_menu_visible;
        consumed=true;
        redraw=true;
    }
    if(!consumed){
        bool audio_redraw=false;
        consumed=audio_panel_handle_mouse(
            mouse.x,mouse.y,mouse.buttons,pressed,released,
            desktop_width,&audio_redraw
        );
        redraw=redraw || audio_redraw;
    }
    if(!consumed && pressed && power_menu_visible){
        uint32_t menu_x=desktop_width-158;
        if(point_inside(mouse.x,mouse.y,menu_x,28,150,30)){
            desktop_apps_save_time();
            (void)userspace_syscall(SYS_REBOOT,0,0,0);
            consumed=true;
        } else if(point_inside(mouse.x,mouse.y,menu_x,58,150,32)){
            desktop_apps_save_time();
            (void)userspace_syscall(SYS_SHUTDOWN,0,0,0);
            consumed=true;
        } else {
            power_menu_visible=false;
            redraw=true;
        }
    }

    if(!consumed){
        bool focus_changed=false;
        consumed=window_manager_handle_pointer(mouse.x,mouse.y,pressed,
                                                &focus_changed);
        if(focus_changed) redraw_managed_scene(0);
    }

    if(!consumed && desktop_apps_is_visible()){
        bool app_redraw=false;
        consumed=desktop_apps_handle_mouse(
            mouse.x,mouse.y,mouse.buttons,pressed,released,
            desktop_width,desktop_height,&app_redraw
        );
        redraw=redraw || app_redraw;
    }
    uint32_t *icon_positions[12]={
        &explorer_icon_x,
        &htop_icon_x,
        &terminal_icon_x,
        &clock_icon_x,
        &calculator_icon_x,
        &calendar_icon_x,
        &settings_icon_x,
        &installer_icon_x,
        &disks_icon_x,
        &tetris_icon_x,
        &logview_icon_x,
        &hexedit_icon_x
    };
    uint32_t *icon_y_positions[12]={
        &explorer_icon_y,
        &htop_icon_y,
        &terminal_icon_y,
        &clock_icon_y,
        &calculator_icon_y,
        &calendar_icon_y,
        &settings_icon_y,
        &installer_icon_y,
        &disks_icon_y,
        &tetris_icon_y,
        &logview_icon_y,
        &hexedit_icon_y
    };
    if(pressed && !consumed){
        for(int8_t index=0;index<12;index++){
            if(index==7 && !installer_icon_visible) continue;
            if(point_inside(
                    mouse.x,mouse.y,
                    *icon_positions[index],*icon_y_positions[index],
                    ICON_W,ICON_H
                )){
                dragged_icon=index;
                icon_drag_offset_x=mouse.x-(int32_t)*icon_positions[index];
                icon_drag_offset_y=mouse.y-(int32_t)*icon_y_positions[index];
                icon_drag_moved=false;
                consumed=true;
                break;
            }
        }
    }
    if(dragged_icon>=0 && (mouse.buttons&1)){
        uint32_t old_x=*icon_positions[dragged_icon];
        uint32_t old_y=*icon_y_positions[dragged_icon];
        int32_t next_x=mouse.x-icon_drag_offset_x;
        int32_t next_y=mouse.y-icon_drag_offset_y;
        if(next_x<0) next_x=0;
        if(next_x>(int32_t)desktop_width-ICON_W) next_x=(int32_t)desktop_width-ICON_W;
        if(next_y<TOPBAR_HEIGHT) next_y=TOPBAR_HEIGHT;
        if(next_y>(int32_t)desktop_height-ICON_H) next_y=(int32_t)desktop_height-ICON_H;
        if((uint32_t)next_x!=*icon_positions[dragged_icon]
           || (uint32_t)next_y!=*icon_y_positions[dragged_icon]) icon_drag_moved=true;
        *icon_positions[dragged_icon]=(uint32_t)next_x;
        *icon_y_positions[dragged_icon]=(uint32_t)next_y;
        if(old_x!=*icon_positions[dragged_icon]
           || old_y!=*icon_y_positions[dragged_icon]){
            redraw_icon_move();
        }
    }
    if(released && dragged_icon>=0){
        int8_t icon=dragged_icon;
        dragged_icon=-1;
        if(!icon_drag_moved){
            if(icon==0)
                (void)userspace_run_program("/bin/program/files");
            else if(icon==1)
                (void)userspace_run_program("/bin/program/monitor");
            else if(icon==2)
                (void)userspace_run_program("/bin/program/terminal");
            else if(icon==6)
                (void)userspace_run_program("/bin/program/settings");
            else if(icon==7) launch_installer();
            else if(icon==8)
                (void)userspace_run_program("/bin/program/disks");
            else if(icon==9)
                (void)userspace_run_program("/bin/program/tetris");
            else if(icon==10)
                (void)userspace_run_program("/bin/program/logview");
            else if(icon==11)
                (void)userspace_run_program("/bin/program/hexedit");
            else desktop_apps_open((enum desktop_app)(icon-3),desktop_width,desktop_height);
        }
        if(!icon_drag_moved) redraw=true;
    }
    previous_mouse_buttons=mouse.buttons;
    if(redraw) redraw_managed_scene(0);
}

static bool handle_special_keyboard(void){
    uint8_t key;
    bool handled=false;

    while(keyboard_try_get_special(&key)){
        if(audio_panel_handle_special_key(key)) handled=true;
    }

    return handled;
}

uint32_t userspace_get_width(void){ return desktop_width; }
uint32_t userspace_get_height(void){ return desktop_height; }

void userspace_set_mouse_debug(bool enabled){
    mouse_set_debug_overlay(enabled);
    if(!enabled){
        struct personalization_colors th;
        desktop_theme(&th);
        display_draw_rect(MOUSE_DEBUG_X,MOUSE_DEBUG_Y,MOUSE_DEBUG_W,MOUSE_DEBUG_H,th.desktop);
    }
    mouse_redraw();
}

void userspace_init(void){
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: validating framebuffer");
    if(!display_is_available()) kernel_panic("userspace requires an active framebuffer");
    if(display_get_width() < 320 || display_get_height() < 240)
        kernel_panic("framebuffer is too small for userspace");

    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: initializing keyboard");
    keyboard_init();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: drawing desktop");
    // draw_desktop clears the boot log; subsequent diagnostics remain in the
    // ring and serial, while panic forcibly restores a visible panic screen.
    klog_set_screen_enabled(false);
    desktop_entries_init();
    draw_desktop();
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: configuring mouse bounds");
    mouse_set_bounds((int32_t)desktop_width,(int32_t)desktop_height);
    userspace_set_mouse_debug(false);
    audio_panel_init();
    desktop_apps_init();
    installer_icon_visible=!installation_present();
    personalization_poll();
    klog_set_screen_enabled(false);
    (void)userspace_run_detached("/bin/program/login",0);
    boot_diag_checkpoint(BOOT_STAGE_USERSPACE_INIT, "userspace: initialization complete");
}

void userspace_input_thread(void *arg){
    (void)arg;
    klog(KLOG_INFO, "sched: input thread started (mouse polling + desktop)");
    for(;;){
        ps2_mouse_poll();
        block_device_poll_usb_hotplug();
        usb_mouse_poll();
        keyboard_poll();
        userspace_audio_update();
        reap_detached_programs();
        if(personalization_poll()) redraw_managed_scene(0);
        (void)service_desktop_redraw();
        if(external_program_has_input_focus()){
            scheduler_sleep(10);
            continue;
        }
        if(!window_manager_has_focus() && handle_special_keyboard())
            redraw_managed_scene(0);
        handle_desktop_mouse();
        desktop_apps_update();
        scheduler_sleep(1);
    }
}

void userspace_keyboard_thread(void *arg){
    (void)arg;
    klog(KLOG_INFO, "sched: desktop keyboard thread started");
    for(;;){
        if(external_program_has_input_focus() || window_manager_has_focus()){
            scheduler_sleep(10);
            continue;
        }
        char c;
        while(!external_program_has_input_focus()
              && !window_manager_has_focus() && keyboard_try_getc(&c))
            (void)desktop_apps_handle_key(c);
        scheduler_sleep(1);
    }
}

void userspace_log_thread(void *arg){
    (void)arg;
    uint64_t cursor=0;
    uint64_t file_size=0;
    uint32_t pending=0;
    uint64_t last_flush_tick=timer_ticks();
    int64_t clear_result=userspace_syscall(
        SYS_FILE_WRITE,(uint64_t)PERSISTENT_LOG_PATH,0,0);
    if(clear_result<0){
        klogf(KLOG_ERROR,
              "klog-disk: cannot create %s status=%d; persistent logging disabled",
              PERSISTENT_LOG_PATH,(int)clear_result);
        scheduler_exit();
        return;
    }
    klogf(KLOG_OK,
          "klog-disk: streaming enabled path=%s chunk=%u max_bytes=%llu ram_ring=%u",
          PERSISTENT_LOG_PATH,PERSISTENT_LOG_CHUNK,
          PERSISTENT_LOG_MAX_BYTES,8U*1024U*1024U);
    for(;;){
        bool data_lost=false;
        uint32_t amount=klog_read_since(
            &cursor,persistent_log_buffer+pending,
            sizeof(persistent_log_buffer)-pending,
            &data_lost);
        pending+=amount;
        if(data_lost){
            klogf(KLOG_ERROR,
                  "klog-disk: RAM ring overrun cursor advanced to=%llu total=%llu",
                  cursor,klog_total_bytes());
        }
        uint64_t now=timer_ticks();
        if(pending==0 || (pending<sizeof(persistent_log_buffer)
                          && now-last_flush_tick<1000)){
            scheduler_yield();
            continue;
        }
        uint64_t remaining=PERSISTENT_LOG_MAX_BYTES-file_size;
        amount=pending;
        if(amount>remaining) amount=(uint32_t)remaining;
        if(amount==0){
            klogf(KLOG_WARN,
                  "klog-disk: file reached FAT32 limit bytes=%llu path=%s",
                  file_size,PERSISTENT_LOG_PATH);
            scheduler_exit();
            return;
        }
        int64_t result=userspace_syscall(
            SYS_FILE_APPEND,(uint64_t)PERSISTENT_LOG_PATH,
            (uint64_t)persistent_log_buffer,amount);
        if(result<0 || (uint32_t)result!=amount){
            klogf(KLOG_ERROR,
                  "klog-disk: append failed status=%d requested=%u written=%u file_size=%llu",
                  (int)result,amount,result>0 ? (uint32_t)result : 0,file_size);
            scheduler_exit();
            return;
        }
        file_size+=(uint32_t)result;
        pending=0;
        last_flush_tick=now;
        scheduler_yield();
    }
}

void userspace_run(void){
    for(;;){
        ps2_mouse_poll();
        block_device_poll_usb_hotplug();
        usb_mouse_poll();
        keyboard_poll();
        userspace_audio_update();
        reap_detached_programs();
        /* Change-driven repaint: includes ALL windows (excluded=0), unlike
         * a redraw requested by a Ring3 process which would bury itself. */
        if(personalization_poll()) redraw_managed_scene(0);
        (void)service_desktop_redraw();
        if(external_program_has_input_focus()){
            scheduler_yield();
            continue;
        }
        if(!window_manager_has_focus() && handle_special_keyboard())
            redraw_managed_scene(0);
        handle_desktop_mouse();

        char c;
        while(!window_manager_has_focus() && keyboard_try_getc(&c))
            (void)desktop_apps_handle_key(c);
        desktop_apps_update();

        __asm__ volatile("pause");
        for(volatile uint32_t wait=0;wait<10000;wait++){
            __asm__ volatile("nop");
        }
    }
}
