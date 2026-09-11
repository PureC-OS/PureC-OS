#include "keyboard.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/process/scheduler.h"
#include <stdint.h>
#include <stdbool.h>

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define KBD_CMD 0x64
#define KBD_STATUS_OUTPUT_FULL 0x01
#define KBD_STATUS_INPUT_FULL  0x02
#define KBD_STATUS_AUX_DATA    0x20
#define KBD_CMD_READ_CONFIG     0x20
#define KBD_CMD_DISABLE_AUX     0xA7
#define KBD_CMD_ENABLE_AUX      0xA8
#define KBD_CMD_DISABLE_KBD     0xAD
#define KBD_CMD_ENABLE_KBD      0xAE
#define KBD_CONFIG_TRANSLATION  0x40
#define KBD_DEVICE_ACK          0xFA
#define KBD_DEVICE_RESEND       0xFE
#define KBD_DEVICE_DISABLE_SCAN 0xF5
#define KBD_DEVICE_SCAN_SET     0xF0
#define KBD_DEVICE_ENABLE_SCAN  0xF4

static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t ret; __asm__ volatile("inb %1,%0":"=a"(ret):"Nd"(port)); return ret; }
static inline void io_wait(void){ outb(0x80,0); }

static bool shift_pressed = false;
static bool control_pressed = false;
static bool caps_lock = false;
static uint8_t active_scan_set = 1;
static bool set2_break_pending = false;
static bool set2_extended = false;
static bool set1_extended = false;
static uint8_t set2_pause_bytes = 0;

// US QWERTY scancode set 1, без shift
static const char scancode_set1_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_set1_shift_map[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// Native PS/2 Scan Code Set 2. Real hardware commonly exposes this set when
// the 8042 translation bit is disabled. For example, A=0x1C and Enter=0x5A.
static const char scancode_set2_map[128] = {
    [0x0D]='\t', [0x0E]='`',
    [0x15]='q', [0x16]='1', [0x1A]='z', [0x1B]='s', [0x1C]='a', [0x1D]='w', [0x1E]='2',
    [0x21]='c', [0x22]='x', [0x23]='d', [0x24]='e', [0x25]='4', [0x26]='3', [0x29]=' ',
    [0x2A]='v', [0x2B]='f', [0x2C]='t', [0x2D]='r', [0x2E]='5',
    [0x31]='n', [0x32]='b', [0x33]='h', [0x34]='g', [0x35]='y', [0x36]='6',
    [0x3A]='m', [0x3B]='j', [0x3C]='u', [0x3D]='7', [0x3E]='8',
    [0x41]=',', [0x42]='k', [0x43]='i', [0x44]='o', [0x45]='0', [0x46]='9',
    [0x49]='.', [0x4A]='/', [0x4B]='l', [0x4C]=';', [0x4D]='p', [0x4E]='-',
    [0x52]='\'', [0x54]='[', [0x55]='=', [0x5A]='\n', [0x5B]=']', [0x5D]='\\',
    [0x66]='\b', [0x76]=27
};

static const char scancode_set2_shift_map[128] = {
    [0x0D]='\t', [0x0E]='~',
    [0x15]='Q', [0x16]='!', [0x1A]='Z', [0x1B]='S', [0x1C]='A', [0x1D]='W', [0x1E]='@',
    [0x21]='C', [0x22]='X', [0x23]='D', [0x24]='E', [0x25]='$', [0x26]='#', [0x29]=' ',
    [0x2A]='V', [0x2B]='F', [0x2C]='T', [0x2D]='R', [0x2E]='%',
    [0x31]='N', [0x32]='B', [0x33]='H', [0x34]='G', [0x35]='Y', [0x36]='^',
    [0x3A]='M', [0x3B]='J', [0x3C]='U', [0x3D]='&', [0x3E]='*',
    [0x41]='<', [0x42]='K', [0x43]='I', [0x44]='O', [0x45]=')', [0x46]='(',
    [0x49]='>', [0x4A]='?', [0x4B]='L', [0x4C]=':', [0x4D]='P', [0x4E]='_',
    [0x52]='"', [0x54]='{', [0x55]='+', [0x5A]='\n', [0x5B]='}', [0x5D]='|',
    [0x66]='\b', [0x76]=27
};

#define KBD_BUF_SIZE 128
#define KBD_SPECIAL_BUF_SIZE 16
static char kbd_buf[KBD_BUF_SIZE];
static uint8_t kbd_head = 0, kbd_tail = 0, kbd_count = 0;
static uint8_t special_buf[KBD_SPECIAL_BUF_SIZE];
static uint8_t special_head = 0, special_tail = 0, special_count = 0;

static void kbd_push(char c){
    if(kbd_count >= KBD_BUF_SIZE) return;
    kbd_buf[kbd_head] = c;
    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
    kbd_count++;
}

static bool kbd_pop(char *out){
    if(kbd_count==0) return false;
    *out = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    kbd_count--;
    return true;
}

static void special_push(uint8_t key){
    if(special_count >= KBD_SPECIAL_BUF_SIZE) return;
    special_buf[special_head] = key;
    special_head = (special_head + 1) % KBD_SPECIAL_BUF_SIZE;
    special_count++;
}

static bool special_pop(uint8_t *out){
    if(special_count==0) return false;
    *out = special_buf[special_tail];
    special_tail = (special_tail + 1) % KBD_SPECIAL_BUF_SIZE;
    special_count--;
    return true;
}

static void emit_key(const char *plain, const char *shifted, uint8_t sc){
    char base = plain[sc];
    char c;
    if(base >= 'a' && base <= 'z'){
        c = (shift_pressed ^ caps_lock) ? shifted[sc] : base;
    } else {
        c = shift_pressed ? shifted[sc] : base;
        if(!c) c = base;
    }
    if(control_pressed && c>='a' && c<='z') c=(char)(c-'a'+1);
    else if(control_pressed && c>='A' && c<='Z') c=(char)(c-'A'+1);
    if(c) kbd_push(c);
}

static void handle_scancode_set1(uint8_t sc){
    if(sc==0xE0){ set1_extended=true; return; }
    if(set1_extended){
        bool released=(sc&0x80)!=0;
        sc&=0x7F;
        set1_extended=false;
        if(released) return;
        switch(sc){
            case 0x47: special_push(KEYBOARD_SPECIAL_HOME); break;
            case 0x4F: special_push(KEYBOARD_SPECIAL_END); break;
            case 0x49: special_push(KEYBOARD_SPECIAL_PAGE_UP); break;
            case 0x51: special_push(KEYBOARD_SPECIAL_PAGE_DOWN); break;
            case 0x4B: special_push(KEYBOARD_SPECIAL_LEFT); break;
            case 0x4D: special_push(KEYBOARD_SPECIAL_RIGHT); break;
            case 0x48: special_push(KEYBOARD_SPECIAL_UP); break;
            case 0x50: special_push(KEYBOARD_SPECIAL_DOWN); break;
            case 0x53: special_push(KEYBOARD_SPECIAL_DELETE); break;
            default: break;
        }
        return;
    }
    if(sc == 0x1D){
        control_pressed = true;
        return;
    }
    if(sc == 0x9D){
        control_pressed = false;
        return;
    }
    if(sc == 0x2A || sc == 0x36){ // LSHIFT / RSHIFT press
        shift_pressed = true;
        return;
    }
    if(sc == 0xAA || sc == 0xB6){ // shift release
        shift_pressed = false;
        return;
    }
    if(sc == 0x3A){ // caps
        caps_lock = !caps_lock;
        return;
    }
    if(sc & 0x80){
        // release of other keys, ignore
        return;
    }
    if(sc == 0x3B){
        special_push(KEYBOARD_SPECIAL_F1);
        return;
    }
    if(sc == 0x3C){
        special_push(KEYBOARD_SPECIAL_F2);
        return;
    }
    if(sc == 0x3D){
        special_push(KEYBOARD_SPECIAL_F3);
        return;
    }
    if(sc >= 128) return;
    emit_key(scancode_set1_map, scancode_set1_shift_map, sc);
}

static void handle_scancode_set2(uint8_t sc){
    if(set2_pause_bytes){
        set2_pause_bytes--;
        return;
    }
    if(sc == 0xE1){
        // Pause is E1 14 77 E1 F0 14 F0 77 and produces no shell character.
        set2_pause_bytes = 7;
        set2_break_pending = false;
        set2_extended = false;
        return;
    }
    if(sc == 0xE0){
        set2_extended = true;
        return;
    }
    if(sc == 0xF0){
        set2_break_pending = true;
        return;
    }

    bool released = set2_break_pending;
    bool extended = set2_extended;
    set2_break_pending = false;
    set2_extended = false;

    if(sc == 0x12 || sc == 0x59){ // LSHIFT / RSHIFT
        shift_pressed = !released;
        return;
    }
    if(sc == 0x14){
        control_pressed = !released;
        return;
    }
    if(released) return;
    if(extended){
        if(sc == 0x5A) kbd_push('\n'); // keypad Enter
        if(sc == 0x4A) kbd_push('/');  // keypad slash
        if(sc == 0x6C) special_push(KEYBOARD_SPECIAL_HOME);
        if(sc == 0x69) special_push(KEYBOARD_SPECIAL_END);
        if(sc == 0x7D) special_push(KEYBOARD_SPECIAL_PAGE_UP);
        if(sc == 0x7A) special_push(KEYBOARD_SPECIAL_PAGE_DOWN);
        if(sc == 0x6B) special_push(KEYBOARD_SPECIAL_LEFT);
        if(sc == 0x74) special_push(KEYBOARD_SPECIAL_RIGHT);
        if(sc == 0x75) special_push(KEYBOARD_SPECIAL_UP);
        if(sc == 0x72) special_push(KEYBOARD_SPECIAL_DOWN);
        if(sc == 0x71) special_push(KEYBOARD_SPECIAL_DELETE);
        return;
    }
    if(sc == 0x05){
        special_push(KEYBOARD_SPECIAL_F1);
        return;
    }
    if(sc == 0x06){
        special_push(KEYBOARD_SPECIAL_F2);
        return;
    }
    if(sc == 0x04){
        special_push(KEYBOARD_SPECIAL_F3);
        return;
    }
    if(sc == 0x58){ // Caps Lock
        caps_lock = !caps_lock;
        return;
    }
    if(sc < 128) emit_key(scancode_set2_map, scancode_set2_shift_map, sc);
}

static void handle_scancode(uint8_t sc){
    if(active_scan_set == 2) handle_scancode_set2(sc);
    else handle_scancode_set1(sc);
}

static bool wait_input_empty(void){
    for(uint32_t i=0; i<100000; i++){
        if(!(inb(KBD_STATUS) & KBD_STATUS_INPUT_FULL)) return true;
    }
    return false;
}

static bool send_controller_command(uint8_t command){
    if(!wait_input_empty()) return false;
    outb(KBD_CMD, command);
    io_wait();
    return true;
}

static bool read_controller_config(uint8_t *config){
    bool ok = send_controller_command(KBD_CMD_DISABLE_KBD)
        && send_controller_command(KBD_CMD_DISABLE_AUX);

    // Both ports are stopped, so stale bytes can be discarded safely.
    for(uint32_t i=0; i<32 && (inb(KBD_STATUS) & KBD_STATUS_OUTPUT_FULL); i++){
        (void)inb(KBD_DATA);
    }

    if(ok && send_controller_command(KBD_CMD_READ_CONFIG)){
        for(uint32_t i=0; i<100000; i++){
            uint8_t status = inb(KBD_STATUS);
            if(status & KBD_STATUS_OUTPUT_FULL){
                *config = inb(KBD_DATA);
                ok = true;
                goto enable_ports;
            }
        }
    }
    ok = false;

enable_ports:
    if(!send_controller_command(KBD_CMD_ENABLE_KBD)) ok = false;
    if(!send_controller_command(KBD_CMD_ENABLE_AUX)) ok = false;
    return ok;
}

static bool send_keyboard_device_byte(uint8_t value){
    for(uint32_t attempt=0; attempt<3; attempt++){
        if(!wait_input_empty()) return false;
        outb(KBD_DATA, value);

        for(uint32_t i=0; i<100000; i++){
            uint8_t status = inb(KBD_STATUS);
            if(!(status & KBD_STATUS_OUTPUT_FULL)) continue;
            uint8_t reply = inb(KBD_DATA);
            if(status & KBD_STATUS_AUX_DATA) continue;
            if(reply == KBD_DEVICE_ACK) return true;
            if(reply == KBD_DEVICE_RESEND) break;
            return false;
        }
    }
    return false;
}

static bool configure_native_scan_set2(void){
    // Stop AUX traffic while command replies share port 0x60 with the mouse.
    bool ok = send_controller_command(KBD_CMD_DISABLE_AUX)
        && send_controller_command(KBD_CMD_ENABLE_KBD);

    for(uint32_t i=0; i<32 && (inb(KBD_STATUS) & KBD_STATUS_OUTPUT_FULL); i++){
        (void)inb(KBD_DATA);
    }

    if(ok) ok = send_keyboard_device_byte(KBD_DEVICE_DISABLE_SCAN);
    if(ok) ok = send_keyboard_device_byte(KBD_DEVICE_SCAN_SET);
    if(ok) ok = send_keyboard_device_byte(0x02);

    // Always try to resume scanning, including recovery from a failed Set 2 command.
    bool enable_scan_ok = send_keyboard_device_byte(KBD_DEVICE_ENABLE_SCAN);
    if(!enable_scan_ok) ok = false;

    if(!send_controller_command(KBD_CMD_ENABLE_AUX)) ok = false;
    return ok;
}

static bool decoder_self_test(void){
    active_scan_set = 2;
    shift_pressed = false;
    control_pressed = false;
    caps_lock = false;
    set2_break_pending = false;
    set2_extended = false;
    set1_extended = false;
    set2_pause_bytes = 0;
    kbd_head = kbd_tail = kbd_count = 0;
    special_head = special_tail = special_count = 0;

    handle_scancode(0x1C); // A make in Set 2
    handle_scancode(0xF0);
    handle_scancode(0x1C); // A break
    handle_scancode(0x5A); // Enter make in Set 2
    handle_scancode(0xE0);
    handle_scancode(0x7D); // Page Up make in Set 2

    char first=0, second=0, extra=0;
    uint8_t navigation=0,extra_special=0;
    bool ok = kbd_pop(&first) && kbd_pop(&second) && !kbd_pop(&extra)
        && first=='a' && second=='\n'
        && special_pop(&navigation)
        && navigation==KEYBOARD_SPECIAL_PAGE_UP
        && !special_pop(&extra_special);

    shift_pressed = false;
    control_pressed = false;
    caps_lock = false;
    set2_break_pending = false;
    set2_extended = false;
    set1_extended = false;
    set2_pause_bytes = 0;
    kbd_head = kbd_tail = kbd_count = 0;
    special_head = special_tail = special_count = 0;
    return ok;
}

void keyboard_poll(void){
    for(;;){
        uint8_t status = inb(KBD_STATUS);
        if(!(status & KBD_STATUS_OUTPUT_FULL)) break;
        // Port 0x60 is shared with the PS/2 mouse. Leave AUX bytes for
        // ps2_mouse_poll()/IRQ12 instead of decoding them as scan codes.
        if(status & KBD_STATUS_AUX_DATA) break;
        uint8_t sc = inb(KBD_DATA);
        handle_scancode(sc);
    }
}

bool keyboard_has_key(void){
    keyboard_poll();
    return kbd_count > 0;
}

bool keyboard_try_getc(char *out){
    keyboard_poll();
    return kbd_pop(out);
}

bool keyboard_try_get_special(uint8_t *out){
    keyboard_poll();
    return special_pop(out);
}

char keyboard_getc(void){
    char c;
    while(!keyboard_try_getc(&c)){
        scheduler_yield();
        __asm__ volatile("pause");
        keyboard_poll();
    }
    return c;
}

void keyboard_init(void){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");

    uint8_t config = 0;
    bool config_ok = read_controller_config(&config);
    uint8_t detected_scan_set = config_ok && (config & KBD_CONFIG_TRANSLATION) ? 1 : 2;
    bool device_config_ok = true;
    if(detected_scan_set == 2) device_config_ok = configure_native_scan_set2();

    // оставляем IRQ1 замаскированным для polling (чтобы не триггерить vector 33 без handler)
    uint8_t mask = inb(0x21);
    mask |= (1<<1);
    outb(0x21, mask);

    bool self_test_ok = decoder_self_test();
    active_scan_set = detected_scan_set;
    if(flags & (1ULL<<9)) __asm__ volatile("sti":::"memory");

    if(!self_test_ok) klog(KLOG_ERROR, "keyboard: Scan Code Set 2 decoder self-test failed");
    if(!config_ok) klog(KLOG_WARN, "keyboard: 8042 config read failed, assuming Scan Code Set 2");
    if(!device_config_ok) klog(KLOG_WARN, "keyboard: device rejected Scan Code Set 2 setup");
    klogf(KLOG_INFO, "keyboard: PS/2 polling ready (Scan Code Set %d, config=0x%x, decoder=%s)",
          active_scan_set, config, self_test_ok ? "ok" : "failed");
}

void keyboard_set_leds(bool caps, bool num, bool scroll){
    (void)caps; (void)num; (void)scroll;
    // TODO: send 0xED + leds
}
