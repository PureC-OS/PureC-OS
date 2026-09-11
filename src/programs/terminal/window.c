#include "window.h"
#include "../../libc/include/purec.h"

#define TERMINAL_MAX_WIDTH 800
#define TERMINAL_MAX_HEIGHT 520
#define TERMINAL_SCREEN_MARGIN 40
#define TERMINAL_CLIENT_INSET 10

static void draw_window(struct terminal_window *terminal){
    pg_window_begin(&terminal->gui);
    pg_window_end(&terminal->gui);
}

static bool configure_console(struct terminal_window *terminal){
    struct pg_rect client=pg_window_client(&terminal->gui);
    uint32_t inset=terminal->inset;
    if(client.width<=inset*2 || client.height<=inset*2) return false;
    return pc_console_configure(client.x+inset,client.y+inset,
        client.width-inset*2,client.height-inset*2,
        terminal->gui.theme.text,terminal->gui.theme.window);
}

bool terminal_window_init_titled(struct terminal_window *terminal,
                                 const char *title){
    if(!terminal) return false;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return false;
    uint32_t width=display.width>TERMINAL_SCREEN_MARGIN*2
        ? display.width-TERMINAL_SCREEN_MARGIN*2 : display.width;
    uint32_t height=display.height>TERMINAL_SCREEN_MARGIN*2
        ? display.height-TERMINAL_SCREEN_MARGIN*2 : display.height;
    if(width>TERMINAL_MAX_WIDTH) width=TERMINAL_MAX_WIDTH;
    if(height>TERMINAL_MAX_HEIGHT) height=TERMINAL_MAX_HEIGHT;
    terminal->inset=TERMINAL_CLIENT_INSET;
    if(!pg_window_center(&terminal->gui,title ? title : "PureC Terminal",
                         width,height))
        return false;
    draw_window(terminal);
    if(!configure_console(terminal)) return false;
    pc_console_clear();
    return true;
}

bool terminal_window_init(struct terminal_window *terminal){
    return terminal_window_init_titled(terminal,"PureC Terminal");
}

bool terminal_window_repaint(struct terminal_window *terminal){
    if(!terminal || !pg_window_is_open(&terminal->gui)) return false;
    pc_console_disable();
    draw_window(terminal);
    if(pg_window_is_minimized(&terminal->gui)) return true;
    return configure_console(terminal);
}

bool terminal_window_restore(struct terminal_window *terminal){
    pc_desktop_redraw();
    return terminal_window_repaint(terminal);
}

bool terminal_window_service(struct terminal_window *terminal){
    if(!terminal || !pg_window_is_open(&terminal->gui)) return false;
    struct pg_event event;
    if(!pg_window_poll_event(&terminal->gui,&event)) return true;
    if(event.type==PG_EVENT_CLOSE) return false;
    if(event.type==PG_EVENT_MINIMIZE || event.type==PG_EVENT_FOCUS
       || event.type==PG_EVENT_REPAINT || event.type==PG_EVENT_MOVE)
        (void)terminal_window_repaint(terminal);
    return pg_window_is_open(&terminal->gui);
}

bool terminal_window_read_line(struct terminal_window *terminal,
                               const char *prompt,
                               char *buffer, uint32_t capacity){
    if(!terminal || !buffer || !capacity) return false;
    if(!prompt) prompt="";
    uint32_t length=0;
    buffer[0]='\0';
    pc_write(prompt);
    while(pg_window_is_open(&terminal->gui)){
        struct pg_event event;
        if(!pg_window_poll_event(&terminal->gui,&event)){
            pc_sleep(8);
            continue;
        }
        if(event.type==PG_EVENT_CLOSE) return false;
        if(event.type==PG_EVENT_MINIMIZE
           || event.type==PG_EVENT_FOCUS
           || event.type==PG_EVENT_REPAINT){
            (void)terminal_window_repaint(terminal);
            continue;
        }
        if(event.type==PG_EVENT_MOVE){
            (void)terminal_window_repaint(terminal);
            continue;
        }
        if(event.type!=PG_EVENT_KEY || pg_window_is_minimized(&terminal->gui))
            continue;
        char character=(char)event.key;
        if(character=='\r' || character=='\n'){
            pc_write("\n");
            return true;
        }
        if(character=='\b' || character==127){
            if(length){
                buffer[--length]='\0';
                pc_write("\b");
            }
            continue;
        }
        if(character<' ' || character>'~' || length+1>=capacity) continue;
        buffer[length++]=character;
        buffer[length]='\0';
        char echo[2]={character,'\0'};
        pc_write(echo);
    }
    return false;
}

void terminal_window_shutdown(struct terminal_window *terminal){
    if(!terminal) return;
    pc_console_disable();
    if(pg_window_is_open(&terminal->gui)) pg_window_close(&terminal->gui);
}
