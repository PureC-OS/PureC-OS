/* PureC OS login screen (Ring 3).
 *
 * Fullscreen window shown right after boot, before the desktop becomes
 * usable. v1: a single Login button (Enter works too). The window pins
 * itself at (0,0) and is marked non-closable, so it can be neither
 * dragged away, nor closed via X, Esc or minimize. Successful login
 * repaints the desktop underneath and exits with status 0, which
 * reveals the desktop; the kernel reaps the process.
 * User/password fields plug in here later (see LOGIN_AUTH_TODO). */
#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"

#define LOGIN_BG        0x11111BU
#define LOGIN_TEXT      0xCDD6F4U
#define LOGIN_MUTED     0x9399B2U
#define LOGIN_ACCENT    0x89B4FAU
#define DIALOG_W        440U
#define DIALOG_H        250U
#define BUTTON_W        220U
#define BUTTON_H        46U

/* LOGIN_AUTH_TODO: replace with username/password fields + hash check
 * (libpurecrypt) once account storage exists. */

static uint32_t umin(uint32_t a, uint32_t b){ return a<b ? a : b; }

static bool point_inside(int32_t px, int32_t py,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h){
    return px>=(int32_t)x && py>=(int32_t)y
        && px<(int32_t)(x+w) && py<(int32_t)(y+h);
}


static bool login_button_hover(const struct pg_window *window,
                               int32_t mx, int32_t my){
    uint32_t cw=window->client.width;
    uint32_t ch=window->client.height;
    uint32_t dw=umin(DIALOG_W,cw>16 ? cw-16 : cw);
    uint32_t dx=cw>dw ? (cw-dw)/2 : 0;
    uint32_t dy=ch>DIALOG_H ? (ch-DIALOG_H)/2 : 0;
    uint32_t bx=dx+(dw>BUTTON_W ? (dw-BUTTON_W)/2 : 0);
    uint32_t by=dy+DIALOG_H-100;
    return point_inside(mx, my,
                        window->client.x+bx, window->client.y+by,
                        BUTTON_W, BUTTON_H);
}

/* Draw one frame. Returns true when the user requested login. */
static bool draw_login(struct pg_window *window, const struct pg_event *event,
                       bool farewell){
    uint32_t cw=window->client.width;
    uint32_t ch=window->client.height;
    uint32_t dw=umin(DIALOG_W,cw>16 ? cw-16 : cw);
    uint32_t dx=cw>dw ? (cw-dw)/2 : 0;
    uint32_t dy=ch>DIALOG_H ? (ch-DIALOG_H)/2 : 0;
    bool login=false;

    pg_window_begin(window);
    pg_window_clear(window,LOGIN_BG);
    pg_panel(window,(struct pg_rect){dx,dy,dw,DIALOG_H});
    pg_window_text(window,dx+32,dy+28,"PureC OS",LOGIN_TEXT);
    pg_label(window,dx+32,dy+56,"Welcome back.");
    if(farewell){
        pg_window_text(window,dx+32,dy+88,"Logging in...",
                       LOGIN_ACCENT);
    }else{
        pg_window_text(window,dx+32,dy+88,
                       "Press Login to enter the desktop.",
                       LOGIN_MUTED);
    }
    login=pg_button(window,(struct pg_rect){
                        dx+(dw> BUTTON_W ? (dw-BUTTON_W)/2 : 0),
                        dy+DIALOG_H-100,BUTTON_W,BUTTON_H},
                    "Login",event);
    pg_window_text(window,dx+32,dy+DIALOG_H-36,
                   "Enter works too - accounts coming soon",
                   LOGIN_MUTED);
    pg_window_end(window);
    return login;
}

static int login_main(void){
    struct pc_display_info info;
    struct pg_window window;
    struct pg_event event={.type=PG_EVENT_NONE};
    bool farewell=false;

    if(!pc_display_get_info(&info) || !info.width || !info.height){
        pc_write("login: no display available\n");
        return 1;
    }
    if(!pg_window_init(&window,"PureC OS - Login",
                       0,0,info.width,info.height)){
        pc_write("login: cannot open fullscreen window\n");
        return 1;
    }
    /* Gate window: no X/Esc/minimize dismissal (enforced by libgui). */
    pg_window_set_closable(&window,false);
    (void)draw_login(&window,&event,false);
    bool last_hover=login_button_hover(&window, event.x, event.y);
    while(pg_window_is_open(&window)){
        /* Pin the fullscreen frame: no dragging it away. */
        if(window.frame.x!=0 || window.frame.y!=0)
            (void)pg_window_move(&window,0,0);
        if(!pg_window_poll_event(&window,&event)){
            pc_sleep(16);
            continue;
        }
        /* Deliberately no CLOSE handling: X/Esc must not skip login. */
        if(event.type==PG_EVENT_KEY
           && (event.key=='\r' || event.key=='\n'))
            farewell=true;
        else if(event.type==PG_EVENT_MOUSE_MOVE){
            bool hover=login_button_hover(&window, event.x, event.y);
            if(hover==last_hover){
                event.type=PG_EVENT_NONE;
                continue;
            }
            last_hover=hover;
            (void)draw_login(&window,&event,farewell);
        } else {
            if(draw_login(&window,&event,farewell))
                farewell=true;
            last_hover=login_button_hover(&window, event.x, event.y);
        }
        if(farewell){
            (void)draw_login(&window,&event,true);
            pc_sleep(250);
            /* Repaint the desktop underneath BEFORE exiting: without
             * this the framebuffer keeps our frozen pixels, because
             * nobody else asks for a redraw when we go away. */
            pc_desktop_redraw();
            return 0;
        }
        event.type=PG_EVENT_NONE;
    }
    return 0;
}

void _start(void){
    pc_exit(login_main());
}
