#include "ps2_mouse.h"
#include "usb_mouse.h"
#include "../interrupts/pic.h"
#include "../serial/serial.h"
#include "../display/gop.h"
#include "../display/vga.h"
#include "../../kernel/diagnostics/klog.h"
#include <stdint.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_AUX_DATA    0x20

static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void io_wait(void){ outb(0x80,0); }

static bool ps2_wait_input(void){
    for(int i=0;i<100000;i++){ if(!(inb(PS2_STATUS)&2)) return true; __asm__ volatile("pause"); }
    return false;
}
static bool ps2_wait_output(void){
    for(int i=0;i<100000;i++){ if(inb(PS2_STATUS)&1) return true; __asm__ volatile("pause"); }
    return false;
}

static bool ps2_write_cmd(uint8_t cmd){
    if(!ps2_wait_input()) return false;
    outb(PS2_CMD, cmd);
    return true;
}
static bool ps2_write_data(uint8_t data){
    if(!ps2_wait_input()) return false;
    outb(PS2_DATA, data);
    return true;
}
static bool ps2_read_data_timeout(uint8_t *out){
    if(!ps2_wait_output()) return false;
    *out=inb(PS2_DATA);
    return true;
}
static uint8_t ps2_read_data(void){
    uint8_t v=0;
    (void)ps2_read_data_timeout(&v);
    return v;
}
static bool mouse_write(uint8_t data){
    if(!ps2_write_cmd(0xD4)) return false;
    if(!ps2_write_data(data)) return false;
    return true;
}
static uint8_t mouse_read_ack(void){
    uint8_t c = 0xFF;
    for(int tries=0; tries<10; tries++){
        if(ps2_read_data_timeout(&c)) break;
        for(volatile int d=0; d<10000; d++) __asm__ volatile("pause");
    }
    return c;
}

static struct mouse_state state = {.x=400,.y=300,.buttons=0};
static int32_t bound_w=1280, bound_h=800;
static uint8_t packet[4];
static int pkt_idx=0;
static bool has_mouse=false;
static int32_t old_x=400, old_y=300;
static bool first_draw=true;
static bool packet_seen=false;
static volatile uint32_t framebuffer_update_depth;
static volatile struct mouse_debug_state debug_state;

#define CURS_W 12
#define CURS_H 12
static uint32_t bg_buf[CURS_W*CURS_H];
static uint32_t cursor_color = 0xFFFFFF;
static uint32_t cursor_border = 0x000000;
static bool debug_overlay_enabled = false; // выключено чтобы не перекрывать boot log, как в linux - boot screen чистый

static void draw_cursor(int32_t x,int32_t y);
static void refresh_mouse_ui(void);

static void draw_hex(uint32_t x, uint32_t y, uint32_t value, int digits){
    char text[9];
    const char *hex="0123456789ABCDEF";
    for(int i=digits-1;i>=0;i--){ text[i]=hex[value&0xF]; value>>=4; }
    text[digits]=0;
    gop_draw_text_at(x, y, text, 0xCDD6F4, 0x313244);
}

static void draw_debug_overlay(void){
    if(!debug_overlay_enabled) return;
    if(!gop_is_available()) return;
    const uint32_t x=12, y=38, bg=0x313244;
    struct usb_mouse_info usb=usb_mouse_get_info();
    gop_draw_rect(x, y, 380, 84, bg);
    gop_draw_text_at(x+6, y+5, "MOUSE DEBUG", 0x89DCEB, bg);
    gop_draw_text_at(x+6, y+17, "INIT EN IF MIM SIM", 0xCDD6F4, bg);
    draw_hex(x+6,   y+27, debug_state.initialized, 2);
    draw_hex(x+46,  y+27, debug_state.enabled, 2);
    draw_hex(x+76,  y+27, debug_state.interrupts_enabled, 2);
    draw_hex(x+106, y+27, inb(0x21), 2);
    draw_hex(x+146, y+27, inb(0xA1), 2);
    gop_draw_text_at(x+6, y+39, "IRQ      POLL     PKT", 0xCDD6F4, bg);
    draw_hex(x+6,   y+49, debug_state.irq_count, 8);
    draw_hex(x+86,  y+49, debug_state.poll_count, 8);
    draw_hex(x+166, y+49, debug_state.packet_count, 8);
    gop_draw_text_at(x+6, y+61, "X    Y    USB REPORTS", 0xCDD6F4, bg);
    draw_hex(x+6,   y+71, (uint32_t)state.x, 4);
    draw_hex(x+54,  y+71, (uint32_t)state.y, 4);
    draw_hex(x+102, y+71, usb.connected, 2);
    draw_hex(x+142, y+71, usb.reports, 8);
}

static void save_bg(int32_t x,int32_t y){
    for(int dy=0;dy<CURS_H;dy++) for(int dx=0;dx<CURS_W;dx++)
        bg_buf[dy*CURS_W+dx] = gop_get_pixel((uint32_t)(x+dx), (uint32_t)(y+dy));
}

static void restore_bg(int32_t x,int32_t y){
    for(int dy=0;dy<CURS_H;dy++) for(int dx=0;dx<CURS_W;dx++)
        gop_put_pixel((uint32_t)(x+dx), (uint32_t)(y+dy), bg_buf[dy*CURS_W+dx]);
}

void mouse_set_bounds(int32_t w,int32_t h){ bound_w=w; bound_h=h; if(state.x>=w) state.x=w-1; if(state.y>=h) state.y=h-1; }

struct mouse_state mouse_get_state(void){ return state; }
struct mouse_debug_state mouse_get_debug_state(void){ return *(const struct mouse_debug_state *)&debug_state; }

void mouse_set_debug_overlay(bool enabled){
    if(enabled==debug_overlay_enabled) return;
    if(gop_is_available() && !first_draw){
        if(gop_has_backbuffer())
            gop_copy_back_to_front(old_x, old_y, CURS_W, CURS_H);
        else
            restore_bg(old_x,old_y);
    }
    debug_overlay_enabled=enabled;
    first_draw=true;
}
bool mouse_get_debug_overlay(void){ return debug_overlay_enabled; }

void mouse_redraw(void){
    mouse_begin_framebuffer_update();
    if(debug_overlay_enabled) draw_debug_overlay();
    mouse_end_framebuffer_update();
}

void mouse_begin_framebuffer_update(void){
    // Сначала открываем GOP-batch без cli чтобы ленивая аллокация
    // backbuffer не держала прерывания выключенными на миллисекунды.
    gop_begin_batch();
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    debug_state.interrupts_enabled=(flags&(1ULL<<9))!=0;
    if(framebuffer_update_depth==0){
        // С backbuffer курсор не прячем здесь: present в end сам
        // перезапишет сцену без курсора. Без backbuffer прячем сразу,
        // иначе сцена будет рисоваться поверх курсора и оставит мусор.
        if(!gop_has_backbuffer()){
            if(gop_is_available() && !first_draw) restore_bg(old_x,old_y);
            first_draw=true;
        }
    }
    framebuffer_update_depth++;
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void mouse_end_framebuffer_update(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    bool outermost=false;
    if(framebuffer_update_depth){
        framebuffer_update_depth--;
        if(framebuffer_update_depth==0) outermost=true;
    }
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");

    gop_end_batch();
    if(outermost)
        draw_cursor(state.x,state.y);
}

static inline bool cursor_inside(int dx, int dy){
    if(dy==0 && dx<8) return true;
    if(dy<8 && dx<=dy) return true;
    if(dy>=8 && dy<10 && dx<3) return true;
    return false;
}

static inline uint32_t cursor_pixel(int dx, int dy){
    bool border = (dx==0 || dy==0 || dx==dy || (dy>=8 && (dx==0||dx==2)));
    return border ? cursor_border : cursor_color;
}

// Рисует курсор как в Linux: стрелка 12x12
static void draw_cursor(int32_t x,int32_t y){
    if(!gop_is_available()){
        // VGA text 80x25, пиксели -> символы
        int tx = x / 8;
        int ty = y / 16;
        int otx = old_x / 8;
        int oty = old_y / 16;
        volatile uint16_t *vga = (volatile uint16_t*)(uintptr_t)0xB8000;
        if(!first_draw){
            if(otx>=0 && otx<80 && oty>=0 && oty<25) vga[oty*80+otx] = (0x0F<<8)|' ';
            if(tx>=0 && tx<80 && ty>=0 && ty<25) vga[ty*80+tx] = (0x8F<<8)|0xDB;
        } else {
            if(tx>=0 && tx<80 && ty>=0 && ty<25) vga[ty*80+tx] = (0x8F<<8)|0xDB;
        }
        old_x=x; old_y=y;
        first_draw=false;
        return;
    }
    if(gop_has_backbuffer()){
        // Сцена без курсора лежит в backbuffer: стирание = blit сцены.
        // bg_buf не используем - он протухает при перерисовке сцены.
        if(!first_draw
           && (x!=old_x || y!=old_y)){
            gop_copy_back_to_front(old_x, old_y, CURS_W, CURS_H);
        } else if(!first_draw){
            // Позиция та же (например present после redraw уже стер),
            // все равно восстанавливаем фон чтобы убрать старый курсор.
            gop_copy_back_to_front(old_x, old_y, CURS_W, CURS_H);
        }
        first_draw=false;
        for(int dy=0;dy<CURS_H;dy++){
            for(int dx=0;dx<CURS_W;dx++){
                if(cursor_inside(dx,dy))
                    gop_put_pixel_front((uint32_t)(x+dx), (uint32_t)(y+dy),
                                        cursor_pixel(dx,dy));
            }
        }
        old_x=x; old_y=y;
        return;
    }
    if(!first_draw){
        restore_bg(old_x, old_y);
    }
    save_bg(x, y);
    first_draw=false;
    for(int dy=0;dy<CURS_H;dy++){
        for(int dx=0;dx<CURS_W;dx++){
            if(cursor_inside(dx,dy)){
                gop_put_pixel((uint32_t)(x+dx), (uint32_t)(y+dy),
                              cursor_pixel(dx,dy));
            }
        }
    }
    old_x=x; old_y=y;
}

static bool process_mouse_byte(uint8_t data){
    debug_state.last_byte=data;

    if(pkt_idx==0 && (data & 0x08)==0){
        return false;
    }
    packet[pkt_idx++] = data;
    if(pkt_idx==3){
        uint8_t b0 = packet[0];
        int8_t dx = (int8_t)packet[1];
        int8_t dy = (int8_t)packet[2];
        mouse_handle_relative(b0&0x07,dx,(int8_t)-dy);
        debug_state.packet_count++;

        if(!packet_seen){
            packet_seen=true;
            klog(KLOG_INFO, "psmouse: packets active, mouse moving");
        }

        pkt_idx=0;
        return true;
    }
    return false;
}

void mouse_handle_relative(uint8_t buttons, int8_t dx, int8_t dy){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    state.dx=dx;
    state.dy=dy;
    state.x+=dx;
    state.y+=dy;
    if(state.x<0) state.x=0;
    if(state.y<0) state.y=0;
    if(state.x>=bound_w-CURS_W) state.x=bound_w-CURS_W-1;
    if(state.y>=bound_h-CURS_H) state.y=bound_h-CURS_H-1;
    state.buttons=buttons&0x07;
    state.has_data=true;
    refresh_mouse_ui();
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

static void refresh_mouse_ui(void){
    // Вызывается под cli из mouse_handle_relative (IRQ/poll).
    // Во время пакетного рисования сцены курсор не трогаем:
    // mouse_end дорисует его в новой позиции одним махом.
    if(framebuffer_update_depth) return;
    if(!gop_is_available()){
        draw_cursor(state.x, state.y);
        return;
    }
    if(gop_has_backbuffer()){
        // Стираем старый курсор блитом сцены, затем оверлей (в backbuffer
        // + present) и новый курсор поверх. Без промежуточных clear'ов -
        // поэтому движения мыши больше не мигают.
        if(!first_draw)
            gop_copy_back_to_front(old_x, old_y, CURS_W, CURS_H);
        first_draw=true;
        if(debug_overlay_enabled){
            gop_begin_batch();
            draw_debug_overlay();
            gop_end_batch();
        }
        draw_cursor(state.x, state.y);
        return;
    }
    if(!first_draw) restore_bg(old_x, old_y);
    first_draw=true;
    if(debug_overlay_enabled) draw_debug_overlay();
    draw_cursor(state.x, state.y);
}

void ps2_mouse_handler(void){
    uint8_t status = inb(PS2_STATUS);
    debug_state.controller_status=status;
    debug_state.irq_count++;
    // IRQ12 should carry AUX data, but verify it before touching shared 0x60.
    // A keyboard byte must remain available for keyboard_poll().
    if((status & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA))
        == (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)){
        (void)process_mouse_byte(inb(PS2_DATA));
    }
}

void ps2_mouse_poll(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    debug_state.interrupts_enabled=(flags & (1ULL<<9)) != 0;
    uint8_t status=inb(PS2_STATUS);
    debug_state.controller_status=status;
    // Port 0x60 is shared with the keyboard. Consume AUX bytes only.
    if((status & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA))
        == (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)){
        debug_state.poll_count++;
        (void)process_mouse_byte(inb(PS2_DATA));
    }
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");
}

void ps2_mouse_init(void){
    klog(KLOG_INFO, "psmouse: initializing PS/2 mouse (Linux psmouse style)");

    uint8_t probe_status = inb(PS2_STATUS);
    if(probe_status == 0xFF){
        klog(KLOG_WARN, "psmouse: no 8042 controller (status 0xFF) – PS/2 disabled, using USB mouse only");
        debug_state.initialized=true;
        debug_state.enabled=false;
        goto ps2_init_done;
    }

    if(!ps2_write_cmd(0xA8)){
        klog(KLOG_WARN, "psmouse: 8042 not responding to 0xA8");
        goto ps2_init_no_mouse;
    }
    io_wait();
    if(!ps2_write_cmd(0x20)){
        klog(KLOG_WARN, "psmouse: 8042 not responding to read cmd byte");
        goto ps2_init_no_mouse;
    }
    uint8_t status;
    if(!ps2_read_data_timeout(&status)){
        klog(KLOG_WARN, "psmouse: 8042 no command byte reply – PS/2 disabled");
        goto ps2_init_no_mouse;
    }
    klogf(KLOG_DEBUG, "psmouse: command byte=0x%x", status);
    status |= 0x02; // enable IRQ12
    status &= ~0x20; // enable mouse
    if(!ps2_write_cmd(0x60) || !ps2_write_data(status)){
        klog(KLOG_WARN, "psmouse: 8042 failed to write command byte");
        goto ps2_init_no_mouse;
    }
    io_wait();

    if(!mouse_write(0xFF)){
        klog(KLOG_WARN, "psmouse: mouse_write 0xFF failed – no PS/2 mouse");
        goto ps2_init_no_mouse;
    }
    uint8_t ack = mouse_read_ack();
    debug_state.reset_ack=ack;
    if(ack==0xFA){
        uint8_t bat=0xFF, id=0xFF;
        bool has_bat = ps2_read_data_timeout(&bat);
        bool has_id = ps2_read_data_timeout(&id);
        (void)has_bat; (void)has_id;
        klog(KLOG_OK, "psmouse: reset OK (BAT 0xAA)");
    } else {
        klogf(KLOG_WARN, "psmouse: reset ack=0x%x (no mouse or not PS/2)", ack);
        // не считаем фатальным – пробуем дальше, может мышь всё равно ответит на enable
    }
    // 4. Set defaults
    if(mouse_write(0xF6)) (void)mouse_read_ack();
    // 5. Включить поток данных
    bool en_ok = false;
    if(mouse_write(0xF4)){
        uint8_t ack2 = mouse_read_ack();
        debug_state.enable_ack=ack2;
        if(ack2==0xFA){
            has_mouse=true;
            debug_state.enabled=true;
            en_ok=true;
            klog(KLOG_OK, "psmouse: enabled, IRQ12 active");
        } else {
            klogf(KLOG_WARN, "psmouse: enable failed ack=0x%x – PS/2 mouse not present", ack2);
        }
    } else {
        klog(KLOG_WARN, "psmouse: mouse_write 0xF4 failed");
    }
    if(!en_ok){
        // PS/2 мыши нет – это нормально на ноутбуках без PS/2, продолжим с USB
    ps2_init_no_mouse:
        debug_state.enabled=false;
        has_mouse=false;
        klog(KLOG_INFO, "psmouse: PS/2 mouse not detected – USB mouse will be used if present");
    }
    // Размаскировать IRQ12 только если есть шанс что мышь есть
    if(has_mouse || debug_state.enabled){
        uint8_t m1 = inb(0x21);
        uint8_t m2 = inb(0xA1);
        m1 &= ~(1<<2); // cascade
        m2 &= ~(1<<4); // IRQ12 -> slave bit4
        outb(0x21, m1);
        outb(0xA1, m2);
        klog(KLOG_DEBUG, "psmouse: PIC unmasked IRQ2+IRQ12");
    } else {
        klog(KLOG_DEBUG, "psmouse: PIC IRQ12 left masked (no PS/2 mouse)");
        // оставляем замаскированным чтобы не получать spurious IRQ12
    }

ps2_init_done:

    if(gop_is_available()){
        bound_w = gop_get_width();
        bound_h = gop_get_height();
        if(bound_w==0) bound_w=1280;
        if(bound_h==0) bound_h=800;
    } else {
        bound_w = 80*8; bound_h = 25*16; // VGA text в пикселях
    }
    state.x = bound_w/2;
    state.y = bound_h/2;
    old_x = state.x; old_y = state.y;
    debug_state.initialized=true;
    mouse_redraw();
    pkt_idx=0;
}
