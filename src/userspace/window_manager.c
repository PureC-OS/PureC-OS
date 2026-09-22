#include "window_manager.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../kernel/diagnostics/klog.h"

struct managed_window {
    uint32_t pid;
    struct gui_window_request frame;
    uint32_t z_order;
    bool used;
    bool repaint_pending;
};

struct wm_chunk {
    struct wm_chunk *next;
};

#define WM_PER_PAGE (4096u/sizeof(struct managed_window))

static struct wm_chunk *wm_chunks;
static uint32_t wm_used_count;
static uint32_t focused_pid;
static uint32_t next_z_order=1;
static uint32_t repainting_pid;
static bool registry_suspended;

static struct managed_window *wm_slot_at(uint32_t n){
    for(struct wm_chunk *c=wm_chunks;c;c=c->next){
        struct managed_window *slots=(struct managed_window*)(c+1);
        if(n<WM_PER_PAGE) return &slots[n];
        n-=WM_PER_PAGE;
    }
    return 0;
}

static uint32_t wm_slot_total(void){
    uint32_t total=0;
    for(struct wm_chunk *c=wm_chunks;c;c=c->next) total+=WM_PER_PAGE;
    return total;
}

static struct managed_window *wm_alloc_slot(void){
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *s=wm_slot_at(n);
        if(s && !s->used){
            memset(s,0,sizeof(*s));
            s->used=true;
            wm_used_count++;
            return s;
        }
    }
    uint64_t phys=pmm_allocate_page();
    if(!phys) return 0;
    struct wm_chunk *c=(struct wm_chunk*)pmm_physical_to_virtual(phys);
    memset(c,0,4096);
    c->next=wm_chunks;
    wm_chunks=c;
    struct managed_window *s=(struct managed_window*)(c+1);
    s->used=true;
    wm_used_count++;
    return s;
}

static void wm_free_slot(struct managed_window *s){
    if(!s || !s->used) return;
    memset(s,0,sizeof(*s));
    wm_used_count--;
}

static struct managed_window *find_window(uint32_t pid){
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *s=wm_slot_at(n);
        if(s && s->used && s->pid==pid) return s;
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
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *window=wm_slot_at(n);
        if(!window || !window->used || !point_inside(x,y,&window->frame)) continue;
        if(!top || window->z_order>top->z_order) top=window;
    }
    return top;
}

static struct managed_window *next_repaint_window(void){
    struct managed_window *next=0;
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *window=wm_slot_at(n);
        if(!window || !window->used || !window->repaint_pending) continue;
        if(!next || window->z_order<next->z_order) next=window;
    }
    return next;
}

bool window_manager_register(uint32_t pid,
                             const struct gui_window_request *request){
    if(!pid || !request || !request->width || !request->height) return false;
    struct managed_window *window=find_window(pid);
    if(!window) window=wm_alloc_slot();
    if(!window) return false;
    window->pid=pid;
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
    if(!window){
        klogf(KLOG_DEBUG,"DIAG wm-reg: unregister pid=%u (not registered)",pid);
        return;
    }
    if(repainting_pid==pid) repainting_pid=0;
    wm_free_slot(window);
    if(focused_pid!=pid){
        klogf(KLOG_DEBUG,"DIAG wm-reg: unregister pid=%u focused=%u left=%u",
              pid,focused_pid,wm_used_count);
        return;
    }
    focused_pid=0;
    uint32_t best_z=0;
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *s=wm_slot_at(n);
        if(!s || !s->used) continue;
        if(!focused_pid || s->z_order>best_z){
            focused_pid=s->pid;
            best_z=s->z_order;
        }
    }
    klogf(KLOG_DEBUG,"DIAG wm-reg: unregister pid=%u focused now=%u left=%u",
          pid,focused_pid,wm_used_count);
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

uint32_t window_manager_focused_pid(void){
    return registry_suspended ? 0 : focused_pid;
}

bool window_manager_focus_pid(uint32_t pid){
    if(registry_suspended || !pid) return false;
    struct managed_window *w = find_window(pid);
    if(!w) return false;
    focused_pid = pid;
    w->z_order = next_z_order++;
    return true;
}

uint32_t window_manager_list(uint32_t *pids,
                             struct gui_window_request *frames,
                             uint32_t capacity){
    uint32_t count = 0;
    uint32_t last_z = 0;
    for(;;){
        struct managed_window *best = 0;
        uint32_t total=wm_slot_total();
        for(uint32_t n=0;n<total;n++){
            struct managed_window *w=wm_slot_at(n);
            if(!w || !w->used || w->z_order<=last_z) continue;
            if(!best || w->z_order<best->z_order) best=w;
        }
        if(!best) break;
        if(count<capacity){
            if(pids) pids[count]=best->pid;
            if(frames) frames[count]=best->frame;
        }
        count++;
        last_z=best->z_order;
    }
    return count;
}

void window_manager_set_suspended(bool suspended){
    registry_suspended=suspended;
}

void window_manager_request_repaint(uint32_t excluded_pid){
    repainting_pid=0;
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *s=wm_slot_at(n);
        if(s) s->repaint_pending=s->used && s->pid!=excluded_pid;
    }
}

bool window_manager_repaint_pending(void){
    if(repainting_pid) return true;
    return next_repaint_window()!=0;
}

void window_manager_cancel_repaint(void){
    repainting_pid=0;
    uint32_t total=wm_slot_total();
    for(uint32_t n=0;n<total;n++){
        struct managed_window *s=wm_slot_at(n);
        if(s) s->repaint_pending=false;
    }
}
