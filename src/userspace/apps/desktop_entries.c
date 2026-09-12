#include "desktop_entries.h"
#include "../../fs/vfs.h"
#include "../../fs/types/fs_types.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"

#define ENTRY_FILE_MAX 4096
#define DIR_CAP 32
#define GRID_X0 40u
#define GRID_DX 72u
#define GRID_Y0 48u
#define GRID_DY 82u
#define GRID_PER_ROW 6u

static struct desktop_entry g_entries[DESKTOP_ENTRY_MAX];
static uint32_t g_count;

static void copy_trunc(char *dst, uint32_t cap, const char *src,
                       uint32_t len) {
    if (!dst || !cap) return;
    if (len >= cap) len = cap - 1;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
    dst[len] = '\0';
}

static const char *trim_l(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    return p;
}

static const char *trim_r(const char *p, const char *end) {
    while (end > p) {
        char c = end[-1];
        if (c == ' ' || c == '\t' || c == '\r') end--;
        else break;
    }
    return end;
}

static bool eq_ci(const char *a, uint32_t alen, const char *b) {
    uint32_t blen = (uint32_t)strlen(b);
    if (alen != blen) return false;
    for (uint32_t i = 0; i < alen; i++) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return true;
}

static bool parse_color(const char *p, uint32_t len, uint32_t *out) {
    while (len && (*p == ' ' || *p == '\t' || *p == '\r')) { p++; len--; }
    while (len && (p[len-1] == ' ' || p[len-1] == '\t' || p[len-1] == '\r')) len--;
    if (!len) return false;
    uint32_t base = 10;
    if (len > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2; len -= 2; base = 16;
    } else if (len > 1 && p[0] == '#') {
        p += 1; len -= 1; base = 16;
    }
    if (!len) return false;
    uint32_t value = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = p[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return false;
        if (d >= base) return false;
        value = (value * base + d) & 0xFFFFFFu;
    }
    *out = value;
    return true;
}

bool desktop_entry_parse(const char *data, uint32_t size,
                         struct desktop_entry *out) {
    if (!data || !out) return false;
    memset(out, 0, sizeof(*out));
    out->icon_color = 0x89B4FAu;
    bool in_section = false;
    bool seen_section = false;
    uint32_t pos = 0;
    while (pos <= size) {
        uint32_t end = pos;
        while (end < size && data[end] != '\n') end++;
        const char *l = trim_l(data + pos, data + end);
        const char *r = trim_r(l, data + end);
        uint32_t llen = (r > l) ? (uint32_t)(r - l) : 0;
        pos = end + 1;
        if (!llen || l[0] == '#' || l[0] == ';') continue;
        if (l[0] == '[') {
            if (eq_ci(l, llen, "[Desktop Entry]") ||
                eq_ci(l, llen, "[PureC Entry]")) {
                in_section = true;
                seen_section = true;
            } else {
                in_section = false;
            }
            continue;
        }
        if (!in_section) continue;
        uint32_t eq = llen;
        for (uint32_t i = 0; i < llen; i++) {
            if (l[i] == '=') { eq = i; break; }
        }
        if (eq >= llen) continue;
        const char *kl = trim_l(l, l + eq);
        const char *kr = trim_r(kl, l + eq);
        const char *vl = trim_l(l + eq + 1, r);
        const char *vr = trim_r(vl, r);
        uint32_t klen = (kr > kl) ? (uint32_t)(kr - kl) : 0;
        uint32_t vlen = (vr > vl) ? (uint32_t)(vr - vl) : 0;
        if (!klen) continue;
        if (eq_ci(kl, klen, "Name")) {
            copy_trunc(out->name, sizeof(out->name), vl, vlen);
        } else if (eq_ci(kl, klen, "Exec")) {
            uint32_t n = vlen;
            for (uint32_t i = 0; i < vlen; i++) {
                if (vl[i] == ' ' || vl[i] == '\t') { n = i; break; }
            }
            copy_trunc(out->exec, sizeof(out->exec), vl, n);
        } else if (eq_ci(kl, klen, "Builtin")) {
            copy_trunc(out->builtin, sizeof(out->builtin), vl, vlen);
        } else if (eq_ci(kl, klen, "Icon")) {
            bool is_path = false;
            for (uint32_t i = 0; i < vlen; i++) {
                if (vl[i] == '/') { is_path = true; break; }
            }
            if (is_path) {
                copy_trunc(out->icon_path, sizeof(out->icon_path), vl, vlen);
            } else if (!out->icon_text[0]) {
                copy_trunc(out->icon_text, sizeof(out->icon_text), vl, vlen);
            }
        } else if (eq_ci(kl, klen, "IconColor") || eq_ci(kl, klen, "Color")) {
            uint32_t c = 0;
            if (parse_color(vl, vlen, &c)) out->icon_color = c;
        } else if (eq_ci(kl, klen, "IconText") || eq_ci(kl, klen, "Symbol")) {
            copy_trunc(out->icon_text, sizeof(out->icon_text), vl, vlen);
        }
    }
    if (!seen_section || !out->name[0]) return false;
    return out->exec[0] || out->builtin[0];
}

static bool has_desktop_suffix(const char *name) {
    uint32_t n = (uint32_t)strlen(name);
    if (n < 8) return false;
    const char *s = name + n - 8;
    return eq_ci(s, 8, ".desktop");
}

static uint8_t g_file_buf[ENTRY_FILE_MAX];

static int32_t read_whole_file(const char *path, uint32_t *out_size) {
    if (out_size) *out_size = 0;
    if (!vfs_is_root_mounted()) return -1;
    filesystem_syscall_lock();
    int32_t fd = vfs_open(path);
    uint32_t total = 0;
    if (fd >= 0) {
        for (;;) {
            if (total >= sizeof(g_file_buf)) break;
            int32_t n = vfs_read(fd, g_file_buf + total,
                                 (uint32_t)(sizeof(g_file_buf) - total));
            if (n <= 0) break;
            total += (uint32_t)n;
        }
        (void)vfs_close(fd);
    }
    filesystem_syscall_unlock();
    if (fd < 0 || !total) return -1;
    if (out_size) *out_size = total;
    return 0;
}

struct fallback_def {
    const char *file;
    const char *name;
    const char *exec;
    const char *builtin;
    uint32_t color;
    const char *symbol;
};

static const struct fallback_def kFallback[] = {
    {"files.desktop", "Files", "/bin/program/files", "", 0xF9E2AFu, "Files"},
    {"monitor.desktop", "HTOP", "/bin/program/monitor", "", 0x89B4FAu, "HTOP"},
    {"terminal.desktop", "Terminal", "/bin/program/terminal", "", 0x1E1E2Eu, ">_"},
    {"clock.desktop", "Clock", "", "clock", 0x89DCEBu, "12"},
    {"calc.desktop", "Calc", "", "calc", 0xA6E3A1u, "+"},
    {"calendar.desktop", "Calendar", "", "calendar", 0xF9E2AFu, "28"},
    {"settings.desktop", "Settings", "/bin/program/settings", "", 0x94E2D5u, "{}"},
    {"install.desktop", "Install", "/bin/installer", "", 0xCBA6F7u, "OS"},
    {"disks.desktop", "Disks", "/bin/program/disks", "", 0xF9E2AFu, "HD"},
    {"tetris.desktop", "Tetris", "/bin/program/tetris", "", 0xF38BA8u, "[]"},
    {"logview.desktop", "Logs", "/bin/program/logview", "", 0x89B4FAu, "LOG"},
    {"hexedit.desktop", "HexEdit", "/bin/program/hexedit", "", 0xF5C2E7u, "HX"},
};

static void apply_fallback(void) {
    uint32_t n = (uint32_t)(sizeof(kFallback) / sizeof(kFallback[0]));
    if (n > DESKTOP_ENTRY_MAX) n = DESKTOP_ENTRY_MAX;
    for (uint32_t i = 0; i < n; i++) {
        struct desktop_entry *e = &g_entries[i];
        memset(e, 0, sizeof(*e));
        copy_trunc(e->name, sizeof(e->name), kFallback[i].name,
                   (uint32_t)strlen(kFallback[i].name));
        copy_trunc(e->exec, sizeof(e->exec), kFallback[i].exec,
                   (uint32_t)strlen(kFallback[i].exec));
        copy_trunc(e->builtin, sizeof(e->builtin), kFallback[i].builtin,
                   (uint32_t)strlen(kFallback[i].builtin));
        copy_trunc(e->icon_text, sizeof(e->icon_text), kFallback[i].symbol,
                   (uint32_t)strlen(kFallback[i].symbol));
        e->icon_color = kFallback[i].color;
        const char *f = kFallback[i].file;
        uint32_t fl = (uint32_t)strlen(f);
        if (fl > 8) copy_trunc(e->id, sizeof(e->id), f, fl - 8);
        else copy_trunc(e->id, sizeof(e->id), f, fl);
        e->x = GRID_X0 + (i % GRID_PER_ROW) * GRID_DX;
        e->y = GRID_Y0 + (i / GRID_PER_ROW) * GRID_DY;
        e->hidden = false;
    }
    g_count = n;
}

static void assign_grid(void) {
    for (uint32_t i = 0; i < g_count; i++) {
        g_entries[i].x = GRID_X0 + (i % GRID_PER_ROW) * GRID_DX;
        g_entries[i].y = GRID_Y0 + (i / GRID_PER_ROW) * GRID_DY;
    }
}

void desktop_entries_rescan(void) {
    memset(g_entries, 0, sizeof(g_entries));
    g_count = 0;
    static struct fs_directory_entry_long dir[DIR_CAP];
    memset(dir, 0, sizeof(dir));
    filesystem_syscall_lock();
    int32_t n = vfs_list_long(DESKTOP_ENTRY_SCAN_DIR, dir, DIR_CAP);
    filesystem_syscall_unlock();
    if (n <= 0) {
        apply_fallback();
        return;
    }
    if (n > (int32_t)DIR_CAP) n = (int32_t)DIR_CAP;
    // Stable order: insertion sort by name.
    for (int32_t i = 1; i < n; i++) {
        struct fs_directory_entry_long t = dir[i];
        int32_t j = i - 1;
        while (j >= 0 && strcmp(dir[j].name, t.name) > 0) {
            dir[j + 1] = dir[j];
            j--;
        }
        dir[j + 1] = t;
    }
    for (int32_t i = 0; i < n; i++) {
        if (g_count >= DESKTOP_ENTRY_MAX) break;
        if (dir[i].name[0] == '\0') continue;
        if (!has_desktop_suffix(dir[i].name)) continue;
        char path[160];
        const char *dd = DESKTOP_ENTRY_SCAN_DIR;
        uint32_t dl = (uint32_t)strlen(dd);
        uint32_t nl = (uint32_t)strlen(dir[i].name);
        if (dl + 1 + nl >= sizeof(path)) continue;
        memcpy(path, dd, dl);
        path[dl] = '/';
        memcpy(path + dl + 1, dir[i].name, nl + 1);
        uint32_t size = 0;
        if (read_whole_file(path, &size) < 0) continue;
        struct desktop_entry e;
        if (!desktop_entry_parse((const char *)g_file_buf, size, &e)) continue;
        uint32_t stem = nl - 8;
        if (stem >= sizeof(e.id)) stem = sizeof(e.id) - 1;
        memcpy(e.id, dir[i].name, stem);
        e.id[stem] = '\0';
        if (!e.icon_text[0] && e.name[0]) {
            uint32_t take = (uint32_t)strlen(e.name);
            if (take > 2) take = 2;
            memcpy(e.icon_text, e.name, take);
            e.icon_text[take] = '\0';
        }
        e.hidden = false;
        g_entries[g_count++] = e;
    }
    if (!g_count) {
        apply_fallback();
        return;
    }
    assign_grid();
}

void desktop_entries_init(void) {
    desktop_entries_rescan();
}

uint32_t desktop_entries_count(void) {
    return g_count;
}

const struct desktop_entry *desktop_entries_get(uint32_t index) {
    if (index >= g_count) return 0;
    return &g_entries[index];
}

void desktop_entries_set_position(uint32_t index, uint32_t x, uint32_t y) {
    if (index >= g_count) return;
    g_entries[index].x = x;
    g_entries[index].y = y;
}

void desktop_entries_set_installer_visible(bool visible) {
    for (uint32_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].exec, "/bin/installer") == 0) {
            g_entries[i].hidden = !visible;
        }
    }
}
