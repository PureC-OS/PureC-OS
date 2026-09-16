#include "internal.h"
#include "../libc/include/purec.h"
#include "../gfx/text.h"

bool pg_internal_point_inside(int32_t x, int32_t y,
                              const struct pg_rect *rect){
    return rect && x>=(int32_t)rect->x && y>=(int32_t)rect->y
        && x<(int32_t)(rect->x+rect->width)
        && y<(int32_t)(rect->y+rect->height);
}

struct pg_rect pg_internal_to_screen(const struct pg_window *window,
                                     struct pg_rect bounds){
    if(window){
        bounds.x+=window->client.x;
        bounds.y+=window->client.y;
    }
    return bounds;
}

static void pg_rect_cb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color, void *ctx){
    (void)ctx;
    if(w && h) pc_draw_rect(x, y, w, h, color);
}

void pg_internal_draw_text_clipped(uint32_t x, uint32_t y,
                                   const char *text, uint32_t color,
                                   uint32_t background,
                                   const struct pg_rect *clip){
    (void)background; /* transparent by design: backend never paints bg */
    if(!text || !clip || y<clip->y || y+8>clip->y+clip->height) return;
    while(*text && x<clip->x+clip->width){
        if(x+8>clip->x+clip->width) break;
        gfx_draw_char(*text, x, y, color, 8,
                      GFX_FONT_CLASSIC, pg_rect_cb, 0);
        x+=8;
        text++;
    }
}

void pg_internal_draw_text_sized_clipped(uint32_t x, uint32_t y,
                                           const char *text, uint32_t color,
                                           uint32_t background, uint32_t size,
                                           const struct pg_rect *clip){
    (void)background; /* transparent by design: backend never paints bg */
    if(!text || !clip || !size) return;
    if(size<8) size=8;
    if(size>48) size=48;
    if(y<clip->y || y+size>clip->y+clip->height) return;
    while(*text && x<clip->x+clip->width){
        if(x+size>clip->x+clip->width) break;
        gfx_draw_char(*text, x, y, color, size,
                      GFX_FONT_CLASSIC, pg_rect_cb, 0);
        x+=size;
        text++;
    }
}
void pg_window_rect(struct pg_window *window, struct pg_rect bounds,
                    uint32_t color){
    if(!window || !window->open || window->minimized
       || !bounds.width || !bounds.height) return;
    struct pg_rect screen=pg_internal_to_screen(window,bounds);
    uint32_t right=window->client.x+window->client.width;
    uint32_t bottom=window->client.y+window->client.height;
    if(screen.x>=right || screen.y>=bottom) return;
    if(screen.width>right-screen.x) screen.width=right-screen.x;
    if(screen.height>bottom-screen.y) screen.height=bottom-screen.y;
    pc_draw_rect(screen.x,screen.y,screen.width,screen.height,color);
}

void pg_window_text(struct pg_window *window, uint32_t x, uint32_t y,
                    const char *text, uint32_t color){
    if(!window || !window->open || window->minimized) return;
    pg_internal_draw_text_clipped(window->client.x+x,window->client.y+y,
                                  text,color,window->theme.window,
                                  &window->client);
}

void pg_window_text_sized(struct pg_window *window, uint32_t x, uint32_t y,
                          const char *text, uint32_t color, uint32_t size){
    if(!window || !window->open || window->minimized) return;
    pg_internal_draw_text_sized_clipped(window->client.x+x,window->client.y+y,
                                        text,color,window->theme.window,size,
                                        &window->client);
}
