#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"
#include "../accounts/accounts.h"

#define LOGIN_BG        0x0B0D17U
#define LOGIN_BG2       0x11142AU
#define LOGIN_CARD      0x161A30U
#define LOGIN_CARD_EDGE 0x262B4AU
#define LOGIN_TEXT      0xE8EBFAU
#define LOGIN_MUTED     0x8A92B8U
#define LOGIN_ACCENT    0x6C8CFFU
#define LOGIN_FIELD     0x0D1020U
#define LOGIN_ERROR     0xFF6B8BU
#define CARD_W          420U
#define FIELD_H         38U
#define BUTTON_H        42U
#define CLOCK_SIZE      48U
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
    uint32_t cw,ch;
    uint32_t clock_y;
    uint32_t dx,dy,dw,dh;
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

static struct login_layout layout_for(const struct pg_window *window, bool live){
    struct login_layout l;
    l.cw=window->client.width;
    l.ch=window->client.height;
    l.dw=umin(CARD_W,l.cw>32 ? l.cw-32 : l.cw);
    l.dh=live ? 196U : 300U;
    uint32_t block=CLOCK_SIZE+40+l.dh;
    uint32_t top=l.ch>block ? (l.ch-block)*2/5 : 0;
    l.clock_y=top;
    l.dx=l.cw>l.dw ? (l.cw-l.dw)/2 : 0;
    l.dy=top+CLOCK_SIZE+40;
    uint32_t inner=l.dw>56 ? l.dw-56 : l.dw;
    l.user=(struct pg_rect){l.dx+28,l.dy+86,inner,FIELD_H};
    l.pass=(struct pg_rect){l.dx+28,l.dy+152,inner,FIELD_H};
    l.button=(struct pg_rect){l.dx+28,l.dy+l.dh-BUTTON_H-24,inner,BUTTON_H};
    return l;
}

struct login_time {
    bool valid;
    uint32_t year,month,day,hour,minute,weekday;
};

static struct login_time local_time(void){
    struct login_time t={0};
    struct time_info info;
    if(pc_time_get(&info)<0 || info.source==0) return t;
    int64_t s=info.unix_seconds+(int64_t)info.tz_minutes*60;
    int64_t days=s/86400;
    int64_t rem=s%86400;
    if(rem<0){ rem+=86400; days--; }
    t.hour=(uint32_t)(rem/3600);
    t.minute=(uint32_t)((rem%3600)/60);
    int64_t w=(days+4)%7;
    t.weekday=(uint32_t)(w<0 ? w+7 : w);
    int64_t z=days+719468;
    int64_t era=(z>=0 ? z : z-146096)/146097;
    int64_t doe=z-era*146097;
    int64_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int64_t doy=doe-(365*yoe+yoe/4-yoe/100);
    int64_t mp=(5*doy+2)/153;
    t.day=(uint32_t)(doy-(153*mp+2)/5+1);
    t.month=(uint32_t)(mp<10 ? mp+3 : mp-9);
    t.year=(uint32_t)(yoe+era*400+(t.month<=2));
    t.valid=true;
    return t;
}

static void append(char *dst, uint32_t cap, const char *src){
    uint32_t n=pc_strlen(dst);
    while(*src && n+1<cap) dst[n++]=*src++;
    dst[n]=0;
}

static void append_num(char *dst, uint32_t cap, uint32_t v, uint32_t digits){
    char tmp[12];
    uint32_t n=0;
    do{ tmp[n++]=(char)('0'+v%10); v/=10; }while(v && n<10);
    while(n<digits && n<10) tmp[n++]='0';
    char out[12];
    for(uint32_t i=0;i<n;i++) out[i]=tmp[n-1-i];
    out[n]=0;
    append(dst,cap,out);
}

static void text_center(struct pg_window *window, uint32_t x, uint32_t w,
                        uint32_t y, const char *text, uint32_t color,
                        uint32_t size){
    uint32_t tw=pc_strlen(text)*size;
    uint32_t tx=x+(w>tw ? (w-tw)/2 : 0);
    if(size==8) pg_window_text(window,tx,y,text,color);
    else pg_window_text_sized(window,tx,y,text,color,size);
}

static void draw_field(struct pg_window *window, struct pg_rect r,
                       const char *label, const char *text, bool secret,
                       bool focused){
    char shown[PASS_MAX_LEN+2];
    uint32_t n=0;
    for(;text[n] && n<PASS_MAX_LEN;n++) shown[n]=secret ? '*' : text[n];
    if(focused) shown[n++]='_';
    shown[n]='\0';
    pg_window_text(window,r.x,r.y-16,label,LOGIN_MUTED);
    pg_window_rect(window,r,focused ? LOGIN_ACCENT : LOGIN_CARD_EDGE);
    pg_window_rect(window,(struct pg_rect){r.x+1,r.y+1,r.width-2,r.height-2},
                   LOGIN_FIELD);
    pg_window_text(window,r.x+12,r.y+15,shown,LOGIN_TEXT);
}

static void draw_button(struct pg_window *window, struct pg_rect r,
                        const char *label, bool hot){
    pg_window_rect(window,r,hot ? 0x8AA2FFU : LOGIN_ACCENT);
    text_center(window,r.x,r.width,r.y+(r.height-8)/2,label,0x0B0D17U,8);
}

static bool draw_login(struct pg_window *window, const struct login_state *s,
                       const struct pg_event *event){
    struct login_layout l=layout_for(window,s->live);
    pg_window_begin(window);
    window->theme.window=LOGIN_BG;
    pg_window_clear(window,LOGIN_BG);
    uint32_t band=l.ch/3;
    pg_window_rect(window,(struct pg_rect){0,0,l.cw,band},LOGIN_BG2);
    pg_window_rect(window,(struct pg_rect){0,band,l.cw,band/2},0x0E1120U);

    pg_window_text(window,24,20,"PureC OS",LOGIN_TEXT);

    struct login_time t=local_time();
    char clock[8]="--:--";
    char date[48]="";
    if(t.valid){
        static const char *const days[7]={"Sunday","Monday","Tuesday",
            "Wednesday","Thursday","Friday","Saturday"};
        static const char *const months[12]={"January","February","March",
            "April","May","June","July","August","September","October",
            "November","December"};
        clock[0]=0;
        append_num(clock,sizeof(clock),t.hour,2);
        append(clock,sizeof(clock),":");
        append_num(clock,sizeof(clock),t.minute,2);
        append(date,sizeof(date),days[t.weekday%7]);
        append(date,sizeof(date),", ");
        append_num(date,sizeof(date),t.day,1);
        append(date,sizeof(date)," ");
        append(date,sizeof(date),months[(t.month+11)%12]);
    }
    window->theme.window=LOGIN_BG2;
    text_center(window,0,l.cw,l.clock_y,clock,LOGIN_TEXT,CLOCK_SIZE);
    text_center(window,0,l.cw,l.clock_y+CLOCK_SIZE+10,date,LOGIN_MUTED,8);
    window->theme.window=LOGIN_CARD;

    pg_window_rect(window,(struct pg_rect){l.dx+4,l.dy+6,l.dw,l.dh},0x07080FU);
    pg_window_rect(window,(struct pg_rect){l.dx,l.dy,l.dw,l.dh},LOGIN_CARD_EDGE);
    pg_window_rect(window,(struct pg_rect){l.dx+1,l.dy+1,l.dw-2,l.dh-2},LOGIN_CARD);
    pg_window_rect(window,(struct pg_rect){l.dx+1,l.dy+1,l.dw-2,3},LOGIN_ACCENT);

    if(s->live){
        pg_window_text_sized(window,l.dx+28,l.dy+28,"Live session",LOGIN_TEXT,16);
        pg_window_text(window,l.dx+28,l.dy+62,"No accounts yet.",LOGIN_MUTED);
        pg_window_text(window,l.dx+28,l.dy+80,"You will sign in as root.",LOGIN_MUTED);
        pg_window_text(window,l.dx+28,l.dy+106,
                       "Run the installer to create accounts.",LOGIN_MUTED);
    }else{
        pg_window_text_sized(window,l.dx+28,l.dy+28,"Sign in",LOGIN_TEXT,16);
        draw_field(window,l.user,"USER NAME",s->user,false,
                   s->focus==FIELD_USER && !s->busy);
        draw_field(window,l.pass,"PASSWORD",s->pass,true,
                   s->focus==FIELD_PASS && !s->busy);
    }
    if(s->message)
        pg_window_text(window,l.dx+28,l.button.y-20,s->message,
                       s->busy ? LOGIN_ACCENT : LOGIN_ERROR);
    bool hot=event && point_inside(event->x,event->y,l.button,window);
    draw_button(window,l.button,s->live ? "Enter" : "Sign in",hot);
    bool clicked=hot && event->type==PG_EVENT_MOUSE_UP && event->button==1;

    window->theme.window=LOGIN_BG;
    text_center(window,0,l.cw,l.ch>28 ? l.ch-28 : 0,
                s->live ? "Enter to continue"
                        : "Tab switches fields    Enter signs in",
                LOGIN_MUTED,8);
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
    pg_window_set_frameless(&window,true);
    wipe(&state,sizeof(state));
    state.live=live_session();
    state.focus=FIELD_USER;
    (void)draw_login(&window,&state,&event);
    uint32_t shown_minute=0xFFFFFFFFU;
    while(pg_window_is_open(&window)){
        if(window.frame.x!=0 || window.frame.y!=0)
            (void)pg_window_move(&window,0,0);
        if(!pg_window_poll_event(&window,&event)){
            struct login_time now=local_time();
            uint32_t stamp=now.valid ? now.hour*60+now.minute+1 : 0;
            if(stamp!=shown_minute){
                shown_minute=stamp;
                event.type=PG_EVENT_NONE;
                (void)draw_login(&window,&state,&event);
            }
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
            struct login_layout l=layout_for(&window,state.live);
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
