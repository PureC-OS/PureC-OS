#include "gop.h"
#include "vga.h"
#include "../../gfx/text.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../drivers/interrupts/timer.h"
#include "../../kernel/process/scheduler.h"
#include <stdint.h>
#include <stddef.h>

static struct gop_state gop = {0};

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
static uint32_t fg=0xCDD6F4, bg=0x1E1E2E;
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

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t c);
static void gop_gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t color, void *ctx){
    (void)ctx;
    for(uint32_t dy=0;dy<h;dy++)
        for(uint32_t dx=0;dx<w;dx++)
            put_pixel(x+dx, y+dy, color);
}

#define GOP_CONSOLE_COLUMNS 128
#define GOP_CONSOLE_ROWS 64

struct gop_console {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t columns;
    uint32_t rows;
    uint32_t cursor_column;
    uint32_t cursor_row;
    uint32_t foreground;
    uint32_t background;
    char characters[GOP_CONSOLE_ROWS][GOP_CONSOLE_COLUMNS];
    bool initialized;
    bool active;
};

static struct gop_console user_console;

void gop_init_from_limine(struct limine_framebuffer *fb, uint64_t firmware_type){
    if(!fb) { gop.available=false; return; }
    gop.addr = (uint32_t*)fb->address;
    gop.width = fb->width;
    gop.height = fb->height;
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
    cur_x=12; cur_y=12; fg=0xCDD6F4; bg=0x1E1E2E;
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

bool gop_apply_live(void *address, uint32_t width, uint32_t height,
                    uint32_t pitch_pixels, uint8_t bpp){
    if(!address || !width || !height || !pitch_pixels) return false;
    if(bpp!=16 && bpp!=24 && bpp!=32) return false;
    uint64_t pixels=(uint64_t)width*(uint64_t)height;
    if(!pixels || pixels>(64ULL*1024ULL*1024ULL)) return false;
    gop.addr=(uint32_t*)address;
    gop.width=width;
    gop.height=height;
    gop.pitch=pitch_pixels;
    gop.bpp=bpp;
    gop.framebuffer_bytes=(uint64_t)pitch_pixels
        *((uint64_t)(bpp==24 ? 3 : (bpp==16 ? 2 : 4)))*height;
    gop.protocol_name="VBE live mode";
    gop.available=true;
    cur_x=12; cur_y=12;
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
    user_console.active=false;
    return true;
}

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
    for(uint32_t y=0; y<gop.height; y++){
        for(uint32_t x=0; x<gop.width; x++){
            virt[(uint64_t)y * gop.width + x] = front_read_32(x, y);
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
    uint32_t span = x1 - x0;
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
}

void gop_copy_back_to_front(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    if(!gop.available || !gop.addr || !backbuffer || !w || !h) return;
    if(x>=gop.width || y>=gop.height) return;
    if(x+w>gop.width) w=gop.width-x;
    if(y+h>gop.height) h=gop.height-y;
    if(!w || !h) return;
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
}

void gop_put_pixel_front(uint32_t x, uint32_t y, uint32_t color){
    front_write_32(x, y, color);
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
                              uint32_t cell_fg, uint32_t cell_bg){
    char text[2] = {c, '\0'};
    gfx_draw_text_opaque(text, x, y, cell_fg, cell_bg, 8,
                         console_gfx_face(), gop_gfx_rect, 0);
}

void gop_putc(char c){
    if(!gop.available){
        vga_putc(c);
        return;
    }
    (void)ensure_backbuffer();
    if(c=='\b'){
        if(cur_x>12) cur_x-=8;
        gop_console_glyph(' ',cur_x,cur_y,fg,bg);
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
    gop_console_glyph(c,cur_x,cur_y,fg,bg);
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

bool gop_console_configure(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t foreground, uint32_t background){
    if(!gop.available || !width || !height || x>=gop.width || y>=gop.height)
        return false;
    if(width>gop.width-x) width=gop.width-x;
    if(height>gop.height-y) height=gop.height-y;
    user_console.x=x;
    user_console.y=y;
    user_console.width=width;
    user_console.height=height;
    user_console.foreground=foreground;
    user_console.background=background;
    user_console.columns=width/8;
    user_console.rows=height/10;
    if(user_console.columns>GOP_CONSOLE_COLUMNS)
        user_console.columns=GOP_CONSOLE_COLUMNS;
    if(user_console.rows>GOP_CONSOLE_ROWS)
        user_console.rows=GOP_CONSOLE_ROWS;
    if(!user_console.columns || !user_console.rows) return false;
    user_console.active=true;
    if(!user_console.initialized){
        memset(user_console.characters,' ',sizeof(user_console.characters));
        user_console.cursor_column=0;
        user_console.cursor_row=0;
        user_console.initialized=true;
    }
    if(user_console.cursor_column>=user_console.columns)
        user_console.cursor_column=user_console.columns-1;
    if(user_console.cursor_row>=user_console.rows)
        user_console.cursor_row=user_console.rows-1;
    gop_begin_batch();
    gop_draw_rect(x,y,width,height,background);
    for(uint32_t row=0;row<user_console.rows;row++){
        for(uint32_t column=0;column<user_console.columns;column++){
            char character=user_console.characters[row][column];
            if(character!=' ')
                gop_console_glyph(character,x+column*8,y+row*10,
                                  foreground,background);
        }
    }
    if(backbuffer){ dirty_expand(x, y, width, height); }
    gop_end_batch();
    return true;
}

bool gop_console_is_active(void){ return user_console.active; }

void gop_console_clear(void){
    if(!user_console.active) return;
    gop_draw_rect(user_console.x,user_console.y,user_console.width,
                  user_console.height,user_console.background);
    memset(user_console.characters,' ',sizeof(user_console.characters));
    user_console.cursor_column=0;
    user_console.cursor_row=0;
}

void gop_console_disable(void){ user_console.active=false; }

void gop_console_putc(char character){
    if(!user_console.active){ gop_putc(character); return; }
    (void)ensure_backbuffer();
    if(character=='\b'){
        if(user_console.cursor_column){
            user_console.cursor_column--;
            user_console.characters[user_console.cursor_row]
                                    [user_console.cursor_column]=' ';
            uint32_t cx=user_console.x+user_console.cursor_column*8;
            uint32_t cy=user_console.y+user_console.cursor_row*10;
            gop_console_glyph(' ',cx,cy,
                              user_console.foreground,
                              user_console.background);
            if(backbuffer){ dirty_expand(cx, cy, 8, 10); maybe_present(); }
        }
        return;
    }
    if(character=='\n'){
        user_console.cursor_column=0;
        user_console.cursor_row++;
    } else if(character=='\r'){
        user_console.cursor_column=0;
    } else {
        if(user_console.cursor_column>=user_console.columns){
            user_console.cursor_column=0;
            user_console.cursor_row++;
        }
        if(user_console.cursor_row>=user_console.rows) goto scroll;
        user_console.characters[user_console.cursor_row]
                                [user_console.cursor_column]=character;
        uint32_t cx=user_console.x+user_console.cursor_column*8;
        uint32_t cy=user_console.y+user_console.cursor_row*10;
        gop_console_glyph(character,cx,cy,
                          user_console.foreground,
                          user_console.background);
        if(backbuffer){ dirty_expand(cx, cy, 8, 10); maybe_present(); }
        user_console.cursor_column++;
    }
scroll:
    if(user_console.cursor_row>=user_console.rows){
        for(uint32_t row=1;row<user_console.rows;row++)
            memcpy(user_console.characters[row-1],
                   user_console.characters[row],GOP_CONSOLE_COLUMNS);
        memset(user_console.characters[user_console.rows-1],' ',
               GOP_CONSOLE_COLUMNS);
        gop_scroll_rect_up(user_console.x,user_console.y,user_console.width,
                           user_console.height,10,user_console.background);
        user_console.cursor_row=user_console.rows-1;
    }
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

bool gop_blit_frame(const uint32_t *pixels, uint32_t width, uint32_t height){
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
