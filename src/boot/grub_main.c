#include "../drivers/serial/serial.h"
#include "../drivers/display/vga.h"
#include "../drivers/display/gop.h"
#include "../drivers/interrupts/pic.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../kernel/syscall/syscall.h"
#include <stdint.h>

// helper для int 0x80
static inline int64_t do_syscall(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "r10","r8","memory"
    );
    return ret;
}

void kernel_main_grub(uint32_t magic, uint32_t *mbi) {
    vga_init();
    vga_write("PureC OS 64-bit [GRUB]\n");
    serial_init();
    serial_write_string("[GRUB] 64-bit long mode via multiboot2 trampoline\n");
    vga_write("[GRUB] long mode OK\n");
    if(magic == 0x36d76289){ serial_write_string("[GRUB] multiboot2 magic OK\n"); vga_write("multiboot2 OK\n"); }
    else { serial_write_string("[GRUB] bad magic\n"); vga_write("bad magic\n"); }

    serial_write_string("[GOP] init start mbi="); 
    // print mbi hex
    { char buf[17]; const char *h="0123456789ABCDEF"; uint64_t v=(uint64_t)mbi; for(int i=15;i>=0;i--) buf[15-i]=h[(v>> (i*4))&0xF]; buf[16]=0; serial_write_string(buf); serial_write_string("\n"); }
    gop_init_from_multiboot(mbi);
    serial_write_string("[GOP] init done\n");
    if(gop_is_available()){
        serial_write_string("[GOP] framebuffer from multiboot2 available\n");
        vga_write("[GOP] FB available\n");
        gop_clear(0x1E1E2E);
        gop_set_color(0xCDD6F4, 0x1E1E2E);
        gop_write("PureC OS 64-bit [GRUB+GOP]\n");
        gop_write("GOP driver active\n");
    } else {
        serial_write_string("[GOP] no FB, using VGA text\n");
    }

    pic_remap(0x20, 0x28);
    pic_mask_all();
    serial_write_string("[PIC] remapped to 0x20/0x28, masked\n");

    gdt_init();
    serial_write_string("[GDT] loaded (GRUB path)\n");
    vga_write("[GDT] OK\n");

    idt_init();
    syscall_init();
    serial_write_string("[IDT] loaded\n");
    vga_write("[IDT] OK\n");

    // Мышь как в Linux: psmouse + evdev (упрощённо)
    ps2_mouse_init();
    if(gop_is_available()) gop_write("[MOUSE] PS/2 ready\n");
    vga_write("[MOUSE] ready\n");

    __asm__ volatile("sti");
    serial_write_string("[INT] sti\n");
    vga_write("[INT] sti\n");

    serial_write_string("[TEST] int3\n");
    vga_write("int3 test...\n");
    __asm__ volatile("int3");
    serial_write_string("[OK] int3 handled\n");
    vga_write("[OK] int3 handled\n");
    if(gop_is_available()) gop_write("[OK] int3 handled\n");

    // --- SYSCALL TEST ---
    serial_write_string("[TEST] syscalls via int 0x80\n");
    vga_write("syscall test...\n");
    if(gop_is_available()) gop_write("syscall test...\n");

    const char *msg = "Hello from syscall WRITE!\n";
    int64_t ret = do_syscall(SYS_WRITE, (uint64_t)msg, 26, 1, 0, 0);
    serial_write_string("[SYSCALL] WRITE ret="); 
    // print ret
    char buf[32]; int len=0; uint64_t v=ret; if(v==0) serial_putc('0'); else { char tmp[20]; int t=0; while(v){tmp[t++]= '0'+v%10; v/=10;} while(t--) serial_putc(tmp[t]);}
    serial_write_string("\n");
    vga_write("syscall WRITE done\n");
    if(gop_is_available()) gop_write("syscall WRITE done\n");

    do_syscall(SYS_CLEAR, 0x282738, 0,0,0,0);
    if(gop_is_available()){
        gop_set_color(0xFFFFFF, 0x282738);
        gop_write("GOP CLEAR via syscall OK\n");
    }
    vga_write("CLEAR syscall done\n");

    // DRAW_RECT via syscall: x=100,y=100,w=200,h=50,color=0xFF0000
    {
        __asm__ volatile(
            "mov $100, %%rbx; mov $100, %%rcx; mov $200, %%rdx; mov $50, %%rsi; mov $0xFF0000, %%rdi; mov $100, %%rax; int $0x80"
            ::: "rax","rbx","rcx","rdx","rsi","rdi","memory"
        );
        serial_write_string("[SYSCALL] DRAW_RECT done\n");
        vga_write("DRAW_RECT done\n");
        if(gop_is_available()) gop_write("DRAW_RECT done\n");
    }

    // Тест мыши как в Linux: evdev -> read /dev/input/mice ~ syscall GET_MOUSE
    serial_write_string("[TEST] mouse via syscall GET_MOUSE (move mouse now)\n");
    vga_write("Move mouse...\n");
    if(gop_is_available()) gop_write("Move mouse to see cursor via GOP\n");
    for(int i=0;i<3;i++){
        struct mouse_state ms;
        do_syscall(SYS_GET_MOUSE, (uint64_t)&ms,0,0,0,0);
        // print via serial
        serial_write_string("[MOUSE] x="); 
        // simple decimal print
        char tmp[12]; int t=0; int32_t v=ms.x; if(v==0) serial_putc('0'); else { char b[12]; int n=0; while(v){b[n++]= '0'+v%10; v/=10;} while(n--) serial_putc(b[n]);}
        serial_write_string(" y="); v=ms.y; if(v==0) serial_putc('0'); else { char b[12]; int n=0; while(v){b[n++]= '0'+v%10; v/=10;} while(n--) serial_putc(b[n]);}
        serial_write_string(" btn="); serial_putc('0'+ms.buttons); serial_write_string("\n");
        // sleep ~500ms
        for(volatile int k=0;k<5000000;k++) __asm__ volatile("nop");
    }

    // halt
    for(;;) __asm__ volatile("cli; hlt");
}
