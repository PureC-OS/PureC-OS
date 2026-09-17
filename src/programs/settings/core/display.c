#include "settings/display.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"

static const struct display_mode modes[]={
    {1024,768,32},
    {1280,720,32},
    {1280,800,32},
    {1366,768,32},
    {1440,900,32},
    {1600,900,32},
    {1920,1080,32},
};

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

void display_defaults(struct display_settings *s){
    if(!s) return;
    s->width=1280;
    s->height=800;
    s->bpp=32;
}

uint32_t display_mode_count(void){
    return sizeof(modes)/sizeof(modes[0]);
}

struct display_mode display_mode_at(uint32_t index){
    if(index>=display_mode_count()) return modes[2];
    return modes[index];
}

uint32_t display_mode_index_of(uint32_t width, uint32_t height){
    uint32_t best=2;
    uint32_t best_score=UINT32_MAX;
    for(uint32_t i=0;i<display_mode_count();i++){
        uint32_t dw=modes[i].width>width ? modes[i].width-width
                                         : width-modes[i].width;
        uint32_t dh=modes[i].height>height ? modes[i].height-height
                                           : height-modes[i].height;
        uint32_t score=dw+dh;
        if(score<best_score){ best_score=score; best=i; }
        if(!score) break;
    }
    return best;
}

bool display_load(struct display_settings *s){
    if(!s) return false;
    display_defaults(s);
    int32_t descriptor=pf_open(DISPLAY_INI_PATH);
    if(descriptor<0) return false;
    char buffer[128]={0};
    int32_t amount=pf_read(descriptor,buffer,sizeof(buffer)-1);
    (void)pf_close(descriptor);
    if(amount<=0) return false;
    buffer[amount]='\0';
    uint32_t width=0,height=0,bpp=0;
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(starts_with(line,"width=")) width=parse_u32(line+6);
        else if(starts_with(line,"height=")) height=parse_u32(line+7);
        else if(starts_with(line,"bpp=")) bpp=parse_u32(line+4);
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(width>=640 && width<=7680) s->width=width;
    if(height>=400 && height<=4320) s->height=height;
    if(bpp==16 || bpp==24 || bpp==32) s->bpp=bpp;
    return true;
}

int32_t display_save(const struct display_settings *s){
    if(!s) return -3;
    char buffer[64];
    char *out=buffer;
    out=append_text(out,"width=");
    out=append_u32(out,s->width);
    out=append_text(out,"\nheight=");
    out=append_u32(out,s->height);
    out=append_text(out,"\nbpp=");
    out=append_u32(out,s->bpp);
    out=append_text(out,"\n");
    (void)pf_create_dir(DISPLAY_INI_DIR);
    return pf_write_file(DISPLAY_INI_PATH,buffer,(uint32_t)(out-buffer));
}

int32_t display_apply_live(const struct display_settings *s){
    if(!s) return -3;
    return pc_display_set_mode(s->width,s->height,s->bpp);
}
