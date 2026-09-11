#include "window_manager.h"
#include "../lib/string.h"

struct managed_window {
    uint32_t pid;
    struct gui_window_request frame;
    uint32_t z_order;
    bool used;
    bool repaint_pending;
};

static struct managed_window windows[WINDOW_MANAGER_CAPACITY];
static uint32_t focused_pid;
static uint32_t next_z_order=1;
static uint32_t repainting_pid;
static bool registry_suspended;

static struct managed_window *find_window(uint32_t pid){
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        if(windows[index].used && windows[index].pid==pid)
            return &windows[index];
    }
    return 0;
}

static bool point_inside(int32_t x, int32_t y,
                         const struct gui_window_request *frame){
    return frame && x>=(int32_t)frame->x && y>=(int32_t)frame->y
        && x<(int32_t)(frame->x+frame->width)
        && y<(int32_t)(frame->y+frame->height);
}

static struct managed_window *top_window_at(int32_t x, int32_t y){
    struct managed_window *top=0;
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        struct managed_window *window=&windows[index];
        if(!window->used || !point_inside(x,y,&window->frame)) continue;
        if(!top || window->z_order>top->z_order) top=window;
    }
    return top;
}

static struct managed_window *next_repaint_window(void){
    struct managed_window *next=0;
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        struct managed_window *window=&windows[index];
        if(!window->used || !window->repaint_pending) continue;
        if(!next || window->z_order<next->z_order) next=window;
    }
    return next;
}

bool window_manager_register(uint32_t pid,
                             const struct gui_window_request *request){
    if(!pid || !request || !request->width || !request->height) return false;
    struct managed_window *window=find_window(pid);
    if(!window){
        for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
            if(!windows[index].used){
                window=&windows[index];
                memset(window,0,sizeof(*window));
                window->used=true;
                window->pid=pid;
                break;
            }
        }
    }
    if(!window) return false;
    window->frame=*request;
    window->z_order=next_z_order++;
    focused_pid=pid;
    return true;
}

bool window_manager_update(uint32_t pid,
                           const struct gui_window_request *request){
    struct managed_window *window=find_window(pid);
    if(!window || !request || !request->width || !request->height)
        return false;
    window->frame=*request;
    return true;
}

void window_manager_unregister(uint32_t pid){
    struct managed_window *window=find_window(pid);
    if(!window) return;
    if(repainting_pid==pid) repainting_pid=0;
    memset(window,0,sizeof(*window));
    if(focused_pid!=pid) return;
    focused_pid=0;
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        if(windows[index].used
           && (!focused_pid
               || windows[index].z_order
                    >find_window(focused_pid)->z_order))
            focused_pid=windows[index].pid;
    }
}

uint32_t window_manager_state(uint32_t pid){
    if(registry_suspended) return 0;
    struct managed_window *window=find_window(pid);
    if(!window) return 0;
    uint32_t state=focused_pid==pid ? GUI_WINDOW_STATE_FOCUSED : 0;
    if(repainting_pid==pid){
        state|=GUI_WINDOW_STATE_REPAINT;
    } else if(!repainting_pid){
        struct managed_window *next=next_repaint_window();
        if(next && next->pid==pid){
            repainting_pid=pid;
            state|=GUI_WINDOW_STATE_REPAINT;
        }
    }
    return state;
}

void window_manager_finish_repaint(uint32_t pid){
    if(repainting_pid!=pid) return;
    struct managed_window *window=find_window(pid);
    if(window) window->repaint_pending=false;
    repainting_pid=0;
}

bool window_manager_handle_pointer(int32_t x, int32_t y, bool pressed,
                                   bool *focus_changed){
    if(registry_suspended){
        if(focus_changed) *focus_changed=false;
        return false;
    }
    struct managed_window *top=top_window_at(x,y);
    uint32_t previous=focused_pid;
    if(pressed){
        if(top){
            focused_pid=top->pid;
            top->z_order=next_z_order++;
        } else {
            focused_pid=0;
        }
    }
    if(focus_changed) *focus_changed=previous!=focused_pid;
    return top!=0;
}

bool window_manager_has_focus(void){
    return !registry_suspended && focused_pid!=0;
}

void window_manager_set_suspended(bool suspended){
    registry_suspended=suspended;
}

void window_manager_request_repaint(uint32_t excluded_pid){
    repainting_pid=0;
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++){
        windows[index].repaint_pending=windows[index].used
            && windows[index].pid!=excluded_pid;
    }
}

bool window_manager_repaint_pending(void){
    if(repainting_pid) return true;
    return next_repaint_window()!=0;
}

void window_manager_cancel_repaint(void){
    repainting_pid=0;
    for(uint32_t index=0;index<WINDOW_MANAGER_CAPACITY;index++)
        windows[index].repaint_pending=false;
}
