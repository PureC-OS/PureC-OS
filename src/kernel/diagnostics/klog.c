#include "klog.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/display/vga.h"
#include "../../drivers/serial/serial.h"
#include "../../lib/string.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define KLOG_RING_SIZE (8 * 1024 * 1024)

static char klog_ring[KLOG_RING_SIZE];
static volatile uint64_t klog_ring_pos = 0;
static bool klog_ring_wrapped = false;
static bool klog_verbose = true;
static bool klog_screen_enabled = true; // если false, логи идут только в serial+ring, не на экран (для userspace)
static bool klog_inited = false;
static uint64_t klog_boot_tsc = 0;

static inline uint64_t rdtsc(void){
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// низкоуровневый вывод одного символа в три приёмника: serial + gop/vga + ring
static void klog_putc_raw(char c){
    serial_putc(c);
    if(klog_screen_enabled){
        if(gop_is_available()) gop_putc(c);
        else vga_putc(c);
    }
    klog_ring[klog_ring_pos % KLOG_RING_SIZE] = c;
    klog_ring_pos++;
    if(klog_ring_pos >= KLOG_RING_SIZE) klog_ring_wrapped = true;
}

static void klog_puts_raw(const char *s){
    while(*s) klog_putc_raw(*s++);
}

static void klog_put_dec_unsigned(uint64_t v){
    char buf[32];
    int i=0;
    if(v==0) { klog_putc_raw('0'); return; }
    while(v>0){ buf[i++] = '0' + (v%10); v/=10; }
    while(i--) klog_putc_raw(buf[i]);
}

static void klog_put_dec_signed(int64_t v){
    if(v<0){ klog_putc_raw('-'); v=-v; }
    klog_put_dec_unsigned((uint64_t)v);
}

static void klog_put_hex(uint64_t v, bool upper){
    const char *h = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[16];
    int i=0;
    if(v==0){ klog_putc_raw('0'); return; }
    while(v>0){ buf[i++] = h[v&0xF]; v>>=4; }
    while(i--) klog_putc_raw(buf[i]);
}

static void klog_put_hex_padded(uint64_t v, int width, char pad, bool upper){
    const char *h = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[16];
    int len=0;
    uint64_t tmp=v;
    if(tmp==0) buf[len++]='0';
    else while(tmp>0){ buf[len++]=h[tmp&0xF]; tmp>>=4; }
    int pad_len = width - len;
    for(int i=0;i<pad_len;i++) klog_putc_raw(pad);
    for(int i=len-1;i>=0;i--) klog_putc_raw(buf[i]);
}

static void klog_put_dec_padded(uint64_t v, int width, char pad){
    char buf[20];
    int len=0;
    uint64_t tmp=v;
    if(tmp==0) buf[len++]='0';
    else while(tmp>0){ buf[len++]= '0' + (tmp%10); tmp/=10; }
    int pad_len = width - len;
    for(int i=0;i<pad_len;i++) klog_putc_raw(pad);
    for(int i=len-1;i>=0;i--) klog_putc_raw(buf[i]);
}

static const char* klog_level_str(enum klog_level lvl){
    switch(lvl){
        case KLOG_OK: return "  OK  ";
        case KLOG_WARN: return " WARN ";
        case KLOG_ERROR: return " FAIL ";
        case KLOG_DEBUG: return " DEBUG";
        case KLOG_INFO: default: return " INFO ";
    }
}

static uint32_t klog_level_color(enum klog_level lvl){
    switch(lvl){
        case KLOG_OK: return KLOG_OK_FG;
        case KLOG_WARN: return KLOG_WARN_FG;
        case KLOG_ERROR: return KLOG_ERROR_FG;
        case KLOG_DEBUG: return KLOG_DEBUG_FG;
        case KLOG_INFO: default: return KLOG_INFO_FG;
    }
}

static void klog_print_timestamp(void){
    uint64_t now = rdtsc();
    uint64_t delta = klog_inited ? (now - klog_boot_tsc) : 0;
    uint64_t us = delta / 3000; // ~3GHz
    uint64_t sec = us / 1000000;
    uint64_t usec = us % 1000000;

    // цвет таймштампа приглушённый, как в linux
    if(gop_is_available()) gop_set_color(KLOG_TIME_FG, KLOG_BG);
    klog_putc_raw('[');
    klog_put_dec_padded(sec, 5, ' ');
    klog_putc_raw('.');
    klog_put_dec_padded(usec, 6, '0');
    klog_putc_raw(']');
    klog_putc_raw(' ');
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
}

static void klog_print_level(enum klog_level lvl){
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
    klog_putc_raw('[');
    if(gop_is_available()) gop_set_color(klog_level_color(lvl), KLOG_BG);
    klog_puts_raw(klog_level_str(lvl));
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
    klog_putc_raw(']');
    klog_putc_raw(' ');
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
}

static void klog_emit_prefix(enum klog_level lvl){
    klog_print_timestamp();
    klog_print_level(lvl);
}

void klog_init(void){
    klog_boot_tsc = rdtsc();
    klog_inited = true;
    klog_verbose = true;
    klog_ring_pos = 0;
    klog_ring_wrapped = false;

    if(gop_is_available()){
        gop_clear(KLOG_BG);
        gop_set_color(KLOG_FG, KLOG_BG);
    } else {
        vga_clear();
    }

    // шапка как в linux dmesg
    klog(KLOG_INFO, "PureC OS 64-bit kernel v0.1.0 [Limine+GOP] booting...");
    if(gop_is_available()){
        uint32_t w = gop_get_width();
        uint32_t h = gop_get_height();
        klog_emit_prefix(KLOG_INFO);
        klog_puts_raw("Framebuffer: ");
        klog_put_dec_unsigned(w);
        klog_putc_raw('x');
        klog_put_dec_unsigned(h);
        klog_puts_raw(" bpp=32");
        klog_putc_raw('\n');
    } else {
        klog(KLOG_INFO, "Framebuffer: VGA text 80x25");
    }
}

void klog_set_verbose(bool v){ klog_verbose = v; }
bool klog_is_verbose(void){ return klog_verbose; }
void klog_set_screen_enabled(bool e){ klog_screen_enabled = e; }
bool klog_is_screen_enabled(void){ return klog_screen_enabled; }

void klog_clear(void){
    if(gop_is_available()){
        gop_clear(KLOG_BG);
        gop_set_color(KLOG_FG, KLOG_BG);
    } else {
        vga_clear();
    }
}

void klog_raw(const char *s){
    if(!s) return;
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
    while(*s) klog_putc_raw(*s++);
}

void klog(enum klog_level lvl, const char *msg){
    if(!msg) return;
    if(lvl==KLOG_DEBUG && !klog_verbose) return;
    size_t mlen = strlen(msg);
    bool need_nl = (mlen==0 || msg[mlen-1]!='\n');
    klog_emit_prefix(lvl);
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
    while(*msg) klog_putc_raw(*msg++);
    if(need_nl) klog_putc_raw('\n');
}

// вспомогательная для форматированного вывода без префикса
static void klog_vprintf_internal(const char *fmt, va_list ap){
    if(gop_is_available()) gop_set_color(KLOG_FG, KLOG_BG);
    for(const char *p=fmt; *p; p++){
        if(*p != '%'){
            klog_putc_raw(*p);
            continue;
        }
        p++;
        if(*p=='\0') break;
        char pad=' ';
        int width=0;
        if(*p=='0'){ pad='0'; p++; }
        while(*p>='0' && *p<='9'){ width = width*10 + (*p-'0'); p++; }
        int long_cnt=0;
        while(*p=='l'){ long_cnt++; p++; }
        char spec = *p;
        if(spec=='\0') break;
        if(spec=='%'){ klog_putc_raw('%'); }
        else if(spec=='s'){
            const char *s = va_arg(ap, const char*);
            if(!s) s="(null)";
            int len = strlen(s);
            int pad_len = width - len;
            if(width>0 && pad_len>0) for(int i=0;i<pad_len;i++) klog_putc_raw(' ');
            while(*s) klog_putc_raw(*s++);
        } else if(spec=='c'){
            int ch = va_arg(ap, int);
            if(width>1) for(int i=1;i<width;i++) klog_putc_raw(' ');
            klog_putc_raw((char)ch);
        } else if(spec=='d' || spec=='i'){
            int64_t v;
            if(long_cnt>=2) v = va_arg(ap, int64_t);
            else if(long_cnt==1) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            char buf[32];
            bool neg=false;
            uint64_t uv;
            if(v<0){ neg=true; uv=(uint64_t)(-v); } else uv=(uint64_t)v;
            int len=0;
            if(uv==0) buf[len++]='0';
            else while(uv>0){ buf[len++]='0'+(uv%10); uv/=10; }
            if(neg) buf[len++]='-';
            int total=len;
            int pad_len = width - total;
            if(pad_len>0 && pad=='0' && neg){
                klog_putc_raw('-');
                for(int i=0;i<pad_len;i++) klog_putc_raw('0');
                for(int i=len-2;i>=0;i--) klog_putc_raw(buf[i]);
            } else {
                if(pad_len>0) for(int i=0;i<pad_len;i++) klog_putc_raw(pad);
                if(!(pad=='0' && neg)) for(int i=len-1;i>=0;i--) klog_putc_raw(buf[i]);
            }
        } else if(spec=='u'){
            uint64_t v;
            if(long_cnt>=2) v = va_arg(ap, uint64_t);
            else if(long_cnt==1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            char buf[32];
            int len=0;
            if(v==0) buf[len++]='0';
            else while(v>0){ buf[len++]='0'+(v%10); v/=10; }
            int pad_len = width - len;
            if(pad_len>0) for(int i=0;i<pad_len;i++) klog_putc_raw(pad);
            for(int i=len-1;i>=0;i--) klog_putc_raw(buf[i]);
        } else if(spec=='x' || spec=='X'){
            uint64_t v;
            if(long_cnt>=2) v = va_arg(ap, uint64_t);
            else if(long_cnt==1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            bool upper = (spec=='X');
            const char *h = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            char buf[16];
            int len=0;
            if(v==0) buf[len++]='0';
            else while(v>0){ buf[len++]=h[v&0xF]; v>>=4; }
            int pad_len = width - len;
            if(pad_len>0) for(int i=0;i<pad_len;i++) klog_putc_raw(pad);
            for(int i=len-1;i>=0;i--) klog_putc_raw(buf[i]);
        } else if(spec=='p'){
            void *ptr = va_arg(ap, void*);
            uint64_t v = (uint64_t)(uintptr_t)ptr;
            klog_puts_raw("0x");
            klog_put_hex(v, false);
        } else {
            // неизвестный спецификатор, выводим как есть
            klog_putc_raw('%');
            if(pad!=' ') klog_putc_raw('0');
            if(width) { char tmp[12]; int l=0; int w=width; if(w==0) {} else { char b[12]; int bl=0; while(w>0){b[bl++]='0'+w%10; w/=10;} while(bl--) tmp[l++]=b[bl]; } tmp[l]='\0'; klog_puts_raw(tmp); }
            for(int i=0;i<long_cnt;i++) klog_putc_raw('l');
            klog_putc_raw(spec);
        }
    }
}

void klogf(enum klog_level lvl, const char *fmt, ...){
    if(lvl==KLOG_DEBUG && !klog_verbose) return;
    klog_emit_prefix(lvl);
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf_internal(fmt, ap);
    va_end(ap);
    // гарантируем \n
    if(fmt && fmt[0]){
        size_t l = strlen(fmt);
        if(l>0 && fmt[l-1]!='\n') klog_putc_raw('\n');
    } else {
        klog_putc_raw('\n');
    }
}

void klog_vf(enum klog_level lvl, const char *fmt, va_list ap){
    if(lvl==KLOG_DEBUG && !klog_verbose) return;
    klog_emit_prefix(lvl);
    klog_vprintf_internal(fmt, ap);
    if(fmt && fmt[0]){
        size_t l = strlen(fmt);
        if(l>0 && fmt[l-1]!='\n') klog_putc_raw('\n');
    } else {
        klog_putc_raw('\n');
    }
}

void kprintf(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf_internal(fmt, ap);
    va_end(ap);
}

void kvprintf(const char *fmt, va_list ap){
    klog_vprintf_internal(fmt, ap);
}

void klog_dump(void){
    serial_write_string("\n--- klog dump ---\n");
    if(!klog_ring_wrapped){
        for(uint32_t i=0;i<klog_ring_pos;i++) serial_putc(klog_ring[i % KLOG_RING_SIZE]);
    } else {
        uint32_t start = klog_ring_pos % KLOG_RING_SIZE;
        for(uint32_t i=0;i<KLOG_RING_SIZE;i++){
            char c = klog_ring[(start + i) % KLOG_RING_SIZE];
            serial_putc(c);
        }
    }
    serial_write_string("\n--- end dump ---\n");
}

void klog_dump_to_screen(void){
    bool prev = klog_screen_enabled;
    klog_screen_enabled = true;
    if(gop_is_available()){
        gop_set_color(KLOG_FG, KLOG_BG);
    }
    if(!klog_ring_wrapped){
        for(uint32_t i=0;i<klog_ring_pos;i++){
            char c = klog_ring[i % KLOG_RING_SIZE];
            if(gop_is_available()) gop_putc(c); else vga_putc(c);
        }
    } else {
        uint32_t start = klog_ring_pos % KLOG_RING_SIZE;
        for(uint32_t i=0;i<KLOG_RING_SIZE;i++){
            char c = klog_ring[(start + i) % KLOG_RING_SIZE];
            if(gop_is_available()) gop_putc(c); else vga_putc(c);
        }
    }
    klog_screen_enabled = prev;
}

void klog_foreach(void (*cb)(char c)){
    if(!cb) return;
    uint64_t snapshot_pos=klog_ring_pos;
    bool snapshot_wrapped=klog_ring_wrapped;
    if(!snapshot_wrapped){
        for(uint64_t i=0;i<snapshot_pos;i++) cb(klog_ring[i % KLOG_RING_SIZE]);
    } else {
        uint32_t start = (uint32_t)(snapshot_pos % KLOG_RING_SIZE);
        for(uint32_t i=0;i<KLOG_RING_SIZE;i++) cb(klog_ring[(start + i) % KLOG_RING_SIZE]);
    }
}

void klog_dump_with(void (*cb)(char c)){
    klog_foreach(cb);
}

uint32_t klog_read_since(uint64_t *cursor, char *buffer, uint32_t capacity,
                         bool *data_lost){
    if(data_lost) *data_lost=false;
    if(!cursor || !buffer || capacity==0) return 0;
    uint64_t snapshot_pos=klog_ring_pos;
    uint64_t earliest=snapshot_pos>KLOG_RING_SIZE
        ? snapshot_pos-KLOG_RING_SIZE : 0;
    if(*cursor<earliest){
        *cursor=earliest;
        if(data_lost) *data_lost=true;
    }
    if(*cursor>snapshot_pos) *cursor=snapshot_pos;
    uint64_t available=snapshot_pos-*cursor;
    uint32_t amount=available<capacity ? (uint32_t)available : capacity;
    for(uint32_t index=0;index<amount;index++){
        buffer[index]=klog_ring[(*cursor+index)%KLOG_RING_SIZE];
    }
    *cursor+=amount;
    return amount;
}

uint64_t klog_total_bytes(void){
    return klog_ring_pos;
}
