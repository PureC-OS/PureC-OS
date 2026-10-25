#include "settings/model.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"

#define SETTINGS_DIRECTORY "/config"
#define SETTINGS_PATH "/config/settings.ini"

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

static bool starts_with(const char *text,const char *prefix){
    while(*prefix){ if(*text++!=*prefix++) return false; }
    return true;
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

void settings_model_init(struct settings_model *model){
    struct audio_status detected={0};
    model->volume=65;
    model->muted=0;
    model->device=0;
    if(pc_audio_get_status(&detected)){
        model->volume=(int)detected.volume;
        model->muted=detected.muted!=0;
        model->device=(int)detected.selected_output_device;
    }
}

bool settings_model_load(struct settings_model *model){
    int32_t descriptor=pf_open(SETTINGS_PATH);
    if(descriptor<0) return false;
    char buffer[256]={0};
    int32_t amount=pf_read(descriptor,buffer,sizeof(buffer)-1);
    (void)pf_close(descriptor);
    if(amount<=0) return false;
    buffer[amount]='\0';
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(starts_with(line,"volume="))
            model->volume=(int)parse_u32(line+7);
        else if(starts_with(line,"muted="))
            model->muted=parse_u32(line+6)!=0;
        else if(starts_with(line,"device="))
            model->device=(int)parse_u32(line+7);
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(model->volume<0) model->volume=0;
    if(model->volume>100) model->volume=100;
    if(model->device<0) model->device=0;
    return true;
}

bool settings_model_save(const struct settings_model *model){
    char buffer[128];
    char *out=buffer;
    out=append_text(out,"volume=");
    out=append_u32(out,(uint32_t)model->volume);
    out=append_text(out,"\nmuted=");
    out=append_u32(out,model->muted ? 1U : 0U);
    out=append_text(out,"\ndevice=");
    out=append_u32(out,(uint32_t)model->device);
    out=append_text(out,"\n");
    (void)pf_create_dir(SETTINGS_DIRECTORY);
    return pf_write_file(SETTINGS_PATH,buffer,(uint32_t)(out-buffer))>=0;
}

bool settings_model_apply(struct settings_model *model){
    struct audio_status status={0};
    if(!pc_audio_get_status(&status)) return false;
    if(model->device<0
       || (uint32_t)model->device>=status.output_device_count)
        model->device=(int)status.selected_output_device;
    bool selected=pc_audio_select_output((uint32_t)model->device);
    pc_audio_set_volume((uint32_t)model->volume);
    pc_audio_set_muted(model->muted!=0);
    return selected;
}
