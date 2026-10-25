#include "settings/audio_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"

#define PAGE_LEFT 198
#define PAGE_TOP 80

static char *append_u32(char *out,uint32_t value){
    char reverse[12]; uint32_t count=0;
    do{ reverse[count++]=(char)('0'+value%10U); value/=10U; }
    while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0'; return out;
}

static bool inside(const struct pg_window *window,struct pg_rect bounds,
                   const struct pg_event *event){
    if(!event || event->type!=PG_EVENT_MOUSE_UP || event->button!=1)
        return false;
    int32_t x=event->x-(int32_t)window->client.x;
    int32_t y=event->y-(int32_t)window->client.y;
    return x>=(int32_t)bounds.x && y>=(int32_t)bounds.y
        && x<(int32_t)(bounds.x+bounds.width)
        && y<(int32_t)(bounds.y+bounds.height);
}

static void commit(struct settings_model *model){
    (void)settings_model_apply(model);
    (void)settings_model_save(model);
}

void audio_page_draw(struct pg_window *window,struct settings_model *model,
                     const struct pg_event *event){
    uint32_t width=window->client.width-PAGE_LEFT-22;
    pg_window_text(window,PAGE_LEFT,PAGE_TOP-2,"Sound",window->theme.text);
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,PAGE_TOP+24,width,118},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,PAGE_TOP+44,"Master volume",
                   window->theme.text);
    char percent[12]; char *end=append_u32(percent,(uint32_t)model->volume);
    *end++='%'; *end='\0';
    pg_window_text(window,PAGE_LEFT+width-64,PAGE_TOP+44,percent,
                   model->muted ? window->theme.danger : window->theme.accent);
    struct pg_rect slider={PAGE_LEFT+18,PAGE_TOP+70,width-36,14};
    pg_window_rect(window,slider,0x181825);
    uint32_t filled=(slider.width*(uint32_t)model->volume)/100U;
    if(filled) pg_window_rect(window,(struct pg_rect){slider.x,slider.y,
        filled,slider.height},model->muted ? 0x585B70 : 0x89B4FA);
    if(inside(window,slider,event)){
        int32_t local_x=event->x-(int32_t)window->client.x-(int32_t)slider.x;
        model->volume=(int)((uint32_t)local_x*100U/slider.width);
        model->muted=0;
        commit(model);
    }
    if(pg_button(window,(struct pg_rect){PAGE_LEFT+18,PAGE_TOP+100,92,28},
                 model->muted ? "Unmute" : "Mute",event)){
        model->muted=!model->muted; commit(model);
    }
    if(pg_button(window,(struct pg_rect){PAGE_LEFT+118,PAGE_TOP+100,92,28},
                 "Test sound",event)) pa_play_test_sound();

    struct pa_status status={0};
    (void)pa_get_status(&status);
    uint32_t output_top=PAGE_TOP+158;
    pg_window_rect(window,(struct pg_rect){PAGE_LEFT,output_top,width,126},
                   0x2B2D40);
    pg_window_text(window,PAGE_LEFT+18,output_top+20,"Output device",
                   window->theme.text);
    const char *backend=pa_backend_name(status.backend);
    pg_window_text(window,PAGE_LEFT+18,output_top+44,backend,
                   window->theme.muted_text);
    char position[32]="Device ";
    end=position+pc_strlen(position);
    end=append_u32(end,status.selected_output_device+1U);
    *end++='/'; end=append_u32(end,status.output_device_count);
    pg_window_text(window,PAGE_LEFT+width-120,output_top+20,position,0xBAC2DE);
    if(pg_button(window,(struct pg_rect){PAGE_LEFT+18,output_top+76,88,28},
                 "Previous",event)){
        uint32_t count=status.output_device_count ? status.output_device_count : 1;
        model->device=(int)((status.selected_output_device+count-1U)%count);
        commit(model);
    }
    if(pg_button(window,(struct pg_rect){PAGE_LEFT+114,output_top+76,88,28},
                 "Next",event)){
        uint32_t count=status.output_device_count ? status.output_device_count : 1;
        model->device=(int)((status.selected_output_device+1U)%count);
        commit(model);
    }
}
