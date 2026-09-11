#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"
#include "settings/app.h"
static int settings_main(void){
    struct pc_display_info d;
    if(!pc_display_get_info(&d)||!d.available) return 1;
    uint32_t width=d.width>980?940:d.width-24;
    uint32_t height=d.height>680?640:d.height-44;
    if(width<640||height<400) return 1;
    struct settings_app app;
    settings_app_init(&app);
    struct pg_window w;
    if(!pg_window_center(&w,"Settings - PureC",width,height)) return 1;
    struct pg_event ev={0};
    settings_app_draw(&app,&w,&ev);
    uint32_t idle_ticks=0;
    while(pg_window_is_open(&w)){
        if(!pg_window_poll_event(&w,&ev)){
            pc_sleep(16);
            if(app.tab==2){
                idle_ticks++;
                if(idle_ticks>=6){
                    idle_ticks=0;
                    struct pg_event repaint={.type=PG_EVENT_REPAINT};
                    settings_app_draw(&app,&w,&repaint);
                }
            } else {
                idle_ticks=0;
            }
            continue;
        }
        idle_ticks=0;
        if(ev.type==PG_EVENT_CLOSE) break;
        if(ev.type==PG_EVENT_MOUSE_MOVE) continue;
        settings_app_draw(&app,&w,&ev);
    }
    return 0;
}
void _start(void){ pc_exit(settings_main()); }
