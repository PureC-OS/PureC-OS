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
    char message[64];
    uint64_t message_until;
    uint32_t scroll_offset;
    uint32_t page_scroll;
    uint64_t last_poll_ms;
    struct net_if_info ifs[NET_IF_MAX_COUNT];
    int32_t if_count;
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
    if(r>=0) pc_copy(state.message, "Saved", sizeof(state.message));
    else pc_copy(state.message, "Save failed", sizeof(state.message));
    state.message_until = now_ms() + 2000;
}
static void refresh_status(void){
    if(!pc_wifi_status(&state.status)) state.status.has_device=0;
    state.network_count = pc_wifi_list(state.networks, WIFI_SCAN_MAX);
    if(state.network_count<0) state.network_count=0;
    if(state.selected >= state.network_count) state.selected = state.network_count-1;
    state.if_count = pc_net_if_list(state.ifs, NET_IF_MAX_COUNT);
    if(state.if_count<0) state.if_count=0;
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
static const char* wifi_state_name(uint32_t s){
    switch(s){
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_SCANNING: return "Scanning";
        case WIFI_STATE_CONNECTING: return "Connecting";
        case WIFI_STATE_CONNECTED: return "Connected";
        case WIFI_STATE_FAILED: return "Failed";
        default: return "Unknown";
    }
}
static bool wired_online(void){
    for(int i=0;i<state.if_count;i++)
        if(state.ifs[i].link_up && state.ifs[i].has_ip) return true;
    return false;
}
static bool internet_online(void){
    if(wired_online()) return true;
    if(state.status.has_device && state.status.connected) return true;
    return false;
}
static const char *online_iface_name(void){
    for(int i=0;i<state.if_count;i++)
        if(state.ifs[i].link_up && state.ifs[i].has_ip) return state.ifs[i].name;
    if(state.status.has_device && state.status.connected) return state.status.interface_name;
    return "";
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
        if(state.status.has_device && state.status.scan_count==0) (void)pc_wifi_scan();
    }
    uint64_t now = now_ms();
    if(now - state.last_poll_ms > 200){
        state.last_poll_ms = now;
        refresh_status();
    }
    if(event && state.password_focused && event->type==PG_EVENT_KEY){
        int32_t k = event->key;
        if(k==27) state.password_focused=false;
        else if(k=='\b' || k==127){
            uint32_t len = pc_strlen(state.password);
            if(len) state.password[len-1]='\0';
        } else if(k=='\r' || k=='\n'){
            if(state.selected>=0 && state.selected < state.network_count){
                const char *ssid = state.networks[state.selected].ssid;
                int32_t rc = pc_wifi_connect(ssid, state.password);
                if(rc==0){ save_config(ssid, state.password); pc_copy(state.message, "Connecting...", sizeof(state.message)); }
                else pc_copy(state.message, "Connect failed", sizeof(state.message));
                state.message_until = now + 2000;
            }
        } else if(k>=32 && k<=126){
            uint32_t len = pc_strlen(state.password);
            if(len+1 < sizeof(state.password)){ state.password[len]=(char)k; state.password[len+1]='\0'; }
        }
    }
    if(event && event->type==PG_EVENT_SPECIAL_KEY){
        if(event->key==10 && state.page_scroll>0) state.page_scroll--;
        if(event->key==11) state.page_scroll++;
    }
    uint32_t wired_rows = state.if_count ? (uint32_t)state.if_count : 1;
    uint32_t total_h = 26 + 30 + (34 + wired_rows*52) + 16 + 150;
    uint32_t visible_h = window->client.height - PAGE_TOP - 30;
    if(visible_h < 120) visible_h = 120;
    uint32_t max_scroll = total_h > visible_h ? (total_h - visible_h + 23)/24 : 0;
    if(state.page_scroll > max_scroll) state.page_scroll = max_scroll;
    int32_t off = -(int32_t)(state.page_scroll * 24);
    uint32_t y = (uint32_t)((int32_t)PAGE_TOP + off);
    pg_window_text(window, PAGE_LEFT, y-2, "Network", window->theme.text);
    y += 18;
    // summary: one line, no box plaque abuse — single bar
    {
        struct pg_rect r = {PAGE_LEFT, y, width, 26};
        pg_window_rect(window, r, 0x2B2D40);
        bool online = internet_online();
        char sum[64]="Internet: ";
        if(online){
            append_text(sum+pc_strlen(sum), "Online");
            const char *iface = online_iface_name();
            if(iface[0]){ append_text(sum+pc_strlen(sum), " ("); append_text(sum+pc_strlen(sum), iface); append_text(sum+pc_strlen(sum), ")"); }
        } else append_text(sum+pc_strlen(sum), "Offline");
        pg_window_text(window, PAGE_LEFT+12, y+9, sum, online ? 0xA6E3A1 : window->theme.danger);
        y += 26 + 8;
    }
    // wired: compact, 3 lines per iface max
    {
        uint32_t box_top = y;
        uint32_t box_h = 34 + wired_rows*52;
        pg_window_rect(window, (struct pg_rect){PAGE_LEFT, box_top, width, box_h}, 0x2B2D40);
        pg_window_text(window, PAGE_LEFT+10, box_top+8, "Wired", window->theme.text);
        if(state.if_count==0){
            pg_window_text(window, PAGE_LEFT+12, box_top+30, "No wired adapters", window->theme.muted_text);
        } else {
            for(int i=0;i<state.if_count;i++){
                struct net_if_info *inf = &state.ifs[i];
                uint32_t iy = box_top + 28 + (uint32_t)i*52;
                char line[64];
                pc_copy(line, inf->name, sizeof(line));
                append_text(line+pc_strlen(line), inf->link_up ? "  up" : "  down");
                pg_window_text(window, PAGE_LEFT+12, iy, line, inf->link_up ? 0xA6E3A1 : window->theme.danger);
                if(inf->has_ip){
                    char ipbuf[48]="IP ";
                    append_ip(ipbuf+3, inf->ip_address);
                    pg_window_text(window, PAGE_LEFT+12, iy+15, ipbuf, window->theme.text);
                    char gw[64]="GW ";
                    if(inf->gateway) append_ip(gw+3, inf->gateway);
                    else append_text(gw+3, "-");
                    if(inf->dns_server){ append_text(gw+pc_strlen(gw), "  DNS "); append_ip(gw+pc_strlen(gw), inf->dns_server); }
                    pg_window_text(window, PAGE_LEFT+12, iy+30, gw, window->theme.muted_text);
                } else {
                    pg_window_text(window, PAGE_LEFT+12, iy+15, inf->link_up ? "DHCP..." : "No link", 0xF9E2AF);
                }
            }
        }
        y = box_top + box_h + 10;
    }
    // wifi: minimal
    {
        uint32_t box_top = y;
        uint32_t box_h = 132;
        pg_window_rect(window, (struct pg_rect){PAGE_LEFT, box_top, width, box_h}, 0x2B2D40);
        pg_window_text(window, PAGE_LEFT+10, box_top+8, "Wi-Fi", window->theme.text);
        if(!state.status.has_device){
            pg_window_text(window, PAGE_LEFT+12, box_top+30, "No adapter", window->theme.muted_text);
        } else {
            char st[80];
            pc_copy(st, wifi_state_name(state.status.state), sizeof(st));
            if(state.status.connected){
                append_text(st+pc_strlen(st), "  ");
                append_text(st+pc_strlen(st), state.status.ssid);
            }
            uint32_t col = window->theme.accent;
            if(state.status.state==WIFI_STATE_CONNECTED) col=0xA6E3A1;
            else if(state.status.state==WIFI_STATE_FAILED) col=window->theme.danger;
            pg_window_text(window, PAGE_LEFT+12, box_top+28, st, col);
            struct pg_rect scan_btn = {PAGE_LEFT + width - 90, box_top+24, 78, 22};
            if(pg_button(window, scan_btn, "Scan", event)){
                (void)pc_wifi_scan();
            }
            if(state.network_count==0){
                pg_window_text(window, PAGE_LEFT+12, box_top+48, "No networks", window->theme.muted_text);
            } else {
                uint32_t visible = 2;
                uint32_t row_h = 24;
                for(uint32_t i=0;i<visible;i++){
                    uint32_t idx = state.scroll_offset + i;
                    if(idx >= (uint32_t)state.network_count) break;
                    struct wifi_network_info *net = &state.networks[idx];
                    struct pg_rect row = {PAGE_LEFT+6, box_top+48 + i*row_h, width-12, row_h-2};
                    bool is_sel = (int)idx == state.selected;
                    pg_window_rect(window, row, is_sel ? 0x45475A : 0x313244);
                    pg_window_text(window, row.x+8, row.y+7, net->ssid, window->theme.text);
                    pg_window_text(window, PAGE_LEFT+width-70, row.y+7, security_name(net->security), window->theme.muted_text);
                    if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                        int32_t x = event->x - (int32_t)window->client.x;
                        int32_t yy = event->y - (int32_t)window->client.y;
                        if(x >= (int32_t)row.x && x < (int32_t)(row.x+row.width) && yy >= (int32_t)row.y && yy < (int32_t)(row.y+row.height)){
                            state.selected = (int)idx;
                            state.password_focused = (net->security!=WIFI_SECURITY_OPEN);
                        }
                    }
                }
                if(state.selected>=0 && state.selected < state.network_count){
                    struct wifi_network_info *sel = &state.networks[state.selected];
                    uint32_t conn_y = box_top + 48 + 2*24 + 6;
                    if(sel->security != WIFI_SECURITY_OPEN){
                        struct pg_rect pass_rect = {PAGE_LEFT+10, conn_y, width-110, 22};
                        pg_window_rect(window, pass_rect, 0x181825);
                        pg_window_text(window, pass_rect.x+6, pass_rect.y+7, state.password, window->theme.text);
                        if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                            int32_t x = event->x - (int32_t)window->client.x;
                            int32_t yy = event->y - (int32_t)window->client.y;
                            state.password_focused = (x >= (int32_t)pass_rect.x && x < (int32_t)(pass_rect.x+pass_rect.width) && yy >= (int32_t)pass_rect.y && yy < (int32_t)(pass_rect.y+pass_rect.height));
                        }
                        if(pg_button(window, (struct pg_rect){pass_rect.x+pass_rect.width+6, pass_rect.y, 84, 22}, "Connect", event)){
                            int32_t rc = pc_wifi_connect(sel->ssid, state.password);
                            if(rc==0){ save_config(sel->ssid, state.password); pc_copy(state.message, "Connecting...", sizeof(state.message)); }
                            else pc_copy(state.message, "Failed", sizeof(state.message));
                            state.message_until = now + 2000;
                        }
                    } else {
                        if(pg_button(window, (struct pg_rect){PAGE_LEFT+10, conn_y, 84, 22}, "Connect", event)){
                            (void)pc_wifi_connect(sel->ssid, "");
                        }
                    }
                    if(state.message[0] && now < state.message_until)
                        pg_window_text(window, PAGE_LEFT+100, conn_y+4, state.message, 0xF9E2AF);
                }
            }
        }
    }
}
