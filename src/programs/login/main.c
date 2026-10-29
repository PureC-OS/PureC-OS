#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"
#include "../accounts/accounts.h"

#define LOGIN_BG        0x11111BU
#define LOGIN_TEXT      0xCDD6F4U
#define LOGIN_MUTED     0x9399B2U
#define LOGIN_ACCENT    0x89B4FAU
#define LOGIN_FIELD     0x1E1E2EU
#define LOGIN_ERROR     0xF38BA8U
#define DIALOG_W        460U
#define DIALOG_H        360U
#define FIELD_H         38U
#define BUTTON_W        220U
#define BUTTON_H        46U
#define NAME_MAX_LEN    31U
#define PASS_MAX_LEN    63U
#define FIELD_USER      0U
#define FIELD_PASS      1U
#define PASSWD_PATH     "/etc/passwd"
#define SHADOW_PATH     "/etc/shadow"

struct login_state {
    char user[NAME_MAX_LEN+1];
    char pass[PASS_MAX_LEN+1];
    uint32_t focus;
    uint32_t failures;
    bool live;
    bool busy;
    bool done;
    const char *message;
};

struct login_layout {
    uint32_t dx,dy,dw;
    struct pg_rect user,pass,button;
};

static uint32_t umin(uint32_t a, uint32_t b){ return a<b ? a : b; }

static void wipe(void *memory, uint32_t size){
    volatile uint8_t *bytes=(volatile uint8_t *)memory;
    for(uint32_t i=0;i<size;i++) bytes[i]=0;
}

static bool point_inside(int32_t px, int32_t py, struct pg_rect r,
                         const struct pg_window *window){
    int32_t x=(int32_t)(window->client.x+r.x);
    int32_t y=(int32_t)(window->client.y+r.y);
    return px>=x && py>=y && px<x+(int32_t)r.width && py<y+(int32_t)r.height;
}

static struct login_layout layout_for(const struct pg_window *window){
    struct login_layout l;
    uint32_t cw=window->client.width;
    uint32_t ch=window->client.height;
    l.dw=umin(DIALOG_W,cw>16 ? cw-16 : cw);
    l.dx=cw>l.dw ? (cw-l.dw)/2 : 0;
    l.dy=ch>DIALOG_H ? (ch-DIALOG_H)/2 : 0;
    uint32_t inner=l.dw>64 ? l.dw-64 : l.dw;
    l.user=(struct pg_rect){l.dx+32,l.dy+112,inner,FIELD_H};
    l.pass=(struct pg_rect){l.dx+32,l.dy+184,inner,FIELD_H};
    l.button=(struct pg_rect){l.dx+(l.dw>BUTTON_W ? (l.dw-BUTTON_W)/2 : 0),
                              l.dy+DIALOG_H-96,BUTTON_W,BUTTON_H};
    return l;
}

static void draw_field(struct pg_window *window, struct pg_rect r,
                       const char *label, const char *text, bool secret,
                       bool focused){
    char shown[PASS_MAX_LEN+2];
    uint32_t n=0;
    for(;text[n] && n<PASS_MAX_LEN;n++) shown[n]=secret ? '*' : text[n];
    if(focused) shown[n++]='_';
    shown[n]='\0';
    pg_window_text(window,r.x,r.y-22,label,LOGIN_MUTED);
    pg_window_rect(window,r,focused ? LOGIN_ACCENT : LOGIN_MUTED);
    pg_window_rect(window,(struct pg_rect){r.x+1,r.y+1,r.width-2,r.height-2},
                   LOGIN_FIELD);
    pg_window_text(window,r.x+10,r.y+12,shown,LOGIN_TEXT);
}

static bool draw_login(struct pg_window *window, const struct login_state *s,
                       const struct pg_event *event){
    struct login_layout l=layout_for(window);
    bool clicked;

    pg_window_begin(window);
    pg_window_clear(window,LOGIN_BG);
    pg_panel(window,(struct pg_rect){l.dx,l.dy,l.dw,DIALOG_H});
    pg_window_text(window,l.dx+32,l.dy+28,"PureC OS",LOGIN_TEXT);
    if(s->live){
        pg_label(window,l.dx+32,l.dy+60,"Live session - no accounts yet.");
        pg_window_text(window,l.dx+32,l.dy+100,
                       "Press Login to enter as root.",LOGIN_MUTED);
    }else{
        pg_label(window,l.dx+32,l.dy+60,"Sign in to continue.");
        draw_field(window,l.user,"User name",s->user,false,
                   s->focus==FIELD_USER && !s->busy);
        draw_field(window,l.pass,"Password",s->pass,true,
                   s->focus==FIELD_PASS && !s->busy);
    }
    if(s->message)
        pg_window_text(window,l.dx+32,l.dy+DIALOG_H-132,s->message,
                       s->busy ? LOGIN_ACCENT : LOGIN_ERROR);
    clicked=pg_button(window,l.button,"Login",event);
    pg_window_text(window,l.dx+32,l.dy+DIALOG_H-32,
                   s->live ? "Install PureC OS to create accounts"
                           : "Tab switches fields, Enter signs in",
                   LOGIN_MUTED);
    pg_window_end(window);
    return clicked && !s->busy;
}

static bool live_session(void){
    int32_t shadow=pc_file_open(SHADOW_PATH);
    if(shadow>=0){
        pc_file_close(shadow);
        return false;
    }
    int32_t passwd=pc_file_open(PASSWD_PATH);
    if(passwd>=0){
        pc_file_close(passwd);
    }
    return true;
}

static void type_key(struct login_state *s, char key){
    char *field=s->focus==FIELD_USER ? s->user : s->pass;
    uint32_t limit=s->focus==FIELD_USER ? NAME_MAX_LEN : PASS_MAX_LEN;
    uint32_t length=pc_strlen(field);
    if(key=='\b' || key==127){
        if(length) field[length-1]='\0';
        return;
    }
    if(key<' ' || key>'~' || length>=limit) return;
    field[length]=key;
    field[length+1]='\0';
}

static bool attempt(struct pg_window *window, struct login_state *s,
                    const struct pg_event *event){
    struct user_info info;
    bool ok=false;
    if(s->live){
        ok=true;
        info.uid=0;
        info.gid=0;
    }else{
        if(!s->user[0]){
            s->message="Enter a user name.";
            s->focus=FIELD_USER;
            return false;
        }
        s->busy=true;
        s->message="Checking...";
        (void)draw_login(window,s,event);
        ok=account_login(s->user,s->pass,&info);
        s->busy=false;
    }
    wipe(s->pass,sizeof(s->pass));
    if(!ok){
        s->failures++;
        s->message="Login incorrect.";
        s->focus=FIELD_PASS;
        pc_sleep(umin(s->failures,5)*600);
        return false;
    }
    if(pc_session_begin(info.uid,info.gid)!=0){
        s->message="Cannot start the session.";
        return false;
    }
    s->done=true;
    return true;
}

static int login_main(void){
    struct pc_display_info info;
    struct pg_window window;
    struct pg_event event={.type=PG_EVENT_NONE};
    struct login_state state;

    if(!pc_display_get_info(&info) || !info.width || !info.height){
        pc_write("login: no display available\n");
        return 1;
    }
    if(!pg_window_init(&window,"PureC OS - Login",
                       0,0,info.width,info.height)){
        pc_write("login: cannot open fullscreen window\n");
        return 1;
    }
    pg_window_set_closable(&window,false);
    wipe(&state,sizeof(state));
    state.live=live_session();
    state.focus=FIELD_USER;
    (void)draw_login(&window,&state,&event);
    while(pg_window_is_open(&window)){
        if(window.frame.x!=0 || window.frame.y!=0)
            (void)pg_window_move(&window,0,0);
        if(!pg_window_poll_event(&window,&event)){
            pc_sleep(16);
            continue;
        }
        bool submit=false;
        if(event.type==PG_EVENT_KEY){
            char key=(char)event.key;
            if(key=='\t' && !state.live){
                state.focus=state.focus==FIELD_USER ? FIELD_PASS : FIELD_USER;
            }else if(key=='\r' || key=='\n'){
                if(!state.live && state.focus==FIELD_USER && state.user[0])
                    state.focus=FIELD_PASS;
                else
                    submit=true;
            }else if(!state.live){
                type_key(&state,key);
            }
            state.message=0;
        }else if(event.type==PG_EVENT_MOUSE_DOWN && !state.live){
            struct login_layout l=layout_for(&window);
            if(point_inside(event.x,event.y,l.user,&window))
                state.focus=FIELD_USER;
            else if(point_inside(event.x,event.y,l.pass,&window))
                state.focus=FIELD_PASS;
        }
        if(draw_login(&window,&state,&event)) submit=true;
        if(submit && attempt(&window,&state,&event)){
            (void)draw_login(&window,&state,&event);
            pc_sleep(250);
            wipe(&state,sizeof(state));
            pc_desktop_redraw();
            return 0;
        }
        if(submit) (void)draw_login(&window,&state,&event);
        event.type=PG_EVENT_NONE;
    }
    wipe(&state,sizeof(state));
    return 0;
}

void _start(void){
    pc_exit(login_main());
}
