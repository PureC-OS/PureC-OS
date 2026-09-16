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
    // wired
    struct net_if_info ifs[NET_IF_MAX_COUNT];
    int32_t if_count;
    // ping test
    char ping_msg[96];
    uint64_t ping_until;
    uint16_t ping_seq;
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
static char *append_u64(char *out, uint64_t v){
    char rev[24]; uint32_t c=0;
    do{ rev[c++] = (char)('0'+ (uint32_t)(v%10)); v/=10; }while(v && c<sizeof(rev));
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
static void ip_to_str(uint32_t ip, char *out, uint32_t cap){
    (void)cap;
    append_ip(out, ip);
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
    // wired: truth from kernel, not from wifi stub
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
static int rssi_bars(int8_t rssi){
    if(rssi >= -50) return 4;
    if(rssi >= -60) return 3;
    if(rssi >= -70) return 2;
    if(rssi >= -80) return 1;
    return 0;
}
static bool wired_online(void){
    for(int i=0;i<state.if_count;i++){
        if(state.ifs[i].link_up && state.ifs[i].has_ip) return true;
    }
    return false;
}
static bool internet_online(void){
    if(wired_online()) return true;
    if(state.status.has_device && state.status.connected) return true;
    return false;
}
static const char *online_iface_name(void){
    for(int i=0;i<state.if_count;i++){
        if(state.ifs[i].link_up && state.ifs[i].has_ip) return state.ifs[i].name;
    }
    if(state.status.has_device && state.status.connected) return state.status.interface_name;
    return "";
}
static void do_ping(const char *target){
    struct network_ping_result res;
    state.ping_seq++;
    int32_t rc = pc_ping(target, state.ping_seq, 2000, &res);
    if(rc==0){
        char *p = state.ping_msg;
        p=append_text(p, "ping ");
        p=append_text(p, target);
        p=append_text(p, " ok ");
        p=append_u32(p, res.round_trip_ms);
        p=append_text(p, "ms ttl=");
        p=append_u32(p, res.ttl);
        p=append_text(p, " ip=");
        append_ip(p+pc_strlen(p), res.address);
    } else {
        char *p = state.ping_msg;
        p=append_text(p, "ping ");
        p=append_text(p, target);
        p=append_text(p, " fail (");
        if(rc==-2) p=append_text(p, "no iface");
        else if(rc==-3) p=append_text(p, "not configured/DHCP");
        else if(rc==-5) p=append_text(p, "timeout");
        else if(rc==-6) p=append_text(p, "dns");
        else { p=append_text(p, "err "); p=append_u32(p, (uint32_t)(rc<0?-rc:rc)); }
        append_text(p, ")");
    }
    state.ping_until = now_ms() + 5000;
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
        state.ping_seq=0;
        state.ping_msg[0]='\0';
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
    // layout with scroll
    uint32_t wired_rows = state.if_count ? (uint32_t)state.if_count : 1;
    // estimate total height: header(24)+summary(30)+wired(title+rows*66)+wifi(~230)+diag(110)
    uint32_t total_h = 24+30+8 + (28 + wired_rows*66) + 8 + 250 + 8 + 110 + 40;
    uint32_t visible_h = window->client.height - PAGE_TOP - 30;
    if(visible_h < 120) visible_h = 120;
    uint32_t max_scroll = total_h > visible_h ? (total_h - visible_h + 23)/24 : 0;
    if(state.page_scroll > max_scroll) state.page_scroll = max_scroll;
    int32_t off = -(int32_t)(state.page_scroll * 24);
    uint32_t y = (uint32_t)((int32_t)PAGE_TOP + off);
    pg_window_text(window, PAGE_LEFT, y-2, "Network", window->theme.text);
    y += 18;
    // ---- summary bar ----
    {
        struct pg_rect r = {PAGE_LEFT, y, width, 28};
        pg_window_rect(window, r, 0x2B2D40);
        bool online = internet_online();
        uint32_t col = online ? 0xA6E3A1 : window->theme.danger;
        const char *iface = online_iface_name();
        char sum[96]="Internet: ";
        if(online){
            append_text(sum+pc_strlen(sum), "Online");
            if(iface[0]){ append_text(sum+pc_strlen(sum), " ("); append_text(sum+pc_strlen(sum), iface); append_text(sum+pc_strlen(sum), ")"); }
        } else {
            if(state.if_count==0 && !state.status.has_device) append_text(sum+pc_strlen(sum), "Offline - no adapters");
            else append_text(sum+pc_strlen(sum), "Offline - no link/IP");
        }
        pg_window_text(window, PAGE_LEFT+12, y+9, sum, col);
        y += 28 + 8;
    }
    // ---- wired section ----
    {
        uint32_t box_top = y;
        uint32_t box_h = 28 + wired_rows*66;
        struct pg_rect box = {PAGE_LEFT, box_top, width, box_h};
        pg_window_rect(window, box, 0x2B2D40);
        char title[40]="Wired";
        if(state.if_count){
            append_text(title+pc_strlen(title), " (");
            append_u32(title+pc_strlen(title), (uint32_t)state.if_count);
            append_text(title+pc_strlen(title), ")");
        }
        pg_window_text(window, PAGE_LEFT+10, box_top+8, title, window->theme.text);
        if(state.if_count==0){
            pg_window_text(window, PAGE_LEFT+12, box_top+30, "No wired adapters found", window->theme.muted_text);
            pg_window_text(window, PAGE_LEFT+12, box_top+46, "Check PCI (e1000/pcnet) in QEMU/VBox", window->theme.muted_text);
        } else {
            for(int i=0;i<state.if_count;i++){
                struct net_if_info *inf = &state.ifs[i];
                uint32_t iy = box_top + 26 + (uint32_t)i*66;
                // name + mac + link
                char line[96];
                pc_copy(line, inf->name, sizeof(line));
                append_text(line+pc_strlen(line), "  ");
                {
                    char *p=line+pc_strlen(line);
                    const char *hex="0123456789ABCDEF";
                    for(int b=0;b<6;b++){ if(b) *p++=':'; *p++=hex[(inf->mac[b]>>4)&0xF]; *p++=hex[inf->mac[b]&0xF]; }
                    *p='\0';
                }
                pg_window_text(window, PAGE_LEFT+12, iy, line, window->theme.text);
                uint32_t link_col = inf->link_up ? 0xA6E3A1 : window->theme.danger;
                const char *link_txt = inf->link_up ? (inf->has_ip ? "Link up, DHCP bound" : (inf->dhcp_bound ? "Link up, waiting IP" : "Link up, DHCP...")) : "Link down / cable?";
                if(!inf->link_up && inf->has_ip) link_txt="IP cached, link down?";
                pg_window_text(window, PAGE_LEFT+width-170, iy, link_txt, link_col);
                if(inf->has_ip){
                    char ipbuf[64]="IP ";
                    append_ip(ipbuf+3, inf->ip_address);
                    append_text(ipbuf+pc_strlen(ipbuf), " / ");
                    append_ip(ipbuf+pc_strlen(ipbuf), inf->netmask);
                    pg_window_text(window, PAGE_LEFT+12, iy+16, ipbuf, window->theme.muted_text);
                    char gw[64]="GW ";
                    if(inf->gateway) append_ip(gw+3, inf->gateway);
                    else append_text(gw+3, "-");
                    append_text(gw+pc_strlen(gw), "  DNS ");
                    if(inf->dns_server) append_ip(gw+pc_strlen(gw), inf->dns_server);
                    else append_text(gw+pc_strlen(gw), "-");
                    pg_window_text(window, PAGE_LEFT+12, iy+32, gw, window->theme.muted_text);
                    char st[64]="RX ";
                    char *q=st+3;
                    q=append_u64(q, inf->rx_packets);
                    q=append_text(q, "/");
                    q=append_u64(q, inf->tx_packets);
                    q=append_text(q, " pkts  drop ");
                    q=append_u64(q, inf->rx_dropped+inf->tx_dropped);
                    pg_window_text(window, PAGE_LEFT+12, iy+48, st, window->theme.muted_text);
                } else {
                    pg_window_text(window, PAGE_LEFT+12, iy+16, inf->link_up ? "Waiting for DHCP..." : "No IP - check cable / DHCP server", 0xF9E2AF);
                    char st2[64]="RX ";
                    char *q=st2+3;
                    q=append_u64(q, inf->rx_packets);
                    q=append_text(q, "/");
                    q=append_u64(q, inf->tx_packets);
                    q=append_text(q, " pkts");
                    pg_window_text(window, PAGE_LEFT+12, iy+32, st2, window->theme.muted_text);
                }
            }
        }
        y = box_top + box_h + 8;
    }
    // ---- wifi section ----
    {
        uint32_t box_top = y;
        uint32_t box_h = 218;
        struct pg_rect box = {PAGE_LEFT, box_top, width, box_h};
        pg_window_rect(window, box, 0x2B2D40);
        pg_window_text(window, PAGE_LEFT+10, box_top+8, "Wi-Fi", window->theme.text);
        if(!state.status.has_device){
            // NOT an error anymore: wired may be online
            pg_window_text(window, PAGE_LEFT+12, box_top+30, "No Wi-Fi adapter - wired only", window->theme.muted_text);
            pg_window_text(window, PAGE_LEFT+12, box_top+46, "e1000/pcnet in QEMU has no radio; this is normal", window->theme.muted_text);
            if(wired_online())
                pg_window_text(window, PAGE_LEFT+12, box_top+62, "Internet works via Wired above (ping goes).", 0xA6E3A1);
            else
                pg_window_text(window, PAGE_LEFT+12, box_top+62, "No wireless hardware detected.", window->theme.muted_text);
            // keep Networks hint minimal
            pg_window_text(window, PAGE_LEFT+12, box_top+90, "Networks: N/A without radio", window->theme.muted_text);
            pg_window_text(window, PAGE_LEFT+12, box_top+106, "Select network: N/A", window->theme.muted_text);
        } else {
            // status line
            char line[64];
            pc_copy(line, state.status.interface_name, sizeof(line));
            pg_window_text(window, PAGE_LEFT+12, box_top+26, line, window->theme.text);
            uint32_t state_col = window->theme.accent;
            if(state.status.state==WIFI_STATE_CONNECTED) state_col=0xA6E3A1;
            else if(state.status.state==WIFI_STATE_FAILED) state_col=window->theme.danger;
            else if(state.status.state==WIFI_STATE_SCANNING || state.status.state==WIFI_STATE_CONNECTING) state_col=0xF9E2AF;
            char state_line[80];
            pc_copy(state_line, wifi_state_name(state.status.state), sizeof(state_line));
            if(state.status.connected){
                append_text(state_line+pc_strlen(state_line), "  ");
                append_text(state_line+pc_strlen(state_line), state.status.ssid);
                if(state.status.ip_address){
                    append_text(state_line+pc_strlen(state_line), "  ");
                    append_ip(state_line+pc_strlen(state_line), state.status.ip_address);
                }
            }
            pg_window_text(window, PAGE_LEFT+12, box_top+42, state_line, state_col);
            struct pg_rect scan_btn = {PAGE_LEFT + width - 90, box_top+24, 78, 22};
            bool do_scan = pg_button(window, scan_btn, state.status.state==WIFI_STATE_SCANNING ? "..." : "Scan", event);
            if(do_scan){
                if(pc_wifi_scan()==0){ pc_copy(state.message, "Scan...", sizeof(state.message)); state.message_until = now+1500; }
                else { pc_copy(state.message, "Busy", sizeof(state.message)); state.message_until = now+1500; }
            }
            // networks list compact
            if(state.network_count==0){
                pg_window_text(window, PAGE_LEFT+12, box_top+62, state.status.state==WIFI_STATE_SCANNING ? "Scanning..." : "No networks - check RFKILL/BIOS", window->theme.muted_text);
            } else {
                if(state.selected>=0){
                    if((uint32_t)state.selected < state.scroll_offset) state.scroll_offset = (uint32_t)state.selected;
                    if((uint32_t)state.selected >= state.scroll_offset+3) state.scroll_offset = (uint32_t)state.selected - 2;
                }
                uint32_t visible = 3;
                uint32_t row_h = 24;
                uint32_t start_y = box_top+60;
                for(uint32_t i=0;i<visible;i++){
                    uint32_t idx = state.scroll_offset + i;
                    if(idx >= (uint32_t)state.network_count) break;
                    struct wifi_network_info *net = &state.networks[idx];
                    struct pg_rect row = {PAGE_LEFT+6, start_y + i*row_h, width-12, row_h-2};
                    bool is_sel = (int)idx == state.selected;
                    pg_window_rect(window, row, is_sel ? 0x45475A : 0x313244);
                    pg_window_text(window, row.x+8, row.y+7, net->ssid, is_sel ? window->theme.text : 0xCDD6F4);
                    int bars = rssi_bars(net->rssi);
                    char barstr[6];
                    for(int b=0;b<4;b++) barstr[b] = b < bars ? '|' : '.';
                    barstr[4]='\0';
                    char right[32];
                    char *q=right;
                    for(int b=0;b<4;b++) *q++=barstr[b];
                    *q++=' ';
                    q=append_text(q, security_name(net->security));
                    pg_window_text(window, PAGE_LEFT+ width-90, row.y+7, right, window->theme.muted_text);
                    if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                        int32_t x = event->x - (int32_t)window->client.x;
                        int32_t yy = event->y - (int32_t)window->client.y;
                        if(x >= (int32_t)row.x && x < (int32_t)(row.x+row.width) && yy >= (int32_t)row.y && yy < (int32_t)(row.y+row.height)){
                            state.selected = (int)idx;
                            if(net->security!=WIFI_SECURITY_OPEN) state.password_focused = true;
                            else state.password[0]='\0';
                        }
                    }
                }
            }
            // connect row
            if(state.selected>=0 && state.selected < state.network_count){
                struct wifi_network_info *sel = &state.networks[state.selected];
                uint32_t conn_y = box_top + 60 + 3*24 + 6;
                if(sel->security != WIFI_SECURITY_OPEN){
                    struct pg_rect pass_rect = {PAGE_LEFT+10, conn_y, width-200, 22};
                    pg_window_rect(window, pass_rect, state.password_focused ? 0x45475A : 0x181825);
                    pg_window_text(window, pass_rect.x+6, pass_rect.y+7, state.password, window->theme.text);
                    if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
                        int32_t x = event->x - (int32_t)window->client.x;
                        int32_t yy = event->y - (int32_t)window->client.y;
                        bool inside = x >= (int32_t)pass_rect.x && x < (int32_t)(pass_rect.x+pass_rect.width) && yy >= (int32_t)pass_rect.y && yy < (int32_t)(pass_rect.y+pass_rect.height);
                        state.password_focused = inside;
                    }
                    struct pg_rect conn_btn = {pass_rect.x+pass_rect.width+6, pass_rect.y, 84, 22};
                    bool do_connect = pg_button(window, conn_btn, state.status.state==WIFI_STATE_CONNECTING ? "..." : "Connect", event);
                    if(do_connect && state.status.state!=WIFI_STATE_CONNECTING){
                        int32_t rc = pc_wifi_connect(sel->ssid, state.password);
                        if(rc==0){ save_config(sel->ssid, state.password); pc_copy(state.message, "Connecting...", sizeof(state.message)); }
                        else pc_copy(state.message, "Failed", sizeof(state.message));
                        state.message_until = now + 3000;
                    }
                    if(state.status.connected){
                        struct pg_rect disc_btn = {PAGE_LEFT+ width-78, conn_y, 72, 22};
                        if(pg_button(window, disc_btn, "Off", event)){
                            (void)pc_wifi_disconnect();
                            pc_copy(state.message, "Off", sizeof(state.message));
                            state.message_until = now+2000;
                        }
                    }
                } else {
                    struct pg_rect conn_btn = {PAGE_LEFT+10, conn_y, 84, 22};
                    if(pg_button(window, conn_btn, "Connect", event)){
                        int32_t rc = pc_wifi_connect(sel->ssid, "");
                        if(rc==0){ save_config(sel->ssid, ""); pc_copy(state.message, "Connecting...", sizeof(state.message)); }
                        else pc_copy(state.message, "Failed", sizeof(state.message));
                        state.message_until = now+3000;
                    }
                }
                if(state.message[0] && now < state.message_until){
                    pg_window_text(window, PAGE_LEFT+100, conn_y+4, state.message, 0xF9E2AF);
                }
            } else {
                pg_window_text(window, PAGE_LEFT+12, box_top+150, "Select network to connect", window->theme.muted_text);
            }
        }
        y = box_top + box_h + 8;
    }
    // ---- diagnostics ----
    {
        uint32_t box_top = y;
        uint32_t box_h = 76;
        struct pg_rect box = {PAGE_LEFT, box_top, width, box_h};
        pg_window_rect(window, box, 0x2B2D40);
        pg_window_text(window, PAGE_LEFT+10, box_top+8, "Diagnostics", window->theme.text);
        struct pg_rect b1 = {PAGE_LEFT+10, box_top+28, 110, 22};
        struct pg_rect b2 = {PAGE_LEFT+126, box_top+28, 110, 22};
        struct pg_rect b3 = {PAGE_LEFT+242, box_top+28, 90, 22};
        if(pg_button(window, b1, "Ping 8.8.8.8", event)) do_ping("8.8.8.8");
        // gateway ping
        uint32_t gw = 0;
        for(int i=0;i<state.if_count;i++) if(state.ifs[i].gateway){ gw=state.ifs[i].gateway; break; }
        char gw_str[32]="GW";
        if(gw) ip_to_str(gw, gw_str, sizeof(gw_str));
        bool has_gw = gw != 0;
        if(pg_button(window, b2, has_gw ? "Ping gateway" : "No gateway", event)){
            if(has_gw) do_ping(gw_str);
            else { pc_copy(state.ping_msg, "No gateway yet (DHCP?)", sizeof(state.ping_msg)); state.ping_until = now+3000; }
        }
        if(pg_button(window, b3, "Refresh", event)){ refresh_status(); pc_copy(state.ping_msg, "Refreshed", sizeof(state.ping_msg)); state.ping_until=now+1500; }
        if(state.ping_msg[0] && now < state.ping_until){
            pg_window_text(window, PAGE_LEFT+10, box_top+56, state.ping_msg, 0xF9E2AF);
        } else {
            pg_window_text(window, PAGE_LEFT+10, box_top+56, "Ping proves real uplink even if Wi-Fi shows N/A", window->theme.muted_text);
        }
        y = box_top + box_h + 8;
    }
    if(max_scroll>0){
        char scroll_info[16];
        char *q=scroll_info;
        q=append_u32(q, state.page_scroll+1);
        *q++='/';
        q=append_u32(q, max_scroll+1);
        *q='\0';
        pg_window_text(window, PAGE_LEFT+width-44, PAGE_TOP-2+off+4, scroll_info, window->theme.muted_text);
    }
}
