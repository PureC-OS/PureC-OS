#include "../lib/common.h"
#include "libc/include/purec.h"
#include <termios.h>
#include <time.h>

#define SCAN_TIMEOUT_MS 15000
#define CONNECT_TIMEOUT_MS 30000
#define DHCP_TIMEOUT_MS 15000

static bool color;

static const char *paint(const char *code)
{
    return color ? code : "";
}

static void sleep_ms(unsigned ms)
{
    usleep(ms * 1000u);
}

static const char *security_name(uint8_t s)
{
    switch (s) {
    case WIFI_SECURITY_OPEN: return "open";
    case WIFI_SECURITY_WEP: return "wep";
    case WIFI_SECURITY_WPA2: return "psk";
    case WIFI_SECURITY_WPA3: return "sae";
    case WIFI_SECURITY_WPA2_WPA3: return "psk/sae";
    default: return "?";
    }
}

static const char *state_name(uint32_t s)
{
    switch (s) {
    case WIFI_STATE_DISCONNECTED: return "disconnected";
    case WIFI_STATE_SCANNING: return "scanning";
    case WIFI_STATE_CONNECTING: return "connecting";
    case WIFI_STATE_CONNECTED: return "connected";
    case WIFI_STATE_FAILED: return "failed";
    default: return "unknown";
    }
}

static const char *error_text(int32_t e)
{
    switch (e) {
    case 0: return "no error";
    case -20: return "WPA2 4-way handshake is not implemented in the driver yet";
    case -21: return "association failed (network out of range or rejected)";
    case -30: return "firmware stopped responding";
    default: return "connection failed";
    }
}

static const char *signal_bars(int8_t rssi)
{
    if (rssi >= -55) return "****";
    if (rssi >= -67) return "*** ";
    if (rssi >= -75) return "**  ";
    if (rssi >= -85) return "*   ";
    return "    ";
}

static void ip_text(uint32_t a, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

static bool status(struct wifi_status_info *st)
{
    memset(st, 0, sizeof(*st));
    return pc_wifi_status(st);
}

static bool device(struct wifi_status_info *st, const char *wanted)
{
    if (!status(st) || !st->has_device) {
        util_err("no Wi-Fi device found");
        return false;
    }
    if (wanted && strcmp(wanted, st->interface_name) != 0) {
        util_err("%s: no such device (try: iwxctl device list)", wanted);
        return false;
    }
    return true;
}

static bool need_root(void)
{
    if (getuid() == 0)
        return true;
    util_err("permission denied (run as root or with sudo)");
    return false;
}

static int cmd_device_list(void)
{
    struct wifi_status_info st;
    if (!status(&st) || !st.has_device) {
        printf("No Wi-Fi devices.\n");
        return 0;
    }
    printf("%s%-10s %-18s %-14s %s%s\n", paint(C_BOLD), "Name", "Address", "State", "Mode", paint(C_RESET));
    printf("%-10s %02x:%02x:%02x:%02x:%02x:%02x  %-14s %s\n", st.interface_name, st.mac[0], st.mac[1], st.mac[2],
           st.mac[3], st.mac[4], st.mac[5], state_name(st.state), "station");
    return 0;
}

static int print_networks(void)
{
    struct wifi_network_info nets[WIFI_SCAN_MAX];
    int32_t n = pc_wifi_list(nets, WIFI_SCAN_MAX);
    if (n < 0)
        return util_err("cannot read scan results");
    struct wifi_status_info st;
    status(&st);
    for (int32_t i = 0; i < n; i++)
        for (int32_t j = i + 1; j < n; j++)
            if (nets[j].rssi > nets[i].rssi) {
                struct wifi_network_info t = nets[i];
                nets[i] = nets[j];
                nets[j] = t;
            }
    if (!n) {
        printf("No networks found. Run: iwxctl station %s scan\n", st.interface_name[0] ? st.interface_name : "wlan0");
        return 0;
    }
    printf("%s  %-32s %-8s %-7s %-6s %s%s\n", paint(C_BOLD), "Network name", "Security", "Channel", "Signal",
           "dBm", paint(C_RESET));
    for (int32_t i = 0; i < n; i++) {
        bool current = st.state == WIFI_STATE_CONNECTED && strcmp(st.ssid, nets[i].ssid) == 0;
        printf("%s%s %-32s %-8s %-7u %s   %d%s\n", current ? paint(C_GREEN) : "", current ? ">" : " ",
               nets[i].ssid[0] ? nets[i].ssid : "(hidden)", security_name(nets[i].security), nets[i].channel,
               signal_bars(nets[i].rssi), nets[i].rssi, current ? paint(C_RESET) : "");
    }
    return 0;
}

static int cmd_scan(bool show)
{
    if (!need_root())
        return 1;
    struct wifi_status_info st;
    if (!device(&st, 0))
        return 1;
    if (st.state == WIFI_STATE_CONNECTED || st.state == WIFI_STATE_CONNECTING) {
        util_err("cannot scan while connected to '%s' (disconnect first); last results:", st.ssid);
        print_networks();
        return 1;
    }
    uint32_t before = st.scan_count;
    if (pc_wifi_scan() < 0)
        return util_err("scan request rejected (a scan may already be running)");
    printf("Scanning");
    fflush(stdout);
    for (unsigned waited = 0; waited < SCAN_TIMEOUT_MS; waited += 250) {
        sleep_ms(250);
        if (waited % 1000 == 0) {
            printf(".");
            fflush(stdout);
        }
        if (status(&st) && st.scan_count != before && st.state != WIFI_STATE_SCANNING) {
            printf(" done\n");
            return show ? print_networks() : 0;
        }
    }
    printf(" timed out\n");
    return 1;
}

static bool read_secret(const char *prompt, char *out, size_t cap)
{
    struct termios old, quiet;
    bool tty = tcgetattr(0, &old) == 0;
    if (tty) {
        quiet = old;
        quiet.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(0, TCSANOW, &quiet);
    }
    printf("%s", prompt);
    fflush(stdout);
    bool ok = fgets(out, (int)cap, stdin) != 0;
    if (tty)
        tcsetattr(0, TCSANOW, &old);
    printf("\n");
    if (!ok)
        return false;
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = 0;
    return true;
}

static int cmd_connect(const char *ssid, const char *passphrase)
{
    if (!need_root())
        return 1;
    struct wifi_status_info st;
    if (!device(&st, 0))
        return 1;
    if (strlen(ssid) >= WIFI_SSID_CAPACITY)
        return util_err("network name is longer than 32 bytes");
    struct wifi_network_info nets[WIFI_SCAN_MAX];
    int32_t n = pc_wifi_list(nets, WIFI_SCAN_MAX);
    int found = -1;
    for (int32_t i = 0; i < n; i++)
        if (strcmp(nets[i].ssid, ssid) == 0)
            found = i;
    uint8_t security = found >= 0 ? nets[found].security : WIFI_SECURITY_WPA2;
    if (found < 0)
        printf("'%s' is not in the scan results, trying it as a hidden network.\n", ssid);
    char pass[WIFI_PASSWORD_CAPACITY] = {0};
    if (security != WIFI_SECURITY_OPEN) {
        if (passphrase)
            snprintf(pass, sizeof(pass), "%s", passphrase);
        else if (!read_secret("Passphrase: ", pass, sizeof(pass)))
            return 1;
        if (strlen(pass) < 8 || strlen(pass) > 63)
            return util_err("a WPA passphrase has 8 to 63 characters");
    }
    int32_t rc = pc_wifi_connect(ssid, pass[0] ? pass : 0);
    memset(pass, 0, sizeof(pass));
    if (rc < 0)
        return util_err("connect request rejected");
    printf("Connecting to '%s'", ssid);
    fflush(stdout);
    unsigned waited = 0;
    for (; waited < CONNECT_TIMEOUT_MS; waited += 250) {
        sleep_ms(250);
        if (waited % 1000 == 0) {
            printf(".");
            fflush(stdout);
        }
        if (!status(&st))
            continue;
        if (st.state == WIFI_STATE_CONNECTED)
            break;
        if (st.state == WIFI_STATE_FAILED || (st.state == WIFI_STATE_DISCONNECTED && waited > 1000)) {
            printf(" %sfailed%s\n", paint(C_RED), paint(C_RESET));
            return util_err("%s", error_text(st.last_error));
        }
    }
    if (st.state != WIFI_STATE_CONNECTED) {
        printf(" timed out\n");
        return 1;
    }
    printf(" %sconnected%s\n", paint(C_GREEN), paint(C_RESET));
    printf("Waiting for an address (DHCP)");
    fflush(stdout);
    for (unsigned w = 0; w < DHCP_TIMEOUT_MS; w += 500) {
        struct net_if_info ifs[NET_IF_MAX_COUNT];
        int32_t k = pc_net_if_list(ifs, NET_IF_MAX_COUNT);
        for (int32_t i = 0; i < k; i++)
            if (strcmp(ifs[i].name, st.interface_name) == 0 && ifs[i].has_ip && ifs[i].ip_address) {
                char a[20];
                ip_text(ifs[i].ip_address, a, sizeof(a));
                printf(" %s\n", a);
                return 0;
            }
        sleep_ms(500);
        if (w % 1000 == 0) {
            printf(".");
            fflush(stdout);
        }
    }
    printf(" no address yet (check: ip addr)\n");
    return 0;
}

static int cmd_disconnect(void)
{
    if (!need_root())
        return 1;
    struct wifi_status_info st;
    if (!device(&st, 0))
        return 1;
    if (st.state != WIFI_STATE_CONNECTED && st.state != WIFI_STATE_CONNECTING) {
        printf("Not connected.\n");
        return 0;
    }
    if (pc_wifi_disconnect() < 0)
        return util_err("disconnect request rejected");
    for (int i = 0; i < 20; i++) {
        sleep_ms(250);
        if (status(&st) && st.state == WIFI_STATE_DISCONNECTED)
            break;
    }
    printf("Disconnected.\n");
    return 0;
}

static int cmd_show(void)
{
    struct wifi_status_info st;
    if (!device(&st, 0))
        return 1;
    printf("%s                                 Station: %s%s\n", paint(C_BOLD), st.interface_name, paint(C_RESET));
    printf("  %-24s %s\n", "State", state_name(st.state));
    if (st.state == WIFI_STATE_CONNECTED || st.state == WIFI_STATE_CONNECTING) {
        printf("  %-24s %s\n", "Connected network", st.ssid);
        printf("  %-24s %02x:%02x:%02x:%02x:%02x:%02x\n", "BSSID", st.bssid[0], st.bssid[1], st.bssid[2],
               st.bssid[3], st.bssid[4], st.bssid[5]);
        printf("  %-24s %s\n", "Security", security_name(st.security));
        printf("  %-24s %u\n", "Channel", st.channel);
        printf("  %-24s %d dBm\n", "RSSI", st.rssi);
    }
    struct net_if_info ifs[NET_IF_MAX_COUNT];
    int32_t k = pc_net_if_list(ifs, NET_IF_MAX_COUNT);
    for (int32_t i = 0; i < k; i++)
        if (strcmp(ifs[i].name, st.interface_name) == 0 && ifs[i].has_ip) {
            char a[20];
            ip_text(ifs[i].ip_address, a, sizeof(a));
            printf("  %-24s %s\n", "IPv4 address", a);
            ip_text(ifs[i].gateway, a, sizeof(a));
            printf("  %-24s %s\n", "Gateway", a);
            ip_text(ifs[i].dns_server, a, sizeof(a));
            printf("  %-24s %s\n", "DNS", a);
        }
    printf("  %-24s %u\n", "Scans done", st.scan_count);
    if (st.state == WIFI_STATE_FAILED)
        printf("  %-24s %s\n", "Last error", error_text(st.last_error));
    return 0;
}

static void help(void)
{
    printf("Usage: iwxctl [COMMAND]   (no command: interactive mode)\n\n"
           "  device list                          list Wi-Fi devices\n"
           "  station DEVICE scan                  scan for networks\n"
           "  station DEVICE get-networks          show the last scan results\n"
           "  station DEVICE connect SSID          connect (asks for the passphrase)\n"
           "  station DEVICE disconnect            disconnect\n"
           "  station DEVICE show                  show the connection state\n\n"
           "Short forms: scan, networks, connect SSID, disconnect, show\n"
           "Option: --passphrase PASS with connect (avoids the prompt)\n"
           "In interactive mode: help, exit\n");
}

static int run(int argc, char **argv)
{
    if (argc < 1)
        return 0;
    const char *passphrase = 0;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--passphrase") && i + 1 < argc) {
            passphrase = argv[i + 1];
            for (int j = i; j + 2 < argc; j++)
                argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    const char *cmd = argv[0];
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
        help();
        return 0;
    }
    if (!strcmp(cmd, "device") || !strcmp(cmd, "devices")) {
        if (argc >= 2 && strcmp(argv[1], "list") != 0)
            return util_err("unknown device command '%s'", argv[1]);
        return cmd_device_list();
    }
    if (!strcmp(cmd, "station")) {
        if (argc < 3)
            return util_err("usage: iwxctl station DEVICE scan|get-networks|connect SSID|disconnect|show");
        struct wifi_status_info st;
        if (!device(&st, argv[1]))
            return 1;
        argv += 2;
        argc -= 2;
        cmd = argv[0];
    }
    if (!strcmp(cmd, "scan"))
        return cmd_scan(true);
    if (!strcmp(cmd, "get-networks") || !strcmp(cmd, "networks") || !strcmp(cmd, "list"))
        return print_networks();
    if (!strcmp(cmd, "connect")) {
        if (argc < 2)
            return util_err("usage: connect SSID [--passphrase PASS]");
        char ssid[WIFI_SSID_CAPACITY * 2] = {0};
        for (int i = 1; i < argc; i++) {
            if (i > 1)
                strncat(ssid, " ", sizeof(ssid) - strlen(ssid) - 1);
            strncat(ssid, argv[i], sizeof(ssid) - strlen(ssid) - 1);
        }
        return cmd_connect(ssid, passphrase);
    }
    if (!strcmp(cmd, "disconnect"))
        return cmd_disconnect();
    if (!strcmp(cmd, "show") || !strcmp(cmd, "status"))
        return cmd_show();
    return util_err("unknown command '%s' (try: iwxctl help)", cmd);
}

static int interactive(void)
{
    printf("iwxctl: type 'help' for commands, 'exit' to quit\n");
    char line[256];
    for (;;) {
        printf("%s[iwx]%s# ", paint(C_GREEN), paint(C_RESET));
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            return 0;
        }
        char *argv[16];
        int argc = 0;
        for (char *p = strtok(line, " \t\r\n"); p && argc < 16; p = strtok(0, " \t\r\n"))
            argv[argc++] = p;
        if (!argc)
            continue;
        if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "quit"))
            return 0;
        (void)run(argc, argv);
    }
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    color = util_color(1, "auto");
    if (argc < 2)
        return interactive();
    return run(argc - 1, argv + 1);
}
