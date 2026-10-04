#include "compositor.h"
#include "../drivers/display/gop.h"
#include "../kernel/process/scheduler.h"
#include "../lib/string.h"
#include "../mm/pmm.h"

#define COMP_MAX_WINDOWS 32
#define COMP_SHADOW 8
#define COMP_CHUNK 1024
#define COMP_MAX_PIXELS (16u * 1024u * 1024u)
#define CURSOR_SPRITE_W 12
#define CURSOR_SPRITE_H 17
#define CURSOR_BOX_W 14
#define CURSOR_BOX_H 19

struct comp_window {
    bool used;
    uint32_t pid;
    int32_t x;
    int32_t y;
    uint32_t fw;
    uint32_t fh;
    uint32_t aw;
    uint32_t ah;
    uint32_t *pixels;
    uint64_t phys;
    uint64_t pages;
    uint32_t depth;
    bool dirty;
    int32_t dx0;
    int32_t dy0;
    int32_t dx1;
    int32_t dy1;
};

struct comp_ctx {
    uint64_t flags;
    bool guard;
    bool irqs;
};

struct comp_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

static struct comp_window windows[COMP_MAX_WINDOWS];
static struct comp_window *zorder[COMP_MAX_WINDOWS];
static uint32_t zcount;
static volatile bool comp_lock;
static uint32_t line_buffer[COMP_CHUNK];
static int32_t cursor_x = -1000;
static int32_t cursor_y = -1000;
static bool cursor_visible = true;

static const char *const cursor_sprite[CURSOR_SPRITE_H] = {
    "X           ",
    "XX          ",
    "XoX         ",
    "XooX        ",
    "XoooX       ",
    "XooooX      ",
    "XoooooX     ",
    "XooooooX    ",
    "XoooooooX   ",
    "XooooooooX  ",
    "XoooooXXXXX ",
    "XooXooX     ",
    "XoX XooX    ",
    "XX  XooX    ",
    "X    XooX   ",
    "     XooX   ",
    "      XX    "
};

static int64_t min64(int64_t a, int64_t b){ return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b){ return a > b ? a : b; }

static void comp_enter(struct comp_ctx *c){
    __asm__ volatile("pushfq; pop %0; cli":"=r"(c->flags)::"memory");
    c->guard = scheduler_preempt_disable();
    struct thread *thread = scheduler_current_thread();
    c->irqs = (c->flags & (1ULL << 9))
        || (c->guard && thread && thread->user_mode);
    while(__atomic_test_and_set(&comp_lock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
    if(c->irqs) __asm__ volatile("sti":::"memory");
}

static void comp_leave(struct comp_ctx *c){
    if(c->irqs) __asm__ volatile("cli":::"memory");
    __atomic_clear(&comp_lock, __ATOMIC_RELEASE);
    if(c->guard) scheduler_preempt_enable();
    if(c->flags & (1ULL << 9)) __asm__ volatile("sti":::"memory");
}

static struct comp_window *find_window(uint32_t pid){
    for(uint32_t i = 0; i < COMP_MAX_WINDOWS; i++)
        if(windows[i].used && windows[i].pid == pid) return &windows[i];
    return 0;
}

static uint32_t visible_w(const struct comp_window *w){
    uint32_t v = w->fw + COMP_SHADOW;
    return v < w->aw ? v : w->aw;
}

static uint32_t visible_h(const struct comp_window *w){
    uint32_t v = w->fh + COMP_SHADOW;
    return v < w->ah ? v : w->ah;
}

static uint32_t cursor_argb(int32_t dx, int32_t dy){
    if(dx >= 0 && dx < CURSOR_SPRITE_W && dy >= 0 && dy < CURSOR_SPRITE_H){
        char c = cursor_sprite[dy][dx];
        if(c == 'X') return 0xFF000000u;
        if(c == 'o') return 0xFFFFFFFFu;
    }
    int32_t sx = dx - 1;
    int32_t sy = dy - 1;
    if(sx >= 0 && sx < CURSOR_SPRITE_W && sy >= 0 && sy < CURSOR_SPRITE_H){
        char c = cursor_sprite[sy][sx];
        if(c == 'X' || c == 'o') return 0x60000000u;
    }
    return 0;
}

static uint32_t blend_pixel(uint32_t dst, uint32_t argb){
    uint32_t a = argb >> 24;
    if(a == 255) return argb & 0x00FFFFFFu;
    uint32_t out = 0;
    for(int shift = 0; shift <= 16; shift += 8){
        uint32_t s = (argb >> shift) & 0xFF;
        uint32_t d = (dst >> shift) & 0xFF;
        uint32_t v = (s * a + d * (255 - a) + 127) / 255;
        out |= v << shift;
    }
    return out;
}

static void present_locked(int64_t x0, int64_t y0, int64_t x1, int64_t y1){
    int64_t screen_w = (int64_t)gop_get_width();
    int64_t screen_h = (int64_t)gop_get_height();
    x0 = max64(x0, 0);
    y0 = max64(y0, 0);
    x1 = min64(x1, screen_w);
    y1 = min64(y1, screen_h);
    if(x0 >= x1 || y0 >= y1) return;
    for(int64_t y = y0; y < y1; y++){
        const uint32_t *back = gop_backbuffer_row((uint32_t)y);
        if(!back) return;
        for(int64_t cx = x0; cx < x1; cx += COMP_CHUNK){
            int64_t ce = min64(cx + COMP_CHUNK, x1);
            uint32_t n = (uint32_t)(ce - cx);
            memcpy(line_buffer, back + cx, (uint64_t)n * sizeof(uint32_t));
            for(uint32_t i = 0; i < zcount; i++){
                const struct comp_window *w = zorder[i];
                if(!w->pixels) continue;
                int64_t wx0 = w->x;
                int64_t wy0 = w->y;
                int64_t wx1 = wx0 + (int64_t)visible_w(w);
                int64_t wy1 = wy0 + (int64_t)visible_h(w);
                if(y < wy0 || y >= wy1) continue;
                int64_t ix0 = max64(cx, wx0);
                int64_t ix1 = min64(ce, wx1);
                if(ix0 >= ix1) continue;
                const uint32_t *src =
                    &w->pixels[(uint64_t)(y - wy0) * w->aw + (uint64_t)(ix0 - wx0)];
                uint32_t *dst = &line_buffer[ix0 - cx];
                for(int64_t k = 0; k < ix1 - ix0; k++){
                    uint32_t p = src[k];
                    if(p & 0xFF000000u) dst[k] = p & 0x00FFFFFFu;
                }
            }
            if(cursor_visible && y >= cursor_y && y < cursor_y + CURSOR_BOX_H){
                int32_t dy = (int32_t)(y - cursor_y);
                int64_t ix0 = max64(cx, cursor_x);
                int64_t ix1 = min64(ce, (int64_t)cursor_x + CURSOR_BOX_W);
                for(int64_t px = ix0; px < ix1; px++){
                    uint32_t argb = cursor_argb((int32_t)(px - cursor_x), dy);
                    if(!argb) continue;
                    line_buffer[px - cx] =
                        blend_pixel(line_buffer[px - cx], argb);
                }
            }
            gop_front_write_row((uint32_t)cx, (uint32_t)y, line_buffer, n);
        }
    }
}

static void present_pair_locked(int64_t ax0, int64_t ay0, int64_t ax1,
                                int64_t ay1, int64_t bx0, int64_t by0,
                                int64_t bx1, int64_t by1){
    bool overlap = ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
    if(overlap){
        present_locked(min64(ax0, bx0), min64(ay0, by0),
                       max64(ax1, bx1), max64(ay1, by1));
        return;
    }
    present_locked(ax0, ay0, ax1, ay1);
    present_locked(bx0, by0, bx1, by1);
}

static void present_window_locked(const struct comp_window *w){
    present_locked(w->x, w->y, (int64_t)w->x + visible_w(w),
                   (int64_t)w->y + visible_h(w));
}

static void mark_dirty(struct comp_window *w, int64_t x0, int64_t y0,
                       int64_t x1, int64_t y1){
    if(!w->dirty){
        w->dx0 = (int32_t)x0;
        w->dy0 = (int32_t)y0;
        w->dx1 = (int32_t)x1;
        w->dy1 = (int32_t)y1;
        w->dirty = true;
        return;
    }
    if(x0 < w->dx0) w->dx0 = (int32_t)x0;
    if(y0 < w->dy0) w->dy0 = (int32_t)y0;
    if(x1 > w->dx1) w->dx1 = (int32_t)x1;
    if(y1 > w->dy1) w->dy1 = (int32_t)y1;
}

static void flush_if_idle(struct comp_window *w){
    if(w->depth != 0 || !w->dirty) return;
    w->dirty = false;
    present_locked(w->dx0, w->dy0, w->dx1, w->dy1);
}

static bool allocate_surface(struct comp_window *w, uint32_t aw, uint32_t ah){
    uint64_t count = (uint64_t)aw * ah;
    if(!aw || !ah || count > COMP_MAX_PIXELS) return false;
    uint64_t pages = (count * sizeof(uint32_t) + 4095ULL) / 4096ULL;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if(!phys) return false;
    uint32_t *virt = (uint32_t *)pmm_physical_to_virtual(phys);
    memset(virt, 0, pages * 4096ULL);
    w->pixels = virt;
    w->phys = phys;
    w->pages = pages;
    w->aw = aw;
    w->ah = ah;
    return true;
}

static void release_surface(struct comp_window *w){
    if(w->pixels) pmm_free_contiguous(w->phys, w->pages);
    w->pixels = 0;
    w->phys = 0;
    w->pages = 0;
}

bool compositor_window_create(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height){
    if(!pid || !width || !height || !gop_has_backbuffer()) return false;
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(w){
        comp_leave(&c);
        return compositor_window_update(pid, x, y, width, height);
    }
    for(uint32_t i = 0; i < COMP_MAX_WINDOWS && !w; i++)
        if(!windows[i].used) w = &windows[i];
    if(!w || !allocate_surface(w, width + COMP_SHADOW, height + COMP_SHADOW)){
        comp_leave(&c);
        return false;
    }
    w->used = true;
    w->pid = pid;
    w->x = (int32_t)x;
    w->y = (int32_t)y;
    w->fw = width;
    w->fh = height;
    w->depth = 0;
    w->dirty = false;
    zorder[zcount++] = w;
    comp_leave(&c);
    return true;
}

bool compositor_window_update(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || !width || !height){
        comp_leave(&c);
        return false;
    }
    int64_t ox0 = w->x;
    int64_t oy0 = w->y;
    int64_t ox1 = ox0 + visible_w(w);
    int64_t oy1 = oy0 + visible_h(w);
    if(width + COMP_SHADOW > w->aw || height + COMP_SHADOW > w->ah){
        struct comp_window grown = *w;
        uint32_t naw = width + COMP_SHADOW > w->aw ? width + COMP_SHADOW : w->aw;
        uint32_t nah = height + COMP_SHADOW > w->ah ? height + COMP_SHADOW : w->ah;
        if(!allocate_surface(&grown, naw, nah)){
            comp_leave(&c);
            return false;
        }
        uint32_t rows = w->ah < nah ? w->ah : nah;
        uint32_t cols = w->aw < naw ? w->aw : naw;
        for(uint32_t row = 0; row < rows; row++)
            memcpy(&grown.pixels[(uint64_t)row * naw],
                   &w->pixels[(uint64_t)row * w->aw],
                   (uint64_t)cols * sizeof(uint32_t));
        release_surface(w);
        w->pixels = grown.pixels;
        w->phys = grown.phys;
        w->pages = grown.pages;
        w->aw = naw;
        w->ah = nah;
    }
    w->x = (int32_t)x;
    w->y = (int32_t)y;
    w->fw = width;
    w->fh = height;
    present_pair_locked(ox0, oy0, ox1, oy1, w->x, w->y,
                        (int64_t)w->x + visible_w(w),
                        (int64_t)w->y + visible_h(w));
    comp_leave(&c);
    return true;
}

void compositor_window_destroy(uint32_t pid){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w){
        comp_leave(&c);
        return;
    }
    int64_t x0 = w->x;
    int64_t y0 = w->y;
    int64_t x1 = x0 + visible_w(w);
    int64_t y1 = y0 + visible_h(w);
    uint32_t out = 0;
    for(uint32_t i = 0; i < zcount; i++)
        if(zorder[i] != w) zorder[out++] = zorder[i];
    zcount = out;
    release_surface(w);
    memset(w, 0, sizeof(*w));
    present_locked(x0, y0, x1, y1);
    comp_leave(&c);
}

void compositor_window_raise(uint32_t pid){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || zcount == 0 || zorder[zcount - 1] == w){
        comp_leave(&c);
        return;
    }
    uint32_t out = 0;
    for(uint32_t i = 0; i < zcount; i++)
        if(zorder[i] != w) zorder[out++] = zorder[i];
    zorder[out++] = w;
    zcount = out;
    present_window_locked(w);
    comp_leave(&c);
}

bool compositor_window_exists(uint32_t pid){
    return find_window(pid) != 0;
}

bool compositor_draw_rect(uint32_t pid, uint32_t ux, uint32_t uy,
                          uint32_t uw, uint32_t uh, uint32_t color){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || !w->pixels){
        comp_leave(&c);
        return false;
    }
    struct comp_rect outside[4];
    uint32_t outside_count = 0;
    int64_t x0 = ux;
    int64_t y0 = uy;
    int64_t x1 = x0 + uw;
    int64_t y1 = y0 + uh;
    int64_t sx0 = w->x;
    int64_t sy0 = w->y;
    int64_t sx1 = sx0 + w->aw;
    int64_t sy1 = sy0 + w->ah;
    int64_t ix0 = max64(x0, sx0);
    int64_t iy0 = max64(y0, sy0);
    int64_t ix1 = min64(x1, sx1);
    int64_t iy1 = min64(y1, sy1);
    if(!uw || !uh){
        comp_leave(&c);
        return true;
    }
    if(ix0 >= ix1 || iy0 >= iy1){
        outside[outside_count++] = (struct comp_rect){ux, uy, uw, uh};
    } else {
        uint32_t value = 0xFF000000u | (color & 0x00FFFFFFu);
        for(int64_t row = iy0; row < iy1; row++){
            uint32_t *line = &w->pixels[(uint64_t)(row - sy0) * w->aw
                                        + (uint64_t)(ix0 - sx0)];
            for(int64_t k = 0; k < ix1 - ix0; k++) line[k] = value;
        }
        mark_dirty(w, ix0, iy0, ix1, iy1);
        if(iy0 > y0)
            outside[outside_count++] = (struct comp_rect){
                ux, uy, uw, (uint32_t)(iy0 - y0)};
        if(iy1 < y1)
            outside[outside_count++] = (struct comp_rect){
                ux, (uint32_t)iy1, uw, (uint32_t)(y1 - iy1)};
        if(ix0 > x0)
            outside[outside_count++] = (struct comp_rect){
                ux, (uint32_t)iy0, (uint32_t)(ix0 - x0),
                (uint32_t)(iy1 - iy0)};
        if(ix1 < x1)
            outside[outside_count++] = (struct comp_rect){
                (uint32_t)ix1, (uint32_t)iy0, (uint32_t)(x1 - ix1),
                (uint32_t)(iy1 - iy0)};
        flush_if_idle(w);
    }
    comp_leave(&c);
    for(uint32_t i = 0; i < outside_count; i++)
        gop_draw_rect(outside[i].x, outside[i].y, outside[i].w,
                      outside[i].h, color);
    return true;
}

bool compositor_draw_line(uint32_t pid, uint32_t ux0, uint32_t uy0,
                          uint32_t ux1, uint32_t uy1, uint32_t color){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || !w->pixels){
        comp_leave(&c);
        return false;
    }
    int64_t sx0 = w->x;
    int64_t sy0 = w->y;
    int64_t sx1 = sx0 + w->aw;
    int64_t sy1 = sy0 + w->ah;
    int64_t x0 = ux0;
    int64_t y0 = uy0;
    int64_t x1 = ux1;
    int64_t y1 = uy1;
    if(x0 < sx0 || x0 >= sx1 || y0 < sy0 || y0 >= sy1
       || x1 < sx0 || x1 >= sx1 || y1 < sy0 || y1 >= sy1){
        comp_leave(&c);
        return false;
    }
    int64_t minx = min64(x0, x1);
    int64_t maxx = max64(x0, x1);
    int64_t miny = min64(y0, y1);
    int64_t maxy = max64(y0, y1);
    int64_t dx = maxx - minx;
    int64_t dy = -(maxy - miny);
    int64_t stepx = x0 < x1 ? 1 : -1;
    int64_t stepy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy;
    uint32_t value = 0xFF000000u | (color & 0x00FFFFFFu);
    for(;;){
        w->pixels[(uint64_t)(y0 - sy0) * w->aw + (uint64_t)(x0 - sx0)] = value;
        if(x0 == x1 && y0 == y1) break;
        int64_t e2 = 2 * err;
        if(e2 >= dy){
            err += dy;
            x0 += stepx;
        }
        if(e2 <= dx){
            err += dx;
            y0 += stepy;
        }
    }
    mark_dirty(w, minx, miny, maxx + 1, maxy + 1);
    flush_if_idle(w);
    comp_leave(&c);
    return true;
}

bool compositor_scroll_rect_up(uint32_t pid, uint32_t ux, uint32_t uy,
                               uint32_t uw, uint32_t uh, uint32_t amount,
                               uint32_t fill_color){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || !w->pixels || !uw || !uh){
        comp_leave(&c);
        return false;
    }
    int64_t sx0 = w->x;
    int64_t sy0 = w->y;
    if((int64_t)ux < sx0 || (int64_t)uy < sy0
       || (int64_t)ux + uw > sx0 + w->aw
       || (int64_t)uy + uh > sy0 + w->ah){
        comp_leave(&c);
        return false;
    }
    uint32_t value = 0xFF000000u | (fill_color & 0x00FFFFFFu);
    uint32_t col = (uint32_t)((int64_t)ux - sx0);
    uint32_t row0 = (uint32_t)((int64_t)uy - sy0);
    if(amount > uh) amount = uh;
    for(uint32_t row = 0; row + amount < uh; row++)
        memmove(&w->pixels[(uint64_t)(row0 + row) * w->aw + col],
                &w->pixels[(uint64_t)(row0 + row + amount) * w->aw + col],
                (uint64_t)uw * sizeof(uint32_t));
    for(uint32_t row = uh - amount; row < uh; row++){
        uint32_t *line = &w->pixels[(uint64_t)(row0 + row) * w->aw + col];
        for(uint32_t k = 0; k < uw; k++) line[k] = value;
    }
    mark_dirty(w, ux, uy, (int64_t)ux + uw, (int64_t)uy + uh);
    flush_if_idle(w);
    comp_leave(&c);
    return true;
}

bool compositor_begin_update(uint32_t pid){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w){
        comp_leave(&c);
        return false;
    }
    w->depth++;
    comp_leave(&c);
    return true;
}

bool compositor_end_update(uint32_t pid){
    struct comp_ctx c;
    comp_enter(&c);
    struct comp_window *w = find_window(pid);
    if(!w || w->depth == 0){
        comp_leave(&c);
        return false;
    }
    w->depth--;
    flush_if_idle(w);
    comp_leave(&c);
    return true;
}

void compositor_cursor_move(int32_t x, int32_t y){
    if(!gop_has_backbuffer()) return;
    struct comp_ctx c;
    comp_enter(&c);
    if(x == cursor_x && y == cursor_y){
        comp_leave(&c);
        return;
    }
    int32_t ox = cursor_x;
    int32_t oy = cursor_y;
    cursor_x = x;
    cursor_y = y;
    if(cursor_visible)
        present_pair_locked(ox, oy, (int64_t)ox + CURSOR_BOX_W,
                            (int64_t)oy + CURSOR_BOX_H, x, y,
                            (int64_t)x + CURSOR_BOX_W,
                            (int64_t)y + CURSOR_BOX_H);
    comp_leave(&c);
}

void compositor_cursor_show(bool visible){
    struct comp_ctx c;
    comp_enter(&c);
    if(cursor_visible != visible){
        cursor_visible = visible;
        present_locked(cursor_x, cursor_y, (int64_t)cursor_x + CURSOR_BOX_W,
                       (int64_t)cursor_y + CURSOR_BOX_H);
    }
    comp_leave(&c);
}

bool compositor_present(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1){
    if(!gop_has_backbuffer()) return false;
    struct comp_ctx c;
    comp_enter(&c);
    present_locked(x0, y0, x1, y1);
    comp_leave(&c);
    return true;
}
