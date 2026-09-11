#include "settings/wifi_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libfs/include/purefs.h"
#include "../../../libc/include/purec.h"
#include <stdint.h>
#include <stdbool.h>
#define PAGE_LEFT 198
#define PAGE_TOP 80
#define WIFI_INI_PATH "/config/wifi.ini"
#define WIFI_INI_DIR "/config"
static struct {
    bool initialized;
    struct wifi_status_info status;
    struct wifi_network_info networks[WIFI_SCAN_MAX];
    int32_t network_count;
    int selected;
    char password[WIFI_PASSWORD_CAPACITY];
    bool password_focused;
    char message[96];
    uint64_t message_until;
    uint32_t scroll_offset;
    uint32_t page_scroll;
    uint64_t last_poll_ms;
} state;
static uint64_t now_ms(void){
    static uint64_t fake=0;
    fake+=80;
    return fake;
}
static char *append_text(char *out, const char *text){
    while(*text) *out++ = *text++;
    *out='\0';
    return out;
}
static char *append_u32(char *out, uint32_t v){
    char rev[12]; uint32_t c=0;
    do{ rev[c++] = (char)('0'+ v%10); v/=10; }while(v && c<sizeof(rev));
    while(c) *out++ = rev[--c];
    *out='\0'; return out;
}
static void append_ip(char *out, uint32_t ip){
    char *p=out;
    p=append_u32(p, (ip>>24)&255); *p++='.'; *p='\0';
    p=append_u32(p, (ip>>16)&255); *p++='.'; *p='\0';
    p=append_u32(p, (ip>>8)&255); *p++='.'; *p='\0';
    p=append_u32(p, ip&255);
}
static void load_saved_config(void){
    int32_t fd = pf_open(WIFI_INI_PATH);
    if(fd<0) return;
    char buf[256]={0};
    int32_t n = pf_read(fd, buf, sizeof(buf)-1);
    (void)pf_close(fd);
    if(n<=0) return;
    buf[n]='\0';
    char ssid[WIFI_SSID_CAPACITY]={0};
    char pass[WIFI_PASSWORD_CAPACITY]={0};
    for(char *line=buf; *line; ){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char save=*end; *end='\0';
        if(line[0]=='#' || !line[0]){}
        else if(pc_strlen(line)>5 && line[0]=='s' && line[1]=='s' && line[2]=='i' && line[3]=='d' && line[4]=='='){
            uint32_t i=0;
            const char *src=line+5;
            while(src[i] && i+1<sizeof(ssid)){ ssid[i]=src[i]; i++; }
            ssid[i]='\0';
        } else if(pc_strlen(line)>9 && line[0]=='p' && line[1]=='a'){
            const char *src=line+9;
            uint32_t i=0;
            while(src[i] && i+1<sizeof(pass)){ pass[i]=src[i]; i++; }
            pass[i]='\0';
        }
        if(!save) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(ssid[0]){
        for(int i=0;i<state.network_count;i++){
            if(pc_strcmp(state.networks[i].ssid, ssid)==0){ state.selected=i; break; }
        }
        pc_copy(state.password, pass, sizeof(state.password));
    }
}
static void save_config(const char *ssid, const char *password){
    char buf[256];
    char *p=buf;
    p=append_text(p, "ssid=");
    p=append_text(p, ssid ? ssid : "");
    p=append_text(p, "\npassword=");
    p=append_text(p, password ? password : "");
    p=append_text(p, "\n");
    (void)pf_create_dir(WIFI_INI_DIR);
    int32_t r = pf_write_file(WIFI_INI_PATH, buf, (uint32_t)(p-buf));
    if(r>=0){
        pc_copy(state.message, "Saved to /config/wifi.ini", sizeof(state.message));
    } else {
        pc_copy(state.message, "Save failed", sizeof(state.message));
    }
    state.message_until = now_ms() + 3000;
}
static void refresh_status(void){
    if(!pc_wifi_status(&state.status)){
        state.status.has_device=0;
    }
    state.network_count = pc_wifi_list(state.networks, WIFI_SCAN_MAX);
    if(state.network_count<0) state.network_count=0;
    if(state.selected >= state.network_count) state.selected = state.network_count-1;
}
static const char* security_name(uint8_t s){
    switch(s){
        case WIFI_SECURITY_OPEN: return "Open";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA2: return "WPA2";
        case WIFI_SECURITY_WPA3: return "WPA3";
        case WIFI_SECURITY_WPA2_WPA3: return "WPA2/3";
        default: return "?";
    }
}
static const char* state_name(uint32_t s){
    switch(s){
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_SCANNING: return "Scanning";
        case WIFI_STATE_CONNECTING: return "Connecting";
        case WIFI_STATE_CONNECTED: return "Connected";
        case WIFI_STATE_FAILED: return "Failed";
        default: return "Unknown";
    }
}
static int rssi_bars(int8_t rssi){
    if(rssi >= -50) return 4;
    if(rssi >= -60) return 3;
    if(rssi >= -70) return 2;
    if(rssi >= -80) return 1;
    return 0;
}
void wifi_page_draw(struct pg_window *window,const struct pg_event *event){
    uint32_t width = window->client.width - PAGE_LEFT - 22;
    if(width < 400) width = 400;
    if(!state.initialized){
        state.initialized=true;
        state.selected=-1;
        state.password[0]='\0';
        state.password_focused=false;
        state.scroll_offset=0;
        state.page_scroll=0;
        refresh_status();
        load_saved_config();
        if(state.status.has_device && state.status.scan_count==0){
            (void)pc_wifi_scan();
        }
    }
    uint64_t now = now_ms();
    if(now - state.last_poll_ms > 200){
        state.last_poll_ms = now;
        refresh_status();
    }
    if(event && state.password_focused){
        if(event->type==PG_EVENT_KEY){
            int32_t k = event->key;
            if(k==27){
                state.password_focused=false;
            } else if(k=='\b' || k==127){
                uint32_t len = pc_strlen(state.password);
                if(len){ state.password[len-1]='\0'; }
            } else if(k=='\r' || k=='\n'){
                if(state.selected>=0 && state.selected < state.network_count){
                    const char *ssid = state.networks[state.selected].ssid;
                    int32_t rc = pc_wifi_connect(ssid, state.password);
                    if(rc==0){
                        save_config(ssid, state.password);
                        pc_copy(state.message, "Connecting...", sizeof(state.message));
                    } else {
                        pc_copy(state.message, "Connect failed", sizeof(state.message));
                    }
                    state.message_until = now + 3000;
                }
            } else if(k>=32 && k<=126){
                uint32_t len = pc_strlen(state.password);
                if(len+1 < sizeof(state.password)){
                    state.password[len]=(char)k;
                    state.password[len+1]='\0';
                }
            }
        } else if(event->type==PG_EVENT_SPECIAL_KEY){
            if(event->key==14){
                uint32_t len = pc_strlen(state.password);
                if(len){ state.password[len-1]='\0'; }
            }
            if(event->key==10 && state.scroll_offset>0) state.scroll_offset--;
            if(event->key==11 && state.scroll_offset+4 < (uint32_t)state.network_count) state.scroll_offset++;
        }
    }
    if(event && event->type==PG_EVENT_SPECIAL_KEY){
        if(event->key==10 && state.page_scroll>0) state.page_scroll--;
        if(event->key==11) state.page_scroll++;
        if(event->key==6 && state.page_scroll>2) state.page_scroll-=2;
        if(event->key==7) state.page_scroll+=2;
    }
    uint32_t total_h = 320;
    uint32_t visible_h = window->client.height - PAGE_TOP - 30;
    if(visible_h < 120) visible_h = 120;
    uint32_t max_scroll = total_h > visible_h ? total_h - visible_h : 0;
    if(state.page_scroll > max_scroll) state.page_scroll = max_scroll;
    int32_t off = -(int32_t)(state.page_scroll * 24);
    pg_window_text(window, PAGE_LEFT, PAGE_TOP-2+off, "Network", window->theme.text);
    struct pg_rect status_rect = {PAGE_LEFT, (uint32_t)((int32_t)(PAGE_TOP+16)+off), width, 72};
    pg_window_rect(window, status_rect, 0x2B2D40);
    char line[96];
    if(state.status.has_device){
        char macbuf[32];
        char *p=macbuf;
        for(int i=0;i<6;i++){
            const char *hex="0123456789ABCDEF";
            if(i) *p++=':';
            *p++=hex[(state.status.mac[i]>>4)&0xF];
            *p++=hex[state.status.mac[i]&0xF];
        }
        *p='\0';
        pc_copy(line, state.status.interface_name, sizeof(line));
        append_text(line+pc_strlen(line), "  ");
        append_text(line+pc_strlen(line), macbuf);
        uint32_t state_col = window->theme.accent;
        if(state.status.state==WIFI_STATE_CONNECTED) state_col=0xA6E3A1;
        else if(state.status.state==WIFI_STATE_FAILED) state_col=window->theme.danger;
        else if(state.status.state==WIFI_STATE_SCANNING || state.status.state==WIFI_STATE_CONNECTING) state_col=0xF9E2AF;
        pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+26)+off), line, window->theme.text);
        char state_line[48];
        pc_copy(state_line, state_name(state.status.state), sizeof(state_line));
        if(state.status.connected){
            append_text(state_line+pc_strlen(state_line), "  ");
            append_text(state_line+pc_strlen(state_line), state.status.ssid);
        }
        pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+42)+off), state_line, state_col);
        if(state.status.connected && state.status.ip_address){
            char ipbuf[40]="IP ";
            append_ip(ipbuf+3, state.status.ip_address);
            // mark stub IP
            if(state.status.ip_address == ((192U<<24)|(168U<<16)|(1U<<8)|77U)){
                append_text(ipbuf+pc_strlen(ipbuf), " (stub)");
            }
            pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+58)+off), ipbuf, window->theme.muted_text);
        } else if(state.status.state==WIFI_STATE_SCANNING){
            pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+58)+off), "Scanning... (demo if FW stuck)", 0xF9E2AF);
        } else if(state.status.state==WIFI_STATE_CONNECTING){
            char conn[64]="Connecting to ";
            pc_copy(conn+14, state.status.ssid, sizeof(conn)-14);
            pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+58)+off), conn, 0xF9E2AF);
        } else if(state.status.state==WIFI_STATE_FAILED){
            char fail[64]="Failed (code ";
            char *f = fail + pc_strlen(fail);
            // show last_error if negative? simplified
            f = append_u32(f, (uint32_t)(state.status.last_error <0 ? -state.status.last_error : state.status.last_error));
            append_text(f, ")");
            pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+58)+off), fail, window->theme.danger);
        } else {
            pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+58)+off), "Not connected – select net & Connect", window->theme.muted_text);
        }
        struct pg_rect scan_btn = {PAGE_LEFT + width - 90, (uint32_t)((int32_t)(PAGE_TOP+26)+off), 78, 22};
        bool do_scan = pg_button(window, scan_btn, state.status.state==WIFI_STATE_SCANNING ? "..." : "Scan", event);
        if(do_scan){
            if(pc_wifi_scan()==0){
                pc_copy(state.message, "Scan...", sizeof(state.message));
                state.message_until = now+1500;
            } else {
                pc_copy(state.message, "Busy", sizeof(state.message));
                state.message_until = now+1500;
            }
        }
        if(max_scroll>0){
            struct pg_rect up = {PAGE_LEFT+width-18, (uint32_t)((int32_t)(PAGE_TOP-2)+off), 16, 16};
            struct pg_rect down = {PAGE_LEFT+width-18, (uint32_t)((int32_t)(PAGE_TOP+16)+off+72+8), 16, 16};
            pg_window_text(window, up.x+4, up.y+4, "^", window->theme.muted_text);
            pg_window_text(window, down.x+4, down.y+4, "v", window->theme.muted_text);
            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                if(x >= (int32_t)up.x && x < (int32_t)(up.x+16) && y >= (int32_t)up.y && y < (int32_t)(up.y+16) && state.page_scroll>0) state.page_scroll--;
                if(x >= (int32_t)down.x && x < (int32_t)(down.x+16) && y >= (int32_t)down.y && y < (int32_t)(down.y+16) && state.page_scroll < max_scroll) state.page_scroll++;
            }
        }
    } else {
        pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+36)+off), "No adapter (wlan0 missing)", window->theme.danger);
        pg_window_text(window, PAGE_LEFT+12, (uint32_t)((int32_t)(PAGE_TOP+52)+off), "e1000 for QEMU", window->theme.muted_text);
    }
    uint32_t list_top = (uint32_t)((int32_t)(PAGE_TOP+16+72+8)+off);
    uint32_t list_h = 132;
    struct pg_rect list_rect = {PAGE_LEFT, list_top, width, list_h};
    pg_window_rect(window, list_rect, 0x2B2D40);
    char title[40]="Networks";
    if(state.status.scan_count){
        append_text(title+pc_strlen(title), " (");
        append_u32(title+pc_strlen(title), state.status.scan_count);
        append_text(title+pc_strlen(title), ")");
    }
    pg_window_text(window, PAGE_LEFT+10, list_top+8, title, window->theme.text);
    if(state.network_count==0){
        if(state.status.state==WIFI_STATE_SCANNING){
            pg_window_text(window, PAGE_LEFT+10, list_top+34, "Scanning... (soft-scan fallback if FW not alive)", window->theme.muted_text);
        } else if(!state.status.has_device){
            pg_window_text(window, PAGE_LEFT+10, list_top+34, "No adapter", window->theme.muted_text);
        } else {
            pg_window_text(window, PAGE_LEFT+10, list_top+34, "No networks – check RFKILL/BIOS, see dmesg", window->theme.muted_text);
            pg_window_text(window, PAGE_LEFT+10, list_top+50, "If stub: 3 demo nets should appear after Scan", window->theme.muted_text);
        }
    } else {
        if(state.selected>=0){
            if((uint32_t)state.selected < state.scroll_offset) state.scroll_offset = (uint32_t)state.selected;
            if((uint32_t)state.selected >= state.scroll_offset+4) state.scroll_offset = (uint32_t)state.selected - 3;
        }
        uint32_t visible = 4;
        uint32_t row_h = 26;
        uint32_t start_y = list_top+26;
        for(uint32_t i=0;i<visible;i++){
            uint32_t idx = state.scroll_offset + i;
            if(idx >= (uint32_t)state.network_count) break;
            struct wifi_network_info *net = &state.networks[idx];
            struct pg_rect row = {PAGE_LEFT+6, start_y + i*row_h, width-12, row_h-2};
            bool is_sel = (int)idx == state.selected;
            uint32_t bg = is_sel ? 0x45475A : 0x313244;
            pg_window_rect(window, row, bg);
            pg_window_text(window, row.x+8, row.y+8, net->ssid, is_sel ? window->theme.text : 0xCDD6F4);
            int bars = rssi_bars(net->rssi);
            char barstr[6];
            for(int b=0;b<4;b++) barstr[b] = b < bars ? '|' : '.';
            barstr[4]='\0';
            char sec[16];
            pc_copy(sec, security_name(net->security), sizeof(sec));
            char right[32];
            char *q=right;
            for(int b=0;b<4;b++) *q++=barstr[b];
            *q++=' ';
            q=append_text(q, sec);
            pg_window_text(window, PAGE_LEFT+ width-90, row.y+8, right, window->theme.muted_text);
            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                if(x >= (int32_t)row.x && x < (int32_t)(row.x+row.width) && y >= (int32_t)row.y && y < (int32_t)(row.y+row.height)){
                    state.selected = (int)idx;
                    if(net->security!=WIFI_SECURITY_OPEN) state.password_focused = true;
                    else state.password[0]='\0';
                }
            }
        }
        if(state.network_count>4){
            char scroll_info[16];
            char *q=scroll_info;
            q=append_u32(q, state.scroll_offset+1);
            *q++='/';
            q=append_u32(q, state.network_count);
            *q='\0';
            pg_window_text(window, PAGE_LEFT+width-44, list_top+8, scroll_info, window->theme.muted_text);
            struct pg_rect up = {PAGE_LEFT+width-18, list_top+26, 14, 14};
            struct pg_rect down = {PAGE_LEFT+width-18, list_top+list_h-16, 14, 14};
            bool up_hit = false;
            bool down_hit = false;
            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                if(x >= (int32_t)up.x && x < (int32_t)(up.x+14) && y >= (int32_t)up.y && y < (int32_t)(up.y+14)) up_hit=true;
                if(x >= (int32_t)down.x && x < (int32_t)(down.x+14) && y >= (int32_t)down.y && y < (int32_t)(down.y+14)) down_hit=true;
            }
            if(up_hit && state.scroll_offset>0) state.scroll_offset--;
            if(down_hit && state.scroll_offset+4 < (uint32_t)state.network_count) state.scroll_offset++;
            pg_window_text(window, up.x+3, up.y+3, "^", window->theme.muted_text);
            pg_window_text(window, down.x+3, down.y+3, "v", window->theme.muted_text);
        }
    }
    uint32_t conn_top = list_top + list_h + 8;
    struct pg_rect conn_rect = {PAGE_LEFT, conn_top, width, 72};
    pg_window_rect(window, conn_rect, 0x2B2D40);
    if(state.selected>=0 && state.selected < state.network_count){
        struct wifi_network_info *sel = &state.networks[state.selected];
        char sel_line[48]=">";
        append_text(sel_line+1, sel->ssid);
        pg_window_text(window, PAGE_LEFT+10, conn_top+10, sel_line, window->theme.accent);
        if(sel->security != WIFI_SECURITY_OPEN){
            struct pg_rect pass_rect = {PAGE_LEFT+10, conn_top+28, width-100, 22};
            uint32_t pass_bg = state.password_focused ? 0x45475A : 0x181825;
            pg_window_rect(window, pass_rect, pass_bg);
            char disp[64];
            pc_copy(disp, state.password, sizeof(disp));
            pg_window_text(window, pass_rect.x+6, pass_rect.y+7, disp, window->theme.text);
            if(state.password_focused && (now/400)%2==0){
                uint32_t cursor_x = pass_rect.x+6 + pc_strlen(disp)*8;
                if(cursor_x +2 < pass_rect.x+pass_rect.width-4)
                    pg_window_rect(window, (struct pg_rect){cursor_x, pass_rect.y+4, 2, 14}, window->theme.text);
            }
            if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                int32_t x = event->x - (int32_t)window->client.x;
                int32_t y = event->y - (int32_t)window->client.y;
                bool inside = x >= (int32_t)pass_rect.x && x < (int32_t)(pass_rect.x+pass_rect.width) && y >= (int32_t)pass_rect.y && y < (int32_t)(pass_rect.y+pass_rect.height);
                state.password_focused = inside;
            }
            struct pg_rect conn_btn = {pass_rect.x+pass_rect.width+6, pass_rect.y, 84, 22};
            bool do_connect = pg_button(window, conn_btn, state.status.state==WIFI_STATE_CONNECTING ? "..." : "Connect", event);
            if(do_connect && state.status.state!=WIFI_STATE_CONNECTING){
                int32_t rc = pc_wifi_connect(sel->ssid, state.password);
                if(rc==0){
                    save_config(sel->ssid, state.password);
                    pc_copy(state.message, "Connecting...", sizeof(state.message));
                } else {
                    pc_copy(state.message, "Failed", sizeof(state.message));
                }
                state.message_until = now + 3000;
            }
        } else {
            struct pg_rect conn_btn = {PAGE_LEFT+10, conn_top+28, 84, 22};
            bool do_connect = pg_button(window, conn_btn, state.status.state==WIFI_STATE_CONNECTING ? "..." : "Connect", event);
            if(do_connect && state.status.state!=WIFI_STATE_CONNECTING){
                int32_t rc = pc_wifi_connect(sel->ssid, "");
                if(rc==0){
                    save_config(sel->ssid, "");
                    pc_copy(state.message, "Connecting...", sizeof(state.message));
                } else {
                    pc_copy(state.message, "Failed", sizeof(state.message));
                }
                state.message_until = now+3000;
            }
            pg_window_text(window, PAGE_LEFT+100, conn_top+32, "Open", 0xA6E3A1);
        }
        if(state.status.connected){
            struct pg_rect disc_btn = {PAGE_LEFT+ width-78, conn_top+28, 72, 22};
            bool do_disc = pg_button(window, disc_btn, "Off", event);
            if(do_disc){
                (void)pc_wifi_disconnect();
                pc_copy(state.message, "Off", sizeof(state.message));
                state.message_until = now+2000;
            }
        }
        if(state.message[0] && now < state.message_until){
            pg_window_text(window, PAGE_LEFT+10, conn_top+54, state.message, 0xF9E2AF);
        }
    } else {
        pg_window_text(window, PAGE_LEFT+10, conn_top+16, "Select network", window->theme.muted_text);
        pg_window_text(window, PAGE_LEFT+10, conn_top+34, "Use arrows / click to scroll", window->theme.muted_text);
    }
}
