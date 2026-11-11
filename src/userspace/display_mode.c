#include "display_mode.h"
#include "display.h"
#include "userspace.h"
#include "rs_session.h"
#include "../drivers/display/gop.h"
#include "../drivers/display/vbe.h"
#include "../drivers/display/svga.h"
#include "../drivers/display/svga3d.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/interrupts/timer.h"
#include "../fs/vfs.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"

static uint64_t g_last_poll_tick;
static uint32_t g_scale = 100;

static bool starts_with(const char *text, const char *prefix){
    while(*prefix){ if(*text++!=*prefix++) return false; }
    return true;
}

static uint32_t parse_u32(const char *text){
    uint32_t value=0;
    while(text && *text>='0' && *text<='9'){
        uint32_t digit=(uint32_t)(*text-'0');
        if(value>(UINT32_MAX-digit)/10U) return UINT32_MAX;
        value=value*10U+digit;
        text++;
    }
    return value;
}

bool display_mode_current(uint32_t *width, uint32_t *height, uint8_t *bpp){
    if(!gop_is_available()) return false;
    if(width) *width=gop_get_physical_width();
    if(height) *height=gop_get_physical_height();
    if(bpp) *bpp=gop_get_bpp();
    return true;
}

static bool display_mode_load_ex(uint32_t *width, uint32_t *height,
                                 uint8_t *bpp, uint32_t *scale){
    if(!vfs_is_root_mounted()) return false;
    if(!filesystem_syscall_acquire()) return false;
    int32_t fd=vfs_open(DISPLAY_INI_PATH);
    char buffer[128];
    int32_t total=0;
    if(fd>=0){
        for(;;){
            if(total>=(int32_t)sizeof(buffer)-1) break;
            int32_t n=vfs_read(fd,buffer+total,
                (uint32_t)(sizeof(buffer)-1-(uint32_t)total));
            if(n<=0) break;
            total+=n;
        }
        (void)vfs_close(fd);
    }
    filesystem_syscall_unlock();
    if(fd<0 || total<=0) return false;
    buffer[total]='\0';
    uint32_t w=0,h=0,b=0,sc=100;
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(starts_with(line,"width=")) w=parse_u32(line+6);
        else if(starts_with(line,"height=")) h=parse_u32(line+7);
        else if(starts_with(line,"bpp=")) b=parse_u32(line+4);
        else if(starts_with(line,"scale=")) sc=parse_u32(line+6);
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(w<640 || w>7680 || h<400 || h>4320) return false;
    if(b!=16 && b!=24 && b!=32) return false;
    if(sc<100 || sc>400) sc=100;
    if(width) *width=w;
    if(height) *height=h;
    if(bpp) *bpp=(uint8_t)b;
    if(scale) *scale=sc;
    return true;
}

bool display_mode_load(uint32_t *width, uint32_t *height, uint8_t *bpp){
    return display_mode_load_ex(width,height,bpp,0);
}

static void write_back_ini(uint32_t width, uint32_t height, uint8_t bpp){
    char buffer[80];
    char *out=buffer;
    const char *p="width=";
    while(*p) *out++=*p++;
    uint32_t v=width;
    char rev[12];
    uint32_t n=0;
    do{ rev[n++]=(char)('0'+v%10U); v/=10U; }while(v && n<sizeof(rev));
    while(n) *out++=rev[--n];
    p="\nheight=";
    while(*p) *out++=*p++;
    v=height; n=0;
    do{ rev[n++]=(char)('0'+v%10U); v/=10U; }while(v && n<sizeof(rev));
    while(n) *out++=rev[--n];
    p="\nbpp=";
    while(*p) *out++=*p++;
    v=bpp; n=0;
    do{ rev[n++]=(char)('0'+v%10U); v/=10U; }while(v && n<sizeof(rev));
    while(n) *out++=rev[--n];
    p="\nscale=";
    while(*p) *out++=*p++;
    v=g_scale; n=0;
    do{ rev[n++]=(char)('0'+v%10U); v/=10U; }while(v && n<sizeof(rev));
    while(n) *out++=rev[--n];
    *out++='\n';
    *out='\0';
    if(!vfs_is_root_mounted()) return;
    if(!filesystem_syscall_acquire()) return;
    (void)vfs_write_file(DISPLAY_INI_PATH,buffer,
                         (uint32_t)(out-buffer));
    filesystem_syscall_unlock();
}

static void apply_scale(uint32_t scale){
    if(scale<100 || scale>400) scale=100;
    g_scale=scale;
    if(gop_get_scale_percent()==scale) return;
    if(!gop_set_scale_percent(scale)){
        klogf(KLOG_WARN,"display: scale %u%% not possible at %ux%u",
              scale,gop_get_physical_width(),gop_get_physical_height());
        g_scale=gop_get_scale_percent();
        write_back_ini(gop_get_physical_width(),gop_get_physical_height(),
                       gop_get_bpp());
        return;
    }
    mouse_set_bounds((int32_t)gop_get_width(),(int32_t)gop_get_height());
    display_invalidate_cache();
    klogf(KLOG_OK,"display: scale %u%% -> logical %ux%u",scale,
          gop_get_width(),gop_get_height());
}

static int display_mode_apply_low(uint32_t width, uint32_t height,
                                  uint8_t bpp, uint32_t scale){
    uint32_t cur_w=0,cur_h=0;
    uint8_t cur_b=0;
    g_scale=scale;
    if(svga_is_available() && bpp!=32){
        bpp=32;
        write_back_ini(width,height,bpp);
    }
    if(display_mode_current(&cur_w,&cur_h,&cur_b)
       && cur_w==width && cur_h==height && cur_b==bpp){
        apply_scale(scale);
        return 0;
    }
    if(svga_is_available()){
        uint32_t sw=0,sh=0;
        bool was3d=svga3d_active();
        svga3d_screen_teardown();
        if(!svga_set_mode(width,height,&sw,&sh)){
            klogf(KLOG_ERROR,"display: SVGA rejected mode %ux%u",width,height);
            return -1;
        }
        void *front3d=was3d ? svga3d_screen_setup(sw,sh) : 0;
        if(front3d ? !gop_apply_live(front3d,sw,sh,sw,32)
                   : !gop_apply_live(svga_fb_virt(),sw,sh,svga_pitch_pixels(),32)){
            klog(KLOG_ERROR,"display: gop_apply_live failed (svga)");
            return -1;
        }
        if(sw!=width || sh!=height){
            klogf(KLOG_WARN,"display: card clamped %ux%u -> %ux%u",
                  width,height,sw,sh);
            write_back_ini(sw,sh,32);
        }
        apply_scale(scale);
        mouse_set_bounds((int32_t)gop_get_width(),(int32_t)gop_get_height());
        display_invalidate_cache();
        klogf(KLOG_OK,"display: svga mode %ux%u",sw,sh);
        return 0;
    }
    if(!vbe_is_available()){
        klog(KLOG_WARN, "display: live mode switch unsupported (no VBE)");
        return -2;
    }
    uint64_t phys=0;
    if(!vbe_framebuffer_phys(&phys)){
        klog(KLOG_ERROR, "display: cannot locate video framebuffer");
        return -1;
    }
    uint32_t actual_w=0,actual_h=0;
    if(!vbe_set_mode(width,height,bpp,&actual_w,&actual_h)){
        klogf(KLOG_ERROR,
              "display: VBE rejected mode %ux%ux%u vram=%lluMiB",
              width,height,bpp,
              (unsigned long long)(vbe_video_memory_bytes()
                                   /(1024ULL*1024ULL)));
        return -1;
    }
    uint64_t need=(uint64_t)actual_w*(uint64_t)actual_h*(bpp/8U);
    volatile void *virt=vbe_map_framebuffer(phys,need);
    if(!virt){
        klog(KLOG_ERROR, "display: cannot map video framebuffer");
        return -1;
    }
    if(!gop_apply_live((void*)virt,actual_w,actual_h,actual_w,bpp)){
        klog(KLOG_ERROR, "display: gop_apply_live failed");
        return -1;
    }
    if(actual_w!=width || actual_h!=height){
        klogf(KLOG_WARN, "display: card clamped %ux%u -> %ux%u",
              width,height,actual_w,actual_h);
        write_back_ini(actual_w,actual_h,bpp);
    }
    apply_scale(scale);
    mouse_set_bounds((int32_t)gop_get_width(),(int32_t)gop_get_height());
    display_invalidate_cache();
    klogf(KLOG_OK, "display: live mode %ux%ux%u",actual_w,actual_h,bpp);
    return 0;
}

int display_mode_apply(uint32_t width, uint32_t height, uint8_t bpp,
                       uint32_t scale){
    if(!scale) scale=g_scale;
    int rc=display_mode_apply_low(width,height,bpp,scale);
    if(rc!=0) return rc;
    if(rs_session_active()) rs_session_display_changed();
    else userspace_redraw_desktop();
    return 0;
}

void display_mode_boot_apply(void){
    uint32_t w=0,h=0,sc=100;
    uint8_t b=0;
    if(!display_mode_load_ex(&w,&h,&b,&sc)) return;
    (void)display_mode_apply_low(w,h,b,sc);
}

void display_mode_poll(void){
    uint64_t now=timer_ticks();
    if(now-g_last_poll_tick<500) return;
    g_last_poll_tick=now;
    uint32_t w=0,h=0,cur_w=0,cur_h=0,sc=100;
    uint8_t b=0,cur_b=0;
    if(!display_mode_load_ex(&w,&h,&b,&sc)) return;
    if(!display_mode_current(&cur_w,&cur_h,&cur_b)) return;
    if(svga_is_available()) b=32;
    if(w==cur_w && h==cur_h && b==cur_b && sc==gop_get_scale_percent()) return;
    klogf(KLOG_INFO, "display: config changed, applying %ux%ux%u @%u%%",w,h,b,sc);
    if(display_mode_apply_low(w,h,b,sc)==0){
        if(rs_session_active()) rs_session_display_changed();
        else userspace_redraw_desktop();
    }
}
