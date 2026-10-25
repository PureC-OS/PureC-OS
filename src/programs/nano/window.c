#include "window.h"
#include "../../libc/include/purec.h"

#define NANO_MAX_WIDTH 760
#define NANO_MAX_HEIGHT 500
#define NANO_SCREEN_MARGIN 48
#define NANO_CLIENT_INSET 10

static bool configure_console(struct nano_window *window){
    struct pg_rect client=pg_window_client(&window->gui);
    uint32_t inset=window->inset;
    if(client.width<=inset*2 || client.height<=inset*2) return false;
    return pc_console_configure(client.x+inset,client.y+inset,
        client.width-inset*2,client.height-inset*2,
        window->gui.theme.text,window->gui.theme.window);
}

bool nano_window_init(struct nano_window *window){
    if(!window) return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return false;
    uint32_t width=display.width>NANO_SCREEN_MARGIN*2
        ? display.width-NANO_SCREEN_MARGIN*2 : display.width;
    uint32_t height=display.height>NANO_SCREEN_MARGIN*2
        ? display.height-NANO_SCREEN_MARGIN*2 : display.height;
    if(width>NANO_MAX_WIDTH) width=NANO_MAX_WIDTH;
    if(height>NANO_MAX_HEIGHT) height=NANO_MAX_HEIGHT;
    window->inset=NANO_CLIENT_INSET;
    window->render_active=false;
    return pg_window_center(&window->gui,"PureC nano",width,height);
}

bool nano_window_begin_render(struct nano_window *window){
    if(!window || !pg_window_is_open(&window->gui)
       || window->render_active) return false;
    pc_console_disable();
    pg_window_begin(&window->gui);
    if(pg_window_is_minimized(&window->gui)){
        pg_window_end(&window->gui);
        return false;
    }
    if(!configure_console(window)){
        pg_window_end(&window->gui);
        return false;
    }
    pc_console_clear();
    window->render_active=true;
    return true;
}

void nano_window_end_render(struct nano_window *window){
    if(!window || !window->render_active) return;
    window->render_active=false;
    pg_window_end(&window->gui);
}

bool nano_window_poll_event(struct nano_window *window,
                            struct pg_event *event){
    return window && pg_window_poll_event(&window->gui,event);
}

bool nano_window_is_minimized(const struct nano_window *window){
    return window && pg_window_is_minimized(&window->gui);
}

uint32_t nano_window_console_rows(const struct nano_window *window){
    if(!window) return 0;
    struct pg_rect client=pg_window_client(&window->gui);
    return client.height>window->inset*2
        ? (client.height-window->inset*2)/10U : 0;
}

uint32_t nano_window_console_columns(const struct nano_window *window){
    if(!window) return 0;
    struct pg_rect client=pg_window_client(&window->gui);
    return client.width>window->inset*2
        ? (client.width-window->inset*2)/8U : 0;
}

void nano_window_shutdown(struct nano_window *window){
    if(!window) return;
    nano_window_end_render(window);
    pc_console_disable();
    if(pg_window_is_open(&window->gui)) pg_window_close(&window->gui);
}
