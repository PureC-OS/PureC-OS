#include "fb.h"
#include "../../gfx/text.h"
#include "../../lib/string.h"
#include <stdint.h>
#include <stddef.h>

static struct limine_framebuffer *g_fb;
static uint32_t *fb_addr;
static uint32_t fb_width, fb_height, fb_pitch;
static uint32_t cur_x=0, cur_y=0;
static uint32_t fg=0xCDD6F4, bg=0x1E1E2E;

void fb_init(struct limine_framebuffer *fb){
    if (!fb) return;
    g_fb = fb;
    fb_addr = (uint32_t*)fb->address;
    fb_width = fb->width;
    fb_height = fb->height;
    fb_pitch = fb->pitch / 4;
    cur_x=12; cur_y=12;
}

void fb_clear(uint32_t color){
    if (!fb_addr) return;
    for(uint32_t y=0;y<fb_height;y++)
        for(uint32_t x=0;x<fb_width;x++)
            fb_addr[y*fb_pitch + x] = color;
    cur_x=12; cur_y=12;
}

void fb_set_color(uint32_t f, uint32_t b){ fg=f; bg=b; }

static void fb_scroll(void){
    if(!fb_addr) return;
    const uint32_t line_h = 10;
    if(fb_height <= line_h){ fb_clear(bg); return; }
    for(uint32_t y=0; y + line_h < fb_height; y++){
        memcpy(&fb_addr[y * fb_pitch], &fb_addr[(y + line_h) * fb_pitch], fb_pitch * sizeof(uint32_t));
    }
    for(uint32_t y = fb_height - line_h; y < fb_height; y++){
        for(uint32_t x=0; x < fb_pitch; x++) fb_addr[y * fb_pitch + x] = bg;
    }
    if(cur_y >= line_h) cur_y -= line_h;
    else cur_y = 12;
}

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t c){
    if (!fb_addr) return;
    if(x>=fb_width || y>=fb_height) return;
    fb_addr[y*fb_pitch + x]=c;
}

static void fb_rect_cb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color, void *ctx){
    (void)ctx;
    for(uint32_t dy=0;dy<h;dy++)
        for(uint32_t dx=0;dx<w;dx++)
            put_pixel(x+dx, y+dy, color);
}

void fb_putc(char c){
    if (!fb_addr) return;
    if(c=='\n'){ cur_x=12; cur_y+=10; if(cur_y + 8 >= fb_height) fb_scroll(); return; }
    if(c=='\r'){ cur_x=12; return; }
    if(cur_x + 8 >= fb_width){ cur_x=12; cur_y+=10; if(cur_y + 8 >= fb_height) fb_scroll(); }
    if(cur_y + 8 >= fb_height) fb_scroll();
    {
        char text[2] = {c, '\0'};
        gfx_draw_text_opaque(text, cur_x, cur_y, fg, bg, 8,
                             GFX_FONT_CLASSIC, fb_rect_cb, 0);
    }
    cur_x+=8;
}

void fb_write_string(const char *s){ while(*s) fb_putc(*s++); }

void fb_write_hex(uint64_t v){
    const char *h="0123456789ABCDEF";
    fb_write_string("0x");
    for(int i=60;i>=0;i-=4) fb_putc(h[(v>>i)&0xF]);
}
