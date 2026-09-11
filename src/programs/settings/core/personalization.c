#include "settings/personalization.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"

#define APPEAR_DIRECTORY "/config"
#define APPEAR_PATH "/config/appear.ini"

static void copy_str(char *dst, uint32_t cap, const char *src){
    if(!dst || !cap) return;
    if(!src) src="";
    pc_copy(dst,src,cap);
}

static bool starts_with(const char *text, const char *prefix){
    while(*prefix){ if(*text++!=*prefix++) return false; }
    return true;
}

static uint32_t parse_u32(const char *text){
    uint32_t value=0;
    while(text && *text>='0' && *text<='9'){
        uint32_t digit=(uint32_t)(*text-'0');
        if(value>(UINT32_MAX-digit)/10U) return UINT32_MAX;
        value=value*10U+digit;
        text++;
    }
    return value;
}

static char *append_text(char *out, const char *text){
    while(*text) *out++=*text++;
    *out='\0';
    return out;
}

static char *append_u32(char *out, uint32_t value){
    char reverse[12];
    uint32_t count=0;
    do{
        reverse[count++]=(char)('0'+value%10U);
        value/=10U;
    }while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0';
    return out;
}

/* Wallpaper catalogue: /config/wplist.ini (user) wins, then the shipped
 * /demo/wplist.ini, then a compiled fallback. One 8.3 path per line,
 * # comments and empty lines skipped. Index 0 is always "" (solid). */
#define WP_LIST_USER "/config/wplist.ini"
#define WP_LIST_SHIPPED "/demo/wplist.ini"
#define WP_LIST_MAX 32
#define WP_LIST_BUFFER 2048

static char wp_list[WP_LIST_MAX][APPEAR_WALLPAPER_CAP];
static uint32_t wp_list_count;
static bool wp_list_loaded;

static const char *wp_fallback[]={
    "/walpaper/ducati1.bmp",
    "/walpaper/bmw2.bmp",
    "/walpaper/bmw3.bmp",
    "/walpaper/space1.bmp",
    "/demo/screenshot.bmp",
    "/src/demo/screenshot.bmp",
    "/demo/image.png",
    "/src/demo/image.png",
    "/config/wp.bmp",
    "/config/wp.png",
};

static void wp_list_push(const char *line){
    if(wp_list_count>=WP_LIST_MAX) return;
    uint32_t length=0;
    while(line[length] && length+1<sizeof(wp_list[0])){
        wp_list[wp_list_count][length]=line[length];
        length++;
    }
    if(line[length]) return; /* overlong path: skip, never truncate */
    wp_list[wp_list_count][length]='\0';
    if(!length) return;
    wp_list_count++;
}

static void wp_list_ensure(void){
    if(wp_list_loaded) return;
    wp_list_loaded=true;
    wp_list_count=0;
    char buffer[WP_LIST_BUFFER];
    int32_t fd=pf_open(WP_LIST_USER);
    if(fd<0) fd=pf_open(WP_LIST_SHIPPED);
    int32_t amount=-1;
    if(fd>=0){
        amount=pf_read(fd,buffer,sizeof(buffer)-1);
        (void)pf_close(fd);
    }
    if(amount>0){
        buffer[amount]='\0';
        for(char *line=buffer;*line;){
            char *end=line;
            while(*end && *end!='\n' && *end!='\r') end++;
            char saved=*end;
            *end='\0';
            while(*line==' ' || *line=='\t') line++;
            char *tail=line;
            while(*tail) tail++;
            while(tail>line && (tail[-1]==' ' || tail[-1]=='\t')) *--tail='\0';
            if(*line && *line!='#') wp_list_push(line);
            if(!saved) break;
            line=end+1;
        }
    }
    if(!wp_list_count){
        for(uint32_t i=0;
            i<sizeof(wp_fallback)/sizeof(wp_fallback[0]);i++)
            wp_list_push(wp_fallback[i]);
    }
}

static const char *fonts[]={
    "clean",
    "classic",
    "bold",
};

void appearance_defaults(struct personalization_settings *s){
    if(!s) return;
    copy_str(s->theme,sizeof(s->theme),"catppuccin-dark");
    s->wallpaper[0]='\0';
    copy_str(s->font,sizeof(s->font),"clean");
    s->font_size=8;
}

uint32_t appearance_wallpaper_count(void){
    wp_list_ensure();
    return 1+wp_list_count; /* index 0 = solid color */
}

const char *appearance_wallpaper_at(uint32_t index){
    wp_list_ensure();
    if(index==0) return "";
    if(index-1<wp_list_count) return wp_list[index-1];
    return "";
}

uint32_t appearance_font_count(void){
    return sizeof(fonts)/sizeof(fonts[0]);
}

const char *appearance_font_name_at(uint32_t index){
    if(index>=appearance_font_count()) return fonts[0];
    return fonts[index];
}

bool appearance_load(struct personalization_settings *s){
    if(!s) return false;
    appearance_defaults(s);
    int32_t descriptor=pf_open(APPEAR_PATH);
    if(descriptor<0) return false;
    char buffer[512]={0};
    int32_t amount=pf_read(descriptor,buffer,sizeof(buffer)-1);
    (void)pf_close(descriptor);
    if(amount<=0) return false;
    buffer[amount]='\0';
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(starts_with(line,"theme="))
            copy_str(s->theme,sizeof(s->theme),line+6);
        else if(starts_with(line,"wallpaper="))
            copy_str(s->wallpaper,sizeof(s->wallpaper),line+10);
        else if(starts_with(line,"font="))
            copy_str(s->font,sizeof(s->font),line+5);
        else if(starts_with(line,"font_size=")){
            uint32_t size=parse_u32(line+10);
            if(size<APPEAR_MIN_FONT_SIZE) size=APPEAR_MIN_FONT_SIZE;
            if(size>APPEAR_MAX_FONT_SIZE) size=APPEAR_MAX_FONT_SIZE;
            s->font_size=size;
        }
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(!s->font_size) s->font_size=8;
    return true;
}

int32_t appearance_save(const struct personalization_settings *s){
    if(!s) return -3;
    char buffer[512];
    char *out=buffer;
    out=append_text(out,"theme=");
    out=append_text(out,s->theme[0] ? s->theme : "catppuccin-dark");
    out=append_text(out,"\nwallpaper=");
    out=append_text(out,s->wallpaper);
    out=append_text(out,"\nwallpaper_mode=stretch\nfont=");
    out=append_text(out,s->font[0] ? s->font : "clean");
    out=append_text(out,"\nfont_size=");
    uint32_t size=s->font_size;
    if(size<APPEAR_MIN_FONT_SIZE) size=APPEAR_MIN_FONT_SIZE;
    if(size>APPEAR_MAX_FONT_SIZE) size=APPEAR_MAX_FONT_SIZE;
    out=append_u32(out,size);
    out=append_text(out,"\n");
    (void)pf_create_dir(APPEAR_DIRECTORY);
    return pf_write_file(APPEAR_PATH,buffer,(uint32_t)(out-buffer));
}
