#include "gop.h"
#include "vga.h"
#include "svga.h"
#include "../../gfx/text.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include "../../kernel/process/process.h"
#include "../../kernel/sync/spinlock.h"
#include "../../gfx/compositor.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/tty/tty.h"
#include "console_vt.h"
#include <stdint.h>
#include <stddef.h>

static struct gop_state gop = {0};

#define GOP_MAX_PHYS_W 8192
static uint32_t scale_num = 1, scale_den = 1;
static uint32_t phys_w, phys_h;
static volatile bool front_user_owned;
static volatile uint32_t front_generation;
static uint32_t xmap_tab[GOP_MAX_PHYS_W];
static uint32_t line_tab[GOP_MAX_PHYS_W];

static bool scale_active(void){ return scale_num != scale_den; }

static void scale_rebuild(void){
    uint32_t n = phys_w < GOP_MAX_PHYS_W ? phys_w : GOP_MAX_PHYS_W;
    for(uint32_t p=0;p<n;p++) xmap_tab[p]=(uint32_t)((uint64_t)p*scale_den/scale_num);
}

static uint32_t scale_up(uint32_t v){
    return (uint32_t)(((uint64_t)v*scale_num+scale_den-1)/scale_den);
}

static void write_scaled_span(uint32_t ly, uint32_t lx0, const uint32_t *src,
                              uint32_t count){
    if(!gop.addr || !count || phys_w>GOP_MAX_PHYS_W) return;
    uint32_t py0=scale_up(ly), py1=scale_up(ly+1);
    uint32_t px0=scale_up(lx0), px1=scale_up(lx0+count);
    if(py1>phys_h) py1=phys_h;
    if(px1>phys_w) px1=phys_w;
    if(py0>=py1 || px0>=px1) return;
    for(uint32_t px=px0;px<px1;px++) line_tab[px]=src[xmap_tab[px]-lx0];
    for(uint32_t py=py0;py<py1;py++)
        memcpy(&gop.addr[(uint64_t)py*gop.pitch+px0], &line_tab[px0],
               (uint64_t)(px1-px0)*sizeof(uint32_t));
}

void gop_front_notify(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    if(front_user_owned) return;
    if(!svga_is_available() || !w || !h) return;
    if(!scale_active()){ svga_update(x,y,w,h); return; }
    uint32_t px0=scale_up(x), py0=scale_up(y);
    uint32_t px1=scale_up(x+w), py1=scale_up(y+h);
    if(px1>phys_w) px1=phys_w;
    if(py1>phys_h) py1=phys_h;
    if(px0<px1 && py0<py1) svga_update(px0,py0,px1-px0,py1-py0);
}

static uint32_t *backbuffer = 0;
static uint64_t backbuffer_phys = 0;
static uint64_t backbuffer_pages = 0;
static uint32_t backbuffer_width = 0;
static uint32_t backbuffer_height = 0;
static uint32_t batch_depth = 0;
static uint32_t compose_depth = 0;
static bool dirty_valid = false;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
static uint32_t cur_x=12, cur_y=12;
static uint32_t fg=0xCDD6F4, bg=0x000000;
#define GOP_PRESENT_MIN_INTERVAL_TICKS 16
#define GOP_DEFERRED_FLUSH_TICKS 50
static uint64_t last_present_tick = 0;
static bool present_deferred = false;
static uint64_t deferred_tick = 0;

static enum gop_font_face console_face=GOP_FONT_CLEAN;

static gfx_font_face_t console_gfx_face(void){
    if(console_face==GOP_FONT_BOLD) return GFX_FONT_BOLD;
    if(console_face==GOP_FONT_CLASSIC) return GFX_FONT_CLASSIC;
    return GFX_FONT_CLEAN;
}

#define GOP_CONSOLE_COLUMNS VT_MAX_COLUMNS
#define GOP_CONSOLE_ROWS VT_MAX_ROWS
#define GOP_CONSOLE_SLOTS 8

static uint32_t console_font_px = 8;

struct gop_console {
    uint32_t cell_w;
    uint32_t cell_h;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t columns;
    uint32_t rows;
    uint32_t foreground;
    uint32_t background;
    struct vt_term vt;
    bool initialized;
    bool active;
    uint32_t owner_pid;
};

static struct gop_console user_consoles[GOP_CONSOLE_SLOTS];
static spinlock_t console_lock = SPINLOCK_INIT;

#define CONSOLE_UNLOCK(flags_var, guard_var) \
    do{ \
        spin_unlock_irqrestore(&console_lock,(flags_var)); \
        if(guard_var) scheduler_preempt_enable(); \
    }while(0)

static struct gop_console *console_find_locked(uint32_t owner_pid){
    for(uint32_t i=0;i<GOP_CONSOLE_SLOTS;i++){
        if(user_consoles[i].owner_pid==owner_pid
           && (user_consoles[i].active || user_consoles[i].initialized))
            return &user_consoles[i];
    }
    return NULL;
}

static struct gop_console *console_alloc_locked(uint32_t owner_pid){
    struct gop_console *found=console_find_locked(owner_pid);
    if(found) return found;
    for(uint32_t i=0;i<GOP_CONSOLE_SLOTS;i++){
        if(user_consoles[i].owner_pid==0 && !user_consoles[i].active
           && !user_consoles[i].initialized){
            user_consoles[i].owner_pid=owner_pid;
            return &user_consoles[i];
        }
    }
    return NULL;
}

#define GOP_CONSOLE_CHAIN 8

static uint32_t console_chain(uint32_t pid, uint32_t *out, uint32_t cap){
    uint32_t n=0;
    uint32_t cur=pid;
    while(n<cap){
        out[n++]=cur;
        int32_t parent=process_parent_pid(cur);
        if(parent<=0) break;
        cur=(uint32_t)parent;
    }
    return n;
}

static struct gop_console *console_pick_locked(const uint32_t *chain, uint32_t depth){
    for(uint32_t i=0;i<depth;i++){
        struct gop_console *slot=console_find_locked(chain[i]);
        if(slot && slot->active) return slot;
    }
    return NULL;
}

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t c);
static void gop_gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t color, void *ctx){
    struct gop_console *slot=(struct gop_console*)ctx;
    if(slot && slot->owner_pid
       && compositor_window_exists(slot->owner_pid)){
        compositor_draw_rect(slot->owner_pid, x, y, w, h, color);
        return;
    }
    for(uint32_t dy=0;dy<h;dy++)
        for(uint32_t dx=0;dx<w;dx++)
            put_pixel(x+dx, y+dy, color);
}

void gop_init_from_limine(struct limine_framebuffer *fb, uint64_t firmware_type){
    if(!fb) { gop.available=false; return; }
    gop.addr = (uint32_t*)fb->address;
    gop.width = fb->width;
    gop.height = fb->height;
    phys_w = fb->width;
    phys_h = fb->height;
    scale_num = scale_den = 1;
    if(fb->bpp==24) gop.pitch = fb->pitch / 3;
    else if(fb->bpp==16) gop.pitch = fb->pitch / 2;
    else gop.pitch = fb->pitch / 4;
    gop.framebuffer_bytes=fb->pitch*fb->height;
    if(firmware_type==LIMINE_FIRMWARE_TYPE_UEFI32
       || firmware_type==LIMINE_FIRMWARE_TYPE_UEFI64){
        gop.protocol_name="UEFI GOP via Limine";
    } else if(firmware_type==LIMINE_FIRMWARE_TYPE_X86BIOS){
        gop.protocol_name="BIOS framebuffer via Limine";
    } else {
        gop.protocol_name="Limine framebuffer";
    }
    gop.bpp = fb->bpp;
    gop.available = true;
    cur_x=12; cur_y=12; fg=0xCDD6F4; bg=0x000000;
    if(backbuffer
       && (backbuffer_width!=gop.width || backbuffer_height!=gop.height)){
        pmm_free_contiguous(backbuffer_phys, backbuffer_pages);
        backbuffer=0;
        backbuffer_phys=0;
        backbuffer_pages=0;
        backbuffer_width=0;
        backbuffer_height=0;
        dirty_valid=false;
        batch_depth=0;
        compose_depth=0;
    }
}

void gop_init_from_multiboot(void *mbi){
    if(!mbi){ gop.available=false; return; }
    uint32_t total = *(uint32_t*)mbi;
    if(total < 8 || total > 32768){ gop.available=false; return; }
    uint32_t reserved = *((uint32_t*)mbi + 1);
    if(reserved != 0){ gop.available=false; return; }
    uint8_t *tag = (uint8_t*)mbi + 8;
    uint8_t *end = (uint8_t*)mbi + total;
    int cnt=0;
    while(tag + 8 <= end && cnt++ < 32){
        uint32_t type = *(uint32_t*)tag;
        uint32_t size = *(uint32_t*)(tag+4);
        if(size < 8 || tag + size > end) break;
        if(type==8 && size>=32){
            uint64_t addr = *(uint64_t*)(tag+8);
            uint32_t pitch = *(uint32_t*)(tag+16);
            uint32_t w = *(uint32_t*)(tag+20);
            uint32_t h = *(uint32_t*)(tag+24);
            uint8_t bpp = *(uint8_t*)(tag+28);
            if(w && h && addr && pitch){
                gop.addr = (uint32_t*)(uintptr_t)addr;
                gop.width = w; gop.height = h; gop.pitch = pitch/4; gop.bpp=bpp;
                phys_w = w; phys_h = h; scale_num = scale_den = 1;
                gop.framebuffer_bytes=(uint64_t)pitch*h;
                gop.protocol_name="Multiboot2 framebuffer";
                gop.available = true;
                cur_x=12; cur_y=12;
                return;
            }
        }
        if(type==0) break;
        uint32_t step = (size + 7) & ~7;
        if(step==0) break;
        tag += step;
    }
    gop.available=false;
}

static void front_write_32(uint32_t x, uint32_t y, uint32_t c);

static void surface_reset(void){
    if(backbuffer){
        pmm_free_contiguous(backbuffer_phys, backbuffer_pages);
        backbuffer=0;
        backbuffer_phys=0;
        backbuffer_pages=0;
        backbuffer_width=0;
        backbuffer_height=0;
    }
    dirty_valid=false;
    batch_depth=0;
    compose_depth=0;
    last_present_tick=0;
    present_deferred=false;
    for(uint32_t i=0;i<GOP_CONSOLE_SLOTS;i++){
        user_consoles[i].active=false;
        user_consoles[i].owner_pid=0;
        user_consoles[i].initialized=false;
    }
}

static void front_clear_physical(void){
    if(gop.bpp==32){
        for(uint32_t y=0;y<phys_h;y++){
            volatile uint32_t *line=&gop.addr[(uint64_t)y*gop.pitch];
            for(uint32_t x=0;x<phys_w;x++) line[x]=bg;
        }
    }else{
        for(uint32_t y=0;y<phys_h;y++)
            for(uint32_t x=0;x<phys_w;x++) front_write_32(x,y,bg);
    }
    if(svga_is_available()) svga_update(0,0,phys_w,phys_h);
}

static bool scale_fits(uint32_t num, uint32_t den){
    if(num==den) return true;
    if(gop.bpp!=32 || phys_w>GOP_MAX_PHYS_W) return false;
    uint64_t lw=(uint64_t)phys_w*den/num, lh=(uint64_t)phys_h*den/num;
    return lw>=640 && lh>=400;
}

static void apply_logical_size(void){
    gop.width=(uint32_t)((uint64_t)phys_w*scale_den/scale_num);
    gop.height=(uint32_t)((uint64_t)phys_h*scale_den/scale_num);
    scale_rebuild();
}

bool gop_apply_live(void *address, uint32_t width, uint32_t height,
                    uint32_t pitch_pixels, uint8_t bpp){
    if(!address || !width || !height || !pitch_pixels) return false;
    if(bpp!=16 && bpp!=24 && bpp!=32) return false;
    uint64_t pixels=(uint64_t)width*(uint64_t)height;
    if(!pixels || pixels>(64ULL*1024ULL*1024ULL)) return false;
    gop.addr=(uint32_t*)address;
    phys_w=width;
    phys_h=height;
    gop.pitch=pitch_pixels;
    gop.bpp=bpp;
    front_generation++;
    gop.framebuffer_bytes=(uint64_t)pitch_pixels
        *((uint64_t)(bpp==24 ? 3 : (bpp==16 ? 2 : 4)))*height;
    gop.protocol_name="VBE live mode";
    gop.available=true;
    if(!scale_fits(scale_num,scale_den)) scale_num=scale_den=1;
    apply_logical_size();
    cur_x=12; cur_y=12; fg=0xCDD6F4; bg=0x000000;
    front_clear_physical();
    surface_reset();
    return true;
}

void gop_retarget_front(void *address, uint32_t pitch_pixels){
    if(!address || !pitch_pixels) return;
    gop.addr=(uint32_t*)address;
    gop.pitch=pitch_pixels;
    front_generation++;
}

bool gop_user_owned(void){ return front_user_owned; }

void gop_kernel_reclaim(void){ front_user_owned=false; }

void gop_set_user_owned(bool owned){
    if(front_user_owned==owned) return;
    front_user_owned=owned;
    if(!owned && gop.available && backbuffer){
        dirty_expand(0,0,gop.width,gop.height);
        gop_present_forced();
        gop_front_notify(0,0,gop.width,gop.height);
    }
}

bool gop_front_physical(struct gop_front_info *out){
    if(!out || !gop.available || !gop.addr) return false;
    uint64_t virt=(uint64_t)(uintptr_t)gop.addr;
    uint64_t phys=pmm_virtual_to_physical(virt);
    if(!phys) phys=vmm_translate(vmm_kernel_address_space(), virt);
    if(!phys) return false;
    out->physical=phys;
    out->width=phys_w;
    out->height=phys_h;
    out->pitch_bytes=gop.pitch*(gop.bpp==24 ? 3U : (gop.bpp==16 ? 2U : 4U));
    out->bpp=gop.bpp;
    out->bytes=(uint64_t)out->pitch_bytes*phys_h;
    out->generation=front_generation;
    return true;
}

void gop_user_damage(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    if(!front_user_owned || !svga_is_available() || !w || !h) return;
    if(x>=phys_w || y>=phys_h) return;
    if(w>phys_w-x) w=phys_w-x;
    if(h>phys_h-y) h=phys_h-y;
    svga_update(x,y,w,h);
}

static uint32_t gcd32(uint32_t a, uint32_t b){
    while(b){ uint32_t t=a%b; a=b; b=t; }
    return a;
}

bool gop_set_scale_percent(uint32_t percent){
    if(!gop.available || !gop.addr || percent<100 || percent>400) return false;
    uint32_t g=gcd32(percent,100);
    uint32_t num=percent/g, den=100/g;
    if(num==den) num=den=1;
    if(!scale_fits(num,den)) return false;
    if(num==scale_num && den==scale_den) return true;
    scale_num=num;
    scale_den=den;
    apply_logical_size();
    cur_x=12; cur_y=12; fg=0xCDD6F4; bg=0x000000;
    front_clear_physical();
    surface_reset();
    return true;
}

uint32_t gop_get_scale_percent(void){
    return (uint32_t)((uint64_t)scale_num*100/scale_den);
}
uint32_t gop_get_physical_width(void){ return phys_w; }
uint32_t gop_get_physical_height(void){ return phys_h; }

bool gop_is_available(void){
    return gop.available;
}
void *gop_get_address(void){
    return gop.addr;
}
uint32_t gop_get_width(void){
    return gop.width;
}
uint32_t gop_get_height(void){
    return gop.height;
}
uint32_t gop_get_pitch(void){
    return gop.pitch;
}
uint8_t gop_get_bpp(void){
    return gop.bpp;
}
uint64_t gop_get_framebuffer_size_bytes(void){
    return gop.framebuffer_bytes;
}
const char *gop_get_protocol_name(void){
    return gop.protocol_name ? gop.protocol_name : "Unavailable";
}
void gop_set_font_face(enum gop_font_face face){
    console_face=face;
}
enum gop_font_face gop_get_font_face(void){
    return console_face;
}

static uint32_t front_read_32(uint32_t x, uint32_t y){
    if(!gop.available || !gop.addr || x>=gop.width || y>=gop.height) return 0;
    if(gop.bpp==24){
        uint8_t *base = (uint8_t*)gop.addr;
        uint32_t pitch_bytes = gop.pitch * 3;
        uint8_t *pixel = base + y * pitch_bytes + x * 3;
        return (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
    }
    if(gop.bpp==16){
        uint16_t *base16 = (uint16_t*)gop.addr;
        uint16_t v = base16[y * gop.pitch + x];
        uint32_t r = (v >> 11) & 0x1F;
        uint32_t gg = (v >> 5) & 0x3F;
        uint32_t b = v & 0x1F;
        r = (r << 3) | (r >> 2);
        gg = (gg << 2) | (gg >> 4);
        b = (b << 3) | (b >> 2);
        return (r << 16) | (gg << 8) | b;
    }
    return gop.addr[y*gop.pitch + x];
}

static void front_write_32(uint32_t x, uint32_t y, uint32_t c){
    if(!gop.available || !gop.addr) return;
    if(x>=gop.width || y>=gop.height) return;
    if(gop.bpp==24){
        uint8_t *base = (uint8_t*)gop.addr;
        uint32_t pitch_bytes = gop.pitch * 3;
        uint8_t *pixel = base + y * pitch_bytes + x * 3;
        pixel[0] = (uint8_t)(c & 0xFF);
        pixel[1] = (uint8_t)((c >> 8) & 0xFF);
        pixel[2] = (uint8_t)((c >> 16) & 0xFF);
        return;
    }
    if(gop.bpp==16){
        uint16_t r = (c >> 19) & 0x1F;
        uint16_t gg = (c >> 10) & 0x3F;
        uint16_t b = (c >> 3) & 0x1F;
        uint16_t v = (r << 11) | (gg << 5) | b;
        uint16_t *base16 = (uint16_t*)gop.addr;
        base16[y * gop.pitch + x] = v;
        return;
    }
    gop.addr[y*gop.pitch + x]=c;
}

static bool ensure_backbuffer(void){
    if(backbuffer) return true;
    if(!gop.available || !gop.addr || !gop.width || !gop.height) return false;
    if(!pmm_is_ready()) return false;
    uint64_t pixels = (uint64_t)gop.width * (uint64_t)gop.height;
    if(!pixels || pixels > (64ULL*1024ULL*1024ULL)) return false;
    uint64_t bytes = pixels * 4ULL;
    uint64_t pages = (bytes + 4095ULL) / 4096ULL;
    if(!pages || pages > 0x100000ULL) return false;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if(!phys) return false;
    uint32_t *virt = (uint32_t*)pmm_physical_to_virtual(phys);
    if(scale_active()){
        for(uint64_t i=0; i<pixels; i++) virt[i] = bg;
    }else{
        for(uint32_t y=0; y<gop.height; y++){
            for(uint32_t x=0; x<gop.width; x++){
                virt[(uint64_t)y * gop.width + x] = front_read_32(x, y);
            }
        }
    }
    backbuffer = virt;
    backbuffer_phys = phys;
    backbuffer_pages = pages;
    backbuffer_width = gop.width;
    backbuffer_height = gop.height;
    dirty_valid = false;
    return true;
}

static void dirty_expand(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    if(!backbuffer || !w || !h) return;
    if(x>=gop.width || y>=gop.height) return;
    uint32_t x1 = x + w;
    uint32_t y1 = y + h;
    if(x1 > gop.width) x1 = gop.width;
    if(y1 > gop.height) y1 = gop.height;
    if(x1<=x || y1<=y) return;
    if(!dirty_valid){
        dirty_x0=x; dirty_y0=y; dirty_x1=x1; dirty_y1=y1;
        dirty_valid=true;
        return;
    }
    if(x<dirty_x0) dirty_x0=x;
    if(y<dirty_y0) dirty_y0=y;
    if(x1>dirty_x1) dirty_x1=x1;
    if(y1>dirty_y1) dirty_y1=y1;
}

static inline void maybe_present(void){
    if(batch_depth==0 && compose_depth==0) gop_present();
}

void gop_begin_batch(void){
    batch_depth++;
}

void gop_end_batch(void){
    if(batch_depth){
        batch_depth--;
        if(batch_depth==0 && compose_depth==0) gop_present_forced();
    }
}

void gop_begin_compose(void){
    compose_depth++;
}

void gop_end_compose(void){
    if(compose_depth){
        compose_depth--;
        if(compose_depth==0 && batch_depth==0) gop_present();
    }
}

void gop_end_batch_keep(void){
    if(batch_depth) batch_depth--;
}

void gop_end_compose_keep(void){
    if(compose_depth) compose_depth--;
}

bool gop_dirty_pending(void){
    return dirty_valid;
}

void gop_cancel_compose(void){
    compose_depth=0;
    batch_depth=0;
}

bool gop_has_backbuffer(void){ return backbuffer!=0; }

static void gop_present_nolock(void);

void gop_present(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    bool preempt_guard=scheduler_preempt_disable();
    struct thread *thread=scheduler_current_thread();
    bool service_irqs=(flags&(1ULL<<9))
        || (preempt_guard && thread && thread->user_mode);
    if(!present_deferred){
        uint64_t now = timer_ticks();
        if(last_present_tick==0 || now-last_present_tick
            >= GOP_PRESENT_MIN_INTERVAL_TICKS){
            if(service_irqs) __asm__ volatile("sti":::"memory");
            gop_present_nolock();
            if(service_irqs) __asm__ volatile("cli":::"memory");
            last_present_tick = now ? now : 1;
        }
    }
    if(preempt_guard) scheduler_preempt_enable();
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void gop_present_forced(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    bool preempt_guard=scheduler_preempt_disable();
    struct thread *thread=scheduler_current_thread();
    bool service_irqs=(flags&(1ULL<<9))
        || (preempt_guard && thread && thread->user_mode);
    if(service_irqs) __asm__ volatile("sti":::"memory");
    gop_present_nolock();
    if(service_irqs) __asm__ volatile("cli":::"memory");
    uint64_t now = timer_ticks();
    last_present_tick = now ? now : 1;
    present_deferred = false;
    if(preempt_guard) scheduler_preempt_enable();
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void gop_defer_present(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    present_deferred = true;
    deferred_tick = timer_ticks();
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

bool gop_flush_needed(void){
    if(!dirty_valid) return false;
    uint64_t now = timer_ticks();
    if(present_deferred)
        return now-deferred_tick >= GOP_DEFERRED_FLUSH_TICKS;
    return last_present_tick==0
        || now-last_present_tick >= GOP_PRESENT_MIN_INTERVAL_TICKS;
}

static void gop_present_nolock(void){
    if(front_user_owned) return;
    if(!gop.available || !gop.addr || !backbuffer) return;
    if(!dirty_valid) return;
    if(dirty_x0>=gop.width || dirty_y0>=gop.height){
        dirty_valid=false;
        return;
    }
    uint32_t x0=dirty_x0, y0=dirty_y0, x1=dirty_x1, y1=dirty_y1;
    if(x1>gop.width) x1=gop.width;
    if(y1>gop.height) y1=gop.height;
    if(x0>=x1 || y0>=y1){ dirty_valid=false; return; }
    dirty_valid=false;
    if(compositor_present(x0, y0, x1, y1)) return;
    uint32_t span = x1 - x0;
    if(scale_active()){
        for(uint32_t y=y0; y<y1; y++)
            write_scaled_span(y, x0, &backbuffer[(uint64_t)y * backbuffer_width + x0], span);
        gop_front_notify(x0, y0, x1 - x0, y1 - y0);
        return;
    }
    if(gop.bpp==24){
        uint8_t *base = (uint8_t*)gop.addr;
        uint32_t pitch_bytes = gop.pitch * 3;
        for(uint32_t y=y0; y<y1; y++){
            uint8_t *dst = base + y*pitch_bytes + x0*3;
            uint32_t *src = &backbuffer[(uint64_t)y * backbuffer_width + x0];
            for(uint32_t i=0;i<span;i++){
                uint32_t c = src[i];
                dst[i*3+0]=(uint8_t)(c&0xFF);
                dst[i*3+1]=(uint8_t)((c>>8)&0xFF);
                dst[i*3+2]=(uint8_t)((c>>16)&0xFF);
            }
        }
        return;
    }
    if(gop.bpp==16){
        uint16_t *base16=(uint16_t*)gop.addr;
        for(uint32_t y=y0; y<y1; y++){
            uint16_t *dst = &base16[(uint64_t)y * gop.pitch + x0];
            uint32_t *src = &backbuffer[(uint64_t)y * backbuffer_width + x0];
            for(uint32_t i=0;i<span;i++){
                uint32_t c = src[i];
                uint16_t r=(c>>19)&0x1F, gg=(c>>10)&0x3F, b=(c>>3)&0x1F;
                dst[i]=(r<<11)|(gg<<5)|b;
            }
        }
        return;
    }
    for(uint32_t y=y0; y<y1; y++){
        memcpy(&gop.addr[(uint64_t)y * gop.pitch + x0],
               &backbuffer[(uint64_t)y * backbuffer_width + x0],
               (uint64_t)span * sizeof(uint32_t));
    }
    gop_front_notify(x0, y0, x1 - x0, y1 - y0);
}

void gop_copy_back_to_front(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    if(front_user_owned) return;
    if(!gop.available || !gop.addr || !backbuffer || !w || !h) return;
    if(x>=gop.width || y>=gop.height) return;
    if(x+w>gop.width) w=gop.width-x;
    if(y+h>gop.height) h=gop.height-y;
    if(!w || !h) return;
    if(compositor_present(x, y, x+w, y+h)) return;
    if(scale_active()){
        for(uint32_t row=0;row<h;row++)
            write_scaled_span(y+row, x, &backbuffer[(uint64_t)(y+row)*backbuffer_width+x], w);
        gop_front_notify(x, y, w, h);
        return;
    }
    if(gop.bpp==24){
        uint8_t *base=(uint8_t*)gop.addr;
        uint32_t pitch_bytes=gop.pitch*3;
        for(uint32_t row=0;row<h;row++){
            uint8_t *dst=base+(uint64_t)(y+row)*pitch_bytes+x*3;
            uint32_t *src=&backbuffer[(uint64_t)(y+row)*backbuffer_width+x];
            for(uint32_t i=0;i<w;i++){
                uint32_t c=src[i];
                dst[i*3+0]=(uint8_t)(c&0xFF);
                dst[i*3+1]=(uint8_t)((c>>8)&0xFF);
                dst[i*3+2]=(uint8_t)((c>>16)&0xFF);
            }
        }
        return;
    }
    if(gop.bpp==16){
        uint16_t *base16=(uint16_t*)gop.addr;
        for(uint32_t row=0;row<h;row++){
            uint16_t *dst=&base16[(uint64_t)(y+row)*gop.pitch+x];
            uint32_t *src=&backbuffer[(uint64_t)(y+row)*backbuffer_width+x];
            for(uint32_t i=0;i<w;i++){
                uint32_t c=src[i];
                uint16_t r=(c>>19)&0x1F, gg=(c>>10)&0x3F, b=(c>>3)&0x1F;
                dst[i]=(r<<11)|(gg<<5)|b;
            }
        }
        return;
    }
    for(uint32_t row=0;row<h;row++){
        memcpy(&gop.addr[(uint64_t)(y+row)*gop.pitch+x],
               &backbuffer[(uint64_t)(y+row)*backbuffer_width+x],
               (uint64_t)w*sizeof(uint32_t));
    }
    gop_front_notify(x, y, w, h);
}

void gop_put_pixel_front(uint32_t x, uint32_t y, uint32_t color){
    if(front_user_owned) return;
    front_write_32(x, y, color);
}

const uint32_t *gop_backbuffer_row(uint32_t y){
    if(!backbuffer || y>=backbuffer_height) return 0;
    return &backbuffer[(uint64_t)y * backbuffer_width];
}

bool gop_backbuffer_put(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const uint32_t *pixels, uint32_t stride){
    if(!pixels || !w || !h || stride<w || !gop.available) return false;
    if(!ensure_backbuffer()) return false;
    if(x>=backbuffer_width || y>=backbuffer_height) return false;
    if(w>backbuffer_width-x) w=backbuffer_width-x;
    if(h>backbuffer_height-y) h=backbuffer_height-y;
    for(uint32_t row=0;row<h;row++)
        memcpy(&backbuffer[(uint64_t)(y+row)*backbuffer_width+x],
               &pixels[(uint64_t)row*stride], (uint64_t)w*sizeof(uint32_t));
    return true;
}

bool gop_draw_pixels(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const uint32_t *pixels, uint32_t stride){
    if(!pixels || !w || !h || stride<w || !gop.available) return false;
    if(x>=gop.width || y>=gop.height) return false;
    if(!ensure_backbuffer()) return false;
    if(w>backbuffer_width-x) w=backbuffer_width-x;
    if(h>backbuffer_height-y) h=backbuffer_height-y;
    for(uint32_t row=0;row<h;row++)
        memcpy(&backbuffer[(uint64_t)(y+row)*backbuffer_width+x],
               &pixels[(uint64_t)row*stride], (uint64_t)w*sizeof(uint32_t));
    dirty_expand(x, y, w, h);
    maybe_present();
    return true;
}

void gop_front_write_row(uint32_t x, uint32_t y, const uint32_t *pixels, uint32_t count){
    if(front_user_owned) return;
    if(!gop.available || !gop.addr || !pixels || x>=gop.width || y>=gop.height) return;
    if(count>gop.width-x) count=gop.width-x;
    if(scale_active()){
        write_scaled_span(y, x, pixels, count);
        return;
    }
    if(gop.bpp==24){
        uint8_t *dst=(uint8_t*)gop.addr + (uint64_t)y*gop.pitch*3 + (uint64_t)x*3;
        for(uint32_t i=0;i<count;i++){
            uint32_t c=pixels[i];
            dst[i*3+0]=(uint8_t)(c&0xFF);
            dst[i*3+1]=(uint8_t)((c>>8)&0xFF);
            dst[i*3+2]=(uint8_t)((c>>16)&0xFF);
        }
        return;
    }
    if(gop.bpp==16){
        uint16_t *dst=&((uint16_t*)gop.addr)[(uint64_t)y*gop.pitch+x];
        for(uint32_t i=0;i<count;i++){
            uint32_t c=pixels[i];
            uint16_t r=(c>>19)&0x1F, gg=(c>>10)&0x3F, b=(c>>3)&0x1F;
            dst[i]=(r<<11)|(gg<<5)|b;
        }
        return;
    }
    memcpy(&gop.addr[(uint64_t)y*gop.pitch+x], pixels, (uint64_t)count*sizeof(uint32_t));
}

static void gop_scroll(void){
    if(!gop.available || !gop.addr) return;
    const uint32_t line_h = 10;
    if(gop.height <= line_h) {
        gop_clear(bg);
        return;
    }
    if(ensure_backbuffer()){
        for(uint32_t y=0; y + line_h < gop.height; y++){
            memmove(&backbuffer[(uint64_t)y * backbuffer_width],
                    &backbuffer[(uint64_t)(y + line_h) * backbuffer_width],
                    (uint64_t)gop.width * sizeof(uint32_t));
        }
        for(uint32_t y=gop.height-line_h; y<gop.height; y++){
            for(uint32_t x=0; x<gop.width; x++)
                backbuffer[(uint64_t)y * backbuffer_width + x] = bg;
        }
        dirty_expand(0, 0, gop.width, gop.height);
        maybe_present();
    } else if(gop.bpp==24){
        uint8_t *base=(uint8_t*)gop.addr;
        uint32_t pitch_bytes=gop.pitch*3;
        for(uint32_t y=0; y + line_h < gop.height; y++){
            memmove(base + y*pitch_bytes, base + (y+line_h)*pitch_bytes, pitch_bytes);
        }
        for(uint32_t y=gop.height-line_h; y<gop.height; y++){
            uint8_t *line=base + y*pitch_bytes;
            for(uint32_t x=0;x<gop.width;x++){
                line[x*3+0]=(uint8_t)(bg&0xFF);
                line[x*3+1]=(uint8_t)((bg>>8)&0xFF);
                line[x*3+2]=(uint8_t)((bg>>16)&0xFF);
            }
        }
    } else if(gop.bpp==16){
        uint16_t *base16=(uint16_t*)gop.addr;
        uint16_t r=(bg>>19)&0x1F; uint16_t g=(bg>>10)&0x3F; uint16_t b=(bg>>3)&0x1F; uint16_t v=(r<<11)|(g<<5)|b;
        for(uint32_t y=0; y+line_h<gop.height; y++) memmove(&base16[y*gop.pitch], &base16[(y+line_h)*gop.pitch], gop.pitch*sizeof(uint16_t));
        for(uint32_t y=gop.height-line_h; y<gop.height; y++) for(uint32_t x=0;x<gop.width;x++) base16[y*gop.pitch+x]=v;
    } else {
        for(uint32_t y=0; y + line_h < gop.height; y++){
            memmove(&gop.addr[y * gop.pitch], &gop.addr[(y + line_h) * gop.pitch], gop.pitch * sizeof(uint32_t));
        }
        for(uint32_t y = gop.height - line_h; y < gop.height; y++){
            for(uint32_t x=0; x < gop.pitch; x++) gop.addr[y * gop.pitch + x] = bg;
        }
    }
    if(cur_y >= line_h) cur_y -= line_h;
    else cur_y = 12;
}

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t c){
    if(!gop.available || !gop.addr) return;
    if(x>=gop.width || y>=gop.height) return;
    if(backbuffer){
        backbuffer[(uint64_t)y * backbuffer_width + x]=c;
        return;
    }
    front_write_32(x, y, c);
}

uint32_t gop_get_pixel(uint32_t x, uint32_t y){
    if(!gop.available || !gop.addr || x>=gop.width || y>=gop.height) return 0;
    if(backbuffer)
        return backbuffer[(uint64_t)y * backbuffer_width + x];
    return front_read_32(x, y);
}

void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color){
    if(!gop.available || !gop.addr) return;
    if(x>=gop.width || y>=gop.height) return;
    if(ensure_backbuffer()){
        backbuffer[(uint64_t)y * backbuffer_width + x]=color;
        dirty_expand(x, y, 1, 1);
        maybe_present();
        return;
    }
    front_write_32(x, y, color);
}

void gop_clear(uint32_t color){
    if(!gop.available){
        vga_clear(); return;
    }
    if(ensure_backbuffer()){
        for(uint32_t y=0;y<gop.height;y++){
            uint32_t *line=&backbuffer[(uint64_t)y*backbuffer_width];
            for(uint32_t x=0;x<gop.width;x++) line[x]=color;
        }
        dirty_expand(0, 0, gop.width, gop.height);
        maybe_present();
        cur_x=12; cur_y=12; bg=color;
        return;
    }
    if(gop.bpp==24){
        uint8_t *base = (uint8_t*)gop.addr;
        uint32_t pitch_bytes = gop.pitch * 3;
        for(uint32_t y=0;y<gop.height;y++){
            uint8_t *line = base + y * pitch_bytes;
            for(uint32_t x=0;x<gop.width;x++){
                line[x*3+0]=(uint8_t)(color & 0xFF);
                line[x*3+1]=(uint8_t)((color>>8)&0xFF);
                line[x*3+2]=(uint8_t)((color>>16)&0xFF);
            }
        }
    } else if(gop.bpp==16){
        uint16_t r = (color >> 19) & 0x1F;
        uint16_t g = (color >> 10) & 0x3F;
        uint16_t b = (color >> 3) & 0x1F;
        uint16_t v = (r << 11) | (g << 5) | b;
        uint16_t *base16=(uint16_t*)gop.addr;
        for(uint32_t y=0;y<gop.height;y++) for(uint32_t x=0;x<gop.width;x++) base16[y*gop.pitch+x]=v;
    } else {
        for(uint32_t y=0;y<gop.height;y++) for(uint32_t x=0;x<gop.width;x++) gop.addr[y*gop.pitch+x]=color;
    }
    cur_x=12; cur_y=12; bg=color;
}

void gop_set_color(uint32_t f, uint32_t b){
    fg=f;
    bg=b;
}

static void gop_console_glyph(char c, uint32_t x, uint32_t y,
                              uint32_t cell_fg, uint32_t cell_bg,
                              struct gop_console *slot){
    char text[2] = {c, '\0'};
    gfx_draw_text_opaque(text, x, y, cell_fg, cell_bg, 8,
                         console_gfx_face(), gop_gfx_rect, slot);
}

void gop_putc(char c){
    if(!gop.available){
        vga_putc(c);
        return;
    }
    (void)ensure_backbuffer();
    if(c=='\b'){
        if(cur_x>12) cur_x-=8;
        gop_console_glyph(' ',cur_x,cur_y,fg,bg,NULL);
        if(backbuffer){
            dirty_expand(cur_x, cur_y, 8, 8);
            maybe_present();
        }
        return;
    }
    if(c=='\n'){
        cur_x=12;
        cur_y+=10;
        if(cur_y+8 >= gop.height) gop_scroll();
        return;
    }
    if(c=='\r'){
        cur_x=12;
        return;
    }
    if(cur_x+8 >= gop.width){
        cur_x=12;
        cur_y+=10;
        if(cur_y+8 >= gop.height) gop_scroll();
    }
    if(cur_y+8 >= gop.height) gop_scroll();
    gop_console_glyph(c,cur_x,cur_y,fg,bg,NULL);
    if(backbuffer){
        dirty_expand(cur_x, cur_y, 8, 8);
        maybe_present();
    }
    cur_x+=8;
}
void gop_write(const char *s){
    while(*s) gop_putc(*s++);
}
void gop_write_hex(uint64_t v){
    const char*h="0123456789ABCDEF";
    gop_write("0x");
    for(int i=60;i>=0;i-=4) gop_putc(h[(v>>i)&0xF]);
    }

static const uint32_t ansi_palette[16]={
    0x000000,0xAA0000,0x00AA00,0xAA5500,0x0000AA,0xAA00AA,0x00AAAA,0xAAAAAA,
    0x555555,0xFF5555,0x55FF55,0xFFFF55,0x5555FF,0xFF55FF,0x55FFFF,0xFFFFFF
};

static void console_colors(const struct gop_console *slot, uint16_t attr,
                           bool cursor, uint32_t *foreground,
                           uint32_t *background){
    uint32_t fg_color=slot->foreground;
    uint32_t bg_color=slot->background;
    if(attr&VT_ATTR_FG_SET){
        uint32_t index=attr&VT_ATTR_FG_MASK;
        if((attr&VT_ATTR_BOLD) && index<8) index|=8;
        fg_color=ansi_palette[index];
    }
    if(attr&VT_ATTR_BG_SET)
        bg_color=ansi_palette[(attr&VT_ATTR_BG_MASK)>>VT_ATTR_BG_SHIFT];
    if(((attr&VT_ATTR_REVERSE)!=0)!=cursor){
        uint32_t swap=fg_color;
        fg_color=bg_color;
        bg_color=swap;
    }
    *foreground=fg_color;
    *background=bg_color;
}

static void console_present(struct gop_console *slot, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h){
    if(slot->owner_pid && compositor_window_exists(slot->owner_pid)) return;
    if(backbuffer){ dirty_expand(x,y,w,h); maybe_present(); }
}

static bool console_update_begin(struct gop_console *slot){
    if(!slot->owner_pid || !compositor_window_exists(slot->owner_pid))
        return false;
    return compositor_begin_update(slot->owner_pid);
}

static void console_update_end(struct gop_console *slot, bool began){
    if(began) (void)compositor_end_update(slot->owner_pid);
}

static void vt_op_draw_cell(void *ctx, uint32_t row, uint32_t column,
                            char character, uint16_t attr, bool cursor){
    struct gop_console *slot=(struct gop_console*)ctx;
    uint32_t cell_fg,cell_bg;
    console_colors(slot,attr,cursor,&cell_fg,&cell_bg);
    uint32_t cw=slot->cell_w ? slot->cell_w : 8;
    uint32_t chh=slot->cell_h ? slot->cell_h : 10;
    uint32_t x=slot->x+column*cw;
    uint32_t y=slot->y+row*chh;
    gop_gfx_rect(x,y,cw,chh,cell_bg,slot);
    if(character!=' '){
        char text[2]={character,'\0'};
        gfx_draw_text(text,x,y+(chh-cw)/2,cell_fg,cw,console_gfx_face(),gop_gfx_rect,slot);
    }
    if(attr&VT_ATTR_UNDERLINE) gop_gfx_rect(x,y+chh-1,cw,1,cell_fg,slot);
    console_present(slot,x,y,cw,chh);
}

static void vt_op_fill(void *ctx, uint32_t row, uint32_t column,
                       uint32_t count, uint16_t attr){
    struct gop_console *slot=(struct gop_console*)ctx;
    uint32_t cell_fg,cell_bg;
    console_colors(slot,attr,false,&cell_fg,&cell_bg);
    uint32_t cw=slot->cell_w ? slot->cell_w : 8;
    uint32_t chh=slot->cell_h ? slot->cell_h : 10;
    uint32_t x=slot->x+column*cw;
    uint32_t y=slot->y+row*chh;
    gop_gfx_rect(x,y,count*cw,chh,cell_bg,slot);
    console_present(slot,x,y,count*cw,chh);
}

static bool vt_op_scroll(void *ctx, uint32_t top, uint32_t bottom,
                         uint32_t count, bool up){
    struct gop_console *slot=(struct gop_console*)ctx;
    if(!up || top!=0 || bottom+1!=slot->vt.rows) return false;
    uint32_t chh=slot->cell_h ? slot->cell_h : 10;
    if(slot->owner_pid && compositor_window_exists(slot->owner_pid)){
        compositor_scroll_rect_up(slot->owner_pid,slot->x,slot->y,
                                  slot->width,slot->height,count*chh,
                                  slot->background);
    } else {
        gop_scroll_rect_up(slot->x,slot->y,slot->width,slot->height,
                           count*chh,slot->background);
    }
    return true;
}

static void vt_op_reply(void *ctx, const char *text){
    (void)ctx;
    tty_inject(text);
}

static void vt_op_mode(void *ctx, uint32_t mode, bool enable){
    (void)ctx;
    if(mode==1) tty_set_application_cursor(enable);
}

static const struct vt_ops console_vt_ops={
    .draw_cell=vt_op_draw_cell,
    .fill=vt_op_fill,
    .scroll=vt_op_scroll,
    .reply=vt_op_reply,
    .mode=vt_op_mode
};

bool gop_console_configure_owner(uint32_t owner_pid,
                                 uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height,
                                 uint32_t foreground, uint32_t background){
    if(!gop.available || !width || !height || x>=gop.width || y>=gop.height)
        return false;
    if(width>gop.width-x) width=gop.width-x;
    if(height>gop.height-y) height=gop.height-y;
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_alloc_locked(owner_pid);
    if(!slot){ CONSOLE_UNLOCK(flags,pg_guard); return false; }
    slot->owner_pid=owner_pid;
    slot->x=x;
    slot->y=y;
    slot->width=width;
    slot->height=height;
    slot->foreground=foreground;
    slot->background=background;
    slot->cell_w=console_font_px;
    slot->cell_h=console_font_px*10/8;
    slot->columns=width/slot->cell_w;
    slot->rows=height/slot->cell_h;
    if(slot->columns>GOP_CONSOLE_COLUMNS)
        slot->columns=GOP_CONSOLE_COLUMNS;
    if(slot->rows>GOP_CONSOLE_ROWS)
        slot->rows=GOP_CONSOLE_ROWS;
    if(!slot->columns || !slot->rows){ CONSOLE_UNLOCK(flags,pg_guard); return false; }
    slot->active=true;
    if(!slot->initialized){
        vt_init(&slot->vt,&console_vt_ops,slot);
        slot->initialized=true;
    }
    vt_resize(&slot->vt,slot->columns,slot->rows);
    gop_begin_batch();
    if(slot->owner_pid && compositor_window_exists(slot->owner_pid)){
        compositor_draw_rect(slot->owner_pid,x,y,width,height,background);
    } else {
        gop_draw_rect(x,y,width,height,background);
    }
    bool began=console_update_begin(slot);
    vt_redraw(&slot->vt);
    console_update_end(slot,began);
    if(!slot->owner_pid || !compositor_window_exists(slot->owner_pid)){
        if(backbuffer){ dirty_expand(x, y, width, height); }
    }
    gop_end_batch();
    CONSOLE_UNLOCK(flags,pg_guard);
    return true;
}

bool gop_console_configure(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t foreground, uint32_t background){
    return gop_console_configure_owner(0,x,y,width,height,foreground,background);
}

bool gop_console_is_active(void){
    uint32_t chain[GOP_CONSOLE_CHAIN];
    uint32_t depth=console_chain((uint32_t)process_current_pid(),chain,GOP_CONSOLE_CHAIN);
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_pick_locked(chain,depth);
    bool active=slot!=NULL;
    CONSOLE_UNLOCK(flags,pg_guard);
    return active;
}

bool gop_console_get_size(uint32_t *columns, uint32_t *rows,
                          uint32_t *width, uint32_t *height){
    uint32_t chain[GOP_CONSOLE_CHAIN];
    uint32_t depth=console_chain((uint32_t)process_current_pid(),chain,GOP_CONSOLE_CHAIN);
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_pick_locked(chain,depth);
    if(slot){
        *columns=slot->columns;
        *rows=slot->rows;
        *width=slot->columns*(slot->cell_w ? slot->cell_w : 8);
        *height=slot->rows*(slot->cell_h ? slot->cell_h : 10);
    }
    CONSOLE_UNLOCK(flags,pg_guard);
    return slot!=NULL;
}

void gop_console_clear(void){
    uint32_t chain[GOP_CONSOLE_CHAIN];
    uint32_t depth=console_chain((uint32_t)process_current_pid(),chain,GOP_CONSOLE_CHAIN);
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_pick_locked(chain,depth);
    if(!slot){ CONSOLE_UNLOCK(flags,pg_guard); return; }
    if(slot->owner_pid && compositor_window_exists(slot->owner_pid)){
        compositor_draw_rect(slot->owner_pid,
                             slot->x,slot->y,
                             slot->width,slot->height,
                             slot->background);
    } else {
        gop_draw_rect(slot->x,slot->y,slot->width,slot->height,slot->background);
    }
    bool began=console_update_begin(slot);
    vt_clear(&slot->vt);
    vt_cursor_show(&slot->vt);
    console_update_end(slot,began);
    CONSOLE_UNLOCK(flags,pg_guard);
}

void gop_console_disable(void){
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_find_locked((uint32_t)process_current_pid());
    if(slot) slot->active=false;
    CONSOLE_UNLOCK(flags,pg_guard);
}

void gop_console_release(uint32_t owner_pid){
    if(!owner_pid) return;
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_find_locked(owner_pid);
    if(slot){ slot->active=false; slot->owner_pid=0; slot->initialized=false; }
    CONSOLE_UNLOCK(flags,pg_guard);
}

void gop_set_console_font_size(uint32_t px){
    if(px<8) px=8;
    if(px>32) px=32;
    console_font_px=px;
}

uint32_t gop_get_console_font_size(void){
    return console_font_px;
}

void gop_console_translate(uint32_t owner_pid, int32_t dx, int32_t dy){
    if(!owner_pid || (!dx && !dy)) return;
    bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
    struct gop_console *slot=console_find_locked(owner_pid);
    if(slot){
        int64_t nx=(int64_t)slot->x+dx;
        int64_t ny=(int64_t)slot->y+dy;
        slot->x=nx<0 ? 0 : (uint32_t)nx;
        slot->y=ny<0 ? 0 : (uint32_t)ny;
    }
    CONSOLE_UNLOCK(flags,pg_guard);
}

#define GOP_CONSOLE_CHUNK 256

#define GOP_WRITE_REPORT_MS 3
#define GOP_WRITE_REPORT_LIMIT 64
static uint32_t write_reports;

void gop_console_write(const char *text, uint32_t count){
    uint64_t started=timer_ticks();
    uint32_t chain[GOP_CONSOLE_CHAIN];
    uint32_t depth=console_chain((uint32_t)process_current_pid(),chain,GOP_CONSOLE_CHAIN);
    uint32_t flags_vt=0;
    if(tty_output_nl_cr()) flags_vt|=VT_FLAG_NL_CR;
    if(!tty_raw_mode()) flags_vt|=VT_FLAG_BS_ERASE;
    uint32_t done=0;
    while(done<count){
        uint32_t chunk=count-done;
        if(chunk>GOP_CONSOLE_CHUNK) chunk=GOP_CONSOLE_CHUNK;
        bool pg_guard=scheduler_preempt_disable();
    uint64_t flags=spin_lock_irqsave(&console_lock);
        struct gop_console *slot=console_pick_locked(chain,depth);
        if(!slot){
            CONSOLE_UNLOCK(flags,pg_guard);
            for(;done<count;done++) gop_putc(text[done]);
            return;
        }
        (void)ensure_backbuffer();
        bool began=console_update_begin(slot);
        vt_write(&slot->vt,text+done,chunk,flags_vt);
        console_update_end(slot,began);
        CONSOLE_UNLOCK(flags,pg_guard);
        done+=chunk;
    }
    uint64_t elapsed=timer_ticks()-started;
    if(elapsed>=GOP_WRITE_REPORT_MS && write_reports<GOP_WRITE_REPORT_LIMIT){
        write_reports++;
        klogf(KLOG_INFO,"console: write of %u bytes took %u ms",
              count,(uint32_t)elapsed);
    }
}

void gop_console_putc(char character){
    gop_console_write(&character,1);
}
void gop_draw_rect(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t c){
    if(!gop.available || !gop.addr || !w || !h) return;
    if(x>=gop.width || y>=gop.height) return;
    uint32_t cw=w, ch=h;
    if(x+cw>gop.width) cw=gop.width-x;
    if(y+ch>gop.height) ch=gop.height-y;
    if(!cw || !ch) return;
    if(ensure_backbuffer()){
        for(uint32_t dy=0;dy<ch;dy++){
            uint32_t *line=&backbuffer[(uint64_t)(y+dy)*backbuffer_width+x];
            for(uint32_t dx=0;dx<cw;dx++) line[dx]=c;
        }
        dirty_expand(x, y, cw, ch);
        maybe_present();
        return;
    }
    for(uint32_t dy=0;dy<ch;dy++) for(uint32_t dx=0;dx<cw;dx++) front_write_32(x+dx,y+dy,c);
}
void gop_scroll_rect_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t amount, uint32_t fill_color){
    if(!gop.available || !gop.addr || x>=gop.width || y>=gop.height || w==0 || h==0) return;
    if(w>gop.width-x) w=gop.width-x;
    if(h>gop.height-y) h=gop.height-y;
    if(amount>=h){
        gop_draw_rect(x,y,w,h,fill_color);
        return;
    }
    if(ensure_backbuffer()){
        for(uint32_t row=0; row+amount<h; row++){
            memmove(&backbuffer[(uint64_t)(y+row)*backbuffer_width+x],
                    &backbuffer[(uint64_t)(y+row+amount)*backbuffer_width+x],
                    (uint64_t)w*sizeof(uint32_t));
        }
        for(uint32_t row=h-amount; row<h; row++){
            uint32_t *line=&backbuffer[(uint64_t)(y+row)*backbuffer_width+x];
            for(uint32_t i=0;i<w;i++) line[i]=fill_color;
        }
        dirty_expand(x, y, w, h);
        maybe_present();
        return;
    }
    if(gop.bpp==24){
        uint8_t *base=(uint8_t*)gop.addr;
        uint32_t pitch_bytes=gop.pitch*3;
        for(uint32_t row=0; row+amount<h; row++){
            uint8_t *dst=base + (y+row)*pitch_bytes + x*3;
            uint8_t *src=base + (y+row+amount)*pitch_bytes + x*3;
            memmove(dst, src, w*3);
        }
    } else if(gop.bpp==16){
        uint16_t *base16=(uint16_t*)gop.addr;
        for(uint32_t row=0; row+amount<h; row++){
            memmove(&base16[(y+row)*gop.pitch+x], &base16[(y+row+amount)*gop.pitch+x], w*sizeof(uint16_t));
        }
    } else {
        for(uint32_t row=0; row+amount<h; row++){
            memmove(&gop.addr[(y+row)*gop.pitch+x],
                   &gop.addr[(y+row+amount)*gop.pitch+x],
                   w*sizeof(uint32_t));
        }
    }
    gop_draw_rect(x, y+h-amount, w, amount, fill_color);
}
void gop_draw_line(uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,uint32_t c){    if(!gop.available || !gop.addr) return;
    (void)ensure_backbuffer();
    uint32_t min_x=x0<x1?x0:x1, max_x=x0<x1?x1:x0;
    uint32_t min_y=y0<y1?y0:y1, max_y=y0<y1?y1:y0;
    int dx = (x1>x0)?(int)(x1-x0):(int)(x0-x1);
    int dy=(y1>y0)?(int)(y1-y0):(int)(y0-y1);
    int sx=(x0<x1)?1:-1, sy=(y0<y1)?1:-1; int err=dx-dy;
    uint32_t guard=0;
    while(1){
        put_pixel(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;}
        if(++guard>(gop.width+gop.height+16)) break;
    }
    if(backbuffer){
        uint32_t w=(max_x>=min_x)?(max_x-min_x+1):1;
        uint32_t h=(max_y>=min_y)?(max_y-min_y+1):1;
        dirty_expand(min_x, min_y, w, h);
        maybe_present();
    }
}

bool gop_blit_cover(const uint32_t *pixels, uint32_t width, uint32_t height){
    if(!gop.available || !gop.addr || !pixels) return false;
    if(!width || !height || width!=gop.width || height!=gop.height)
        return false;
    if(!ensure_backbuffer()) return false;
    for(uint32_t y=0;y<gop.height;y++){
        memcpy(&backbuffer[(uint64_t)y*backbuffer_width],
               &pixels[(uint64_t)y*width],
               (uint64_t)gop.width*sizeof(uint32_t));
    }
    dirty_expand(0,0,gop.width,gop.height);
    maybe_present();
    return true;
}

bool gop_draw_image_stretch(const uint32_t *pixels, uint32_t src_w,
                            uint32_t src_h, uint32_t dx, uint32_t dy,
                            uint32_t dw, uint32_t dh){
    if(!gop.available || !gop.addr || !pixels) return false;
    if(!src_w || !src_h || !dw || !dh) return false;
    if(dx>=gop.width || dy>=gop.height) return false;
    if(dx+dw>gop.width) dw=gop.width-dx;
    if(dy+dh>gop.height) dh=gop.height-dy;
    if(!dw || !dh) return false;
    if(!ensure_backbuffer()) return false;
    for(uint32_t y=0;y<dh;y++){
        uint32_t src_y=(y*src_h)/dh;
        uint32_t *dst=&backbuffer[(uint64_t)(dy+y)*backbuffer_width+dx];
        const uint32_t *src_row=&pixels[(uint64_t)src_y*src_w];
        for(uint32_t x=0;x<dw;x++){
            uint32_t src_x=(x*src_w)/dw;
            dst[x]=src_row[src_x];
        }
    }
    dirty_expand(dx,dy,dw,dh);
    maybe_present();
    return true;
}
