#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../libfs/include/purefs.h"

#define WIFI_INI_PATH "/config/wifi.ini"
#define WIFI_INI_DIR "/config"

static struct {
    bool init;
    struct wifi_status_info status;
    bool have_status;
    struct wifi_network_info nets[WIFI_SCAN_MAX];
    int count;
    struct net_if_info ifs[NET_IF_MAX_COUNT];
    int if_count;
    char selected[WIFI_SSID_CAPACITY];
    char password[WIFI_PASSWORD_CAPACITY];
    char saved_ssid[WIFI_SSID_CAPACITY];
    char saved_pass[WIFI_PASSWORD_CAPACITY];
    uint64_t last;
    uint64_t scan_at;
} st;

static void load_saved(void)
{
    int32_t fd = pf_open(WIFI_INI_PATH);
    if (fd < 0)
        return;
    char buf[256];
    int32_t n = pf_read(fd, buf, sizeof(buf) - 1);
    (void)pf_close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r')
            end++;
        char save = *end;
        *end = 0;
        if (line[0] == 's' && line[1] == 's' && line[2] == 'i' && line[3] == 'd' && line[4] == '=')
            fmt_copy(st.saved_ssid, line + 5, sizeof(st.saved_ssid));
        else if (line[0] == 'p' && line[1] == 'a' && line[2] == 's' && line[3] == 's' && line[4] == 'w' &&
                 line[8] == '=')
            fmt_copy(st.saved_pass, line + 9, sizeof(st.saved_pass));
        if (!save)
            break;
        line = end + 1;
    }
}

static bool save_cfg(const char *ssid, const char *password)
{
    char buf[256];
    struct fmt f;
    fmt_init(&f, buf, sizeof(buf));
    fs(fs(fs(fs(fs(&f, "ssid="), ssid), "\npassword="), password), "\n");
    (void)pf_create_dir(WIFI_INI_DIR);
    if (pf_write_file(WIFI_INI_PATH, buf, (uint32_t)f.len) < 0)
        return false;
    fmt_copy(st.saved_ssid, ssid, sizeof(st.saved_ssid));
    fmt_copy(st.saved_pass, password, sizeof(st.saved_pass));
    return true;
}

static void refresh(struct settings_app *app)
{
    uint64_t now = settings_now();
    if (!st.init) {
        st.init = true;
        load_saved();
        app->refresh = true;
    }
    if (!app->refresh && now - st.last < 700)
        return;
    st.last = now;
    st.have_status = pc_wifi_status(&st.status);
    if (!st.have_status)
        st.status.has_device = 0;
    st.count = pc_wifi_list(st.nets, WIFI_SCAN_MAX);
    if (st.count < 0)
        st.count = 0;
    st.if_count = pc_net_if_list(st.ifs, NET_IF_MAX_COUNT);
    if (st.if_count < 0)
        st.if_count = 0;
    if (app->refresh && st.status.has_device && st.status.scan_count == 0 && !st.scan_at) {
        (void)pc_wifi_scan();
        st.scan_at = now;
    }
}

static const char *sec_name(uint8_t s)
{
    switch (s) {
    case WIFI_SECURITY_OPEN:
        return "Open";
    case WIFI_SECURITY_WEP:
        return "WEP";
    case WIFI_SECURITY_WPA2:
        return "WPA2";
    case WIFI_SECURITY_WPA3:
        return "WPA3";
    case WIFI_SECURITY_WPA2_WPA3:
        return "WPA2/WPA3";
    default:
        return "Secured";
    }
}

static const struct net_if_info *online_if(void)
{
    for (int i = 0; i < st.if_count; i++)
        if (st.ifs[i].link_up && st.ifs[i].has_ip)
            return &st.ifs[i];
    return 0;
}

static void connect(struct settings_app *app, const struct wifi_network_info *n, const char *pass)
{
    int32_t rc = pc_wifi_connect(n->ssid, pass);
    if (rc == 0) {
        (void)save_cfg(n->ssid, pass);
        settings_toast(app, "Connecting...", 0x89b4fa);
        st.selected[0] = 0;
    } else {
        settings_toast(app, "Connection failed", 0xf38ba8);
    }
}

static int status_card(struct page_ctx *p, int y)
{
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    const struct net_if_info *wired = online_if();
    bool wifi_on = st.status.has_device && st.status.connected;
    bool online = wired || wifi_on;
    int h = 132;
    ui_card(u, p->x, y, p->w, h);
    uint32_t col = online ? pal->success : st.status.state == WIFI_STATE_CONNECTING ? pal->warning : pal->text3;
    int icon = wifi_on ? SI_WIFI : wired ? SI_ETHERNET : SI_WIFI_OFF;
    sg_rrect(u->c, p->x + 24, y + 24, 84, 84, 24, col, pal->light ? 36 : 40);
    sg_icon(u->c, icon, SG_ICON_L, p->x + 38, y + 38, col);

    char title[80], sub[96];
    struct fmt f;
    fmt_init(&f, title, sizeof(title));
    fmt_init(&f, sub, sizeof(sub));
    struct fmt t;
    fmt_init(&t, title, sizeof(title));
    if (wifi_on) {
        fs(fs(&t, "Connected to "), st.status.ssid);
        fs(&f, st.status.interface_name);
        fs(&f, "  -  ");
        if (st.status.ip_address)
            fip(&f, st.status.ip_address);
        else
            fs(&f, "obtaining address...");
    } else if (wired) {
        fs(&t, "Connected via Ethernet");
        fs(&f, wired->name);
        fs(&f, "  -  ");
        fip(&f, wired->ip_address);
    } else if (st.status.has_device && st.status.state == WIFI_STATE_CONNECTING) {
        fs(fs(&t, "Connecting to "), st.status.ssid);
        fs(&f, "Authenticating...");
    } else {
        fs(&t, "Not connected");
        fs(&f, st.status.has_device ? "Choose a Wi-Fi network below" : "No active network connection");
    }
    sg_text_fit(u->c, SF_TITLE, p->x + 130, y + 30, p->w - 300, title, pal->text);
    sg_text_fit(u->c, SF_BODY, p->x + 130, y + 64, p->w - 300, sub, pal->text2);
    int bx = p->x + 130;
    bx += ui_badge(u, bx, y + 92, online ? "Internet" : "Offline", online ? pal->success : pal->danger) + 8;
    if (wifi_on) {
        char ch[24];
        fmt_init(&f, ch, sizeof(ch));
        fs(&f, "Channel ");
        fu(&f, st.status.channel);
        bx += ui_badge(u, bx, y + 92, ch, pal->info) + 8;
        ui_badge(u, bx, y + 92, sec_name(st.status.security), pal->accent);
    }
    if (wifi_on &&
        ui_button(u, ui_id("wifi-disc", 0), p->x + p->w - 148, y + 47, 124, 38, "Disconnect", -1,
                  UI_BTN_SECONDARY)) {
        (void)pc_wifi_disconnect();
        settings_toast(p->app, "Disconnected", pal->warning);
        p->app->refresh = true;
    }
    return h;
}

static int wifi_card(struct page_ctx *p, int y)
{
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    int y0 = y;
    int list_h = 0;
    int rows = st.count;
    int row_h = 58;
    bool expanded = false;
    for (int i = 0; i < rows; i++)
        if (st.selected[0] && fmt_streq(st.selected, st.nets[i].ssid))
            expanded = true;
    if (!st.status.has_device)
        list_h = 90;
    else if (!rows)
        list_h = 80;
    else
        list_h = rows * row_h + (expanded ? 64 : 0);
    int h = 84 + list_h + 12;
    ui_card(u, p->x, y, p->w, h);
    char sub[48];
    struct fmt f;
    fmt_init(&f, sub, sizeof(sub));
    if (st.status.has_device) {
        fu(&f, (uint64_t)rows);
        fs(&f, rows == 1 ? " network available" : " networks available");
    } else {
        fs(&f, "Adapter not detected");
    }
    ui_card_title(u, p->x + 20, y + 20, SI_WIFI, "Wi-Fi", sub);
    if (st.status.has_device) {
        bool scanning = st.status.state == WIFI_STATE_SCANNING ||
                        (st.scan_at && settings_now() - st.scan_at < 2500);
        if (ui_button(u, ui_id("wifi-scan", 0), p->x + p->w - 130, y + 22, 106, 36,
                      scanning ? "Scanning" : "Scan", SI_REFRESH, UI_BTN_SECONDARY) &&
            !scanning) {
            (void)pc_wifi_scan();
            st.scan_at = settings_now();
            p->app->refresh = true;
        }
        if (scanning)
            u->animating = true;
    }
    y += 76;
    ui_separator(u, p->x + 20, y, p->w - 40);
    y += 8;
    if (!st.status.has_device) {
        sg_text(u->c, SF_BODY, p->x + 24, y + 24, "No Wi-Fi hardware was found on this computer.", pal->text3);
        sg_text(u->c, SF_SMALL, p->x + 24, y + 46, "Supported: Atheros AR9285, Intel AX201 (in progress).",
                pal->text3);
        return h;
    }
    if (!rows) {
        sg_text(u->c, SF_BODY, p->x + 24, y + 26, "No networks found yet. Press Scan to search again.",
                pal->text3);
        return h;
    }
    for (int i = 0; i < rows; i++) {
        const struct wifi_network_info *n = &st.nets[i];
        bool connected = st.status.connected && fmt_streq(st.status.ssid, n->ssid);
        bool sel = st.selected[0] && fmt_streq(st.selected, n->ssid);
        int rx = p->x + 10, rw = p->w - 20;
        if (ui_row(u, ui_id("wifi-row", i), rx, y, rw, row_h - 4, sel)) {
            if (connected) {
                st.selected[0] = 0;
            } else if (n->security == WIFI_SECURITY_OPEN) {
                connect(p->app, n, "");
            } else if (sel) {
                st.selected[0] = 0;
            } else {
                fmt_copy(st.selected, n->ssid, sizeof(st.selected));
                if (fmt_streq(st.saved_ssid, n->ssid))
                    fmt_copy(st.password, st.saved_pass, sizeof(st.password));
                else
                    st.password[0] = 0;
                u->focus = ui_id("wifi-pass", 0);
            }
        }
        ui_signal_bars(u, rx + 16, y + (row_h - 4 - 18) / 2, n->rssi,
                       connected ? pal->success : sel ? pal->accent : pal->text2);
        const char *name = n->ssid[0] ? n->ssid : "Hidden network";
        sg_text_fit(u->c, SF_MEDIUM, rx + 50, y + 9, rw - 240, name, n->ssid[0] ? pal->text : pal->text3);
        char info[64];
        fmt_init(&f, info, sizeof(info));
        fs(&f, sec_name(n->security));
        fs(&f, "  -  ch ");
        fu(&f, n->channel);
        fs(&f, n->channel > 14 ? "  -  5 GHz" : "  -  2.4 GHz");
        fs(&f, "  -  ");
        fi(&f, n->rssi);
        fs(&f, " dBm");
        sg_text(u->c, SF_SMALL, rx + 50, y + 30, info, pal->text3);
        int ex = rx + rw - 16;
        if (connected) {
            int bw = sg_text_w(SF_SMALL, "Connected") + 16;
            ui_badge(u, ex - bw, y + 16, "Connected", pal->success);
        } else if (n->security != WIFI_SECURITY_OPEN) {
            sg_icon(u->c, SI_LOCK, SG_ICON_S, ex - 18, y + 18, pal->text3);
        }
        y += row_h;
        if (sel) {
            int bw = 116;
            bool go = ui_textbox(u, ui_id("wifi-pass", 0), rx + 50, y, rw - 50 - bw - 112 - 16, 40,
                                 st.password, sizeof(st.password), true, "Network password");
            if (ui_button(u, ui_id("wifi-cancel", 0), rx + rw - bw - 112 - 8, y, 104, 40, "Cancel", -1,
                          UI_BTN_SECONDARY))
                st.selected[0] = 0;
            if (ui_button(u, ui_id("wifi-connect", 0), rx + rw - bw, y, bw, 40, "Connect", -1,
                          UI_BTN_PRIMARY) ||
                go)
                connect(p->app, n, st.password);
            y += 64;
        }
    }
    return h;
}

static int wired_card(struct page_ctx *p, int y)
{
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    int rows = st.if_count;
    int row_h = 112;
    int h = 84 + (rows ? rows * row_h : 70);
    ui_card(u, p->x, y, p->w, h);
    ui_card_title(u, p->x + 20, y + 20, SI_LAN, "Network adapters", "Wired and virtual interfaces");
    y += 76;
    ui_separator(u, p->x + 20, y, p->w - 40);
    y += 12;
    if (!rows) {
        sg_text(u->c, SF_BODY, p->x + 24, y + 20, "No network adapters detected.", pal->text3);
        return h;
    }
    for (int i = 0; i < rows; i++) {
        const struct net_if_info *n = &st.ifs[i];
        int x = p->x + 24;
        sg_icon(u->c, SI_ETHERNET, SG_ICON_S, x, y + 2, n->link_up ? pal->accent : pal->text3);
        sg_text(u->c, SF_HEAD, x + 30, y, n->name, pal->text);
        int nx = x + 40 + sg_text_w(SF_HEAD, n->name);
        const char *state = !n->link_up ? "No link" : n->has_ip ? "Connected" : "Waiting for DHCP";
        uint32_t sc = !n->link_up ? pal->danger : n->has_ip ? pal->success : pal->warning;
        ui_badge(u, nx, y - 1, state, sc);
        char a[64], b[64], c[64], d[64];
        struct fmt f;
        fmt_init(&f, a, sizeof(a));
        if (n->has_ip) {
            fip(&f, n->ip_address);
        } else {
            fs(&f, "-");
        }
        fmt_init(&f, b, sizeof(b));
        if (n->gateway)
            fip(&f, n->gateway);
        else
            fs(&f, "-");
        fmt_init(&f, c, sizeof(c));
        if (n->dns_server)
            fip(&f, n->dns_server);
        else
            fs(&f, "-");
        fmt_init(&f, d, sizeof(d));
        fmac(&f, n->mac);
        int colw = (p->w - 48) / 2;
        ui_kv(u, x, y + 34, colw - 20, "IP address", a);
        ui_kv(u, x + colw, y + 34, colw - 20, "Gateway", b);
        ui_kv(u, x, y + 58, colw - 20, "DNS", c);
        ui_kv(u, x + colw, y + 58, colw - 20, "MAC", d);
        fmt_init(&f, a, sizeof(a));
        fbytes(&f, n->rx_bytes);
        fs(&f, " received");
        fmt_init(&f, b, sizeof(b));
        fbytes(&f, n->tx_bytes);
        fs(&f, " sent");
        sg_icon(u->c, SI_DOWNLOAD, SG_ICON_S, x, y + 80, pal->info);
        sg_text(u->c, SF_SMALL, x + 22, y + 82, a, pal->text3);
        sg_icon(u->c, SI_UPLOAD, SG_ICON_S, x + colw, y + 80, pal->success);
        sg_text(u->c, SF_SMALL, x + colw + 22, y + 82, b, pal->text3);
        y += row_h;
        if (i + 1 < rows)
            ui_separator(u, p->x + 20, y - 8, p->w - 40);
    }
    return h;
}

int page_network(struct page_ctx *p)
{
    refresh(p->app);
    int y = p->y;
    y += status_card(p, y) + 16;
    y += wifi_card(p, y) + 16;
    y += wired_card(p, y);
    return y - p->y;
}
