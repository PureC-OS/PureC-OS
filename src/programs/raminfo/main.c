#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"

#define RAMVIEW_WIDTH 860
#define RAMVIEW_HEIGHT 600
#define RAMVIEW_MAX_PROCS 64
#define PAGE_SIZE 4096ULL
#define MIB (1024ULL*1024ULL)
#define KIB 1024ULL

struct ram_state {
    struct pg_window window;
    struct memory_monitor_info mem;
    struct process_monitor_info procs[RAMVIEW_MAX_PROCS];
    int32_t count;
    int32_t scroll;
    bool has_data;
};

static char *append_text(char *out, const char *text){
    while(*text) *out++=*text++;
    *out='\0';
    return out;
}

static char *append_u64(char *out, uint64_t v){
    char rev[24];
    uint32_t c=0;
    do { rev[c++]=(char)('0'+v%10U); v/=10U; } while(v && c<sizeof(rev));
    while(c) *out++=rev[--c];
    *out='\0';
    return out;
}

static void sort_by_ram(struct process_monitor_info *p, int32_t n){
    for(int32_t i=0;i<n;i++)
        for(int32_t j=i+1;j<n;j++)
            if(p[j].resident_bytes>p[i].resident_bytes){
                struct process_monitor_info t=p[i];
                p[i]=p[j];
                p[j]=t;
            }
}

static bool refresh(struct ram_state *st){
    struct memory_monitor_info mem={0};
    struct process_monitor_info procs[RAMVIEW_MAX_PROCS];
    int32_t n=pc_process_list(procs,RAMVIEW_MAX_PROCS);
    if(!pc_memory_info(&mem) || n<0){
        st->has_data=false;
        return false;
    }
    st->mem=mem;
    st->count=n>RAMVIEW_MAX_PROCS ? RAMVIEW_MAX_PROCS : n;
    for(int32_t i=0;i<st->count;i++) st->procs[i]=procs[i];
    sort_by_ram(st->procs,st->count);
    if(st->scroll<0) st->scroll=0;
    st->has_data=true;
    return true;
}

static void draw_ui(struct ram_state *st){
    struct pg_window *w=&st->window;
    pg_window_begin(w);
    if(!st->has_data){
        pg_window_text(w,18,18,"Memory data unavailable",w->theme.danger);
        pg_window_end(w);
        return;
    }
    uint64_t total=st->mem.total_bytes;
    uint64_t used=st->mem.used_bytes;
    uint64_t avail=st->mem.available_bytes;
    uint64_t fb=st->mem.framebuffer_bytes;
    uint32_t pct=total ? (uint32_t)((used*100)/total) : 0;

    char line[160];
    char *p=append_text(line,"Total: ");
    p=append_u64(p,total/MIB);
    p=append_text(p," MiB (");
    p=append_u64(p,total);
    p=append_text(p," B)  pages: ");
    p=append_u64(p,total/PAGE_SIZE);
    (void)append_text(p," x 4096");
    pg_window_text(w,18,14,line,w->theme.text);

    p=append_text(line,"Used: ");
    p=append_u64(p,used/MIB);
    p=append_text(p," MiB (");
    p=append_u64(p,used);
    p=append_text(p," B)  ");
    p=append_u64(p,pct);
    (void)append_text(p,"%");
    pg_window_text(w,18,36,line,w->theme.text);

    {
        uint32_t bar_x=560;
        uint32_t bar_w=w->client.width>bar_x+20 ? w->client.width-bar_x-18 : 60;
        pg_window_rect(w,(struct pg_rect){bar_x,36,bar_w,12},w->theme.border);
        uint32_t fill=(uint32_t)((bar_w*(uint64_t)pct)/100);
        if(fill) pg_window_rect(w,(struct pg_rect){bar_x,36,fill,12},w->theme.accent);
    }

    p=append_text(line,"Avail: ");
    p=append_u64(p,avail/MIB);
    p=append_text(p," MiB (");
    p=append_u64(p,avail);
    p=append_text(p," B)  FB: ");
    p=append_u64(p,fb/MIB);
    (void)append_text(p," MiB");
    pg_window_text(w,18,58,line,w->theme.muted_text);

    pg_window_text(w,18,82,
        "PID    PROCESS              BYTES        KiB      MiB    PAGES   %",
        w->theme.muted_text);
    pg_window_rect(w,(struct pg_rect){18,100,w->client.width-36,1},w->theme.border);

    uint32_t row_y=110;
    uint32_t row_h=22;
    int32_t visible_rows=0;
    if(w->client.height>row_y+30)
        visible_rows=(int32_t)((w->client.height-row_y-30)/row_h);
    if(st->scroll>st->count) st->scroll=st->count;
    if(st->scroll<0) st->scroll=0;
    if(st->scroll+visible_rows>st->count && st->count>visible_rows)
        st->scroll=st->count-visible_rows;
    if(st->scroll<0) st->scroll=0;

    for(int32_t r=0;r<visible_rows;r++){
        int32_t idx=st->scroll+r;
        if(idx>=st->count) break;
        const struct process_monitor_info *pr=&st->procs[idx];
        uint64_t b=pr->resident_bytes;
        uint32_t ppct=total ? (uint32_t)((b*100)/total) : 0;
        char row[160];
        char *o=row;
        o=append_u64(o,pr->pid);
        {
            uint64_t tmp=pr->pid;
            int digits=1;
            while(tmp>=10){ digits++; tmp/=10; }
            for(int k=digits;k<7;k++) *o++=' ';
        }
        {
            const char *nm=pr->name[0] ? pr->name : "?";
            uint32_t i=0;
            while(nm[i] && i<20){ *o++=nm[i++]; }
            while(i++<21) *o++=' ';
        }
        o=append_u64(o,b);
        *o='\0';
        pg_window_text(w,18,row_y+(uint32_t)r*row_h,row,w->theme.text);
        char col2[96];
        char *c=append_u64(col2,b/KIB);
        c=append_text(c,"  ");
        c=append_u64(c,b/MIB);
        c=append_text(c,"  ");
        c=append_u64(c,b/PAGE_SIZE);
        c=append_text(c,"  ");
        c=append_u64(c,ppct);
        (void)append_text(c,"%");
        pg_window_text(w,460,row_y+(uint32_t)r*row_h,col2,w->theme.muted_text);
    }

    if(st->count>visible_rows && visible_rows>0){
        int32_t bar_h=(int32_t)((visible_rows*(w->client.height-120))/st->count);
        if(bar_h<20) bar_h=20;
        int32_t max_sc=st->count-visible_rows;
        int32_t bar_y=110;
        if(max_sc>0)
            bar_y=110+(int32_t)((st->scroll*(int32_t)(w->client.height-120-bar_h))/max_sc);
        pg_window_rect(w,(struct pg_rect){w->client.width-10,110,4,w->client.height-140},w->theme.border);
        pg_window_rect(w,(struct pg_rect){(uint32_t)(w->client.width-10),(uint32_t)bar_y,4,(uint32_t)bar_h},w->theme.accent);
    }

    char footer[160];
    char *f=append_text(footer,"Procs: ");
    f=append_u64(f,(uint64_t)st->count);
    f=append_text(f,"  scroll ");
    f=append_u64(f,(uint64_t)st->scroll);
    (void)append_text(f,"  [up/down/pgup/pgdn/home/end] scroll  [r] refresh  [q] quit");
    pg_window_text(w,18,w->client.height-18,footer,w->theme.muted_text);
    pg_window_end(w);
}

static int ramview_main(void){
    struct pc_display_info di;
    if(!pc_display_get_info(&di) || !di.available){
        struct memory_monitor_info m={0};
        if(!pc_memory_info(&m)) return 1;
        pc_write("RAM total: ");
        pc_write_u64(m.total_bytes);
        pc_write(" B\nRAM used: ");
        pc_write_u64(m.used_bytes);
        pc_write(" B\n");
        return 0;
    }
    static struct ram_state st;
    st.scroll=0;
    uint32_t w=di.width>RAMVIEW_WIDTH+20 ? RAMVIEW_WIDTH : di.width-20;
    uint32_t h=di.height>RAMVIEW_HEIGHT+40 ? RAMVIEW_HEIGHT : di.height-40;
    if(!pg_window_center(&st.window,"RAM Inspector",w,h)) return 1;
    refresh(&st);
    draw_ui(&st);
    uint32_t elapsed=0;
    while(pg_window_is_open(&st.window)){
        struct pg_event ev;
        if(pg_window_poll_event(&st.window,&ev)){
            if(ev.type==PG_EVENT_CLOSE) break;
            if(ev.type==PG_EVENT_KEY){
                if(ev.key=='q' || ev.key=='Q') break;
                if(ev.key=='r' || ev.key=='R'){ refresh(&st); draw_ui(&st); }
            } else if(ev.type==PG_EVENT_SPECIAL_KEY){
                if(ev.key==10){ if(st.scroll>0) st.scroll--; draw_ui(&st); }
                else if(ev.key==11){ st.scroll++; draw_ui(&st); }
                else if(ev.key==6){ st.scroll-=10; if(st.scroll<0) st.scroll=0; draw_ui(&st); }
                else if(ev.key==7){ st.scroll+=10; draw_ui(&st); }
                else if(ev.key==4){ st.scroll=0; draw_ui(&st); }
                else if(ev.key==5){ st.scroll=st.count; draw_ui(&st); }
            } else if(ev.type==PG_EVENT_REPAINT || ev.type==PG_EVENT_MOVE
                      || ev.type==PG_EVENT_FOCUS){
                draw_ui(&st);
            }
        }
        pc_sleep(16);
        elapsed+=16;
        if(elapsed>=1000){
            elapsed=0;
            if(!pg_window_is_minimized(&st.window)){ refresh(&st); draw_ui(&st); }
        }
    }
    pg_window_close(&st.window);
    return 0;
}

void _start(void){ pc_exit(ramview_main()); }
