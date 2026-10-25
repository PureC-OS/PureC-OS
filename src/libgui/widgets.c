#include "include/pguiw.h"
#include "internal.h"
#include "../libc/include/purec.h"

void pg_label(struct pg_window *window, uint32_t x, uint32_t y,
              const char *text){
    if(window) pg_window_text(window,x,y,text,window->theme.text);
}

void pg_panel(struct pg_window *window, struct pg_rect bounds){
    if(!window) return;
    pg_window_rect(window,bounds,window->theme.titlebar);
}

bool pg_button(struct pg_window *window, struct pg_rect bounds,
               const char *label, const struct pg_event *event){
    if(!window || !event) return false;
    if(!label) label="";
    struct pg_rect screen=pg_internal_to_screen(window,bounds);
    bool inside=pg_internal_point_inside(event->x,event->y,&screen);
    enum pg_widget_state state=PG_WIDGET_NORMAL;
    if(inside && event->type==PG_EVENT_MOUSE_DOWN) state=PG_WIDGET_PRESSED;
    else if(inside) state=PG_WIDGET_HOVER;
    uint32_t color=window->theme.border;
    if(state==PG_WIDGET_HOVER) color=window->theme.accent;
    if(state==PG_WIDGET_PRESSED) color=window->theme.muted_text;
    pg_window_rect(window,bounds,color);
    uint32_t text_width=pc_strlen(label)*8;
    uint32_t text_x=bounds.x+(bounds.width>text_width
        ? (bounds.width-text_width)/2 : 4);
    uint32_t text_y=bounds.y+(bounds.height>8 ? (bounds.height-8)/2 : 0);
    pg_window_text(window,text_x,text_y,label,window->theme.text);
    return inside && event->type==PG_EVENT_MOUSE_UP && event->button==1;
}
