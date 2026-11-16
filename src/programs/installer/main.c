#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "../accounts/accounts.h"

#define MAX_DISKS 16
#define MAX_TRUSTED 64
#define PKG_CONF "/etc/pkg/pkg.conf"
#define USERSPACE_PKG "userspace"
#define DEFAULT_SAVE "/root/purec-install.conf"

enum key { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_ESC, K_BACKSPACE, K_SPACE, K_HOME, K_END, K_PGUP,
           K_PGDN, K_TAB, K_CHAR };

static const char *const zones[] = {
    "UTC", "Pacific/Honolulu", "America/Anchorage", "America/Los_Angeles", "America/Phoenix", "America/Denver",
    "America/Chicago", "America/Mexico_City", "America/New_York", "America/Toronto", "America/Sao_Paulo",
    "America/Argentina/Buenos_Aires", "Atlantic/Reykjavik", "Europe/London", "Europe/Lisbon", "Africa/Lagos",
    "Europe/Berlin", "Europe/Paris", "Europe/Madrid", "Europe/Rome", "Europe/Warsaw", "Europe/Prague",
    "Europe/Stockholm", "Africa/Johannesburg", "Europe/Kyiv", "Europe/Bucharest", "Europe/Chisinau",
    "Europe/Athens", "Europe/Helsinki", "Europe/Riga", "Europe/Vilnius", "Europe/Tallinn", "Europe/Istanbul",
    "Europe/Minsk", "Europe/Moscow", "Asia/Riyadh", "Asia/Tbilisi", "Asia/Yerevan", "Asia/Baku", "Asia/Dubai",
    "Asia/Tashkent", "Asia/Almaty", "Asia/Karachi", "Asia/Kolkata", "Asia/Dhaka", "Asia/Novosibirsk",
    "Asia/Bangkok", "Asia/Shanghai", "Asia/Singapore", "Australia/Perth", "Asia/Tokyo", "Asia/Seoul",
    "Asia/Vladivostok", "Australia/Brisbane", "Australia/Sydney", "Pacific/Auckland"};
#define ZONE_COUNT ((int)(sizeof(zones) / sizeof(zones[0])))

struct config {
    bool have_disk;
    struct storage_device_info disk;
    char hostname[INSTALL_HOSTNAME_MAX];
    char timezone[32];
    bool root_set;
    char root_hash[INSTALL_HASH_MAX];
    struct install_user users[INSTALL_USERS_MAX];
    uint32_t user_count;
    bool desktop;
    char userspace_url[256];
    char packages[MAX_TRUSTED][64];
    int package_count;
};

static struct config cfg;
static struct termios saved_tty;
static bool raw_on;
static int cols = 80, lines = 25;
static char trusted[MAX_TRUSTED][64];
static int trusted_count;
static bool userspace_in_conf;
static char key_char;

static void out(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        (void)write(1, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

static void raw_mode(bool on)
{
    if (on == raw_on)
        return;
    if (on) {
        if (tcgetattr(0, &saved_tty) != 0)
            return;
        struct termios t = saved_tty;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        out("\033[?1049h\033[?25l");
    } else {
        out("\033[0m\033[2J\033[H\033[?25h\033[?1049l");
        tcsetattr(0, TCSANOW, &saved_tty);
    }
    raw_on = on;
}

static void on_signal(int sig)
{
    raw_mode(false);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void update_size(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40 && ws.ws_row >= 15) {
        cols = ws.ws_col;
        lines = ws.ws_row;
    }
}

static bool byte_ready(int ms)
{
    struct pollfd p = {.fd = 0, .events = POLLIN};
    return poll(&p, 1, ms) > 0;
}

static enum key read_key(void)
{
    unsigned char c;
    if (read(0, &c, 1) != 1)
        return K_ESC;
    if (c == 27) {
        if (!byte_ready(60))
            return K_ESC;
        unsigned char a, b;
        if (read(0, &a, 1) != 1 || (a != '[' && a != 'O'))
            return K_ESC;
        if (read(0, &b, 1) != 1)
            return K_ESC;
        switch (b) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        default: break;
        }
        if (b >= '0' && b <= '9') {
            unsigned char t;
            while (read(0, &t, 1) == 1 && t != '~') {
            }
            if (b == '5')
                return K_PGUP;
            if (b == '6')
                return K_PGDN;
            if (b == '1' || b == '7')
                return K_HOME;
            if (b == '4' || b == '8')
                return K_END;
        }
        return K_NONE;
    }
    if (c == '\r' || c == '\n')
        return K_ENTER;
    if (c == 127 || c == 8)
        return K_BACKSPACE;
    if (c == '\t')
        return K_TAB;
    if (c == ' ')
        return K_SPACE;
    if (c >= 32 && c < 127) {
        key_char = (char)c;
        return K_CHAR;
    }
    return K_NONE;
}

static void human(uint64_t bytes, char *dst, size_t cap)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        u++;
    }
    snprintf(dst, cap, "%.1f %s", v, units[u]);
}

static void wipe(void *p, size_t n)
{
    volatile unsigned char *b = p;
    while (n--)
        *b++ = 0;
}

static void header(const char *title)
{
    update_size();
    out("\033[0m\033[2J\033[H\033[7m %-*.*s\033[0m\n", cols - 1, cols - 1, title);
}

static void wrap_text(int row, const char *text)
{
    int width = cols - 4;
    const char *p = text;
    while (*p && row < lines) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > width) {
            int cut = width;
            while (cut > 0 && p[cut] != ' ')
                cut--;
            len = cut ? cut : width;
        }
        out("\033[%d;3H\033[2m%.*s\033[0m", row++, len, p);
        p += len;
        if (*p == '\n' || *p == ' ')
            p++;
    }
}

static void footer(const char *help)
{
    out("\033[%d;1H\033[7m %-*.*s\033[0m", lines, cols - 1, cols - 1, help);
}

static bool contains_icase(const char *hay, const char *needle)
{
    if (!*needle)
        return true;
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n))
            return true;
    return false;
}

static int list_select(const char *title, const char *const *items, int count, int current, const char *info,
                       bool *checked)
{
    int sel = current >= 0 && current < count ? current : 0, top = 0;
    char filter[40] = "";
    bool searching = false;
    int *visible = malloc(sizeof(int) * (size_t)(count ? count : 1));
    for (;;) {
        int vis = 0;
        for (int i = 0; i < count; i++)
            if (contains_icase(items[i], filter))
                visible[vis++] = i;
        int pos = 0;
        for (int i = 0; i < vis; i++)
            if (visible[i] == sel)
                pos = i;
        if (vis && visible[pos] != sel)
            sel = visible[0];
        header(title);
        int info_rows = info ? 4 : 0;
        int room = lines - 5 - info_rows;
        if (room < 3)
            room = 3;
        if (pos < top)
            top = pos;
        if (pos >= top + room)
            top = pos - room + 1;
        int row = 3;
        if (searching || filter[0])
            out("\033[2;3HSearch: %s_", filter);
        for (int i = top; i < vis && i < top + room; i++) {
            int idx = visible[i];
            out("\033[%d;3H", row++);
            if (i == pos)
                out("\033[7m");
            if (checked)
                out(" [%c] %-*.*s", checked[idx] ? 'x' : ' ', cols - 12, cols - 12, items[idx]);
            else
                out(" %s %-*.*s", i == pos ? ">" : " ", cols - 10, cols - 10, items[idx]);
            out("\033[0m");
        }
        if (!vis)
            out("\033[3;3H(nothing matches)");
        if (top + room < vis)
            out("\033[%d;3H\033[2m  ... %d more\033[0m", row, vis - top - room);
        if (info)
            wrap_text(lines - info_rows, info);
        footer(checked ? "Up/Down move  Space toggle  Enter confirm  / search  Esc back"
                       : "Up/Down move  Enter select  / search  Esc back");
        enum key k = read_key();
        if (searching) {
            if (k == K_CHAR || k == K_SPACE) {
                size_t n = strlen(filter);
                if (n + 1 < sizeof(filter)) {
                    filter[n] = k == K_SPACE ? ' ' : key_char;
                    filter[n + 1] = 0;
                }
                continue;
            }
            if (k == K_BACKSPACE) {
                size_t n = strlen(filter);
                if (n)
                    filter[n - 1] = 0;
                else
                    searching = false;
                continue;
            }
            if (k == K_ESC) {
                searching = false;
                filter[0] = 0;
                continue;
            }
            if (k == K_ENTER && !checked) {
                searching = false;
            }
        }
        if (k == K_CHAR && key_char == '/') {
            searching = true;
            continue;
        }
        if (k == K_UP || (k == K_CHAR && key_char == 'k'))
            pos = pos > 0 ? pos - 1 : vis - 1;
        else if (k == K_DOWN || (k == K_CHAR && key_char == 'j'))
            pos = pos + 1 < vis ? pos + 1 : 0;
        else if (k == K_PGUP)
            pos = pos > room ? pos - room : 0;
        else if (k == K_PGDN)
            pos = pos + room < vis ? pos + room : vis - 1;
        else if (k == K_HOME)
            pos = 0;
        else if (k == K_END)
            pos = vis - 1;
        else if (k == K_SPACE && checked && vis)
            checked[visible[pos]] = !checked[visible[pos]];
        else if (k == K_ENTER && vis) {
            int r = visible[pos];
            free(visible);
            return r;
        } else if (k == K_ESC || (k == K_CHAR && key_char == 'q')) {
            free(visible);
            return -1;
        }
        if (vis && pos >= 0 && pos < vis)
            sel = visible[pos];
    }
}

static bool text_input(const char *title, const char *prompt, const char *info, char *value, size_t cap,
                       bool hidden)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", hidden ? "" : value);
    if (cap > sizeof(buf))
        cap = sizeof(buf);
    for (;;) {
        header(title);
        out("\033[3;3H%s", prompt);
        size_t n = strlen(buf);
        out("\033[5;3H\033[7m ");
        int width = cols - 8;
        size_t start = n > (size_t)width ? n - (size_t)width : 0;
        for (size_t i = start; i < n; i++)
            out("%c", hidden ? '*' : buf[i]);
        out("_%*s\033[0m", (int)(width - (n - start)), "");
        if (info)
            wrap_text(7, info);
        footer("Type text  Backspace delete  Enter accept  Esc cancel");
        enum key k = read_key();
        if (k == K_ENTER) {
            snprintf(value, cap, "%s", buf);
            wipe(buf, sizeof(buf));
            return true;
        }
        if (k == K_ESC) {
            wipe(buf, sizeof(buf));
            return false;
        }
        if (k == K_BACKSPACE && n)
            buf[n - 1] = 0;
        else if ((k == K_CHAR || k == K_SPACE) && n + 1 < cap) {
            buf[n] = k == K_SPACE ? ' ' : key_char;
            buf[n + 1] = 0;
        }
    }
}

static void message(const char *title, const char *text)
{
    header(title);
    wrap_text(3, text);
    footer("Press any key");
    read_key();
}

static bool confirm(const char *title, const char *question, bool def)
{
    const char *items[] = {"Yes", "No"};
    int r = list_select(title, items, 2, def ? 0 : 1, question, NULL);
    return r == 0;
}

static bool valid_host(char *s)
{
    size_t n = strlen(s);
    if (!n || n >= INSTALL_HOSTNAME_MAX || s[0] == '-' || s[n - 1] == '-')
        return false;
    for (char *p = s; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '.')
            return false;
    }
    return true;
}

static bool valid_user_name(const char *s)
{
    if (!*s || strlen(s) >= INSTALL_USER_MAX || !(islower((unsigned char)*s) || *s == '_'))
        return false;
    for (const char *p = s; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '_' && *p != '-')
            return false;
    return strcmp(s, "root") != 0;
}

static void load_trusted(void)
{
    trusted_count = 0;
    userspace_in_conf = false;
    FILE *f = fopen(PKG_CONF, "r");
    if (!f)
        return;
    char line[320];
    bool in = false;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == '[') {
            in = !strncmp(s, "[trusted]", 9);
            continue;
        }
        if (!in || *s == '#' || !strchr(s, '='))
            continue;
        char name[64];
        int n = 0;
        while (s[n] && s[n] != '=' && s[n] != ' ' && s[n] != '\t' && n < 63) {
            name[n] = s[n];
            n++;
        }
        name[n] = 0;
        if (!n)
            continue;
        if (!strcmp(name, USERSPACE_PKG)) {
            userspace_in_conf = true;
            continue;
        }
        if (trusted_count < MAX_TRUSTED)
            snprintf(trusted[trusted_count++], 64, "%s", name);
    }
    fclose(f);
}

static bool ask_password(const char *who, char *hash, size_t cap)
{
    char a[128] = "", b[128] = "", title[96], prompt[96];
    snprintf(title, sizeof(title), "Password for %s", who);
    for (;;) {
        snprintf(prompt, sizeof(prompt), "Enter the password for %s:", who);
        if (!text_input(title, prompt, NULL, a, sizeof(a), true))
            return false;
        if (!a[0]) {
            message(title, "The password must not be empty.");
            continue;
        }
        if (!text_input(title, "Repeat the password:", NULL, b, sizeof(b), true)) {
            wipe(a, sizeof(a));
            return false;
        }
        if (strcmp(a, b) != 0) {
            wipe(a, sizeof(a));
            wipe(b, sizeof(b));
            message(title, "The passwords do not match, try again.");
            continue;
        }
        bool ok = account_hash_password(a, hash, (uint32_t)cap);
        wipe(a, sizeof(a));
        wipe(b, sizeof(b));
        if (!ok)
            message(title, "Cannot hash the password.");
        return ok;
    }
}

static const char *transport_name(uint8_t t)
{
    switch (t) {
    case 1: return "ATA";
    case 2: return "SATA";
    case 3: case 4: return "USB";
    default: return "?";
    }
}

static void disk_text(const struct storage_device_info *d, char *dst, size_t cap)
{
    char size[32];
    human(d->sector_count * d->sector_size, size, sizeof(size));
    snprintf(dst, cap, "%-6s %10s  %-4s  %s", d->name, size, transport_name(d->transport), d->model);
}

static void menu_disk(void)
{
    struct storage_device_info list[MAX_DISKS];
    int32_t n = pc_list_disks(list, MAX_DISKS);
    char text[MAX_DISKS][128];
    const char *items[MAX_DISKS];
    struct storage_device_info usable[MAX_DISKS];
    int count = 0, current = 0;
    for (int i = 0; i < n && i < MAX_DISKS; i++) {
        if (!list[i].operational || !list[i].writable || list[i].sector_size != 512)
            continue;
        usable[count] = list[i];
        disk_text(&list[i], text[count], sizeof(text[0]));
        if (cfg.have_disk && !strcmp(cfg.disk.name, list[i].name))
            current = count;
        items[count] = text[count];
        count++;
    }
    if (!count) {
        message("Disk configuration", "No writable disk with 512-byte sectors was found.");
        return;
    }
    int r = list_select("Disk configuration: select the disk to install to", items, count, current,
                        "The whole disk is erased and laid out automatically (best-effort default layout):\n"
                        "GPT, EFI system partition with the Limine boot loader, ext2 root on the rest, and a swap "
                        "partition (1/8 of the disk, at most 2 GiB) on ATA/SATA disks.",
                        NULL);
    if (r < 0)
        return;
    cfg.disk = usable[r];
    cfg.have_disk = true;
}

static void menu_hostname(void)
{
    char v[INSTALL_HOSTNAME_MAX];
    snprintf(v, sizeof(v), "%s", cfg.hostname);
    for (;;) {
        if (!text_input("Hostname", "Name of the computer:", "Letters, digits and '-'.", v, sizeof(v), false))
            return;
        if (valid_host(v)) {
            snprintf(cfg.hostname, sizeof(cfg.hostname), "%s", v);
            return;
        }
        message("Hostname", "Only letters, digits, '-' and '.', not starting or ending with '-', up to 63 characters.");
    }
}

static void menu_timezone(void)
{
    int current = 0;
    for (int i = 0; i < ZONE_COUNT; i++)
        if (!strcmp(zones[i], cfg.timezone))
            current = i;
    int r = list_select("Timezone", zones, ZONE_COUNT, current, "Press / and type to search, e.g. kyiv.", NULL);
    if (r >= 0)
        snprintf(cfg.timezone, sizeof(cfg.timezone), "%s", zones[r]);
}

static void menu_root_password(void)
{
    char hash[INSTALL_HASH_MAX];
    if (ask_password("root", hash, sizeof(hash))) {
        snprintf(cfg.root_hash, sizeof(cfg.root_hash), "%s", hash);
        cfg.root_set = true;
    }
    wipe(hash, sizeof(hash));
}

static void add_user(void)
{
    if (cfg.user_count >= INSTALL_USERS_MAX) {
        message("User accounts", "At most 8 users can be created by the installer.");
        return;
    }
    struct install_user u;
    memset(&u, 0, sizeof(u));
    char name[INSTALL_USER_MAX] = "";
    for (;;) {
        if (!text_input("Add a user", "User name:", "Lower-case letters, digits, '_' and '-', starting with a letter.",
                        name, sizeof(name), false))
            return;
        bool dup = false;
        for (uint32_t i = 0; i < cfg.user_count; i++)
            if (!strcmp(cfg.users[i].name, name))
                dup = true;
        if (valid_user_name(name) && !dup)
            break;
        message("Add a user", dup ? "This user already exists." : "This is not a valid user name (root is taken).");
    }
    snprintf(u.name, sizeof(u.name), "%s", name);
    char gecos[INSTALL_GECOS_MAX] = "";
    if (text_input("Add a user", "Full name (optional):", NULL, gecos, sizeof(gecos), false))
        for (char *p = gecos; *p; p++)
            if (*p == ':')
                *p = ' ';
    snprintf(u.gecos, sizeof(u.gecos), "%s", gecos[0] ? gecos : u.name);
    if (!ask_password(u.name, u.hash, sizeof(u.hash)))
        return;
    char q[96];
    snprintf(q, sizeof(q), "Should \"%s\" be a superuser (sudo, group wheel)?", u.name);
    u.admin = confirm("Add a user", q, true) ? 1 : 0;
    cfg.users[cfg.user_count++] = u;
}

static void edit_user(uint32_t idx)
{
    struct install_user *u = &cfg.users[idx];
    char title[64];
    snprintf(title, sizeof(title), "User %s", u->name);
    const char *items[] = {"Change password", u->admin ? "Demote (remove from wheel)" : "Promote to superuser (wheel)",
                           "Delete user", "Back"};
    int r = list_select(title, items, 4, 0, NULL, NULL);
    if (r == 0) {
        char hash[INSTALL_HASH_MAX];
        if (ask_password(u->name, hash, sizeof(hash)))
            snprintf(u->hash, sizeof(u->hash), "%s", hash);
        wipe(hash, sizeof(hash));
    } else if (r == 1) {
        u->admin = !u->admin;
    } else if (r == 2) {
        for (uint32_t i = idx; i + 1 < cfg.user_count; i++)
            cfg.users[i] = cfg.users[i + 1];
        cfg.user_count--;
        memset(&cfg.users[cfg.user_count], 0, sizeof(cfg.users[0]));
    }
}

static void menu_users(void)
{
    for (;;) {
        char text[INSTALL_USERS_MAX + 2][96];
        const char *items[INSTALL_USERS_MAX + 2];
        int n = 0;
        snprintf(text[n], sizeof(text[0]), "Add a user");
        items[n] = text[n];
        n++;
        for (uint32_t i = 0; i < cfg.user_count; i++) {
            snprintf(text[n], sizeof(text[0]), "%-20s %-12s %s", cfg.users[i].name,
                     cfg.users[i].admin ? "superuser" : "user", cfg.users[i].gecos);
            items[n] = text[n];
            n++;
        }
        snprintf(text[n], sizeof(text[0]), "Confirm and exit");
        items[n] = text[n];
        n++;
        int r = list_select("User accounts", items, n, 0,
                            "Superusers are put into the group wheel and may use sudo. No users is fine: then only "
                            "root can log in.",
                            NULL);
        if (r < 0 || r == n - 1)
            return;
        if (r == 0)
            add_user();
        else
            edit_user((uint32_t)(r - 1));
    }
}

static void menu_profile(void)
{
    const char *items[] = {"Minimal  - text consoles only", "Desktop  - graphical user space from GitHub"};
    int r = list_select("Profile", items, 2, cfg.desktop ? 1 : 0,
                        "Desktop installs the package \"userspace\" (display server and programs) from its GitHub "
                        "repository after the base system and starts graphical.target. It needs a network.",
                        NULL);
    if (r < 0)
        return;
    if (r == 0) {
        cfg.desktop = false;
        return;
    }
    if (!userspace_in_conf) {
        char url[256];
        snprintf(url, sizeof(url), "%s", cfg.userspace_url[0] ? cfg.userspace_url : "https://github.com/");
        for (;;) {
            if (!text_input("Profile: Desktop", "GitHub repository of the user space:",
                            "\"userspace\" is not in [trusted] of " PKG_CONF ". Give its address, it is added to the "
                            "installed system as trusted.",
                            url, sizeof(url), false))
                return;
            if (!strncmp(url, "https://github.com/", 19) && strchr(url + 19, '/') && url[strlen(url) - 1] != '/')
                break;
            message("Profile: Desktop", "Give an address like https://github.com/OWNER/REPO");
        }
        snprintf(cfg.userspace_url, sizeof(cfg.userspace_url), "%s", url);
    }
    cfg.desktop = true;
}

static void menu_packages(void)
{
    if (!trusted_count) {
        message("Additional packages", "There are no trusted repositories in " PKG_CONF
                                       ". Add lines like \"vim = https://github.com/OWNER/REPO\" under [trusted] "
                                       "(or install packages later with pkg).");
        return;
    }
    bool checked[MAX_TRUSTED] = {0};
    const char *items[MAX_TRUSTED];
    for (int i = 0; i < trusted_count; i++) {
        items[i] = trusted[i];
        for (int k = 0; k < cfg.package_count; k++)
            if (!strcmp(cfg.packages[k], trusted[i]))
                checked[i] = true;
    }
    int r = list_select("Additional packages", items, trusted_count, 0,
                        "Packages come from the trusted GitHub repositories in " PKG_CONF
                        " and are installed with pkg after the base system.",
                        checked);
    if (r < 0)
        return;
    cfg.package_count = 0;
    for (int i = 0; i < trusted_count; i++)
        if (checked[i])
            snprintf(cfg.packages[cfg.package_count++], 64, "%s", trusted[i]);
}

static const char *missing_text(char *dst, size_t cap)
{
    dst[0] = 0;
    if (!cfg.have_disk)
        strncat(dst, "disk, ", cap - strlen(dst) - 1);
    if (!cfg.root_set)
        strncat(dst, "root password, ", cap - strlen(dst) - 1);
    for (uint32_t i = 0; i < cfg.user_count; i++)
        if (!cfg.users[i].hash[0]) {
            strncat(dst, "password of ", cap - strlen(dst) - 1);
            strncat(dst, cfg.users[i].name, cap - strlen(dst) - 1);
            strncat(dst, ", ", cap - strlen(dst) - 1);
        }
    size_t n = strlen(dst);
    if (n >= 2)
        dst[n - 2] = 0;
    return dst;
}

static bool save_config(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "# PureC OS installer configuration (no passwords)\n");
    if (cfg.have_disk)
        fprintf(f, "disk = %s\n", cfg.disk.name);
    fprintf(f, "hostname = %s\ntimezone = %s\nprofile = %s\n", cfg.hostname, cfg.timezone,
            cfg.desktop ? "desktop" : "minimal");
    if (cfg.userspace_url[0])
        fprintf(f, "userspace = %s\n", cfg.userspace_url);
    for (uint32_t i = 0; i < cfg.user_count; i++)
        fprintf(f, "user = %s %s %s\n", cfg.users[i].name, cfg.users[i].admin ? "superuser" : "user",
                cfg.users[i].gecos);
    for (int i = 0; i < cfg.package_count; i++)
        fprintf(f, "package = %s\n", cfg.packages[i]);
    fclose(f);
    return true;
}

static void menu_save(void)
{
    char path[160] = DEFAULT_SAVE;
    if (!text_input("Save configuration", "File:",
                    "Passwords are not saved. Start the installer with --config FILE to load it.", path, sizeof(path),
                    false))
        return;
    message("Save configuration", save_config(path) ? "Saved." : "Cannot write the file.");
}

static int load_config(const char *path, char *report, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[320];
    report[0] = 0;
    struct storage_device_info list[MAX_DISKS];
    int32_t disks = pc_list_disks(list, MAX_DISKS);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq)
            continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        while (*val == ' ')
            val++;
        size_t kl = strlen(key);
        while (kl && key[kl - 1] == ' ')
            key[--kl] = 0;
        if (!strcmp(key, "disk")) {
            for (int i = 0; i < disks && i < MAX_DISKS; i++)
                if (!strcmp(list[i].name, val)) {
                    cfg.disk = list[i];
                    cfg.have_disk = true;
                }
        } else if (!strcmp(key, "hostname") && valid_host(val)) {
            snprintf(cfg.hostname, sizeof(cfg.hostname), "%s", val);
        } else if (!strcmp(key, "timezone")) {
            for (int i = 0; i < ZONE_COUNT; i++)
                if (!strcmp(zones[i], val))
                    snprintf(cfg.timezone, sizeof(cfg.timezone), "%s", val);
        } else if (!strcmp(key, "profile")) {
            cfg.desktop = !strcmp(val, "desktop");
        } else if (!strcmp(key, "userspace")) {
            snprintf(cfg.userspace_url, sizeof(cfg.userspace_url), "%s", val);
        } else if (!strcmp(key, "package") && cfg.package_count < MAX_TRUSTED) {
            snprintf(cfg.packages[cfg.package_count++], 64, "%s", val);
        } else if (!strcmp(key, "user") && cfg.user_count < INSTALL_USERS_MAX) {
            char name[64] = "", role[16] = "";
            int used = 0;
            if (sscanf(val, "%63s %15s %n", name, role, &used) >= 1 && valid_user_name(name)) {
                struct install_user *u = &cfg.users[cfg.user_count++];
                memset(u, 0, sizeof(*u));
                snprintf(u->name, sizeof(u->name), "%s", name);
                u->admin = !strcmp(role, "superuser");
                snprintf(u->gecos, sizeof(u->gecos), "%s", used && val[used] ? val + used : name);
                size_t rl = strlen(report);
                snprintf(report + rl, cap - rl, "%s%s", rl ? ", " : "", name);
            }
        }
    }
    fclose(f);
    return 0;
}

static int run(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void progress_bar(uint32_t progress, const char *stage)
{
    int filled = (int)(progress > 100 ? 100 : progress) * 30 / 100;
    char b[31];
    for (int i = 0; i < 30; i++)
        b[i] = i < filled ? '#' : '.';
    b[30] = 0;
    printf("\r[%s] %3u%% %-48.48s", b, progress, stage);
    fflush(stdout);
}

static bool kernel_install(void)
{
    struct install_account_request acc;
    memset(&acc, 0, sizeof(acc));
    acc.present = 1;
    acc.user_count = cfg.user_count;
    snprintf(acc.hostname, sizeof(acc.hostname), "%s", cfg.hostname);
    snprintf(acc.timezone, sizeof(acc.timezone), "%s", cfg.timezone);
    snprintf(acc.root_hash, sizeof(acc.root_hash), "%s", cfg.root_hash);
    for (uint32_t i = 0; i < cfg.user_count; i++) {
        acc.users[i] = cfg.users[i];
        acc.users[i].uid = INSTALL_FIRST_UID + i;
    }
    int32_t rc = pc_install_start_accounts(cfg.disk.name, cfg.disk.serial, FS_TYPE_EXT2, &acc);
    wipe(&acc, sizeof(acc));
    if (rc < 0) {
        printf("The kernel refused to start the installation (error %d).\n", rc);
        if (rc == -10)
            printf("Start the installer as /sbin/installer: only that path may write whole disks.\n");
        else if (rc == -12)
            printf("Another installation is already running.\n");
        else if (rc == -14)
            printf("The kernel rejected the accounts (hostname, password hash or a user).\n");
        return false;
    }
    struct install_status st;
    char last[INSTALL_STAGE_CAPACITY] = "";
    for (;;) {
        if (pc_install_status(&st)) {
            if (strcmp(st.stage, last) != 0 || st.state >= 2) {
                snprintf(last, sizeof(last), "%s", st.stage);
                progress_bar(st.progress, st.stage);
            }
            if (st.state == 2) {
                printf("\n");
                return true;
            }
            if (st.state == 3) {
                printf("\nInstallation failed: %s (error %d)\n", st.stage, st.result);
                struct install_log log;
                if (pc_install_log(&log))
                    for (uint32_t i = 0; i < log.count && i < INSTALL_LOG_CAPACITY; i++)
                        printf("  %3u%% %s\n", log.entries[i].progress, log.entries[i].stage);
                return false;
            }
        }
        usleep(200000);
    }
}

static bool add_trusted_line(const char *name, const char *url)
{
    FILE *f = fopen(PKG_CONF, "r");
    bool has = false;
    if (f) {
        char line[320];
        while (fgets(line, sizeof(line), f))
            if (!strncmp(line, "[trusted]", 9))
                has = true;
        fclose(f);
    }
    f = fopen(PKG_CONF, "a");
    if (!f)
        return false;
    if (!has)
        fprintf(f, "\n[trusted]\n");
    fprintf(f, "%s = %s\n", name, url);
    fclose(f);
    return true;
}

static bool install_packages(void)
{
    char *argv[MAX_TRUSTED + 8];
    int n = 0;
    argv[n++] = "/usr/bin/pkg";
    argv[n++] = "-S";
    argv[n++] = "--noconfirm";
    argv[n++] = "--needed";
    if (cfg.desktop)
        argv[n++] = USERSPACE_PKG;
    for (int i = 0; i < cfg.package_count; i++)
        argv[n++] = cfg.packages[i];
    argv[n] = NULL;
    if (n == 4)
        return true;
    char *refresh[] = {"/usr/bin/pkg", "-Sy", NULL};
    if (run(refresh) != 0)
        printf("Some trusted repositories could not be checked.\n");
    return run(argv) == 0;
}

static int do_install(void)
{
    raw_mode(false);
    printf("PureC OS installer\n\n== Installing the base system to %s ==\n", cfg.disk.name);
    if (!kernel_install()) {
        printf("\nNothing more was done. Press Enter.\n");
        getchar();
        return 1;
    }
    bool warnings = false;
    printf("\n== Boot loader (root=UUID=) ==\n");
    char *mkinit[] = {"/sbin/mkinitcpio", "-u", NULL};
    if (run(mkinit) != 0) {
        warnings = true;
        printf("WARNING: mkinitcpio failed; the installed system may boot the live initramfs.\n"
               "After booting it, run: mkinitcpio -u\n");
    }
    if (cfg.desktop && cfg.userspace_url[0] && !add_trusted_line(USERSPACE_PKG, cfg.userspace_url)) {
        warnings = true;
        printf("WARNING: cannot add %s to " PKG_CONF "\n", USERSPACE_PKG);
    }
    bool desktop_ok = cfg.desktop;
    if (cfg.desktop || cfg.package_count) {
        printf("\n== Packages ==\n");
        if (!install_packages()) {
            warnings = true;
            desktop_ok = false;
            printf("WARNING: packages were not installed. After booting run: pkg -Sy, then pkg -S <names>\n");
        }
    }
    mkdir("/etc/init", 0755);
    FILE *f = fopen("/etc/init/default.target", "w");
    if (f) {
        fprintf(f, "%s\n", desktop_ok ? "graphical.target" : "multi-user.target");
        fclose(f);
    }
    save_config("/root/purec-install.conf");
    sync();
    printf("\nInstallation %s.\n", warnings ? "finished with warnings" : "complete");
    printf("Reboot now? [Y/n] ");
    fflush(stdout);
    char line[8] = "";
    if (!fgets(line, sizeof(line), stdin) || line[0] != 'n') {
        char *rb[] = {"/sbin/reboot", NULL};
        run(rb);
    }
    return warnings ? 2 : 0;
}

static bool final_confirm(void)
{
    char text[1024], size[32];
    human(cfg.disk.sector_count * cfg.disk.sector_size, size, sizeof(size));
    int n = snprintf(text, sizeof(text),
                     "ALL DATA on %s (%s, %s) will be erased.\n\nHostname %s, timezone %s, profile %s, %u user(s)",
                     cfg.disk.name, cfg.disk.model, size, cfg.hostname, cfg.timezone,
                     cfg.desktop ? "desktop" : "minimal", cfg.user_count);
    if (cfg.package_count && n > 0 && (size_t)n < sizeof(text)) {
        n += snprintf(text + n, sizeof(text) - (size_t)n, ", packages:");
        for (int i = 0; i < cfg.package_count && (size_t)n < sizeof(text); i++)
            n += snprintf(text + n, sizeof(text) - (size_t)n, " %s", cfg.packages[i]);
    }
    if ((size_t)n < sizeof(text))
        snprintf(text + n, sizeof(text) - (size_t)n, ".\n\nStart the installation?");
    return confirm("Install", text, false);
}

enum item { I_DISK, I_HOST, I_ROOT, I_USERS, I_PROFILE, I_PACKAGES, I_TZ, I_SEP, I_SAVE, I_INSTALL, I_ABORT,
            I_COUNT };

static const char *const item_names[I_COUNT] = {"Disk configuration", "Hostname", "Root password", "User accounts",
                                                 "Profile", "Additional packages", "Timezone", "",
                                                 "Save configuration", "Install", "Abort"};

static const char *const item_info[I_COUNT] = {
    "Select the disk. It is erased and gets a GPT with an EFI partition (Limine), an ext2 root and swap on ATA/SATA "
    "disks.",
    "The name of the computer, written to /etc/hostname.",
    "Password of the root account. Required.",
    "Add, change or delete users. Superusers are put into the group wheel and may use sudo.",
    "Minimal: text consoles. Desktop: also installs the graphical user space from GitHub.",
    "Extra packages from the trusted GitHub repositories in " PKG_CONF ".",
    "The time zone of the installed system.",
    "",
    "Write these choices (without passwords) to a file; load it with installer --config FILE.",
    "Erase the disk and install PureC OS with these settings.",
    "Leave without changing anything."};

static void item_value(int i, char *dst, size_t cap, bool *bad)
{
    *bad = false;
    dst[0] = 0;
    switch (i) {
    case I_DISK:
        if (cfg.have_disk) {
            char size[32];
            human(cfg.disk.sector_count * cfg.disk.sector_size, size, sizeof(size));
            snprintf(dst, cap, "%s %s  wipe, default layout", cfg.disk.name, size);
        } else {
            snprintf(dst, cap, "not selected");
            *bad = true;
        }
        break;
    case I_HOST: snprintf(dst, cap, "%s", cfg.hostname); break;
    case I_ROOT:
        snprintf(dst, cap, "%s", cfg.root_set ? "set" : "not set");
        *bad = !cfg.root_set;
        break;
    case I_USERS:
        if (!cfg.user_count) {
            snprintf(dst, cap, "none (only root)");
        } else {
            size_t used = 0;
            for (uint32_t u = 0; u < cfg.user_count && used < cap; u++)
                used += (size_t)snprintf(dst + used, cap - used, "%s%s%s", u ? ", " : "", cfg.users[u].name,
                                         cfg.users[u].admin ? " (superuser)" : "");
        }
        break;
    case I_PROFILE: snprintf(dst, cap, "%s", cfg.desktop ? "Desktop" : "Minimal"); break;
    case I_PACKAGES:
        if (!cfg.package_count) {
            snprintf(dst, cap, "none");
        } else {
            size_t used = 0;
            for (int k = 0; k < cfg.package_count && used < cap; k++)
                used += (size_t)snprintf(dst + used, cap - used, "%s%s", k ? " " : "", cfg.packages[k]);
        }
        break;
    case I_TZ: snprintf(dst, cap, "%s", cfg.timezone); break;
    default: break;
    }
}

static int main_menu(void)
{
    int sel = 0;
    for (;;) {
        header("PureC OS installer");
        int label = 24;
        int row = 3;
        for (int i = 0; i < I_COUNT; i++, row++) {
            if (i == I_SEP) {
                out("\033[%d;3H\033[2m%.*s\033[0m", row, cols - 6,
                    "----------------------------------------------------------------------------------------------");
                continue;
            }
            char value[160];
            bool bad;
            item_value(i, value, sizeof(value), &bad);
            out("\033[%d;3H", row);
            if (i == sel)
                out("\033[7m");
            out(" %s %-*s", i == sel ? ">" : " ", label, item_names[i]);
            if (i == sel)
                out("\033[0m");
            if (value[0])
                out("  %s%-.*s\033[0m", bad ? "\033[31m" : "\033[32m", cols - label - 10, value);
        }
        char missing[96];
        missing_text(missing, sizeof(missing));
        row++;
        if (missing[0])
            out("\033[%d;3H\033[31mMissing before Install: %s\033[0m", row++, missing);
        out("\033[%d;3H\033[1m%s\033[0m", row + 1, item_names[sel]);
        wrap_text(row + 2, item_info[sel]);
        footer("Up/Down move  Enter select  Esc/q abort");
        enum key k = read_key();
        if (k == K_UP || (k == K_CHAR && key_char == 'k')) {
            sel = sel > 0 ? sel - 1 : I_COUNT - 1;
            if (sel == I_SEP)
                sel--;
        } else if (k == K_DOWN || (k == K_CHAR && key_char == 'j')) {
            sel = sel + 1 < I_COUNT ? sel + 1 : 0;
            if (sel == I_SEP)
                sel++;
        } else if (k == K_HOME) {
            sel = 0;
        } else if (k == K_END) {
            sel = I_COUNT - 1;
        } else if (k == K_ESC || (k == K_CHAR && key_char == 'q')) {
            if (confirm("Abort", "Leave the installer? Nothing has been changed.", false))
                return 1;
        } else if (k == K_ENTER) {
            switch (sel) {
            case I_DISK: menu_disk(); break;
            case I_HOST: menu_hostname(); break;
            case I_ROOT: menu_root_password(); break;
            case I_USERS: menu_users(); break;
            case I_PROFILE: menu_profile(); break;
            case I_PACKAGES: menu_packages(); break;
            case I_TZ: menu_timezone(); break;
            case I_SAVE: menu_save(); break;
            case I_ABORT:
                if (confirm("Abort", "Leave the installer? Nothing has been changed.", false))
                    return 1;
                break;
            case I_INSTALL:
                if (missing[0]) {
                    char msg[160];
                    snprintf(msg, sizeof(msg), "Set these first: %s.", missing);
                    message("Install", msg);
                } else if (final_confirm()) {
                    return do_install();
                }
                break;
            default: break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (getuid() != 0) {
        fprintf(stderr, "installer: run it as root\n");
        return 1;
    }
    snprintf(cfg.hostname, sizeof(cfg.hostname), "purec");
    snprintf(cfg.timezone, sizeof(cfg.timezone), "UTC");
    load_trusted();
    char loaded[256] = "";
    const char *config = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            config = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: installer [--config FILE]\n");
            return 0;
        } else {
            fprintf(stderr, "installer: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    if (config && load_config(config, loaded, sizeof(loaded)) < 0) {
        fprintf(stderr, "installer: cannot read %s\n", config);
        return 1;
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "installer: needs a terminal\n");
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    raw_mode(true);
    if (loaded[0]) {
        char msg[400];
        snprintf(msg, sizeof(msg),
                 "Loaded %s. Users without a password: %s. Set their passwords in User accounts "
                 "(Change password) and the root password before Install.",
                 config, loaded);
        message("Configuration loaded", msg);
    }
    int rc = main_menu();
    raw_mode(false);
    return rc;
}
