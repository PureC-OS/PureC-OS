#include "window_manager.h"
#include "../drivers/display/gop.h"
#include "../drivers/interrupts/timer.h"
#include "../gfx/compositor.h"
#include "../kernel/process/process.h"
#include "../kernel/process/scheduler.h"
#include "../kernel/sync/spinlock.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

#define WM_DOUBLE_CLICK_TICKS 400
#define WM_DRAG_THRESHOLD 4

enum wm_zone {
    ZONE_NONE,
    ZONE_CLIENT,
    ZONE_TITLE,
    ZONE_CLOSE,
    ZONE_MINIMIZE,
    ZONE_MAXIMIZE
};

struct managed_window {
    uint32_t pid;
    struct gui_window_request frame;
    struct gui_window_request restore;
    uint32_t z_order;
    uint32_t flags;
    uint32_t generation;
    uint32_t sequence;
    uint32_t previous_focus;
    bool used;
    bool repaint_pending;
    bool minimized;
    bool maximized;
    bool close_requested;
};

struct wm_chunk {
    struct wm_chunk *next;
};

#define WM_PER_PAGE ((4096u - sizeof(struct wm_chunk)) / sizeof(struct managed_window))

static struct wm_chunk *wm_chunks;
static uint32_t focused_pid;
static uint32_t next_z_order=1;
static uint32_t next_sequence=1;
static uint32_t repainting_pid;
static bool registry_suspended;
static bool server_mode;
static spinlock_t wm_lock=SPINLOCK_INIT;

static int32_t work_x;
static int32_t work_y;
static uint32_t work_w;
static uint32_t work_h;

static uint8_t previous_buttons;
static uint32_t press_pid;
static enum wm_zone press_zone;
static uint32_t drag_pid;
static bool drag_moved;
static int32_t drag_start_x;
static int32_t drag_start_y;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static uint32_t click_pid;
static uint64_t click_tick;

struct wm_guard {
    bool preempt;
};

static struct wm_guard wm_enter(void){
    struct wm_guard guard={scheduler_preempt_disable()};
    spin_lock(&wm_lock);
    return guard;
}

static void wm_leave(struct wm_guard guard){
    spin_unlock(&wm_lock);
    if(guard.preempt) scheduler_preempt_enable();
}

static struct managed_window *wm_slot_at(uint32_t index){
    for(struct wm_chunk *chunk=wm_chunks;chunk;chunk=chunk->next){
        struct managed_window *slots=(struct managed_window *)(chunk+1);
        if(index<WM_PER_PAGE) return &slots[index];
        index-=WM_PER_PAGE;
    }
    return 0;
}

static uint32_t wm_slot_total(void){
    uint32_t total=0;
    for(struct wm_chunk *chunk=wm_chunks;chunk;chunk=chunk->next)
        total+=WM_PER_PAGE;
    return total;
}

static struct managed_window *wm_alloc_slot(void){
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *slot=wm_slot_at(index);
        if(slot && !slot->used){
            memset(slot,0,sizeof(*slot));
            slot->used=true;
            return slot;
        }
    }
    uint64_t physical=pmm_allocate_page();
    if(!physical) return 0;
    struct wm_chunk *chunk=(struct wm_chunk *)pmm_physical_to_virtual(physical);
    memset(chunk,0,4096);
    chunk->next=wm_chunks;
    wm_chunks=chunk;
    struct managed_window *slot=(struct managed_window *)(chunk+1);
    slot->used=true;
    return slot;
}

static struct managed_window *find_window(uint32_t pid){
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *slot=wm_slot_at(index);
        if(slot && slot->used && slot->pid==pid)
            return slot;
    }
    return 0;
}

static uint32_t visible_height(const struct managed_window *window){
    return window->minimized
        ? WM_TITLEBAR_HEIGHT+WM_BORDER*2 : window->frame.height;
}

static struct gui_window_request visible_frame(
        const struct managed_window *window){
    struct gui_window_request frame=window->frame;
    frame.height=visible_height(window);
    return frame;
}

static bool point_inside(int32_t x, int32_t y,
                         const struct gui_window_request *frame){
    return frame && x>=(int32_t)frame->x && y>=(int32_t)frame->y
        && x<(int32_t)(frame->x+frame->width)
        && y<(int32_t)(frame->y+frame->height);
}

static struct managed_window *top_window_at(int32_t x, int32_t y){
    struct managed_window *top=0;
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *window=wm_slot_at(index);
        if(!window || !window->used) continue;
        struct gui_window_request frame=visible_frame(window);
        if(!point_inside(x,y,&frame)) continue;
        if(!top || window->z_order>top->z_order) top=window;
    }
    return top;
}

static struct managed_window *next_repaint_window(void){
    struct managed_window *next=0;
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *window=wm_slot_at(index);
        if(!window || !window->used || !window->repaint_pending) continue;
        if(!next || window->z_order<next->z_order) next=window;
    }
    return next;
}

static void push_frame(struct managed_window *window){
    (void)compositor_window_update(window->pid,window->frame.x,
                                   window->frame.y,window->frame.width,
                                   visible_height(window));
    window->generation++;
}

static void ensure_work_area(void){
    if(work_w && work_h) return;
    work_x=0;
    work_y=0;
    work_w=gop_get_width();
    work_h=gop_get_height();
}

static void maximize_window(struct managed_window *window){
    if(!window || window->maximized || (window->flags&GUI_WINDOW_FLAG_FIXED))
        return;
    ensure_work_area();
    if(!work_w || !work_h) return;
    window->restore=window->frame;
    window->minimized=false;
    window->maximized=true;
    window->frame.x=(uint32_t)work_x;
    window->frame.y=(uint32_t)work_y;
    window->frame.width=work_w;
    window->frame.height=work_h;
    push_frame(window);
}

static void unmaximize_window(struct managed_window *window){
    if(!window || !window->maximized) return;
    window->maximized=false;
    window->frame=window->restore;
    push_frame(window);
}

static void set_minimized(struct managed_window *window, bool minimized){
    if(!window || window->minimized==minimized) return;
    if(minimized && (window->flags&GUI_WINDOW_FLAG_FIXED)) return;
    window->minimized=minimized;
    push_frame(window);
}

static enum wm_zone zone_at(const struct managed_window *window,
                            int32_t x, int32_t y){
    struct gui_window_request frame=visible_frame(window);
    if(!point_inside(x,y,&frame)) return ZONE_NONE;
    if(window->flags&GUI_WINDOW_FLAG_POPUP) return ZONE_CLIENT;
    int32_t relative_x=x-(int32_t)frame.x;
    int32_t relative_y=y-(int32_t)frame.y;
    if(relative_y>=WM_TITLEBAR_HEIGHT+WM_BORDER*2) return ZONE_CLIENT;
    if(relative_y>=WM_BUTTON_Y && relative_y<WM_BUTTON_Y+WM_BUTTON_SIZE){
        int32_t from_right=(int32_t)frame.width-relative_x;
        if(from_right<=WM_CLOSE_OFFSET
           && from_right>WM_CLOSE_OFFSET-WM_BUTTON_SIZE)
            return ZONE_CLOSE;
        if(from_right<=WM_MINIMIZE_OFFSET
           && from_right>WM_MINIMIZE_OFFSET-WM_BUTTON_SIZE)
            return ZONE_MINIMIZE;
        if(from_right<=WM_MAXIMIZE_OFFSET
           && from_right>WM_MAXIMIZE_OFFSET-WM_BUTTON_SIZE)
            return ZONE_MAXIMIZE;
    }
    return ZONE_TITLE;
}

static void toggle_maximize(struct managed_window *window){
    if(window->maximized) unmaximize_window(window);
    else maximize_window(window);
}

static void reset_pointer_state(uint32_t pid){
    if(press_pid==pid){
        press_pid=0;
        press_zone=ZONE_NONE;
    }
    if(drag_pid==pid) drag_pid=0;
    if(click_pid==pid) click_pid=0;
}

static void drag_window(struct managed_window *window, int32_t x, int32_t y){
    if(window->maximized && !drag_moved){
        int32_t moved_x=x-drag_start_x;
        int32_t moved_y=y-drag_start_y;
        if(moved_x<0) moved_x=-moved_x;
        if(moved_y<0) moved_y=-moved_y;
        if(moved_x<WM_DRAG_THRESHOLD && moved_y<WM_DRAG_THRESHOLD) return;
        uint32_t old_width=window->frame.width;
        int32_t old_x=(int32_t)window->frame.x;
        unmaximize_window(window);
        if(old_width)
            drag_offset_x=(int32_t)((int64_t)(x-old_x)
                                    *window->frame.width/old_width);
    }
    drag_moved=true;
    int32_t screen_width=(int32_t)gop_get_width();
    int32_t screen_height=(int32_t)gop_get_height();
    int32_t next_x=x-drag_offset_x;
    int32_t next_y=y-drag_offset_y;
    int32_t max_x=screen_width-(int32_t)window->frame.width;
    int32_t max_y=screen_height-(WM_TITLEBAR_HEIGHT+WM_BORDER*2);
    ensure_work_area();
    if(max_x<0) max_x=0;
    if(next_x<0) next_x=0;
    if(next_x>max_x) next_x=max_x;
    if(next_y<work_y) next_y=work_y;
    if(next_y>max_y) next_y=max_y;
    if(next_y<0) next_y=0;
    if((uint32_t)next_x==window->frame.x && (uint32_t)next_y==window->frame.y)
        return;
    window->frame.x=(uint32_t)next_x;
    window->frame.y=(uint32_t)next_y;
    push_frame(window);
}

static void finish_press(int32_t x, int32_t y){
    struct managed_window *window=press_pid ? find_window(press_pid) : 0;
    if(window){
        enum wm_zone zone=zone_at(window,x,y);
        if(zone==press_zone){
            if(zone==ZONE_CLOSE){
                if(!(window->flags&GUI_WINDOW_FLAG_NOCLOSE))
                    window->close_requested=true;
            } else if(zone==ZONE_MINIMIZE){
                set_minimized(window,!window->minimized);
            } else if(zone==ZONE_MAXIMIZE){
                toggle_maximize(window);
            } else if(zone==ZONE_TITLE && !drag_moved){
                uint64_t now=timer_ticks();
                if(click_pid==window->pid
                   && now-click_tick<WM_DOUBLE_CLICK_TICKS){
                    toggle_maximize(window);
                    click_pid=0;
                } else {
                    click_pid=window->pid;
                    click_tick=now;
                }
            }
        }
    }
    press_pid=0;
    press_zone=ZONE_NONE;
    drag_pid=0;
    drag_moved=false;
}

static bool register_locked(uint32_t pid,
                            const struct gui_window_request *request){
    if(!pid || !request || !request->width || !request->height) return false;
    struct managed_window *window=find_window(pid);
    if(!window){
        window=wm_alloc_slot();
    }
    if(!window) return false;
    window->pid=pid;
    window->frame=*request;
    window->restore=*request;
    window->minimized=false;
    window->maximized=false;
    window->close_requested=false;
    window->flags=0;
    window->generation=0;
    window->sequence=next_sequence++;
    window->previous_focus=focused_pid;
    window->z_order=next_z_order++;
    focused_pid=pid;
    (void)compositor_window_create(pid,request->x,request->y,
                                   request->width,request->height);
    compositor_window_raise(pid);
    return true;
}

bool window_manager_register(uint32_t pid,
                             const struct gui_window_request *request){
    struct wm_guard guard=wm_enter();
    bool result=register_locked(pid,request);
    wm_leave(guard);
    return result;
}

bool window_manager_update(uint32_t pid,
                           const struct gui_window_request *request){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=find_window(pid);
    bool result=false;
    if(window && request && request->width && request->height){
        result=true;
        if(!(server_mode && window->maximized)){
            window->frame=*request;
            if(server_mode){
                (void)compositor_window_update(pid,request->x,request->y,
                                               request->width,
                                               visible_height(window));
            } else {
                (void)compositor_window_update(pid,request->x,request->y,
                                               request->width,
                                               request->height);
            }
        }
    }
    wm_leave(guard);
    return result;
}

void window_manager_unregister(uint32_t pid){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=find_window(pid);
    if(!window){
        wm_leave(guard);
        return;
    }
    if(repainting_pid==pid) repainting_pid=0;
    reset_pointer_state(pid);
    memset(window,0,sizeof(*window));
    compositor_window_destroy(pid);
    if(focused_pid==pid){
        focused_pid=0;
        uint32_t total=wm_slot_total();
        uint32_t best_z=0;
        for(uint32_t index=0;index<total;index++){
            struct managed_window *slot=wm_slot_at(index);
            if(slot && slot->used && slot->z_order>=best_z){
                best_z=slot->z_order;
                focused_pid=slot->pid;
            }
        }
    }
    wm_leave(guard);
}

uint32_t window_manager_state(uint32_t pid){
    struct wm_guard guard=wm_enter();
    uint32_t state=0;
    struct managed_window *window=registry_suspended ? 0 : find_window(pid);
    if(window){
        if(focused_pid==pid) state|=GUI_WINDOW_STATE_FOCUSED;
        if(repainting_pid==pid){
            state|=GUI_WINDOW_STATE_REPAINT;
        } else if(!repainting_pid){
            struct managed_window *next=next_repaint_window();
            if(next && next->pid==pid){
                repainting_pid=pid;
                state|=GUI_WINDOW_STATE_REPAINT;
            }
        }
        if(server_mode){
            state|=GUI_WINDOW_STATE_SERVER;
            if(window->close_requested) state|=GUI_WINDOW_STATE_CLOSE;
            if(drag_pid==pid) state|=GUI_WINDOW_STATE_DRAGGING;
            state|=(window->generation&0xFFFFFFu)<<GUI_WINDOW_STATE_GEN_SHIFT;
        }
    }
    wm_leave(guard);
    return state;
}

void window_manager_finish_repaint(uint32_t pid){
    struct wm_guard guard=wm_enter();
    if(repainting_pid==pid){
        struct managed_window *window=find_window(pid);
        if(window) window->repaint_pending=false;
        repainting_pid=0;
    }
    wm_leave(guard);
}

bool window_manager_handle_pointer(int32_t x, int32_t y, bool pressed,
                                   bool *focus_changed){
    struct wm_guard guard=wm_enter();
    if(registry_suspended){
        if(focus_changed) *focus_changed=false;
        wm_leave(guard);
        return false;
    }
    struct managed_window *top=top_window_at(x,y);
    uint32_t previous=focused_pid;
    if(pressed){
        if(top){
            focused_pid=top->pid;
            top->z_order=next_z_order++;
            compositor_window_raise(top->pid);
        } else {
            focused_pid=0;
        }
    }
    if(focus_changed) *focus_changed=previous!=focused_pid;
    bool consumed=top!=0;
    wm_leave(guard);
    return consumed;
}

uint32_t window_manager_pointer(int32_t x, int32_t y, uint8_t buttons,
                                bool *focus_changed){
    struct wm_guard guard=wm_enter();
    bool changed=false;
    bool consumed=false;
    bool left=(buttons&1)!=0;
    bool pressed=left && !(previous_buttons&1);
    bool released=!left && (previous_buttons&1);
    previous_buttons=buttons;
    if(registry_suspended){
        press_pid=0;
        drag_pid=0;
        if(focus_changed) *focus_changed=false;
        wm_leave(guard);
        return 0;
    }
    struct managed_window *top=top_window_at(x,y);
    bool over_popup=top && (top->flags&GUI_WINDOW_FLAG_POPUP);
    if(over_popup) top=0;
    if(pressed && over_popup){
        press_pid=0;
        press_zone=ZONE_NONE;
    } else if(pressed){
        uint32_t previous=focused_pid;
        if(top){
            focused_pid=top->pid;
            top->z_order=next_z_order++;
            compositor_window_raise(top->pid);
            press_pid=top->pid;
            press_zone=zone_at(top,x,y);
            drag_moved=false;
            if(press_zone==ZONE_TITLE){
                drag_pid=top->pid;
                drag_start_x=x;
                drag_start_y=y;
                drag_offset_x=x-(int32_t)top->frame.x;
                drag_offset_y=y-(int32_t)top->frame.y;
            }
        } else {
            focused_pid=0;
            press_pid=0;
            press_zone=ZONE_NONE;
        }
        changed=previous!=focused_pid;
    } else if(left && drag_pid){
        struct managed_window *window=find_window(drag_pid);
        if(window) drag_window(window,x,y);
        else drag_pid=0;
    }
    consumed=top!=0 || press_pid!=0 || drag_pid!=0;
    if(released) finish_press(x,y);
    if(focus_changed) *focus_changed=changed;
    wm_leave(guard);
    return consumed ? 1u : 0u;
}

bool window_manager_has_focus(void){
    struct wm_guard guard=wm_enter();
    bool result=!registry_suspended && focused_pid!=0;
    wm_leave(guard);
    return result;
}

uint32_t window_manager_focused_pid(void){
    struct wm_guard guard=wm_enter();
    uint32_t result=registry_suspended ? 0 : focused_pid;
    wm_leave(guard);
    return result;
}

bool window_manager_focus_pid(uint32_t pid){
    struct wm_guard guard=wm_enter();
    bool result=false;
    struct managed_window *window=(registry_suspended || !pid)
        ? 0 : find_window(pid);
    if(window){
        focused_pid=pid;
        window->z_order=next_z_order++;
        compositor_window_raise(pid);
        result=true;
    }
    wm_leave(guard);
    return result;
}

uint32_t window_manager_list(uint32_t *pids,
                             struct gui_window_request *frames,
                             uint32_t capacity){
    struct wm_guard guard=wm_enter();
    uint32_t count=0;
    uint32_t last_z=0;
    for(;;){
        struct managed_window *best=0;
        uint32_t total=wm_slot_total();
        for(uint32_t index=0;index<total;index++){
            struct managed_window *window=wm_slot_at(index);
            if(!window || !window->used || window->z_order<=last_z)
                continue;
            if(!best || window->z_order<best->z_order)
                best=window;
        }
        if(!best) break;
        if(count<capacity){
            if(pids) pids[count]=best->pid;
            if(frames) frames[count]=best->frame;
        }
        count++;
        last_z=best->z_order;
    }
    wm_leave(guard);
    return count;
}

void window_manager_set_suspended(bool suspended){
    struct wm_guard guard=wm_enter();
    registry_suspended=suspended;
    wm_leave(guard);
}

void window_manager_request_repaint(uint32_t excluded_pid){
    struct wm_guard guard=wm_enter();
    repainting_pid=0;
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *window=wm_slot_at(index);
        if(window)
            window->repaint_pending=window->used && window->pid!=excluded_pid
                && !compositor_window_exists(window->pid);
    }
    wm_leave(guard);
}

bool window_manager_repaint_pending(void){
    struct wm_guard guard=wm_enter();
    bool result=repainting_pid!=0 || next_repaint_window()!=0;
    wm_leave(guard);
    return result;
}

void window_manager_cancel_repaint(void){
    struct wm_guard guard=wm_enter();
    repainting_pid=0;
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *window=wm_slot_at(index);
        if(window) window->repaint_pending=false;
    }
    wm_leave(guard);
}

void window_manager_set_server_mode(bool enabled){
    struct wm_guard guard=wm_enter();
    server_mode=enabled;
    wm_leave(guard);
}

bool window_manager_server_mode(void){
    return server_mode;
}

void window_manager_set_work_area(int32_t x, int32_t y, uint32_t width,
                                  uint32_t height){
    struct wm_guard guard=wm_enter();
    work_x=x<0 ? 0 : x;
    work_y=y<0 ? 0 : y;
    work_w=width;
    work_h=height;
    uint32_t total=wm_slot_total();
    for(uint32_t index=0;index<total;index++){
        struct managed_window *window=wm_slot_at(index);
        if(!window || !window->used || !window->maximized) continue;
        window->frame.x=(uint32_t)work_x;
        window->frame.y=(uint32_t)work_y;
        window->frame.width=work_w;
        window->frame.height=work_h;
        push_frame(window);
    }
    wm_leave(guard);
}

bool window_manager_info(uint32_t pid, struct gui_window_info *out){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=out ? find_window(pid) : 0;
    if(window){
        out->x=window->frame.x;
        out->y=window->frame.y;
        out->width=window->frame.width;
        out->height=window->frame.height;
        out->flags=(window->minimized ? GUI_WINDOW_INFO_MINIMIZED : 0)
            | (window->maximized ? GUI_WINDOW_INFO_MAXIMIZED : 0);
    }
    wm_leave(guard);
    return window!=0;
}

bool window_manager_set_flags(uint32_t pid, uint32_t flags){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=find_window(pid);
    if(window){
        bool was_popup=(window->flags&GUI_WINDOW_FLAG_POPUP)!=0;
        window->flags=flags;
        if((flags&GUI_WINDOW_FLAG_POPUP) && !was_popup){
            if(focused_pid==pid)
                focused_pid=find_window(window->previous_focus)
                    ? window->previous_focus : 0;
            compositor_window_set_topmost(pid,true);
        } else if(!(flags&GUI_WINDOW_FLAG_POPUP) && was_popup){
            compositor_window_set_topmost(pid,false);
        }
    }
    wm_leave(guard);
    return window!=0;
}

bool window_manager_command(uint32_t pid, uint32_t command){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=find_window(pid);
    if(window){
        if(command==GUI_WINDOW_CMD_MINIMIZE) set_minimized(window,true);
        else if(command==GUI_WINDOW_CMD_RESTORE) set_minimized(window,false);
        else if(command==GUI_WINDOW_CMD_MAXIMIZE) maximize_window(window);
        else if(command==GUI_WINDOW_CMD_UNMAXIMIZE) unmaximize_window(window);
        else window=0;
    }
    wm_leave(guard);
    return window!=0;
}

uint32_t window_manager_tasks(struct gui_task_info *out, uint32_t capacity){
    struct wm_guard guard=wm_enter();
    uint32_t count=0;
    uint32_t last=0;
    for(;;){
        struct managed_window *best=0;
        uint32_t total=wm_slot_total();
        for(uint32_t index=0;index<total;index++){
            struct managed_window *window=wm_slot_at(index);
            if(!window || !window->used || window->sequence<=last
               || (window->flags&GUI_WINDOW_FLAG_POPUP))
                continue;
            if(!best || window->sequence<best->sequence) best=window;
        }
        if(!best) break;
        last=best->sequence;
        if(out && count<capacity){
            struct gui_task_info *task=&out[count];
            memset(task,0,sizeof(*task));
            task->pid=best->pid;
            task->flags=(best->minimized ? GUI_TASK_MINIMIZED : 0)
                | (focused_pid==best->pid ? GUI_TASK_FOCUSED : 0);
            struct process *process=process_lookup_by_pid(best->pid);
            if(process){
                const char *name=process->name;
                const char *base=name;
                for(const char *p=name;*p;p++)
                    if(*p=='/') base=p+1;
                uint32_t i=0;
                for(;base[i] && i+1<sizeof(task->name);i++)
                    task->name[i]=base[i];
                task->name[i]='\0';
            }
        }
        count++;
    }
    wm_leave(guard);
    return count;
}

bool window_manager_activate(uint32_t pid){
    struct wm_guard guard=wm_enter();
    struct managed_window *window=registry_suspended ? 0 : find_window(pid);
    if(window && (window->flags&GUI_WINDOW_FLAG_POPUP)) window=0;
    if(window){
        if(window->minimized) set_minimized(window,false);
        focused_pid=pid;
        window->z_order=next_z_order++;
        compositor_window_raise(pid);
    }
    wm_leave(guard);
    return window!=0;
}
