#include "internal.h"
#include "../libc/include/purec.h"

#define PG_MINIMUM_WIDTH 160
#define PG_MINIMUM_HEIGHT 96

static void update_client_rect(struct pg_window *window){
    window->client.x=window->frame.x+PG_WINDOW_BORDER;
    window->client.y=window->frame.y+PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER;
    window->client.width=window->frame.width-PG_WINDOW_BORDER*2;
    window->client.height=window->frame.height-PG_TITLEBAR_HEIGHT
        -PG_WINDOW_BORDER*2;
}

uint32_t pg_internal_sync_frame(struct pg_window *window, uint32_t state){
    if(!window || !window->registered) return 0;
    if(!(state&GUI_WINDOW_STATE_SERVER)){
        window->server=false;
        return 0;
    }
    window->server=true;
    uint32_t generation=state>>GUI_WINDOW_STATE_GEN_SHIFT;
    if(window->frame_synced && generation==window->frame_generation)
        return 0;
    struct gui_window_info info;
    if(!pc_gui_window_info(&info)) return 0;
    uint32_t changes=0;
    bool minimized=(info.flags&GUI_WINDOW_INFO_MINIMIZED)!=0;
    if(info.x!=window->frame.x || info.y!=window->frame.y)
        changes|=PG_SYNC_MOVED;
    if(info.width!=window->frame.width || info.height!=window->frame.height
       || ((info.flags&GUI_WINDOW_INFO_MAXIMIZED)!=0)!=window->maximized)
        changes|=PG_SYNC_RESIZED;
    if(minimized!=window->minimized) changes|=PG_SYNC_MINIMIZED;
    window->frame=(struct pg_rect){info.x,info.y,info.width,info.height};
    window->minimized=minimized;
    window->maximized=(info.flags&GUI_WINDOW_INFO_MAXIMIZED)!=0;
    update_client_rect(window);
    window->frame_generation=generation;
    window->frame_synced=true;
    return changes;
}

bool pg_internal_point_in_titlebar(const struct pg_window *window,
                                   int32_t x, int32_t y){
    return x>=(int32_t)window->frame.x
        && x<(int32_t)(window->frame.x+window->frame.width)
        && y>=(int32_t)window->frame.y
        && y<(int32_t)(window->frame.y+PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2);
}

bool pg_internal_update_registered_frame(struct pg_window *window){
    if(!window || !window->registered) return false;
    struct gui_window_request request={
        .x=window->frame.x,
        .y=window->frame.y,
        .width=window->frame.width,
        .height=(window->minimized && !window->server)
            ? PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2 : window->frame.height
    };
    return pc_gui_window_update(&request);
}

bool pg_window_init(struct pg_window *window, const char *title,
                    uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height){
    if(!window || width<PG_MINIMUM_WIDTH || height<PG_MINIMUM_HEIGHT)
        return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available
       || width>display.width || height>display.height) return false;
    if(x>display.width-width) x=display.width-width;
    if(y>display.height-height) y=display.height-height;
    window->frame=(struct pg_rect){x,y,width,height};
    window->theme=pg_theme_default();
    pg_font_sync();
    pc_copy(window->title,title ? title : "PureGUI",sizeof(window->title));
    window->previous_mouse_x=-1;
    window->previous_mouse_y=-1;
    window->drag_offset_x=0;
    window->drag_offset_y=0;
    window->previous_mouse_buttons=0;
    window->previous_wheel=0;
    window->wheel_ready=false;
    window->dragging=false;
    window->minimized=false;
    window->open=true;
    window->focused=true;
    window->registered=false;
    window->repainting=false;
    window->closable=true;
    window->esc_closes=true;
    window->server=false;
    window->maximized=false;
    window->frame_synced=false;
    window->frame_generation=0;
    update_client_rect(window);
    struct gui_window_request request={x,y,width,height};
    if(!pc_gui_window_register(&request)){
        window->open=false;
        return false;
    }
    window->registered=true;
    (void)pc_gui_window_set_title(window->title);
    (void)pg_internal_sync_frame(window,pc_gui_window_state());
    return true;
}

bool pg_window_center(struct pg_window *window, const char *title,
                      uint32_t width, uint32_t height){
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || width>display.width
       || height>display.height) return false;
    return pg_window_init(window,title,(display.width-width)/2,
                          (display.height-height)/2,width,height);
}

void pg_window_begin(struct pg_window *window){
    if(!window || !window->open) return;
    (void)pg_internal_sync_frame(window,pc_gui_window_state());
    pc_display_begin_update();
    pc_draw_rect(window->frame.x+6,window->frame.y+6,
                 window->frame.width,window->minimized
                    ? PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2
                    : window->frame.height,
                 window->theme.shadow);
    pc_draw_rect(window->frame.x,window->frame.y,
                 window->frame.width,window->minimized
                    ? PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2
                    : window->frame.height,
                 window->theme.border);
    pc_draw_rect(window->frame.x+PG_WINDOW_BORDER,
                 window->frame.y+PG_WINDOW_BORDER,
                 window->frame.width-PG_WINDOW_BORDER*2,
                 PG_TITLEBAR_HEIGHT,window->theme.titlebar);
    if(!window->minimized)
        pc_draw_rect(window->client.x,window->client.y,
                     window->client.width,window->client.height,
                     window->theme.window);
    pg_internal_draw_text_clipped(window->frame.x+10,window->frame.y+10,
                                  window->title,window->theme.text,
                                  window->theme.titlebar,&window->frame);
    pc_draw_rect(window->frame.x+window->frame.width-24,
                 window->frame.y+8,14,14,window->theme.danger);
    pc_draw_text_tr(window->frame.x+window->frame.width-21,
                 window->frame.y+11,"x",window->theme.window);
    pc_draw_rect(window->frame.x+window->frame.width-44,
                 window->frame.y+8,14,14,window->theme.accent);
    pc_draw_text_tr(window->frame.x+window->frame.width-41,
                 window->frame.y+11,window->minimized ? "+" : "-",
                 window->theme.window);
    if(window->server){
        pc_draw_rect(window->frame.x+window->frame.width-64,
                     window->frame.y+8,14,14,window->theme.muted_text);
        pc_draw_text_tr(window->frame.x+window->frame.width-61,
                     window->frame.y+11,window->maximized ? "o" : "O",
                     window->theme.window);
    }
}

void pg_window_end(struct pg_window *window){
    if(!window || !window->open) return;
    pc_display_end_update();
    if(window->repainting){
        window->repainting=false;
        pc_gui_window_repaint_done();
    }
}

void pg_window_close(struct pg_window *window){
    if(!window) return;
    window->open=false;
    if(window->registered){
        pc_gui_window_unregister();
        window->registered=false;
    }
    if(!window->server) pc_desktop_redraw();
}

void pg_window_set_closable(struct pg_window *window, bool closable){
    if(!window) return;
    window->closable=closable;
    if(window->registered)
        (void)pc_gui_window_set_flags(closable ? 0
            : GUI_WINDOW_FLAG_FIXED|GUI_WINDOW_FLAG_NOCLOSE);
}

void pg_window_set_esc_closes(struct pg_window *window, bool esc_closes){
    if(window) window->esc_closes=esc_closes;
}

bool pg_window_move(struct pg_window *window, uint32_t x, uint32_t y){
    if(!window || !window->open) return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available
       || window->frame.width>display.width
       || window->frame.height>display.height) return false;
    if(x>display.width-window->frame.width)
        x=display.width-window->frame.width;
    if(y>display.height-window->frame.height)
        y=display.height-window->frame.height;
    if(x==window->frame.x && y==window->frame.y) return true;
    window->frame.x=x;
    window->frame.y=y;
    update_client_rect(window);
    (void)pg_internal_update_registered_frame(window);
    if(!window->server) pc_desktop_redraw();
    return true;
}

void pg_window_minimize(struct pg_window *window){
    if(!window || !window->open) return;
    if(window->server){
        (void)pc_gui_window_command(GUI_WINDOW_CMD_MINIMIZE);
        return;
    }
    window->minimized=true;
    (void)pg_internal_update_registered_frame(window);
    pc_desktop_redraw();
}

void pg_window_restore(struct pg_window *window){
    if(!window || !window->open) return;
    if(window->server){
        (void)pc_gui_window_command(GUI_WINDOW_CMD_RESTORE);
        return;
    }
    window->minimized=false;
    (void)pg_internal_update_registered_frame(window);
    pc_desktop_redraw();
}

void pg_window_maximize(struct pg_window *window){
    if(window && window->open && window->server)
        (void)pc_gui_window_command(GUI_WINDOW_CMD_MAXIMIZE);
}

void pg_window_unmaximize(struct pg_window *window){
    if(window && window->open && window->server)
        (void)pc_gui_window_command(GUI_WINDOW_CMD_UNMAXIMIZE);
}

bool pg_window_is_maximized(const struct pg_window *window){
    return window && window->open && window->maximized;
}

bool pg_window_is_open(const struct pg_window *window){
    return window && window->open;
}

bool pg_window_is_minimized(const struct pg_window *window){
    return window && window->open && window->minimized;
}

struct pg_rect pg_window_client(const struct pg_window *window){
    return window ? window->client : (struct pg_rect){0,0,0,0};
}

void pg_window_clear(struct pg_window *window, uint32_t color){
    if(window && window->open && !window->minimized)
        pc_draw_rect(window->client.x,window->client.y,
                     window->client.width,window->client.height,color);
}
