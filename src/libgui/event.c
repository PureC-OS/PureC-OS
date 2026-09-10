#include "internal.h"
#include "../libc/include/purec.h"

static uint8_t changed_button(uint8_t changed){
    if(changed&1) return 1;
    if(changed&2) return 2;
    if(changed&4) return 3;
    return 0;
}

bool pg_window_poll_event(struct pg_window *window, struct pg_event *event){
    if(!window || !event || !window->open) return false;
    *event=(struct pg_event){.type=PG_EVENT_NONE};
    uint32_t state=pc_gui_window_state();
    bool focused=(state&GUI_WINDOW_STATE_FOCUSED)!=0;
    if(state&GUI_WINDOW_STATE_REPAINT){
        window->focused=focused;
        window->repainting=true;
        event->type=PG_EVENT_REPAINT;
        return true;
    }
    if(focused!=window->focused){
        window->focused=focused;
        if(focused){
            event->type=PG_EVENT_FOCUS;
            return true;
        }
    }
    if(focused){
        int32_t key=pc_try_getchar();
        if(key>=0){
            event->key=key;
            /* Gate windows (login) cannot be dismissed with Esc. */
            if(key==27 && window->closable){
                event->type=PG_EVENT_CLOSE;
                pg_window_close(window);
            }else{
                event->type=PG_EVENT_KEY;
            }
            return true;
        }
        int32_t special=pc_try_get_special();
        if(special>=0){
            event->key=special;
            event->type=PG_EVENT_SPECIAL_KEY;
            return true;
        }
    }
    struct mouse_state mouse;
    if(!pc_mouse_get(&mouse)) return false;
    event->x=mouse.x;
    event->y=mouse.y;
    uint8_t changed=mouse.buttons^window->previous_mouse_buttons;
    if(changed){
        event->button=changed_button(changed);
        event->type=(mouse.buttons&changed) ? PG_EVENT_MOUSE_DOWN
                                           : PG_EVENT_MOUSE_UP;
    } else if(mouse.x!=window->previous_mouse_x
              || mouse.y!=window->previous_mouse_y){
        event->type=PG_EVENT_MOUSE_MOVE;
    }
    window->previous_mouse_x=mouse.x;
    window->previous_mouse_y=mouse.y;
    window->previous_mouse_buttons=mouse.buttons;
    if(!focused) return false;
    struct pg_rect close={
        window->frame.x+window->frame.width-24,
        window->frame.y+8,14,14
    };
    struct pg_rect minimize={
        window->frame.x+window->frame.width-44,
        window->frame.y+8,14,14
    };
    if(event->type==PG_EVENT_MOUSE_DOWN && event->button==1){
        struct pg_rect titlebar={
            window->frame.x,window->frame.y,
            window->frame.width,PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2
        };
        if(pg_internal_point_inside(mouse.x,mouse.y,&titlebar)
           && !pg_internal_point_inside(mouse.x,mouse.y,&close)
           && !pg_internal_point_inside(mouse.x,mouse.y,&minimize)){
            window->dragging=true;
            window->drag_offset_x=mouse.x-(int32_t)window->frame.x;
            window->drag_offset_y=mouse.y-(int32_t)window->frame.y;
        }
    }
    if(event->type==PG_EVENT_MOUSE_MOVE && window->dragging
       && (mouse.buttons&1)){
        int32_t next_x=mouse.x-window->drag_offset_x;
        int32_t next_y=mouse.y-window->drag_offset_y;
        if(next_x<0) next_x=0;
        if(next_y<0) next_y=0;
        if(pg_window_move(window,(uint32_t)next_x,(uint32_t)next_y))
            event->type=PG_EVENT_MOVE;
    }
    if(event->type==PG_EVENT_MOUSE_UP && event->button==1){
        bool was_dragging=window->dragging;
        window->dragging=false;
        if(!was_dragging && pg_internal_point_inside(mouse.x,mouse.y,&close)){
            /* Gate windows (login) cannot be closed or minimized. */
            if(window->closable){
                event->type=PG_EVENT_CLOSE;
                pg_window_close(window);
                return true;
            }
            event->type=PG_EVENT_NONE;
            return false;
        }
        if(window->closable && !was_dragging
           && pg_internal_point_inside(mouse.x,mouse.y,&minimize)){
            window->minimized=!window->minimized;
            (void)pg_internal_update_registered_frame(window);
            pc_desktop_redraw();
            event->type=PG_EVENT_MINIMIZE;
        }
    }
    if(!(mouse.buttons&1)) window->dragging=false;
    return event->type!=PG_EVENT_NONE;
}
