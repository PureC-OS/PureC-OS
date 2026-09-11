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

static const char *boot_configs[]={
    "/limine.conf",
    "/boot/limine/limine.conf",
    "/boot/limine.conf",
    "/limine/limine.conf",
    "/EFI/limine/limine.conf",
    "/EFI/BOOT/limine.conf",
};

#define DISPLAY_IN_BUFFER 4096
#define DISPLAY_OUT_BUFFER 4608

static char display_in_buffer[DISPLAY_IN_BUFFER];
static char display_out_buffer[DISPLAY_OUT_BUFFER];

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
    for(uint32_t i=0;i<display_mode_count();i++){
        if(modes[i].width==width && modes[i].height==height) return i;
    }
    return 2; /* 1280x800 default */
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

static bool is_resolution_line(const char *line){
    while(*line==' ' || *line=='\t') line++;
    return starts_with(line,"resolution:");
}

static char *emit_text(char *out, char *limit, const char *text){
    while(*text && out<limit) *out++=*text++;
    return out;
}
static uint32_t rewrite_config(const char *input,
                               const struct display_settings *s){
    char mode_line[48];
    char *m=mode_line;
    m=append_text(m,"    resolution: ");
    m=append_u32(m,s->width);
    m=append_text(m,"x");
    m=append_u32(m,s->height);
    m=append_text(m,"x");
    m=append_u32(m,s->bpp);
    m=append_text(m,"\n");
    char *out=display_out_buffer;
    char *limit=display_out_buffer+sizeof(display_out_buffer)-1;
    uint32_t titles=0;
    const char *cursor=input;
    while(*cursor && out<limit){
        const char *end=cursor;
        while(*end && *end!='\n') end++;
        uint32_t length=(uint32_t)(end-cursor);
        bool has_newline=*end=='\n';
        char saved_line[256];
        uint32_t copy=length<sizeof(saved_line)-1 ? length
                                                  : sizeof(saved_line)-1;
        for(uint32_t i=0;i<copy;i++) saved_line[i]=cursor[i];
        saved_line[copy]='\0';
        if(!is_resolution_line(saved_line)){
            out=emit_text(out,limit,saved_line);
            if(has_newline && out<limit) *out++='\n';
            if(saved_line[0]=='/'){
                out=emit_text(out,limit,mode_line);
                titles++;
            }
        }
        cursor=has_newline ? end+1 : end;
        while(*cursor=='\r') cursor++;
    }
    if(out<display_out_buffer+sizeof(display_out_buffer)) *out='\0';
    return titles;
}

int32_t display_apply_to_boot(const struct display_settings *s){
    if(!s) return -3;
    int32_t patched=0;
    int32_t last_error=-2; /* not found */
    for(uint32_t i=0;
        i<sizeof(boot_configs)/sizeof(boot_configs[0]);i++){
        int32_t fd=pf_open(boot_configs[i]);
        if(fd<0) continue;
        int32_t amount=pf_read(fd,display_in_buffer,
                               sizeof(display_in_buffer)-1);
        (void)pf_close(fd);
        if(amount<=0 || amount>=(int32_t)sizeof(display_in_buffer)-1){
            last_error=-1;
            continue;
        }
        display_in_buffer[amount]='\0';
        if(!rewrite_config(display_in_buffer,s)) continue;
        uint32_t length=0;
        while(display_out_buffer[length]) length++;
        int32_t written=pf_write_file(boot_configs[i],display_out_buffer,
                                      length);
        if(written<0){ last_error=written; continue; }
        patched++;
    }
    if(patched>0) return patched;
    return last_error;
}
