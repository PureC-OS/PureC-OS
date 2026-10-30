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

bool pg_window_blit(struct pg_window *window, int32_t x, int32_t y,
                    uint32_t width, uint32_t height, const uint32_t *pixels,
                    uint32_t stride){
    if(!window || !window->open || window->minimized || !pixels
       || !width || !height || stride<width) return false;
    int64_t left=x;
    int64_t top=y;
    int64_t right=left+(int64_t)width;
    int64_t bottom=top+(int64_t)height;
    if(left<0) left=0;
    if(top<0) top=0;
    if(right>(int64_t)window->client.width) right=window->client.width;
    if(bottom>(int64_t)window->client.height) bottom=window->client.height;
    if(left>=right || top>=bottom) return true;
    const uint32_t *origin=pixels+(uint64_t)(top-y)*stride+(uint64_t)(left-x);
    int32_t screen_x=(int32_t)(window->client.x+(uint32_t)left);
    int32_t screen_y=(int32_t)(window->client.y+(uint32_t)top);
    return pc_fb_blit(origin,stride,(uint32_t)(right-left),
                      (uint32_t)(bottom-top),screen_x,screen_y,0)==0;
}

#define PG_SCALE_BAND_ROWS 16
#define PG_SCALE_MAX_WIDTH 4096

static uint32_t scale_band[PG_SCALE_BAND_ROWS*PG_SCALE_MAX_WIDTH];

static bool scaled_layout(const struct pg_window *window,
                          uint32_t source_width, uint32_t source_height,
                          uint32_t *dest_width, uint32_t *dest_height,
                          uint32_t *offset_x, uint32_t *offset_y){
    uint32_t cw=window->client.width;
    uint32_t ch=window->client.height;
    if(!source_width || !source_height || !cw || !ch) return false;
    uint32_t dw;
    uint32_t dh;
    if((uint64_t)cw*source_height<=(uint64_t)ch*source_width){
        dw=cw;
        dh=(uint32_t)((uint64_t)source_height*cw/source_width);
    } else {
        dh=ch;
        dw=(uint32_t)((uint64_t)source_width*ch/source_height);
    }
    if(dw>PG_SCALE_MAX_WIDTH){
        dh=(uint32_t)((uint64_t)dh*PG_SCALE_MAX_WIDTH/dw);
        dw=PG_SCALE_MAX_WIDTH;
    }
    if(!dw || !dh) return false;
    *dest_width=dw;
    *dest_height=dh;
    *offset_x=(cw-dw)/2;
    *offset_y=(ch-dh)/2;
    return true;
}

bool pg_window_blit_scaled(struct pg_window *window, const uint32_t *pixels,
                           uint32_t source_width, uint32_t source_height,
                           uint32_t stride){
    if(!window || !window->open || window->minimized || !pixels
       || stride<source_width) return false;
    uint32_t dw,dh,ox,oy;
    if(!scaled_layout(window,source_width,source_height,&dw,&dh,&ox,&oy))
        return false;
    bool ok=true;
    for(uint32_t row=0;row<dh;row+=PG_SCALE_BAND_ROWS){
        uint32_t rows=dh-row;
        if(rows>PG_SCALE_BAND_ROWS) rows=PG_SCALE_BAND_ROWS;
        for(uint32_t band=0;band<rows;band++){
            uint32_t sy=(uint32_t)((uint64_t)(row+band)*source_height/dh);
            const uint32_t *line=pixels+(uint64_t)sy*stride;
            uint32_t *out=&scale_band[(uint64_t)band*dw];
            uint64_t step=((uint64_t)source_width<<16)/dw;
            uint64_t position=0;
            for(uint32_t x=0;x<dw;x++){
                out[x]=line[position>>16];
                position+=step;
            }
        }
        ok=pg_window_blit(window,(int32_t)ox,(int32_t)(oy+row),dw,rows,
                          scale_band,dw) && ok;
    }
    return ok;
}

bool pg_window_scaled_point(const struct pg_window *window,
                            uint32_t source_width, uint32_t source_height,
                            int32_t window_x, int32_t window_y,
                            int32_t *source_x, int32_t *source_y){
    uint32_t dw,dh,ox,oy;
    if(!window || !source_x || !source_y
       || !scaled_layout(window,source_width,source_height,&dw,&dh,&ox,&oy))
        return false;
    int64_t x=(int64_t)window_x-(int64_t)window->client.x-(int64_t)ox;
    int64_t y=(int64_t)window_y-(int64_t)window->client.y-(int64_t)oy;
    if(x<0) x=0;
    if(y<0) y=0;
    if(x>=(int64_t)dw) x=(int64_t)dw-1;
    if(y>=(int64_t)dh) y=(int64_t)dh-1;
    *source_x=(int32_t)(x*source_width/dw);
    *source_y=(int32_t)(y*source_height/dh);
    return true;
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
