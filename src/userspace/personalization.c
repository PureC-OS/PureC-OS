#include "personalization.h"
#include "wallpaper.h"
#include "../libc/include/purec.h"
#include "../libc/include/hosted/string.h"

static struct personalization g_current;
static bool g_has_current;
static uint64_t g_last_poll_tick;

static const struct {
    const char *name;
    struct personalization_colors colors;
} theme_table[]={
    {"catppuccin-dark", {0x181825,0x1E1E2E,0x313244,0x45475A,0xCDD6F4,0x9399B2,0x89B4FA,0xF38BA8,0x11111B}},
    {"nord",            {0x2E3440,0x3B4252,0x434C5E,0x4C566A,0xECEFF4,0x9AA0B0,0x88C0D0,0xBF616A,0x1A1C24}},
    {"dracula",         {0x282A36,0x2E3247,0x44475A,0x6272A4,0xF8F8F2,0x9AA0B2,0xBD93F9,0xFF5555,0x1A1B26}},
    {"light",           {0xE6E9EF,0xEFF1F5,0xDCE0E8,0xBCC0CC,0x4C4F69,0x8C8FA1,0x1E66F5,0xD20F39,0x9CA0B0}},
    {"tokyo-night",     {0x1A1B26,0x24283B,0x292E42,0x565F89,0xC0CAF5,0x9AA5CE,0x7AA2F7,0xF7768E,0x101014}},
    {"tokyo-day",       {}},

};

static void copy_str(char *dst, uint32_t cap, const char *src){
    if(!dst || !cap) return;
    if(!src) src="";
    uint32_t i=0;
    while(src[i] && i+1<cap){ dst[i]=src[i]; i++; }
    dst[i]='\0';
}

static bool starts_with(const char *text, const char *prefix){
    while(*prefix){ if(*text++!=*prefix++) return false; }
    return true;
}

static void personalization_defaults(struct personalization *p){
    if(!p) return;
    copy_str(p->theme,sizeof(p->theme),"catppuccin-dark");
    p->wallpaper[0]='\0';
}

static bool personalization_theme_colors(
    const char *name,struct personalization_colors *out){
    if(out) *out=theme_table[0].colors;
    if(!name || !out) return false;
    uint32_t count=(uint32_t)(sizeof(theme_table)/sizeof(theme_table[0]));
    for(uint32_t i=0;i<count;i++){
        if(strcmp(name,theme_table[i].name)==0){
            *out=theme_table[i].colors;
            return true;
        }
    }
    return false;
}

static bool personalization_load(struct personalization *p){
    if(!p) return false;
    personalization_defaults(p);
    int32_t fd=pc_file_open(PERSONALIZATION_PATH);
    char buffer[512];
    int32_t total=0;
    if(fd>=0){
        for(;;){
            if(total>=(int32_t)sizeof(buffer)-1) break;
            int32_t n=pc_file_read(fd,buffer+total,
                (uint32_t)(sizeof(buffer)-1-(uint32_t)total));
            if(n<=0) break;
            total+=n;
        }
        (void)pc_file_close(fd);
    }
    if(fd<0){
        return true;
    }
    if(total<=0) return true;
    buffer[total]='\0';
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(starts_with(line,"theme="))
            copy_str(p->theme,sizeof(p->theme),line+6);
        else if(starts_with(line,"wallpaper="))
            copy_str(p->wallpaper,sizeof(p->wallpaper),line+10);
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    return true;
}

static bool same_personalization(const struct personalization *a,
                                 const struct personalization *b){
    return strcmp(a->theme,b->theme)==0
        && strcmp(a->wallpaper,b->wallpaper)==0;
}

static void personalization_apply(const struct personalization *p){
    if(!p) return;
    wallpaper_set_path(p->wallpaper);
}

bool personalization_poll(void){
    struct cpu_monitor_info cpu;
    uint64_t now=pc_cpu_info(&cpu) ? cpu.uptime_ms : 0;
    if(g_has_current && now-g_last_poll_tick<500) return false;
    g_last_poll_tick=now;
    struct personalization next;
    personalization_load(&next);
    if(g_has_current && same_personalization(&next,&g_current)) return false;
    bool first=!g_has_current;
    g_current=next;
    g_has_current=true;
    personalization_apply(&g_current);
    if(first) return false;
    return true;
}

static const struct personalization *personalization_current(void){
    if(!g_has_current){
        personalization_load(&g_current);
        g_has_current=true;
        personalization_apply(&g_current);
    }
    return &g_current;
}

void personalization_current_colors(struct personalization_colors *out){
    if(!out) return;
    personalization_theme_colors(personalization_current()->theme,out);
}
