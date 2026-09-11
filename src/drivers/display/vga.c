#include "vga.h"
#include <stdint.h>

static uint64_t hhdm_offset = 0;
static volatile uint16_t *VGA = (volatile uint16_t*)0xB8000;
static uint8_t cur_x=0, cur_y=0;
static const uint8_t COLOR=0x0F;

void vga_set_hhdm(uint64_t offset){
    hhdm_offset = offset;
    VGA = (volatile uint16_t*)(uintptr_t)(hhdm_offset ? hhdm_offset + 0xB8000 : 0xB8000);
}

static void scroll(void){
    for(int y=1;y<25;y++) for(int x=0;x<80;x++) VGA[(y-1)*80+x]=VGA[y*80+x];
    for(int x=0;x<80;x++) VGA[24*80+x]= (COLOR<<8)|' ';
    cur_y=24;
}

void vga_clear(void){
    for(int i=0;i<80*25;i++) VGA[i]= (COLOR<<8)|' ';
    cur_x=0; cur_y=0;
    // move cursor via ports 0x3D4/0x3D5?
}

void vga_init(void){ vga_clear(); }

void vga_putc(char c){
    if(c=='\n'){ cur_x=0; cur_y++; if(cur_y>=25) scroll(); return; }
    if(c=='\r'){ cur_x=0; return; }
    VGA[cur_y*80 + cur_x] = (COLOR<<8)|(uint8_t)c;
    cur_x++; if(cur_x>=80){ cur_x=0; cur_y++; if(cur_y>=25) scroll(); }
}

void vga_write(const char *s){ while(*s) vga_putc(*s++); }
