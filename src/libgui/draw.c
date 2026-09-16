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

static gfx_font_face_t g_pg_face = GFX_FONT_CLEAN;

void pg_set_font_face(uint32_t face){
    if(face==PG_FONT_CLASSIC) g_pg_face=GFX_FONT_CLASSIC;
    else if(face==PG_FONT_BOLD) g_pg_face=GFX_FONT_BOLD;
    else g_pg_face=GFX_FONT_CLEAN;
}

static uint32_t pg_streq(const char *a, const char *b){
    while(*a && *a==*b){ a++; b++; }
    return (uint8_t)*a==(uint8_t)*b;
}

void pg_font_sync(void){
    int32_t fd=pc_file_open("/config/appear.ini");
    if(fd<0) return;
    char buf[512];
    int32_t n=pc_file_read(fd,buf,sizeof(buf)-1);
    (void)pc_file_close(fd);
    if(n<=0) return;
    buf[n]='\0';
    for(char *line=buf;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char save=*end;
        *end='\0';
        if(line[0]=='f' && line[1]=='o' && line[2]=='n' && line[3]=='t' && line[4]=='='){
            const char *v=line+5;
            if(pg_streq(v,"classic")) g_pg_face=GFX_FONT_CLASSIC;
            else if(pg_streq(v,"bold")) g_pg_face=GFX_FONT_BOLD;
            else g_pg_face=GFX_FONT_CLEAN;
            return;
        }
        if(!save) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
}

void pg_internal_draw_text_clipped(uint32_t x, uint32_t y,
                                   const char *text, uint32_t color,
                                   uint32_t background,
                                   const struct pg_rect *clip){
    (void)background;
    if(!text || !clip || y<clip->y || y+8>clip->y+clip->height) return;
    while(*text && x<clip->x+clip->width){
        if(x+8>clip->x+clip->width) break;
        gfx_draw_char(*text, x, y, color, 8,
                      g_pg_face, pg_rect_cb, 0);
        x+=8;
        text++;
    }
}

void pg_internal_draw_text_sized_clipped(uint32_t x, uint32_t y,
                                           const char *text, uint32_t color,
                                           uint32_t background, uint32_t size,
                                           const struct pg_rect *clip){
    (void)background;
    if(!text || !clip || !size) return;
    if(size<8) size=8;
    if(size>48) size=48;
    if(y<clip->y || y+size>clip->y+clip->height) return;
    while(*text && x<clip->x+clip->width){
        if(x+size>clip->x+clip->width) break;
        gfx_draw_char(*text, x, y, color, size,
                      g_pg_face, pg_rect_cb, 0);
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
