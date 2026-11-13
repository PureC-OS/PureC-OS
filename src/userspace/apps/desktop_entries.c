#include "../user_config.h"
#include "desktop_entries.h"
#include "../../fs/vfs.h"
#include "../../fs/types/fs_types.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"

#define ENTRY_FILE_MAX 4096
#define DIR_CAP 128
#define LAYOUT_PATH "/config/desktop.ini"
#define LAYOUT_DIR "/config"
#define GRID_MAX 64u

static struct desktop_entry g_entries[DESKTOP_ENTRY_MAX];
static uint32_t g_count;
static uint32_t m_label = 8, m_w = 1024, m_h = 768, m_top = 28, m_bottom = 30;
static bool g_used[GRID_MAX * GRID_MAX];

static uint32_t icon_w(void) { return 58u * m_label / 8u; }
static uint32_t icon_h(void) { return 72u * m_label / 8u; }
static uint32_t cell_dx(void) { return icon_w() + 14u; }
static uint32_t cell_dy(void) { return icon_h() + 10u; }
static uint32_t grid_x0(void) { return 24u; }
static uint32_t grid_y0(void) { return m_top + 14u; }

static uint32_t grid_cols(void) {
    uint32_t n = m_w > grid_x0() ? (m_w - grid_x0()) / cell_dx() : 1u;
    if (n < 1u) n = 1u;
    return n > GRID_MAX ? GRID_MAX : n;
}

static uint32_t grid_rows(void) {
    uint32_t avail = m_h > m_bottom + grid_y0() ? m_h - m_bottom - grid_y0() : 0u;
    uint32_t n = avail / cell_dy();
    if (n < 1u) n = 1u;
    return n > GRID_MAX ? GRID_MAX : n;
}

uint32_t desktop_entries_icon_w(void) { return icon_w(); }
uint32_t desktop_entries_icon_h(void) { return icon_h(); }
uint32_t desktop_entries_cell_w(void) { return cell_dx(); }


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
    if (!filesystem_syscall_acquire()) return -1;
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

static void cell_pos(uint32_t col, uint32_t row, uint32_t *x, uint32_t *y) {
    *x = grid_x0() + col * cell_dx();
    *y = grid_y0() + row * cell_dy();
}

static void mark_used(uint32_t skip) {
    uint32_t cols = grid_cols(), rows = grid_rows();
    for (uint32_t i = 0; i < cols * rows; i++) g_used[i] = false;
    for (uint32_t i = 0; i < g_count; i++) {
        if (i == skip || g_entries[i].hidden || !g_entries[i].placed) continue;
        if (g_entries[i].col < cols && g_entries[i].row < rows)
            g_used[g_entries[i].col * rows + g_entries[i].row] = true;
    }
}

static bool nearest_free(uint32_t want_col, uint32_t want_row,
                         uint32_t *out_col, uint32_t *out_row) {
    uint32_t cols = grid_cols(), rows = grid_rows();
    if (want_col >= cols) want_col = cols - 1;
    if (want_row >= rows) want_row = rows - 1;
    bool found = false;
    uint64_t best = 0;
    for (uint32_t c = 0; c < cols; c++) {
        for (uint32_t r = 0; r < rows; r++) {
            if (g_used[c * rows + r]) continue;
            int64_t dc = (int64_t)c - (int64_t)want_col;
            int64_t dr = (int64_t)r - (int64_t)want_row;
            uint64_t d = (uint64_t)(dc * dc + dr * dr);
            if (!found || d < best) {
                found = true;
                best = d;
                *out_col = c;
                *out_row = r;
            }
        }
    }
    return found;
}

static void apply_layout(void) {
    uint32_t cols = grid_cols(), rows = grid_rows();
    mark_used((uint32_t)-1);
    for (uint32_t i = 0; i < g_count; i++) {
        struct desktop_entry *e = &g_entries[i];
        if (e->hidden || !e->placed) continue;
        if (e->col >= cols || e->row >= rows) e->placed = false;
    }
    mark_used((uint32_t)-1);
    uint32_t next_col = 0, next_row = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        struct desktop_entry *e = &g_entries[i];
        if (e->hidden || e->placed) continue;
        bool done = false;
        for (; next_col < cols && !done; next_col++) {
            for (; next_row < rows; next_row++) {
                if (g_used[next_col * rows + next_row]) continue;
                e->col = (uint16_t)next_col;
                e->row = (uint16_t)next_row;
                e->placed = true;
                g_used[next_col * rows + next_row] = true;
                done = true;
                next_row++;
                break;
            }
            if (!done) next_row = 0;
            else break;
        }
    }
    for (uint32_t i = 0; i < g_count; i++) {
        struct desktop_entry *e = &g_entries[i];
        if (!e->placed) continue;
        cell_pos(e->col, e->row, &e->x, &e->y);
    }
}

static void save_layout(void) {
    char text[DESKTOP_ENTRY_MAX * 48];
    uint32_t len = 0;
    for (uint32_t i = 0; i < g_count; i++) {
        const struct desktop_entry *e = &g_entries[i];
        if (!e->placed || !e->id[0]) continue;
        uint32_t idl = (uint32_t)strlen(e->id);
        if (len + idl + 16 >= sizeof(text)) break;
        memcpy(text + len, e->id, idl);
        len += idl;
        text[len++] = '=';
        char num[12];
        uint32_t n = 0;
        uint32_t v = e->col;
        do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) text[len++] = num[--n];
        text[len++] = ',';
        v = e->row;
        do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) text[len++] = num[--n];
        text[len++] = '\n';
    }
    if (!vfs_is_root_mounted() || !filesystem_syscall_acquire()) return;
    char path[192];
    user_config_path("desktop.ini", path, sizeof(path), true);
    if (vfs_write_file(path, text, len) >= 0)
        user_config_claim(path);
    filesystem_syscall_unlock();
}

static uint32_t parse_num(const char **p) {
    uint32_t v = 0;
    while (**p >= '0' && **p <= '9') {
        v = v * 10u + (uint32_t)(**p - '0');
        (*p)++;
    }
    return v;
}

static void load_layout(void) {
    uint32_t size = 0;
    char path[192];
    if (!vfs_is_root_mounted() || !filesystem_syscall_acquire()) return;
    user_config_path("desktop.ini", path, sizeof(path), false);
    filesystem_syscall_unlock();
    if (read_whole_file(path, &size) < 0) return;
    const char *p = (const char *)g_file_buf;
    const char *end = p + size;
    while (p < end) {
        const char *line = p;
        while (p < end && *p != '\n') p++;
        const char *eq = line;
        while (eq < p && *eq != '=') eq++;
        if (eq < p) {
            uint32_t idl = (uint32_t)(eq - line);
            const char *q = eq + 1;
            uint32_t col = parse_num(&q);
            if (q < p && *q == ',') {
                q++;
                uint32_t row = parse_num(&q);
                for (uint32_t i = 0; i < g_count; i++) {
                    if (strlen(g_entries[i].id) == idl &&
                        memcmp(g_entries[i].id, line, idl) == 0) {
                        g_entries[i].col = (uint16_t)col;
                        g_entries[i].row = (uint16_t)row;
                        g_entries[i].placed = true;
                    }
                }
            }
        }
        if (p < end) p++;
    }
}

void desktop_entries_rescan(void) {
    memset(g_entries, 0, sizeof(g_entries));
    g_count = 0;
    static struct fs_directory_entry_long dir[DIR_CAP];
    memset(dir, 0, sizeof(dir));
    if (!vfs_is_root_mounted()) return;
    if (!filesystem_syscall_acquire()) return;
    int32_t n = vfs_list_long(DESKTOP_ENTRY_SCAN_DIR, dir, DIR_CAP);
    filesystem_syscall_unlock();
    if (n <= 0) return;
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
        bool needs_separator = dl && dd[dl - 1] != '/';
        uint32_t separator = needs_separator ? 1u : 0u;
        if (dl + separator + nl >= sizeof(path)) continue;
        memcpy(path, dd, dl);
        if (needs_separator) path[dl] = '/';
        memcpy(path + dl + separator, dir[i].name, nl + 1);
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
    load_layout();
    apply_layout();
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
    apply_layout();
}

void desktop_entries_configure(uint32_t label_px, uint32_t desktop_width,
                               uint32_t desktop_height, uint32_t top,
                               uint32_t bottom) {
    if (label_px < 8) label_px = 8;
    if (label_px > 20) label_px = 20;
    if (label_px == m_label && desktop_width == m_w &&
        desktop_height == m_h && top == m_top && bottom == m_bottom)
        return;
    m_label = label_px;
    m_w = desktop_width;
    m_h = desktop_height;
    m_top = top;
    m_bottom = bottom;
    apply_layout();
}

static void wanted_cell(uint32_t index, uint32_t *col, uint32_t *row) {
    const struct desktop_entry *e = &g_entries[index];
    uint32_t dx = cell_dx(), dy = cell_dy();
    uint32_t px = e->x + dx / 2u, py = e->y + dy / 2u;
    *col = px > grid_x0() ? (px - grid_x0()) / dx : 0u;
    *row = py > grid_y0() ? (py - grid_y0()) / dy : 0u;
    uint32_t cols = grid_cols(), rows = grid_rows();
    if (*col >= cols) *col = cols - 1;
    if (*row >= rows) *row = rows - 1;
}

void desktop_entries_target_cell(uint32_t index, uint32_t *x, uint32_t *y) {
    if (index >= g_count) return;
    uint32_t col, row, tc, tr;
    wanted_cell(index, &col, &row);
    mark_used(index);
    if (!nearest_free(col, row, &tc, &tr)) { tc = col; tr = row; }
    cell_pos(tc, tr, x, y);
}

void desktop_entries_snap(uint32_t index) {
    if (index >= g_count) return;
    uint32_t col, row, tc, tr;
    wanted_cell(index, &col, &row);
    mark_used(index);
    if (!nearest_free(col, row, &tc, &tr)) { tc = col; tr = row; }
    g_entries[index].col = (uint16_t)tc;
    g_entries[index].row = (uint16_t)tr;
    g_entries[index].placed = true;
    cell_pos(tc, tr, &g_entries[index].x, &g_entries[index].y);
    save_layout();
}
