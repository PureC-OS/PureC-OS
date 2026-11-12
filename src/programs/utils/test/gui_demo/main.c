#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"

static bool draw_demo(struct pg_window *window, const struct pg_event *event,
                      const char *message){
    pg_window_begin(window);
    pg_label(window,24,24,"PureGUI " PG_VERSION " system library");
    pg_window_text(window,24,44,
                   "Applications provide text and client coordinates.",
                   window->theme.muted_text);
    pg_panel(window,(struct pg_rect){24,76,window->client.width-48,72});
    pg_window_text(window,40,94,message,window->theme.text);
    bool clicked=pg_button(window,(struct pg_rect){24,172,156,38},
                           "Press me",event);
    pg_window_text(window,202,187,"Esc or x closes the window",
                   window->theme.muted_text);
    pg_window_end(window);
    return clicked;
}

static int gui_demo_main(void){
    struct pg_window window;
    if(!pg_window_center(&window,"PureGUI demo",560,280)) return 1;
    struct pg_event event={.type=PG_EVENT_NONE};
    const char *message="The window and widgets are drawn by the library.";
    (void)draw_demo(&window,&event,message);
    while(pg_window_is_open(&window)){
        if(!pg_window_poll_event(&window,&event)){
            pc_sleep(16);
            continue;
        }
        if(event.type==PG_EVENT_CLOSE) break;
        if(draw_demo(&window,&event,message)){
            message="Button event received by the application.";
            event.type=PG_EVENT_NONE;
            (void)draw_demo(&window,&event,message);
        }
    }
    return 0;
}

void _start(void){
    pc_exit(gui_demo_main());
}
