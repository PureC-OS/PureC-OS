#include "display.h"
#include "syscall.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"

static struct display_info cached_info;
static bool cached_info_valid;

static int64_t display_syscall5(uint64_t number, uint64_t argument1,
                                uint64_t argument2, uint64_t argument3,
                                uint64_t argument4, uint64_t argument5){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3),
          "S"(argument4),"D"(argument5)
        : "r10","r8","memory"
    );
    return result;
}

bool display_get_info(struct display_info *info){
    if(!info) return false;
    if(!cached_info_valid){
        struct framebuffer_info kernel_info;
        if(userspace_syscall(SYS_FB_INFO,(uint64_t)&kernel_info,0,0)<0)
            return false;
        cached_info.width=kernel_info.width;
        cached_info.height=kernel_info.height;
        cached_info.pitch=kernel_info.pitch;
        cached_info.size_bytes=kernel_info.size_bytes;
        cached_info.bpp=kernel_info.bpp;
        cached_info.available=kernel_info.available!=0;
        strncpy(cached_info.protocol_name,kernel_info.protocol_name,
                sizeof(cached_info.protocol_name)-1);
        cached_info.protocol_name[sizeof(cached_info.protocol_name)-1]='\0';
        cached_info_valid=true;
    }
    *info=cached_info;
    return true;
}

bool display_is_available(void){
    struct display_info info;
    return display_get_info(&info) && info.available;
}

uint32_t display_get_width(void){
    struct display_info info;
    return display_get_info(&info) ? info.width : 0;
}

uint32_t display_get_height(void){
    struct display_info info;
    return display_get_info(&info) ? info.height : 0;
}

uint8_t display_get_bpp(void){
    struct display_info info;
    return display_get_info(&info) ? info.bpp : 0;
}

uint64_t display_get_framebuffer_size_bytes(void){
    struct display_info info;
    return display_get_info(&info) ? info.size_bytes : 0;
}

const char *display_get_protocol_name(void){
    struct display_info info;
    return display_get_info(&info) ? cached_info.protocol_name : "unknown";
}

void display_set_font_face(enum display_font_face face){
    (void)userspace_syscall(SYS_SET_FONT_FACE,(uint64_t)face,0,0);
}

enum display_font_face display_get_font_face(void){
    int64_t face=userspace_syscall(SYS_GET_FONT_FACE,0,0,0);
    if(face==DISPLAY_FONT_CLEAN) return DISPLAY_FONT_CLEAN;
    if(face==DISPLAY_FONT_BOLD) return DISPLAY_FONT_BOLD;
    return DISPLAY_FONT_CLASSIC;
}

void display_clear(uint32_t color){
    (void)userspace_syscall(SYS_CLEAR,color,0,0);
}

void display_draw_text_at(uint32_t x, uint32_t y, const char *text,
                          uint32_t fg, uint32_t bg){
    struct framebuffer_text_request request={
        .x=x,
        .y=y,
        .text=text,
        .fg=fg,
        .bg=bg,
        .size=8
    };
    (void)userspace_syscall(SYS_DRAW_TEXT,(uint64_t)&request,0,0);
}

void display_draw_text_sized_at(uint32_t x, uint32_t y, const char *text,
                                uint32_t fg, uint32_t bg, uint32_t size){
    struct framebuffer_text_request request={
        .x=x,
        .y=y,
        .text=text,
        .fg=fg,
        .bg=bg,
        .size=size
    };
    (void)userspace_syscall(SYS_DRAW_TEXT_SIZED,(uint64_t)&request,0,0);
}

void display_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color){
    (void)display_syscall5(SYS_DRAW_RECT,x,y,w,h,color);
}

void display_scroll_rect_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t amount, uint32_t fill_color){
    struct framebuffer_scroll_request request={
        .x=x,
        .y=y,
        .w=w,
        .h=h,
        .amount=amount,
        .fill_color=fill_color
    };
    (void)userspace_syscall(SYS_SCROLL_RECT_UP,(uint64_t)&request,0,0);
}

void display_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                       uint32_t color){
    (void)display_syscall5(SYS_DRAW_LINE,x0,y0,x1,y1,color);
}
