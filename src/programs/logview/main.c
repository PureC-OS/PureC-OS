#include "../../libgui/include/puregui.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define LOGVIEW_WIDTH 1020
#define LOGVIEW_HEIGHT 640
#define LOGVIEW_MAX_LINES 8192
#define LOGVIEW_MAX_LINE_LEN 256
#define LOGVIEW_VISIBLE_LINES 24
#define LOGVIEW_BUFFER_SIZE (1024*1024)

enum log_level {
    LOG_LVL_INFO=0,
    LOG_LVL_OK=1,
    LOG_LVL_WARN=2,
    LOG_LVL_ERROR=3,
    LOG_LVL_DEBUG=4,
    LOG_LVL_COUNT=5
};

struct log_line {
    enum log_level level;
    char text[LOGVIEW_MAX_LINE_LEN];
    bool has_level;
};

struct logview_state {
    struct pg_window window;
    struct log_line lines[LOGVIEW_MAX_LINES];
    int32_t line_count;
    int32_t filtered_indices[LOGVIEW_MAX_LINES];
    int32_t filtered_count;
    int32_t scroll_offset;
    bool filter_enabled[LOG_LVL_COUNT];
    char search[64];
    bool search_active;
    char status[128];
    uint64_t last_read_ms;
    bool show_save_dialog;
    int32_t save_selected;
    struct storage_device_info save_disks[8];
    int32_t save_disk_count;
    char save_path[64];
};

static const char *level_name(enum log_level l){
    switch(l){
        case LOG_LVL_INFO: return "INFO";
        case LOG_LVL_OK: return "OK";
        case LOG_LVL_WARN: return "WARN";
        case LOG_LVL_ERROR: return "ERROR";
        case LOG_LVL_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}
static uint32_t level_color(struct pg_window *w, enum log_level l){
    switch(l){
        case LOG_LVL_INFO: return 0x89B4FA;
        case LOG_LVL_OK: return 0xA6E3A1;
        case LOG_LVL_WARN: return 0xF9E2AF;
        case LOG_LVL_ERROR: return 0xF38BA8;
        case LOG_LVL_DEBUG: return 0x6C7086;
        default: return w->theme.text;
    }
}
static enum log_level parse_level(const char *line){
    if(pc_strlen(line)<15) return LOG_LVL_INFO;
    if(pc_strcmp(line+9, "[ INFO ]")==0 || pc_strcmp(line+11, "[ INFO ]")==0) {
    }
    const char *p=line;
    while(*p){
        if(p[0]=='[' && p[1]==' '){
            if(p[2]=='I' && p[3]=='N' && p[4]=='F' && p[5]=='O') return LOG_LVL_INFO;
            if(p[2]==' ' && p[3]=='O' && p[4]=='K') return LOG_LVL_OK;
            if(p[2]==' ' && p[3]=='W' && p[4]=='A' && p[5]=='R' && p[6]=='N') return LOG_LVL_WARN;
            if(p[2]==' ' && p[3]=='E' && p[4]=='R' && p[5]=='R') return LOG_LVL_ERROR;
            if(p[2]=='D' && p[3]=='E' && p[4]=='B' && p[5]=='U' && p[6]=='G') return LOG_LVL_DEBUG;
            if(p[2]=='O' && p[3]=='K') return LOG_LVL_OK;
            if(p[2]=='I' && p[3]=='N' && p[4]=='F' && p[5]=='O') return LOG_LVL_INFO;
            if(p[2]=='W' && p[3]=='A' && p[4]=='R' && p[5]=='N') return LOG_LVL_WARN;
            if(p[2]=='F' && p[3]=='A' && p[4]=='I' && p[5]=='L') return LOG_LVL_ERROR;
            if(p[2]=='D' && p[3]=='E' && p[4]=='B' && p[5]=='U' && p[6]=='G') return LOG_LVL_DEBUG;
        }
        if(p[0]=='[' && p[5]==']'){
            // crude
        }
        p++;
    }
    if(pc_strcmp(line, "INFO")>=0){ /* dummy */}
    const char *s=line;
    while(*s){
        if(s[0]=='I' && s[1]=='N' && s[2]=='F' && s[3]=='O') return LOG_LVL_INFO;
        if(s[0]=='W' && s[1]=='A' && s[2]=='R' && s[3]=='N') return LOG_LVL_WARN;
        if(s[0]=='E' && s[1]=='R' && s[2]=='R' && s[3]=='O' && s[4]=='R') return LOG_LVL_ERROR;
        if(s[0]=='D' && s[1]=='E' && s[2]=='B' && s[3]=='U' && s[4]=='G') return LOG_LVL_DEBUG;
        if(s[0]=='O' && s[1]=='K' && s[2]==' ') {
            return LOG_LVL_OK;
        }
        s++;
    }
    return LOG_LVL_INFO;
}
static bool contains_substr(const char *hay, const char *needle){
    if(!needle || !needle[0]) return true;
    uint32_t hlen=pc_strlen(hay);
    uint32_t nlen=pc_strlen(needle);
    if(nlen>hlen) return false;
    for(uint32_t i=0;i+ nlen <= hlen;i++){
        bool match=true;
        for(uint32_t j=0;j<nlen;j++){
            char a=hay[i+j];
            char b=needle[j];
            // case-insensitive
            if(a>='A' && a<='Z') a = a - 'A' + 'a';
            if(b>='A' && b<='Z') b = b - 'A' + 'a';
            if(a!=b){ match=false; break; }
        }
        if(match) return true;
    }
    return false;
}
static void rebuild_filter(struct logview_state *st){
    st->filtered_count=0;
    for(int32_t i=0;i<st->line_count;i++){
        enum log_level lvl=st->lines[i].level;
        if(!st->filter_enabled[lvl]) continue;
        if(!contains_substr(st->lines[i].text, st->search)) continue;
        st->filtered_indices[st->filtered_count++]=i;
    }
    if(st->scroll_offset > st->filtered_count - LOGVIEW_VISIBLE_LINES)
        st->scroll_offset = st->filtered_count - LOGVIEW_VISIBLE_LINES;
    if(st->scroll_offset<0) st->scroll_offset=0;
}

static void load_logs(struct logview_state *st){
    st->line_count=0;
    st->filtered_count=0;
    st->scroll_offset=0;
    const char *paths[]={"/kernel.log","/dmesg.txt","/kernel/dmesg.txt",0};
    int32_t fd=-1;
    for(int i=0;paths[i];i++){
        fd=pf_open(paths[i]);
        if(fd>=0) break;
    }
    if(fd<0){
        pc_copy(st->status,"No log file (/kernel.log)", sizeof(st->status));
        return;
    }
    static char buffer[LOGVIEW_BUFFER_SIZE];
    int32_t total=0;
    while(total < (int32_t)sizeof(buffer)-1){
        int32_t r=pf_read(fd, buffer+total, sizeof(buffer)-1 - total);
        if(r<=0) break;
        total+=r;
        if(total >= (int32_t)sizeof(buffer)-1) break;
    }
    pf_close(fd);
    buffer[total]='\0';
    char *p=buffer;
    int32_t line_idx=0;
    while(*p && line_idx < LOGVIEW_MAX_LINES){
        char *line_start=p;
        while(*p && *p!='\n' && *p!='\r') p++;
        char saved=*p;
        *p='\0';
        uint32_t len=pc_strlen(line_start);
        if(len>0){
            if(len>=LOGVIEW_MAX_LINE_LEN) len=LOGVIEW_MAX_LINE_LEN-1;
            for(uint32_t k=0;k<len;k++) st->lines[line_idx].text[k]=line_start[k];
            st->lines[line_idx].text[len]='\0';
            enum log_level lvl=LOG_LVL_INFO;
            if(contains_substr(line_start,"ERROR") || contains_substr(line_start,"FAIL") || contains_substr(line_start,"FATAL")) lvl=LOG_LVL_ERROR;
            else if(contains_substr(line_start,"WARN")) lvl=LOG_LVL_WARN;
            else if(contains_substr(line_start,"DEBUG")) lvl=LOG_LVL_DEBUG;
            else if(contains_substr(line_start,"[  OK  ]") || contains_substr(line_start,"[ OK ]") || contains_substr(line_start," OK ")) lvl=LOG_LVL_OK;
            else if(contains_substr(line_start,"INFO")) lvl=LOG_LVL_INFO;
            else lvl=LOG_LVL_INFO;
            st->lines[line_idx].level=lvl;
            st->lines[line_idx].has_level=true;
            line_idx++;
        }
        if(saved=='\0') break;
        if(saved=='\r' && *(p+1)=='\n') p+=2;
        else p++;
    }
    st->line_count=line_idx;
    rebuild_filter(st);
    char tmp[64];
    st->status[0]='\0';
    (void)tmp;
}

static void draw_ui(struct logview_state *st){
    struct pg_window *w=&st->window;
    pg_window_begin(w);
    pg_window_clear(w, w->theme.window);

    char header[128];
    header[0]='\0';
    pg_window_text(w, 12, 10, "Filters: ", w->theme.muted_text);
    const char *names[LOG_LVL_COUNT]={"INFO","OK","WARN","ERROR","DEBUG"};
    uint32_t bx=80;
    for(int i=0;i<LOG_LVL_COUNT;i++){
        uint32_t col = st->filter_enabled[i] ? level_color(w, i) : w->theme.border;
        uint32_t bg = st->filter_enabled[i] ? 0x313244 : w->theme.window;
        struct pg_rect r={bx, 6, 58, 18};
        pg_window_rect(w, r, bg);
        pg_window_rect(w, (struct pg_rect){r.x, r.y, r.width, 1}, col);
        pg_window_rect(w, (struct pg_rect){r.x, r.y+r.height-1, r.width, 1}, col);
        pg_window_rect(w, (struct pg_rect){r.x, r.y, 1, r.height}, col);
        pg_window_rect(w, (struct pg_rect){r.x+r.width-1, r.y, 1, r.height}, col);
        pg_window_text(w, bx+8, 9, names[i], st->filter_enabled[i] ? col : w->theme.muted_text);
        bx+=66;
    }
    struct pg_rect save_btn={w->client.width-80, 6, 68, 18};
    pg_window_rect(w, save_btn, 0x45475A);
    pg_window_rect(w, (struct pg_rect){save_btn.x, save_btn.y, save_btn.width, 1}, 0x89B4FA);
    pg_window_rect(w, (struct pg_rect){save_btn.x, save_btn.y+save_btn.height-1, save_btn.width, 1}, 0x89B4FA);
    pg_window_text(w, save_btn.x+18, 9, "Save", w->theme.text);
    char search_line[96];
    search_line[0]='\0';
    pc_copy(search_line,"Search: ", sizeof(search_line));
    uint32_t slen=pc_strlen(search_line);
    for(uint32_t i=0;i<pc_strlen(st->search) && slen+1 < sizeof(search_line);i++) search_line[slen++]=st->search[i];
    search_line[slen]='\0';
    if(st->search_active){
        if(slen+1 < sizeof(search_line)){ search_line[slen++]='_'; search_line[slen]='\0'; }
        pg_window_text(w, 12, 30, search_line, w->theme.accent);
    } else {
        pg_window_text(w, 12, 30, search_line, w->theme.text);
        if(st->search[0]=='\0') pg_window_text(w, 12+70, 30, "(type / to search, r=refresh, q=close)", w->theme.muted_text);
    }
    pg_window_text(w, w->client.width-260, 30, "[a]=ax201 [e]=EC [w]=wifi [c]=clear", w->theme.muted_text);

    pg_window_rect(w, (struct pg_rect){12, 48, w->client.width-24, 1}, w->theme.border);

    int32_t start = st->scroll_offset;
    int32_t end = start + LOGVIEW_VISIBLE_LINES;
    if(end > st->filtered_count) end = st->filtered_count;
    for(int32_t i=start;i<end;i++){
        int32_t idx = st->filtered_indices[i];
        struct log_line *line=&st->lines[idx];
        uint32_t y = 56 + (i - start)*22;
        uint32_t col=level_color(w, line->level);
        pg_window_rect(w, (struct pg_rect){14, y+6, 6, 6}, col);
        char disp[128];
        uint32_t max_chars = (w->client.width - 32) / 7;
        if(max_chars>120) max_chars=120;
        if(max_chars>=sizeof(disp)) max_chars=sizeof(disp)-1;
        uint32_t tlen=pc_strlen(line->text);
        if(tlen>max_chars){
            for(uint32_t k=0;k<max_chars-3;k++) disp[k]=line->text[k];
            disp[max_chars-3]='.'; disp[max_chars-2]='.'; disp[max_chars-1]='.'; disp[max_chars]='\0';
        } else {
            pc_copy(disp, line->text, sizeof(disp));
        }
        pg_window_text(w, 24, y, disp, w->theme.text);
    }
    if(st->filtered_count==0){
        pg_window_text(w, 24, 60, "(no lines match filter)", w->theme.muted_text);
    }
    if(st->filtered_count > LOGVIEW_VISIBLE_LINES){
        int32_t total=st->filtered_count;
        int32_t visible=LOGVIEW_VISIBLE_LINES;
        int32_t bar_h = (visible * (w->client.height-70)) / total;
        if(bar_h<20) bar_h=20;
        int32_t bar_y = 56 + (st->scroll_offset * (w->client.height-70 - bar_h)) / (total - visible);
        pg_window_rect(w, (struct pg_rect){w->client.width-10, 56, 4, w->client.height-70}, w->theme.border);
        pg_window_rect(w, (struct pg_rect){w->client.width-10, bar_y, 4, bar_h}, w->theme.accent);
    }

    char footer[160];
    footer[0]='\0';
    char *p=footer;
    const char *pre="Lines: ";
    while(*pre) *p++=*pre++;
    char num[32];
    int32_t fc=st->filtered_count, tc=st->line_count;
    char rev[12]; int rlen=0;
    if(fc==0) rev[rlen++]='0'; else { char r2[12]; int l2=0; int v=fc; while(v>0){ r2[l2++]='0'+v%10; v/=10; } while(l2) rev[rlen++]=r2[--l2]; }
    for(int i=0;i<rlen;i++) *p++=rev[i];
    *p++='/';
    rlen=0;
    if(tc==0) rev[rlen++]='0'; else { char r2[12]; int l2=0; int v=tc; while(v>0){ r2[l2++]='0'+v%10; v/=10; } while(l2) rev[rlen++]=r2[--l2]; }
    for(int i=0;i<rlen;i++) *p++=rev[i];
    const char *mid="  Scroll: ";
    while(*mid) *p++=*mid++;
    char rev2[12]; rlen=0;
    if(st->scroll_offset==0) rev2[rlen++]='0'; else { char r2[12]; int l2=0; int v=st->scroll_offset; while(v>0){ r2[l2++]='0'+v%10; v/=10; } while(l2) rev2[rlen++]=r2[--l2]; }
    for(int i=0;i<rlen;i++) *p++=rev2[i];
    *p++='/';
    int32_t max_scroll = st->filtered_count - LOGVIEW_VISIBLE_LINES;
    if(max_scroll<0) max_scroll=0;
    rlen=0;
    if(max_scroll==0) rev2[rlen++]='0'; else { char r2[12]; int l2=0; int v=max_scroll; while(v>0){ r2[l2++]='0'+v%10; v/=10; } while(l2) rev2[rlen++]=r2[--l2]; }
    for(int i=0;i<rlen;i++) *p++=rev2[i];
    const char *help="  [/]=search [1-5]=levels [c]=clear [r]=refresh [q]=quit [up/down]=scroll";
    while(*help && p - footer < 150) *p++=*help++;
    *p='\0';
    pg_window_text(w, 12, w->client.height-18, footer, w->theme.muted_text);
    if(st->show_save_dialog){
        pg_window_rect(w, (struct pg_rect){0,0,w->client.width,w->client.height}, 0x11111B);
        struct pg_rect dlg={w->client.width/2 - 180, 70, 360, 300};
        pg_window_rect(w, dlg, 0x1E1E2E);
        pg_window_rect(w, (struct pg_rect){dlg.x, dlg.y, dlg.width, 26}, 0x313244);
        pg_window_text(w, dlg.x+12, dlg.y+7, "Save logs to FAT32 device", w->theme.text);
        pg_window_text(w, dlg.x+12, dlg.y+36, "Select device:", w->theme.muted_text);
        for(int i=0;i<st->save_disk_count && i<6;i++){
            struct pg_rect row={dlg.x+12, (uint32_t)(dlg.y+56+i*28), dlg.width-24, 24};
            uint32_t bg = (i==st->save_selected) ? 0x45475A : 0x313244;
            pg_window_rect(w, row, bg);
            char disp[80];
            pc_copy(disp, st->save_disks[i].name, sizeof(disp));
            uint32_t l=pc_strlen(disp);
            disp[l++]=' '; disp[l++]='(';
            uint64_t bytes=st->save_disks[i].sector_count*st->save_disks[i].sector_size;
            uint64_t mb=bytes/(1024*1024);
            if(mb>=1024){
                uint64_t gb=mb/1024;
                char r[12]; int rl=0;
                if(gb==0) r[rl++]='0'; else { uint64_t v=gb; while(v){ r[rl++]='0'+v%10; v/=10; } }
                for(int k=rl-1;k>=0;k--) disp[l++]=r[k];
                disp[l++]='G'; disp[l++]='B';
            } else {
                char r[12]; int rl=0;
                uint64_t v=mb; if(v==0) r[rl++]='0'; else while(v){ r[rl++]='0'+v%10; v/=10; }
                for(int k=rl-1;k>=0;k--) disp[l++]=r[k];
                disp[l++]='M'; disp[l++]='B';
            }
            disp[l++]=')';
            if(!st->save_disks[i].writable){
                const char *ro=" RO";
                for(int k=0;ro[k] && l+1<80;k++) disp[l++]=ro[k];
            }
            disp[l]='\0';
            pg_window_text(w, row.x+8, row.y+6, disp, st->save_disks[i].writable ? w->theme.text : w->theme.muted_text);
        }
        if(st->save_disk_count==0){
            pg_window_text(w, dlg.x+12, dlg.y+70, "No writable FAT32 disks found", w->theme.danger);
        }
        char path_line[96];
        pc_copy(path_line, "Path: ", sizeof(path_line));
        uint32_t pl=pc_strlen(path_line);
        for(uint32_t k=0;k<pc_strlen(st->save_path) && pl+1 < sizeof(path_line);k++) path_line[pl++]=st->save_path[k];
        path_line[pl]='\0';
        pg_window_text(w, dlg.x+12, dlg.y+230, path_line, w->theme.text);
        pg_window_text(w, dlg.x+12, dlg.y+250, "Enter to edit, S to save", w->theme.muted_text);
        struct pg_rect save_b={dlg.x+40, dlg.y+270, 120, 28};
        struct pg_rect cancel_b={dlg.x+200, dlg.y+270, 120, 28};
        pg_window_rect(w, save_b, 0xA6E3A1);
        pg_window_text(w, save_b.x+38, save_b.y+8, "Save", 0x1E1E2E);
        pg_window_rect(w, cancel_b, 0x45475A);
        pg_window_text(w, cancel_b.x+32, cancel_b.y+8, "Cancel", w->theme.text);
    }

    pg_window_end(w);
}

static void handle_key(struct logview_state *st, int32_t key){
    if(st->search_active){
        if(key==27){
            st->search_active=false;
            return;
        }
        if(key=='\r' || key=='\n'){
            st->search_active=false;
            rebuild_filter(st);
            return;
        }
        if(key=='\b' || key==127){
            uint32_t len=pc_strlen(st->search);
            if(len) st->search[len-1]='\0';
            rebuild_filter(st);
            return;
        }
        if(key>=' ' && key<='~' && pc_strlen(st->search)+1 < sizeof(st->search)){
            uint32_t len=pc_strlen(st->search);
            st->search[len]=(char)key;
            st->search[len+1]='\0';
            rebuild_filter(st);
            return;
        }
        return;
    }
    if(key=='/' ){
        st->search_active=true;
        return;
    }
    if(key=='c' || key=='C'){
        st->search[0]='\0';
        for(int i=0;i<LOG_LVL_COUNT;i++) st->filter_enabled[i]=true;
        rebuild_filter(st);
        return;
    }
    if(key>='1' && key<='5'){
        int idx=key-'1';
        st->filter_enabled[idx]=!st->filter_enabled[idx];
        rebuild_filter(st);
        return;
    }
    if(key=='a' || key=='A'){
        pc_copy(st->search,"ax201", sizeof(st->search));
        rebuild_filter(st);
        return;
    }
    if(key=='e' || key=='E'){
        pc_copy(st->search,"EC", sizeof(st->search));
        rebuild_filter(st);
        return;
    }
    if(key=='w' || key=='W'){
        pc_copy(st->search,"wifi", sizeof(st->search));
        rebuild_filter(st);
        return;
    }
    if(key=='r' || key=='R'){
        load_logs(st);
        return;
    }
    if(key=='q' || key=='Q'){
        return;
    }
    if(key=='s' || key=='S'){
        st->show_save_dialog = true;
        st->save_disk_count = pc_list_disks(st->save_disks, 8);
        if(st->save_disk_count < 0) st->save_disk_count = 0;
        if(st->save_disk_count > 8) st->save_disk_count = 8;
        st->save_selected = 0;
        pc_copy(st->save_path, "/kernel.log", sizeof(st->save_path));
        draw_ui(st);
        return;
    }
}

int main(void){
    static struct logview_state st;
    for(int i=0;i<LOGVIEW_MAX_LINES;i++) st.lines[i].text[0]='\0';
    st.line_count=0;
    st.filtered_count=0;
    st.scroll_offset=0;
    for(int i=0;i<LOG_LVL_COUNT;i++) st.filter_enabled[i]=true;
    st.search[0]='\0';
    st.search_active=false;
    st.status[0]='\0';

    struct pc_display_info disp;
    if(!pc_display_get_info(&disp) || !disp.available) return 1;
    uint32_t w = disp.width > LOGVIEW_WIDTH+20 ? LOGVIEW_WIDTH : disp.width-20;
    uint32_t h = disp.height > LOGVIEW_HEIGHT+40 ? LOGVIEW_HEIGHT : disp.height-40;
    if(!pg_window_center(&st.window, "Kernel Log Viewer - PureC Debug", w, h)) return 1;

    load_logs(&st);
    draw_ui(&st);

    while(pg_window_is_open(&st.window)){
        struct pg_event ev;
        if(pg_window_poll_event(&st.window, &ev)){
            if(ev.type==PG_EVENT_CLOSE) break;
            if(ev.type==PG_EVENT_KEY){
                if(ev.key=='q' || ev.key=='Q'){
                    break;
                }
                handle_key(&st, ev.key);
                draw_ui(&st);
            } else if(ev.type==PG_EVENT_SPECIAL_KEY){
                if(ev.key==10){
                    if(st.scroll_offset>0) st.scroll_offset--;
                    draw_ui(&st);
                } else if(ev.key==11){
                    if(st.scroll_offset + LOGVIEW_VISIBLE_LINES < st.filtered_count) st.scroll_offset++;
                    draw_ui(&st);
                } else if(ev.key==6){
                    st.scroll_offset -= LOGVIEW_VISIBLE_LINES;
                    if(st.scroll_offset<0) st.scroll_offset=0;
                    draw_ui(&st);
                } else if(ev.key==7){
                    st.scroll_offset += LOGVIEW_VISIBLE_LINES;
                    if(st.scroll_offset > st.filtered_count - LOGVIEW_VISIBLE_LINES) st.scroll_offset = st.filtered_count - LOGVIEW_VISIBLE_LINES;
                    if(st.scroll_offset<0) st.scroll_offset=0;
                    draw_ui(&st);
                } else if(ev.key==4){
                    st.scroll_offset=0;
                    draw_ui(&st);
                } else if(ev.key==5){
                    st.scroll_offset = st.filtered_count - LOGVIEW_VISIBLE_LINES;
                    if(st.scroll_offset<0) st.scroll_offset=0;
                    draw_ui(&st);
                }
            } else if(ev.type==PG_EVENT_MOUSE_DOWN){
                uint32_t bx=80;
                for(int i=0;i<LOG_LVL_COUNT;i++){
                    struct pg_rect r={bx, 6, 58, 18};
                    if(ev.x >= (int32_t)r.x && ev.x < (int32_t)(r.x+r.width) && ev.y >= (int32_t)r.y && ev.y < (int32_t)(r.y+r.height)){
                        st.filter_enabled[i]=!st.filter_enabled[i];
                        rebuild_filter(&st);
                        draw_ui(&st);
                        break;
                    }
                    bx+=66;
                }
                if(ev.y>=28 && ev.y<44 && ev.x>=12 && ev.x<400){
                    st.search_active=true;
                    draw_ui(&st);
                }
            } else if(ev.type==PG_EVENT_REPAINT || ev.type==PG_EVENT_MOVE || ev.type==PG_EVENT_FOCUS){
                draw_ui(&st);
            }
        }
        if(st.show_save_dialog && ev.type==PG_EVENT_MOUSE_DOWN){
            struct pg_rect dlg={st.window.client.width/2 - 180, 70, 360, 300};
            struct pg_rect save_b={dlg.x+40, dlg.y+270, 120, 28};
            struct pg_rect cancel_b={dlg.x+200, dlg.y+270, 120, 28};
            if(ev.x >= (int32_t)save_b.x && ev.x < (int32_t)(save_b.x+save_b.width) &&
               ev.y >= (int32_t)save_b.y && ev.y < (int32_t)(save_b.y+save_b.height)){
                int32_t rc=-1;
                if(st.save_selected>=0 && st.save_selected<st.save_disk_count){
                    rc=pc_save_klog(st.save_disks[st.save_selected].name, st.save_path);
                }
                if(rc >= 0){
                    pc_copy(st.status, "Log saved successfully", sizeof(st.status));
                } else {
                    pc_copy(st.status, "Save failed", sizeof(st.status));
                }
                st.show_save_dialog = false;
                st.save_selected = 0;
                st.save_path[0] = '\0';
                draw_ui(&st);
                break;
            }
            if(ev.x >= (int32_t)cancel_b.x && ev.x < (int32_t)(cancel_b.x+cancel_b.width) &&
               ev.y >= (int32_t)cancel_b.y && ev.y < (int32_t)(cancel_b.y+cancel_b.height)){
                st.show_save_dialog = false;
                st.save_selected = 0;
                st.save_path[0] = '\0';
                draw_ui(&st);
                break;
            }
        }
                pc_sleep(16);
    }
    pg_window_close(&st.window);
    return 0;
}

void _start(void){ pc_exit(main()); }
