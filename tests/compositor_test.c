#include "gfx/compositor.h"
#include "drivers/display/gop.h"
#include "drivers/display/svga.h"
#include "kernel/process/scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480

static uint32_t backbuffer[SCREEN_W * SCREEN_H];
static uint32_t front[SCREEN_W * SCREEN_H];
static int front_writes;
static int svga_updates;
static struct thread fake_thread;

uint32_t gop_get_width(void) { return SCREEN_W; }
uint32_t gop_get_height(void) { return SCREEN_H; }
bool gop_has_backbuffer(void) { return true; }
const uint32_t *gop_backbuffer_row(uint32_t y) {
    return y < SCREEN_H ? &backbuffer[y * SCREEN_W] : 0;
}
void gop_front_write_row(uint32_t x, uint32_t y, const uint32_t *pixels,
                         uint32_t count) {
    assert(y < SCREEN_H && x + count <= SCREEN_W);
    memcpy(&front[y * SCREEN_W + x], pixels, count * sizeof(uint32_t));
    front_writes++;
}
void gop_console_translate(uint32_t pid, int32_t dx, int32_t dy) {
    (void)pid; (void)dx; (void)dy;
}
bool svga_is_available(void) { return true; }
void svga_update(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
    svga_updates++;
}
bool scheduler_preempt_disable(void) { return true; }
void scheduler_preempt_enable(void) {}
struct thread *scheduler_current_thread(void) { return &fake_thread; }
uint64_t pmm_allocate_contiguous(uint64_t pages) {
    return (uint64_t)(uintptr_t)aligned_alloc(4096, pages * 4096);
}
void pmm_free_contiguous(uint64_t phys, uint64_t pages) {
    (void)pages;
    free((void *)(uintptr_t)phys);
}
void *pmm_physical_to_virtual(uint64_t phys) { return (void *)(uintptr_t)phys; }

static void reset_front(void) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        front[i] = 0xDEAD0000u;
    front_writes = 0;
    svga_updates = 0;
}

static uint32_t at(int x, int y) { return front[y * SCREEN_W + x]; }

int main(void) {
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        backbuffer[i] = 0x181825;

    assert(compositor_window_create(10, 100, 100, 200, 150));
    compositor_window_raise(10);
    reset_front();

    assert(compositor_draw_rect(10, 110, 110, 50, 20, 0x112233));
    assert(at(120, 120) == 0x112233);
    assert(svga_updates > 0);

    reset_front();
    assert(compositor_begin_update(10));
    assert(compositor_draw_rect(10, 120, 120, 30, 10, 0x445566));
    assert(front_writes == 0);
    assert(compositor_end_update(10));
    assert(at(125, 125) == 0x445566);
    assert(front_writes > 0);

    reset_front();
    assert(compositor_begin_update(10));
    assert(compositor_begin_update(10));
    assert(compositor_draw_rect(10, 130, 130, 10, 10, 0x778899));
    assert(compositor_end_update(10));
    assert(front_writes == 0);
    assert(compositor_end_update(10));
    assert(at(135, 135) == 0x778899);

    assert(!compositor_end_update(10));

    uint32_t pixels[16 * 16];
    for (int i = 0; i < 16 * 16; i++)
        pixels[i] = 0x00ABCDEF;
    reset_front();
    assert(compositor_blit(10, 140, 140, 16, 16, pixels, 16, 0));
    assert(at(145, 145) == 0xABCDEF);

    assert(compositor_window_create(20, 150, 150, 100, 100));
    compositor_window_raise(20);
    assert(compositor_draw_rect(20, 160, 160, 20, 20, 0x0000FF));
    assert(compositor_draw_rect(10, 100, 100, 200, 150, 0x333333));
    assert(compositor_blit(10, 140, 140, 16, 16, pixels, 16, 0));
    reset_front();
    compositor_window_raise(10);
    assert(at(145, 145) == 0xABCDEF);
    assert(at(165, 165) == 0x333333);

    assert(compositor_window_create(30, 0, 0, 50, 50));
    compositor_window_set_topmost(30, true);
    assert(compositor_draw_rect(30, 5, 5, 20, 20, 0xFF0000));
    compositor_window_raise(10);
    compositor_window_raise(20);
    reset_front();
    compositor_present(0, 0, 60, 60);
    assert(at(10, 10) == 0xFF0000);

    reset_front();
    assert(compositor_window_update(10, 300, 200, 200, 150));
    assert(at(310, 210) == 0x181825 || at(310, 210) != 0xDEAD0000u);
    assert(compositor_draw_rect(10, 310, 210, 10, 10, 0x123456));
    assert(at(315, 215) == 0x123456);

    assert(compositor_window_update(10, 300, 200, 100, 80));
    reset_front();
    compositor_present(290, 190, 520, 380);
    assert(at(450, 300) == 0x181825);

    compositor_window_destroy(20);
    reset_front();
    compositor_present(150, 150, 260, 260);
    assert(at(165, 165) == 0x181825);

    printf("compositor_test: ok\n");
    return 0;
}
