#include "gop.h"
#include "vga.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include <stdint.h>
#include <stddef.h>

static struct gop_state gop = {0};

static uint32_t *backbuffer = 0;
static uint64_t backbuffer_phys = 0;
static uint64_t backbuffer_pages = 0;
static uint32_t backbuffer_width = 0;
static uint32_t backbuffer_height = 0;
static uint32_t batch_depth = 0;
static bool dirty_valid = false;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
static uint32_t cur_x=12, cur_y=12;
static uint32_t fg=0xCDD6F4, bg=0x1E1E2E;
static enum gop_font_face font_face=GOP_FONT_CLEAN;

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

// тот же 8x8 font что в fb.c
static const uint8_t font[128][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 space
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},{0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},{0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
  {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},{0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},{0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00},{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
  {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},{0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
  {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},{0x7C,0xCE,0xDE,0xF6,0xE6,0xC6,0x7C,0x00},
  {0x30,0x70,0x30,0x30,0x30,0x30,0xFC,0x00},{0x78,0xCC,0x0C,0x38,0x60,0xCC,0xFC,0x00},{0x78,0xCC,0x0C,0x38,0x0C,0xCC,0x78,0x00},{0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},
  {0xFC,0xC0,0xF8,0x0C,0x0C,0xCC,0x78,0x00},{0x38,0x60,0xC0,0xF8,0xCC,0xCC,0x78,0x00},{0xFC,0xCC,0x0C,0x18,0x30,0x30,0x30,0x00},{0x78,0xCC,0xCC,0x78,0xCC,0xCC,0x78,0x00},
  {0x78,0xCC,0xCC,0xFC,0x0C,0x18,0x70,0x00},{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},{0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},{0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
  {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},{0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},{0x78,0xCC,0x0C,0x18,0x18,0x00,0x18,0x00},{0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},
  {0x30,0x78,0xCC,0xCC,0xFC,0xCC,0xCC,0x00},{0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},{0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},{0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
  {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},{0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},{0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},{0xCC,0xCC,0xCC,0xFC,0xCC,0xCC,0xCC,0x00},
  {0x78,0x30,0x30,0x30,0x30,0x30,0x78,0x00},{0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},{0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},{0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
  {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00},{0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},{0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0x00},{0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
  {0x78,0xCC,0xCC,0xCC,0xDC,0x78,0x1C,0x00},{0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},{0x78,0xCC,0xE0,0x70,0x1C,0xCC,0x78,0x00},{0xFC,0xB4,0x30,0x30,0x30,0x30,0x78,0x00},
  {0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xFC,0x00},{0xCC,0xCC,0xCC,0xCC,0xCC,0x78,0x30,0x00},{0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},{0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
  {0xCC,0xCC,0xCC,0x78,0x30,0x30,0x78,0x00},{0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},{0x78,0x60,0x60,0x60,0x60,0x60,0x78,0x00},{0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
  {0x78,0x18,0x18,0x18,0x18,0x18,0x78,0x00},{0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00},{0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},{0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00},{0x00,0x00,0x78,0xCC,0xC0,0xCC,0x78,0x00},{0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00},
  {0x00,0x00,0x78,0xCC,0xFC,0xC0,0x78,0x00},{0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00},{0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78},{0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
  {0x30,0x00,0x70,0x30,0x30,0x30,0x78,0x00},{0x0C,0x00,0x0C,0x0C,0x0C,0xCC,0xCC,0x78},{0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},{0x70,0x30,0x30,0x30,0x30,0x30,0x78,0x00},
  {0x00,0x00,0xCC,0xFE,0xFE,0xD6,0xC6,0x00},{0x00,0x00,0xE6,0x66,0x66,0x66,0xE6,0x00},{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},{0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
  {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},{0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},{0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},{0x30,0x30,0xFC,0x30,0x30,0xCC,0x78,0x00},
  {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},{0x00,0x00,0xCC,0xCC,0xCC,0x78,0x30,0x00},{0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x6C,0x00},{0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
  {0x00,0x00,0xCC,0xCC,0xCC,0x7C,0x0C,0x78},{0x00,0x00,0xFE,0xCC,0x18,0x30,0xFE,0x00},{0x38,0x60,0x60,0x30,0x60,0x60,0x38,0x00},{0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
  {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},{0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// Compact 5x7 face. Rows use the low five bits and are centered by the renderer.
static const uint8_t clean_font[128][7] = {
    [' ']={0,0,0,0,0,0,0}, ['!']={4,4,4,4,4,0,4}, ['"']={10,10,0,0,0,0,0},
    ['#']={10,31,10,10,31,10,0}, ['$']={4,15,20,14,5,30,4},
    ['%']={25,26,4,8,11,19,0}, ['&']={12,18,20,8,21,18,13},
    ['\'']={4,4,0,0,0,0,0}, ['(']={2,4,8,8,8,4,2}, [')']={8,4,2,2,2,4,8},
    ['*']={0,21,14,31,14,21,0}, ['+']={0,4,4,31,4,4,0},
    [',']={0,0,0,0,0,4,8}, ['-']={0,0,0,31,0,0,0}, ['.']={0,0,0,0,0,0,4},
    ['/']={1,2,4,8,16,0,0},
    ['0']={14,17,19,21,25,17,14}, ['1']={4,12,4,4,4,4,14},
    ['2']={14,17,1,2,4,8,31}, ['3']={30,1,1,14,1,1,30},
    ['4']={2,6,10,18,31,2,2}, ['5']={31,16,16,30,1,1,30},
    ['6']={14,16,16,30,17,17,14}, ['7']={31,1,2,4,8,8,8},
    ['8']={14,17,17,14,17,17,14}, ['9']={14,17,17,15,1,1,14},
    [':']={0,4,0,0,4,0,0}, [';']={0,4,0,0,4,4,8},
    ['<']={2,4,8,16,8,4,2}, ['=']={0,31,0,31,0,0,0},
    ['>']={8,4,2,1,2,4,8}, ['?']={14,17,1,2,4,0,4},
    ['@']={14,17,23,21,23,16,14},
    ['A']={14,17,17,31,17,17,17}, ['B']={30,17,17,30,17,17,30},
    ['C']={14,17,16,16,16,17,14}, ['D']={30,17,17,17,17,17,30},
    ['E']={31,16,16,30,16,16,31}, ['F']={31,16,16,30,16,16,16},
    ['G']={14,17,16,23,17,17,14}, ['H']={17,17,17,31,17,17,17},
    ['I']={14,4,4,4,4,4,14}, ['J']={7,2,2,2,2,18,12},
    ['K']={17,18,20,24,20,18,17}, ['L']={16,16,16,16,16,16,31},
    ['M']={17,27,21,21,17,17,17}, ['N']={17,25,21,19,17,17,17},
    ['O']={14,17,17,17,17,17,14}, ['P']={30,17,17,30,16,16,16},
    ['Q']={14,17,17,17,21,18,13}, ['R']={30,17,17,30,20,18,17},
    ['S']={15,16,16,14,1,1,30}, ['T']={31,4,4,4,4,4,4},
    ['U']={17,17,17,17,17,17,14}, ['V']={17,17,17,17,17,10,4},
    ['W']={17,17,17,21,21,21,10}, ['X']={17,17,10,4,10,17,17},
    ['Y']={17,17,10,4,4,4,4}, ['Z']={31,1,2,4,8,16,31},
    ['[']={14,8,8,8,8,8,14}, ['\\']={16,8,4,2,1,0,0},
    [']']={14,2,2,2,2,2,14}, ['^']={4,10,17,0,0,0,0},
    ['_']={0,0,0,0,0,0,31}, ['`']={8,4,0,0,0,0,0},
    ['a']={0,0,14,1,15,17,15}, ['b']={16,16,22,25,17,17,30},
    ['c']={0,0,14,16,16,17,14}, ['d']={1,1,13,19,17,17,15},
    ['e']={0,0,14,17,31,16,14}, ['f']={6,8,8,30,8,8,8},
    ['g']={0,0,15,17,15,1,14}, ['h']={16,16,22,25,17,17,17},
    ['i']={4,0,12,4,4,4,14}, ['j']={2,0,6,2,2,18,12},
    ['k']={16,16,18,20,24,20,18}, ['l']={12,4,4,4,4,4,14},
    ['m']={0,0,26,21,21,17,17}, ['n']={0,0,22,25,17,17,17},
    ['o']={0,0,14,17,17,17,14}, ['p']={0,0,30,17,30,16,16},
    ['q']={0,0,15,17,15,1,1}, ['r']={0,0,22,25,16,16,16},
    ['s']={0,0,15,16,14,1,30}, ['t']={8,8,30,8,8,9,6},
    ['u']={0,0,17,17,17,19,13}, ['v']={0,0,17,17,17,10,4},
    ['w']={0,0,17,17,21,21,10}, ['x']={0,0,17,10,4,10,17},
    ['y']={0,0,17,17,15,1,14}, ['z']={0,0,31,2,4,8,31},
    ['{']={2,4,4,8,4,4,2}, ['|']={4,4,4,4,4,4,4},
    ['}']={8,4,4,2,4,4,8}, ['~']={0,0,9,22,0,0,0}
};

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
    }
}

void gop_init_from_multiboot(void *mbi){
    if(!mbi){ gop.available=false; return; }
    // Защита от битых mbi (как в QEMU с GRUB без framebuffer)
    // total_size должен быть разумным < 64K
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

bool gop_is_available(void){ return gop.available; }
uint32_t gop_get_width(void){ return gop.width; }
uint32_t gop_get_height(void){ return gop.height; }
uint32_t gop_get_pitch(void){ return gop.pitch; }
uint8_t gop_get_bpp(void){ return gop.bpp; }
uint64_t gop_get_framebuffer_size_bytes(void){ return gop.framebuffer_bytes; }
const char *gop_get_protocol_name(void){
    return gop.protocol_name ? gop.protocol_name : "Unavailable";
}
void gop_set_font_face(enum gop_font_face face){ font_face=face; }
enum gop_font_face gop_get_font_face(void){ return font_face; }

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
    if(batch_depth==0) gop_present();
}

void gop_begin_batch(void){
    batch_depth++;
}

void gop_end_batch(void){
    if(batch_depth){
        batch_depth--;
        if(batch_depth==0) gop_present();
    }
}

bool gop_has_backbuffer(void){ return backbuffer!=0; }

void gop_present(void){
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

// Курсор всегда поверх сцены: пишет строго во front, backbuffer не пачкает.
void gop_put_pixel_front(uint32_t x, uint32_t y, uint32_t color){
    front_write_32(x, y, color);
}

static void gop_scroll(void){
    if(!gop.available || !gop.addr) return;
    const uint32_t line_h = 10;
    if(gop.height <= line_h) { gop_clear(bg); return; }
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
            for(uint32_t x=0;x<gop.width;x++){ line[x*3+0]=(uint8_t)(bg&0xFF); line[x*3+1]=(uint8_t)((bg>>8)&0xFF); line[x*3+2]=(uint8_t)((bg>>16)&0xFF); }
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

// Сценовый пиксель: в backbuffer если активен, иначе напрямую во front.
// Dirty-отметку делает вызывающий высокоуровневый примитив.
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
    if(!gop.available){ vga_clear(); return; }
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

void gop_set_color(uint32_t f, uint32_t b){ fg=f; bg=b; }

static uint8_t get_font_row(uint8_t character, uint32_t row){
    uint8_t bits=font[character][row];
    if(font_face==GOP_FONT_BOLD) return bits|(bits>>1);
    if(font_face==GOP_FONT_CLEAN) return row<7 ? clean_font[character][row]<<2 : 0;
    return bits;
}

static void draw_char(char c, uint32_t x, uint32_t y){
    uint8_t ch=(uint8_t)c; if(ch>=128) ch='?';
    for(int r=0;r<8;r++){ uint8_t bits=get_font_row(ch,(uint32_t)r);
        for(int col=0;col<8;col++){
        uint32_t colr = (bits & (1<<(7-col))) ? fg : bg;
        put_pixel(x+col,y+r,colr);
    }}
}

static void draw_char_sized(char c, uint32_t x, uint32_t y, uint32_t size,
                            uint32_t text_fg, uint32_t text_bg){
    if(size==0) return;
    uint8_t ch=(uint8_t)c; if(ch>=128) ch='?';
    for(uint32_t py=0; py<size; py++){
        uint32_t source_row=(py*8)/size;
        uint8_t bits=get_font_row(ch,source_row);
        for(uint32_t px=0; px<size; px++){
            uint32_t source_col=(px*8)/size;
            uint32_t color=(bits & (1<<(7-source_col))) ? text_fg : text_bg;
            put_pixel(x+px, y+py, color);
        }
    }
}

void gop_putc(char c){
    if(!gop.available){ vga_putc(c); return; }
    (void)ensure_backbuffer();
    if(c=='\b'){
        if(cur_x>12) cur_x-=8;
        draw_char(' ',cur_x,cur_y);
        if(backbuffer){ dirty_expand(cur_x, cur_y, 8, 8); maybe_present(); }
        return;
    }
    if(c=='\n'){ cur_x=12; cur_y+=10; if(cur_y+8 >= gop.height) gop_scroll(); return; }
    if(c=='\r'){ cur_x=12; return; }
    if(cur_x+8 >= gop.width){ cur_x=12; cur_y+=10; if(cur_y+8 >= gop.height) gop_scroll(); }
    if(cur_y+8 >= gop.height) gop_scroll();
    draw_char(c,cur_x,cur_y);
    if(backbuffer){ dirty_expand(cur_x, cur_y, 8, 8); maybe_present(); }
    cur_x+=8;
}
void gop_write(const char *s){ while(*s) gop_putc(*s++); }
void gop_write_hex(uint64_t v){ const char*h="0123456789ABCDEF"; gop_write("0x"); for(int i=60;i>=0;i-=4) gop_putc(h[(v>>i)&0xF]); }

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
    uint32_t saved_fg=fg,saved_bg=bg;
    fg=foreground;
    bg=background;
    for(uint32_t row=0;row<user_console.rows;row++){
        for(uint32_t column=0;column<user_console.columns;column++){
            char character=user_console.characters[row][column];
            if(character!=' ')
                draw_char(character,x+column*8,y+row*10);
        }
    }
    fg=saved_fg;
    bg=saved_bg;
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
            uint32_t saved_fg=fg,saved_bg=bg;
            fg=user_console.foreground;
            bg=user_console.background;
            uint32_t cx=user_console.x+user_console.cursor_column*8;
            uint32_t cy=user_console.y+user_console.cursor_row*10;
            draw_char(' ',cx,cy);
            fg=saved_fg;
            bg=saved_bg;
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
        uint32_t saved_fg=fg,saved_bg=bg;
        fg=user_console.foreground;
        bg=user_console.background;
        uint32_t cx=user_console.x+user_console.cursor_column*8;
        uint32_t cy=user_console.y+user_console.cursor_row*10;
        draw_char(character,cx,cy);
        fg=saved_fg;
        bg=saved_bg;
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
void gop_draw_text_at(uint32_t x, uint32_t y, const char *text, uint32_t text_fg, uint32_t text_bg){
    if(!gop.available || !text) return;
    (void)ensure_backbuffer();
    uint32_t saved_fg=fg, saved_bg=bg;
    uint32_t start_x=x;
    fg=text_fg; bg=text_bg;
    while(*text){
        if(*text=='\n'){ x=12; y+=10; }
        else { draw_char(*text, x, y); if(backbuffer) dirty_expand(x, y, 8, 8); x+=8; }
        text++;
    }
    fg=saved_fg; bg=saved_bg;
    if(backbuffer){
        if(x>start_x) dirty_expand(start_x, y, x-start_x, 8);
        maybe_present();
    }
}
void gop_draw_text_sized_at(uint32_t x, uint32_t y, const char *text,
                            uint32_t text_fg, uint32_t text_bg, uint32_t size){
    if(!gop.available || !text || size==0) return;
    (void)ensure_backbuffer();
    uint32_t initial_x=x;
    uint32_t min_x=x, max_x=x, min_y=y, max_y=y+size;
    while(*text){
        if(*text=='\n'){
            x=initial_x;
            y+=size+3;
            if(y+size>max_y) max_y=y+size;
        } else {
            draw_char_sized(*text, x, y, size, text_fg, text_bg);
            x+=size;
            if(x>max_x) max_x=x;
            if(y<min_y) min_y=y;
            if(y+size>max_y) max_y=y+size;
        }
        text++;
    }
    if(backbuffer && max_x>min_x && max_y>min_y){
        dirty_expand(min_x, min_y, max_x-min_x, max_y-min_y);
        maybe_present();
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
void gop_draw_line(uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,uint32_t c){
    if(!gop.available || !gop.addr) return;
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
        // +1 чтобы покрыть конечные пиксели.
        uint32_t w=(max_x>=min_x)?(max_x-min_x+1):1;
        uint32_t h=(max_y>=min_y)?(max_y-min_y+1):1;
        dirty_expand(min_x, min_y, w, h);
        maybe_present();
    }
}
