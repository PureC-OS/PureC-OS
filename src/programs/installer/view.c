#include "app.h"
#include "../../fs/install_accounts.h"

#define C_CARD 0x14172AU
#define C_SIDE 0x0E1121U
#define C_BORDER 0x262B4AU
#define C_FIELD 0x0D1020U
#define C_ROW 0x0F1226U
#define C_ROW_HOT 0x161B36U
#define C_TEXT 0xE8EBFAU
#define C_TEXT2 0x9AA3C7U
#define C_MUTED 0x5B6390U
#define C_ACC0 0x6C8CFFU
#define C_ACC1 0xB388FFU
#define C_OK 0x6EE7A8U
#define C_ERR 0xFF6B8BU
#define C_WARN 0xF5C26BU

#define F_BODY GFX_FONT_CLEAN
#define F_BOLD GFX_FONT_BOLD

enum {
    ID_NONE = -1,
    ID_NEXT = 1,
    ID_BACK,
    ID_CANCEL,
    ID_SECOND,
    ID_HOST,
    ID_FULLNAME,
    ID_USERNAME,
    ID_PASS,
    ID_PASS2,
    ID_ROOTPASS,
    ID_ROOTPASS2,
    ID_SAME,
    ID_ERASE,
    ID_REVEAL,
    ID_DISK0 = 100
};

enum { STYLE_PRIMARY = 0, STYLE_SECONDARY, STYLE_DANGER };

struct lay {
    int32_t m, cx, cy, cw, ch, sw, px, py, pw, fy;
};

static struct lay L;

static const char *const step_names[APP_PHASES] = {
    "Welcome", "Disk", "Computer", "Account", "Review", "Install"};

static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

static bool inside(const struct ui *u, int32_t x, int32_t y, int32_t w,
                   int32_t h) {
    return u->in.mx >= x && u->in.my >= y && u->in.mx < x + w &&
           u->in.my < y + h;
}

static void layout(const struct ui *u) {
    L.m = 32;
    L.cx = L.m;
    L.cy = L.m;
    L.cw = u->c.w - 2 * L.m;
    L.ch = u->c.h - 2 * L.m;
    L.sw = L.cw >= 860 ? 252 : 212;
    L.px = L.cx + L.sw + 40;
    L.py = L.cy + 34;
    L.pw = L.cw - L.sw - 80;
    L.fy = L.cy + L.ch - 36 - 44;
}

static void register_focus(struct ui *u, int32_t id) {
    if (u->id_new_count < 24)
        u->ids_new[u->id_new_count++] = id;
}

void ui_focus_next(struct ui *u, int32_t dir) {
    if (u->id_count <= 0)
        return;
    int32_t at = -1;
    for (int32_t i = 0; i < u->id_count; i++)
        if (u->ids[i] == u->focus)
            at = i;
    if (at < 0)
        at = dir > 0 ? -1 : 0;
    at = (at + dir + u->id_count) % u->id_count;
    u->focus = u->ids[at];
}

static bool focus_is_last(const struct ui *u) {
    return u->id_count > 0 && u->ids[u->id_count - 1] == u->focus;
}

static uint32_t lighten(uint32_t c, uint32_t t) { return ui_mix(c, 0xFFFFFF, t); }

static bool button(struct ui *u, int32_t id, int32_t x, int32_t y, int32_t w,
                   int32_t h, const char *label, int style, bool enabled) {
    bool hot = enabled && inside(u, x, y, w, h);
    bool held = hot && u->active == id && u->in.down;
    if (hot && u->in.pressed)
        u->active = id;
    uint32_t a0 = C_ACC0, a1 = C_ACC1;
    if (style == STYLE_DANGER) {
        a0 = 0xFF5C7A;
        a1 = 0xFF9A6B;
    }
    int32_t off = held ? 1 : 0;
    if (style == STYLE_SECONDARY) {
        uint32_t fill = hot ? C_ROW_HOT : C_ROW;
        ui_rrect(&u->c, x, y + off, w, h, 12, ui_solid(fill), 255);
        ui_rrect_stroke(&u->c, x, y + off, w, h, 12, 2,
                        ui_solid(hot ? C_ACC0 : C_BORDER), 255);
        ui_text_center(&u->c, x, w, y + off + (h - 16) / 2, label,
                       enabled ? C_TEXT : C_MUTED, 16, F_BOLD);
    } else {
        if (!enabled) {
            ui_rrect(&u->c, x, y, w, h, 12, ui_solid(0x23294A), 255);
            ui_text_center(&u->c, x, w, y + (h - 16) / 2, label, C_MUTED, 16,
                           F_BOLD);
        } else {
            if (hot)
                ui_shadow(&u->c, x, y + off, w, h, 12, 8, 3, 38);
            if (hot) {
                a0 = lighten(a0, 28);
                a1 = lighten(a1, 28);
            }
            ui_rrect(&u->c, x, y + off, w, h, 12, ui_hgrad(a0, a1), 255);
            ui_text_center(&u->c, x, w, y + off + (h - 16) / 2, label,
                           0xFFFFFF, 16, F_BOLD);
        }
    }
    return hot && u->in.released && u->active == id;
}

static void caption(struct ui *u, int32_t x, int32_t y, const char *text) {
    ui_text(&u->c, x, y, text, C_MUTED, 8, F_BODY);
}

static bool text_edit(struct ui *u, char *buf, uint32_t cap) {
    if (u->in.key < 0)
        return false;
    uint32_t len = ui_strlen(buf);
    int32_t k = u->in.key;
    if (k == '\b' || k == 127) {
        if (!len)
            return false;
        buf[len - 1] = '\0';
        return true;
    }
    if (k >= 32 && k <= 126 && len + 1 < cap) {
        buf[len] = (char)k;
        buf[len + 1] = '\0';
        return true;
    }
    return false;
}

static bool field(struct ui *u, int32_t id, int32_t x, int32_t y, int32_t w,
                  const char *label, char *buf, uint32_t cap, bool secret,
                  bool reveal, const char *placeholder, const char *err) {
    const int32_t fh = 44;
    register_focus(u, id);
    bool focused = u->focus == id;
    if (u->in.pressed && inside(u, x, y + 14, w, fh))
        u->focus = id, focused = true;
    bool changed = false;
    if (focused)
        changed = text_edit(u, buf, cap);
    ui_text(&u->c, x, y, label, focused ? C_TEXT2 : C_MUTED, 8, F_BODY);
    ui_rrect(&u->c, x, y + 14, w, fh, 12, ui_solid(C_FIELD), 255);
    if (err && err[0])
        ui_rrect_stroke(&u->c, x, y + 14, w, fh, 12, 2, ui_solid(C_ERR), 255);
    else if (focused)
        ui_rrect_stroke(&u->c, x, y + 14, w, fh, 12, 2,
                        ui_hgrad(C_ACC0, C_ACC1), 255);
    else
        ui_rrect_stroke(&u->c, x, y + 14, w, fh, 12, 2, ui_solid(C_BORDER),
                        255);
    int32_t right = secret ? 62 : 16;
    int32_t room = w - 16 - right;
    uint32_t len = ui_strlen(buf);
    int32_t ty = y + 14 + (fh - 16) / 2;
    int32_t end_x = x + 16;
    if (secret && !reveal) {
        int32_t fit = imax(1, room / 14);
        int32_t first = (int32_t)len > fit ? (int32_t)len - fit : 0;
        int32_t n = (int32_t)len - first;
        for (int32_t i = 0; i < n; i++)
            ui_circle(&u->c, x + 16 + i * 14 + 5, y + 14 + fh / 2, 4,
                      ui_solid(C_TEXT), 255);
        end_x = x + 16 + n * 14;
    } else if (len) {
        int32_t fit = imax(1, room / 16);
        const char *shown = buf;
        if ((int32_t)len > fit)
            shown = buf + (len - (uint32_t)fit);
        ui_text(&u->c, x + 16, ty, shown, C_TEXT, 16, F_BODY);
        end_x = x + 16 + ui_text_w(shown, 16);
    } else if (placeholder) {
        ui_text(&u->c, x + 16, ty, placeholder, C_MUTED, 16, F_BODY);
    }
    if (focused && u->caret_on)
        ui_fill(&u->c, end_x + 2, y + 14 + 11, 2, fh - 22, C_ACC0, 255);
    if (err && err[0])
        ui_text(&u->c, x + 2, y + 14 + fh + 6, err, C_ERR, 8, F_BODY);
    return changed;
}

static bool reveal_toggle(struct ui *u, int32_t x, int32_t y, int32_t w,
                          bool *reveal) {
    int32_t bx = x + w - 56, by = y + 14 + 12;
    bool hot = inside(u, bx, by, 48, 20);
    if (hot && u->in.pressed)
        u->active = ID_REVEAL;
    ui_rrect(&u->c, bx, by, 48, 20, 8, ui_solid(hot ? C_ROW_HOT : C_ROW), 255);
    ui_text_center(&u->c, bx, 48, by + 6, *reveal ? "HIDE" : "SHOW",
                   hot ? C_TEXT : C_TEXT2, 8, F_BOLD);
    if (hot && u->in.released && u->active == ID_REVEAL) {
        *reveal = !*reveal;
        return true;
    }
    return false;
}

static void checkbox(struct ui *u, int32_t id, int32_t x, int32_t y,
                     const char *label, bool *value) {
    int32_t w = 30 + ui_text_w(label, 16) + 8;
    bool hot = inside(u, x, y, w, 26);
    if (hot && u->in.pressed)
        u->active = id;
    if (*value) {
        ui_rrect(&u->c, x, y, 26, 26, 8, ui_hgrad(C_ACC0, C_ACC1), 255);
        ui_check(&u->c, x + 13, y + 13, 22, 0xFFFFFF, 255);
    } else {
        ui_rrect(&u->c, x, y, 26, 26, 8, ui_solid(C_FIELD), 255);
        ui_rrect_stroke(&u->c, x, y, 26, 26, 8, 2,
                        ui_solid(hot ? C_ACC0 : C_BORDER), 255);
    }
    ui_text(&u->c, x + 38, y + 5, label, hot ? C_TEXT : C_TEXT2, 16, F_BODY);
    if (hot && u->in.released && u->active == id)
        *value = !*value;
}

static int32_t strength(const char *p) {
    uint32_t len = ui_strlen(p);
    if (!len)
        return 0;
    bool lower = false, upper = false, digit = false, other = false;
    for (uint32_t i = 0; i < len; i++) {
        char c = p[i];
        if (c >= 'a' && c <= 'z')
            lower = true;
        else if (c >= 'A' && c <= 'Z')
            upper = true;
        else if (c >= '0' && c <= '9')
            digit = true;
        else
            other = true;
    }
    int32_t classes = lower + upper + digit + other;
    int32_t score = 1;
    if (len >= 8)
        score++;
    if (len >= 12 && classes >= 2)
        score++;
    if (classes >= 3 && len >= 8)
        score++;
    return score > 4 ? 4 : score;
}

static void strength_meter(struct ui *u, int32_t x, int32_t y, int32_t w,
                           const char *p) {
    static const char *const names[5] = {"", "Weak", "Fair", "Good", "Strong"};
    static const uint32_t colors[5] = {C_BORDER, C_ERR, C_WARN, 0xA8E06E,
                                       C_OK};
    int32_t s = strength(p);
    int32_t seg = 18;
    int32_t bx = x + w - 4 * (seg + 4) - (s ? 52 : 0);
    for (int32_t i = 0; i < 4; i++)
        ui_rrect(&u->c, bx + i * (seg + 4), y + 1, seg, 6, 3,
                 ui_solid(i < s ? colors[s] : C_BORDER), 255);
    if (s)
        ui_text(&u->c, bx + 4 * (seg + 4) + 4, y, names[s], colors[s], 8,
                F_BOLD);
}

static bool host_ok(const char *h) {
    return h[0] && install_account_hostname_valid(h);
}

static bool user_ok(const char *n) {
    return n[0] && n[0] != '_' && install_account_name_valid(n) &&
           !(n[0] == 'r' && n[1] == 'o' && n[2] == 'o' && n[3] == 't' &&
             !n[4]);
}

static bool streq(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static bool account_ok(const struct app *a) {
    if (!user_ok(a->s.user_name))
        return false;
    if (!a->s.user_gecos[0] || !install_account_gecos_valid(a->s.user_gecos))
        return false;
    if (!a->s.user_password[0] ||
        !streq(a->s.user_password, a->user_pass2))
        return false;
    if (!a->same_root &&
        (!a->s.root_password[0] || !streq(a->s.root_password, a->root_pass2)))
        return false;
    return true;
}

static bool disk_usable(const struct storage_device_info *d) {
    return d->operational && d->writable;
}

static void fmt_u(char *out, uint32_t cap, uint64_t v) {
    char rev[24];
    uint32_t n = 0;
    do {
        rev[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < sizeof(rev));
    uint32_t i = 0;
    while (n && i + 1 < cap)
        out[i++] = rev[--n];
    out[i] = '\0';
}

static void cat(char *dst, uint32_t cap, const char *s) {
    uint32_t n = ui_strlen(dst);
    ui_copy(dst + n, s, cap - n);
}

static void disk_size_text(const struct storage_device_info *d, char *out,
                           uint32_t cap) {
    uint64_t bytes = d->sector_count * d->sector_size;
    char num[24];
    out[0] = '\0';
    if (bytes >= (1ULL << 30)) {
        uint64_t tenth = bytes * 10 / (1ULL << 30);
        fmt_u(num, sizeof(num), tenth / 10);
        cat(out, cap, num);
        if (tenth < 1000 && tenth % 10) {
            cat(out, cap, ".");
            fmt_u(num, sizeof(num), tenth % 10);
            cat(out, cap, num);
        }
        cat(out, cap, " GiB");
    } else {
        fmt_u(num, sizeof(num), bytes >> 20);
        cat(out, cap, num);
        cat(out, cap, " MiB");
    }
}

static const char *transport_name(const struct storage_device_info *d) {
    switch (d->transport) {
    case STORAGE_TRANSPORT_AHCI:
        return "SATA";
    case STORAGE_TRANSPORT_ATA_PIO:
        return "IDE";
    case STORAGE_TRANSPORT_USB_MSC:
    case STORAGE_TRANSPORT_USB_EHCI:
        return "USB";
    default:
        return "DISK";
    }
}

static void go(struct app *a, struct ui *u, enum app_page page) {
    a->page = page;
    u->focus = ID_NONE;
    u->active = ID_NONE;
    u->id_count = 0;
    u->dirty = true;
    if (page == PAGE_COMPUTER)
        u->focus = ID_HOST;
    else if (page == PAGE_ACCOUNT)
        u->focus = ID_FULLNAME;
    if (page == PAGE_REVIEW)
        a->erase_ok = false;
}

static void back(struct app *a, struct ui *u) {
    switch (a->page) {
    case PAGE_DISK:
        go(a, u, PAGE_WELCOME);
        break;
    case PAGE_COMPUTER:
        go(a, u, PAGE_DISK);
        break;
    case PAGE_ACCOUNT:
        go(a, u, PAGE_COMPUTER);
        break;
    case PAGE_REVIEW:
        go(a, u, PAGE_ACCOUNT);
        break;
    default:
        break;
    }
}

static int32_t step_of(const struct app *a) {
    switch (a->page) {
    case PAGE_WELCOME:
        return 0;
    case PAGE_DISK:
        return 1;
    case PAGE_COMPUTER:
        return 2;
    case PAGE_ACCOUNT:
        return 3;
    case PAGE_REVIEW:
        return 4;
    default:
        return 5;
    }
}

static void draw_chrome(struct ui_canvas *c) {
    int32_t x = L.cx, y = L.cy;
    ui_rrect(c, L.cx, L.cy, L.cw, L.ch, 22, ui_solid(C_CARD), 255);
    ui_rrect(c, x, y, L.sw + 24, L.ch, 22, ui_vgrad(0x10142A, C_SIDE), 255);
    ui_fill(c, x + L.sw, y, 24, L.ch, C_CARD, 255);
    ui_fill(c, x + L.sw - 1, y + 20, 1, L.ch - 40, C_BORDER, 255);
    ui_rrect_stroke(c, L.cx, L.cy, L.cw, L.ch, 22, 1, ui_solid(C_BORDER), 255);
    ui_logo(c, x + 28, y + 30, 48);
    ui_text(c, x + 90, y + 33, "PureC OS", C_TEXT, 16, GFX_FONT_BOLD);
    ui_text(c, x + 90, y + 56, "SETUP", C_ACC0, 8, GFX_FONT_BOLD);
    ui_text(c, x + 28, y + L.ch - 34, "Tab  Enter  Esc", C_MUTED, 8,
            GFX_FONT_CLEAN);
}

static void draw_sidebar(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    int32_t x = L.cx, y = L.cy;
    int32_t cur = step_of(a);
    bool finished = a->page == PAGE_DONE;
    bool failed = a->page == PAGE_FAILED;
    int32_t sy = y + 130;
    int32_t gap = imin(58, (L.ch - 130 - 80) / APP_PHASES);
    for (int32_t i = 0; i < APP_PHASES; i++) {
        int32_t cy = sy + i * gap + 15;
        int32_t cx = x + 28 + 15;
        bool done = i < cur || finished;
        bool now = i == cur && !finished;
        if (i + 1 < APP_PHASES)
            ui_fill(c, cx - 1, cy + 18, 2, gap - 36,
                    (i < cur || finished) ? C_ACC0 : C_BORDER, 255);
        if (now && failed) {
            ui_circle(c, cx, cy, 15, ui_solid(C_ERR), 255);
            ui_cross(c, cx, cy, 24, 0xFFFFFF, 255);
        } else if (done) {
            ui_circle(c, cx, cy, 15, ui_hgrad(C_ACC0, C_ACC1), 255);
            ui_check(c, cx, cy, 22, 0xFFFFFF, 255);
        } else if (now) {
            ui_circle(c, cx, cy, 17, ui_hgrad(C_ACC0, C_ACC1), 70);
            ui_circle(c, cx, cy, 15, ui_hgrad(C_ACC0, C_ACC1), 255);
            char n[2] = {(char)('1' + i), 0};
            ui_text_center(c, cx - 15, 30, cy - 7, n, 0xFFFFFF, 16, F_BOLD);
        } else {
            ui_ring(c, cx, cy, 15, 13, ui_solid(C_BORDER), 255);
            char n[2] = {(char)('1' + i), 0};
            ui_text_center(c, cx - 15, 30, cy - 7, n, C_MUTED, 16, F_BODY);
        }
        ui_text(c, cx + 30, cy - 7, step_names[i],
                now ? C_TEXT : (done ? C_TEXT2 : C_MUTED), 16,
                now ? F_BOLD : F_BODY);
    }
}

static void header(struct app *a, struct ui *u, const char *title,
                   const char *sub) {
    char cap[24] = "STEP ";
    char n[4];
    fmt_u(n, sizeof(n), (uint64_t)step_of(a) + 1);
    cat(cap, sizeof(cap), n);
    cat(cap, sizeof(cap), " OF 6");
    ui_text_bold(&u->c, L.px, L.py, cap, C_ACC0, 8);
    ui_text(&u->c, L.px, L.py + 18, title, C_TEXT, 24, F_BOLD);
    if (sub)
        ui_text(&u->c, L.px, L.py + 56, sub, C_TEXT2, 8, F_BODY);
}

static void footer(struct app *a, struct ui *u, const char *next_label,
                   bool next_enabled, int style, bool *next_clicked,
                   bool *back_clicked) {
    *back_clicked = false;
    *next_clicked = false;
    int32_t y = L.fy;
    if (a->page != PAGE_WELCOME)
        *back_clicked = button(u, ID_BACK, L.px, y, 130, 44, "Back",
                               STYLE_SECONDARY, true);
    int32_t w = imax(170, ui_text_w(next_label, 16) + 56);
    *next_clicked = button(u, ID_NEXT, L.px + L.pw - w, y, w, 44, next_label,
                           style, next_enabled);
}

static void page_welcome(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    int32_t mid = L.px + L.pw / 2;
    ui_logo(c, mid - 48, L.py + 8, 96);
    ui_text_center(c, L.px, L.pw, L.py + 128, "Welcome to PureC OS", C_TEXT, 32,
                   F_BOLD);
    ui_text_center(c, L.px, L.pw, L.py + 176,
                   "Let's set up your computer. It only takes a few minutes.",
                   C_TEXT2, 8, F_BODY);
    static const char *const chips[3] = {"ext2 system partition",
                                         "SHA-512 password hashing",
                                         "Swap space when the disk allows"};
    int32_t cy = L.py + 214;
    for (int32_t i = 0; i < 3; i++) {
        int32_t w = ui_text_w(chips[i], 8) + 52;
        int32_t cx = mid - w / 2;
        ui_rrect(c, cx, cy + i * 40, w, 30, 15, ui_solid(C_ROW), 255);
        ui_rrect_stroke(c, cx, cy + i * 40, w, 30, 15, 1,
                        ui_solid(C_BORDER), 255);
        ui_circle(c, cx + 16, cy + i * 40 + 15, 7, ui_hgrad(C_ACC0, C_ACC1),
                  255);
        ui_check(c, cx + 16, cy + i * 40 + 15, 12, 0xFFFFFF, 255);
        ui_text(c, cx + 34, cy + i * 40 + 11, chips[i], C_TEXT2, 8, F_BODY);
    }
    bool next, bk;
    footer(a, u, "Get started", true, STYLE_PRIMARY, &next, &bk);
    if (next || u->in.key == 13 || u->in.key == 10)
        go(a, u, PAGE_DISK);
}

static void draw_drive_icon(struct ui *u, int32_t x, int32_t y, bool usb,
                            bool ok) {
    ui_rrect(&u->c, x, y, 44, 36, 10, ui_vgrad(0x262C52, 0x1A1F3C), 255);
    ui_rrect_stroke(&u->c, x, y, 44, 36, 10, 1, ui_solid(C_BORDER), 255);
    if (usb) {
        ui_fill(&u->c, x + 12, y + 12, 20, 3, C_TEXT2, 255);
        ui_fill(&u->c, x + 12, y + 20, 20, 3, C_TEXT2, 255);
    } else {
        ui_fill(&u->c, x + 8, y + 22, 28, 3, C_BORDER, 255);
        ui_circle(&u->c, x + 34, y + 12, 3, ui_solid(ok ? C_OK : C_ERR), 255);
    }
}

static void page_disk(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    header(a, u, "Choose a disk",
           "PureC OS will be installed on the selected disk.");
    int32_t top = L.py + 88;
    int32_t ch = 70, gap = 10;
    int32_t room = L.fy - 120 - top;
    int32_t visible = imax(1, room / (ch + gap));
    if (a->disk_count == 0) {
        ui_rrect(c, L.px, top, L.pw, 110, 16, ui_solid(C_ROW), 255);
        ui_text_center(c, L.px, L.pw, top + 38, "No disks were detected",
                       C_TEXT, 16, F_BOLD);
        ui_text_center(c, L.px, L.pw, top + 68,
                       "Attach a disk and restart the installer.", C_TEXT2, 8,
                       F_BODY);
    }
    int32_t maxscroll = imax(0, a->disk_count - visible);
    if (u->in.wheel)
        a->scroll = imax(0, imin(maxscroll, a->scroll - u->in.wheel));
    if (a->scroll > maxscroll)
        a->scroll = maxscroll;
    for (int32_t row = 0; row < visible && a->scroll + row < a->disk_count;
         row++) {
        int32_t i = a->scroll + row;
        const struct storage_device_info *d = &a->disks[i];
        bool ok = disk_usable(d);
        int32_t y = top + row * (ch + gap);
        bool hot = ok && inside(u, L.px, y, L.pw, ch);
        bool sel = i == a->selected;
        if (hot && u->in.pressed)
            u->active = ID_DISK0 + i;
        if (hot && u->in.released && u->active == ID_DISK0 + i)
            a->selected = i, sel = true;
        ui_rrect(c, L.px, y, L.pw, ch, 16,
                 ui_solid(sel ? 0x181D3C : (hot ? C_ROW_HOT : C_ROW)), 255);
        ui_rrect_stroke(c, L.px, y, L.pw, ch, 16, 2,
                        sel ? ui_hgrad(C_ACC0, C_ACC1) : ui_solid(C_BORDER),
                        255);
        bool usb = d->transport == STORAGE_TRANSPORT_USB_MSC ||
                   d->transport == STORAGE_TRANSPORT_USB_EHCI;
        draw_drive_icon(u, L.px + 18, y + (ch - 36) / 2, usb, ok);
        uint32_t dim = ok ? 255 : 110;
        ui_text_alpha(c, L.px + 78, y + 14, d->name, C_TEXT, 16, F_BODY, dim);
        int32_t nw = ui_text_w(d->name, 16);
        const char *tn = transport_name(d);
        int32_t bw = ui_text_w(tn, 8) + 14;
        ui_rrect(c, L.px + 78 + nw + 10, y + 13, bw, 18, 9,
                 ui_solid(0x232950), (uint32_t)dim);
        ui_text_alpha(c, L.px + 78 + nw + 17, y + 18, tn, C_ACC0, 8, F_BOLD,
                      dim);
        char model[34];
        ui_copy(model, d->model[0] ? d->model : "Unknown model", sizeof(model));
        ui_text_alpha(c, L.px + 78, y + 42, model, C_TEXT2, 8, F_BODY, dim);
        char size[24];
        disk_size_text(d, size, sizeof(size));
        int32_t sw = ui_text_w(size, 16);
        int32_t rx = L.px + L.pw - 62 - sw;
        ui_text_alpha(c, rx, y + 14, size, C_TEXT, 16, F_BODY, dim);
        const char *state = !d->operational ? "unavailable"
                            : !d->writable ? "read-only" : d->serial;
        if (state && state[0])
            ui_text_alpha(c, L.px + L.pw - 62 - ui_text_w(state, 8), y + 42,
                          state, ok ? C_MUTED : C_ERR, 8, F_BODY, dim);
        int32_t rcx = L.px + L.pw - 30, rcy = y + ch / 2;
        if (ok) {
            ui_ring(c, rcx, rcy, 11, 9, ui_solid(sel ? C_ACC0 : C_BORDER),
                    255);
            if (sel)
                ui_circle(c, rcx, rcy, 6, ui_hgrad(C_ACC0, C_ACC1), 255);
        }
    }
    if (maxscroll > 0) {
        int32_t th = visible * (ch + gap) - gap;
        int32_t bh = imax(24, th * visible / a->disk_count);
        int32_t by = top + (th - bh) * a->scroll / maxscroll;
        ui_rrect(c, L.px + L.pw + 10, top, 4, th, 2, ui_solid(C_BORDER), 255);
        ui_rrect(c, L.px + L.pw + 10, by, 4, bh, 2, ui_solid(C_ACC0), 255);
    }

    int32_t ly = L.fy - 92;
    if (a->selected >= 0 && a->selected < a->disk_count &&
        disk_usable(&a->disks[a->selected])) {
        caption(u, L.px, ly, "DISK LAYOUT");
        uint64_t bytes = a->disks[a->selected].sector_count *
                         a->disks[a->selected].sector_size;
        bool swap = bytes >= (2ULL << 30);
        int32_t bw = L.pw;
        int32_t esp = imax(14, bw * 4 / 100);
        int32_t sw2 = swap ? bw * 12 / 100 : 0;
        ui_rrect(c, L.px, ly + 14, bw, 18, 9, ui_solid(C_BORDER), 255);
        ui_rrect(c, L.px, ly + 14, esp + 10, 18, 9, ui_solid(C_ACC0), 255);
        ui_rrect(c, L.px + esp, ly + 14, bw - esp - sw2, 18, 0,
                 ui_hgrad(C_ACC0, C_ACC1), 255);
        if (swap) {
            ui_rrect(c, L.px + bw - sw2 - 10, ly + 14, sw2 + 10, 18, 9,
                     ui_solid(C_OK), 255);
            ui_rrect(c, L.px + bw - sw2 - 10, ly + 14, 20, 18, 0,
                     ui_hgrad(C_ACC1, C_OK), 255);
        }
        int32_t lx = L.px;
        static const char *const names[3] = {"EFI boot", "System (ext2)",
                                             "Swap"};
        static const uint32_t cols[3] = {C_ACC0, C_ACC1, C_OK};
        for (int32_t i = 0; i < (swap ? 3 : 2); i++) {
            ui_circle(c, lx + 5, ly + 46, 4, ui_solid(cols[i]), 255);
            ui_text(c, lx + 16, ly + 42, names[i], C_TEXT2, 8, F_BODY);
            lx += 16 + ui_text_w(names[i], 8) + 22;
        }
    }
    int32_t wy = L.fy - 30;
    ui_circle(c, L.px + 9, wy + 11, 9, ui_solid(0x3A1B28), 255);
    ui_text_center(c, L.px, 18, wy + 6, "!", C_ERR, 8, F_BOLD);
    ui_text(c, L.px + 28, wy + 7,
            "Everything on the selected disk will be erased.", C_ERR, 8,
            F_BODY);

    bool usable = a->selected >= 0 && a->selected < a->disk_count &&
                  disk_usable(&a->disks[a->selected]);
    bool next, bk;
    footer(a, u, "Continue", usable, STYLE_PRIMARY, &next, &bk);
    if (u->in.special == 10 && a->selected > 0) {
        int32_t i = a->selected - 1;
        while (i >= 0 && !disk_usable(&a->disks[i]))
            i--;
        if (i >= 0)
            a->selected = i;
    }
    if (u->in.special == 11) {
        int32_t i = a->selected + 1;
        while (i < a->disk_count && !disk_usable(&a->disks[i]))
            i++;
        if (i < a->disk_count)
            a->selected = i;
    }
    if (a->selected >= 0) {
        if (a->selected < a->scroll)
            a->scroll = a->selected;
        if (a->selected >= a->scroll + visible)
            a->scroll = a->selected - visible + 1;
    }
    if (bk || u->in.key == 27)
        back(a, u);
    else if (next || (usable && (u->in.key == 13 || u->in.key == 10)))
        go(a, u, PAGE_COMPUTER);
}

static void page_computer(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    header(a, u, "Name your computer",
           "This name identifies the machine on your network.");
    int32_t y = L.py + 100;
    bool bad = a->s.hostname[0] && !host_ok(a->s.hostname);
    field(u, ID_HOST, L.px, y, L.pw, "COMPUTER NAME", a->s.hostname,
          INSTALL_HOSTNAME_MAX, false, false, "purec",
          bad ? "Use lowercase letters, digits, - and . only" : 0);

    int32_t ty = y + 100;
    ui_rrect(c, L.px, ty, L.pw, 112, 16, ui_solid(0x0A0D1B), 255);
    ui_rrect_stroke(c, L.px, ty, L.pw, 112, 16, 1, ui_solid(C_BORDER), 255);
    ui_circle(c, L.px + 20, ty + 18, 5, ui_solid(C_ERR), 255);
    ui_circle(c, L.px + 38, ty + 18, 5, ui_solid(C_WARN), 255);
    ui_circle(c, L.px + 56, ty + 18, 5, ui_solid(C_OK), 255);
    const char *host = a->s.hostname[0] && !bad ? a->s.hostname : "purec";
    char prompt[96] = "";
    cat(prompt, sizeof(prompt), "user@");
    cat(prompt, sizeof(prompt), host);
    cat(prompt, sizeof(prompt), ":/$");
    ui_text(c, L.px + 20, ty + 48, prompt, C_OK, 16, F_BODY);
    int32_t px = L.px + 20 + ui_text_w(prompt, 16) + 12;
    ui_text(c, px, ty + 48, "uname -n", C_TEXT2, 16, F_BODY);
    ui_text(c, L.px + 20, ty + 76, host, C_TEXT, 16, F_BODY);
    ui_text(c, L.px, ty + 128, "Preview of your terminal prompt.", C_MUTED, 8,
            F_BODY);

    bool ok = host_ok(a->s.hostname);
    bool next, bk;
    footer(a, u, "Continue", ok, STYLE_PRIMARY, &next, &bk);
    if (bk || u->in.key == 27)
        back(a, u);
    else if (next || (ok && (u->in.key == 13 || u->in.key == 10)))
        go(a, u, PAGE_ACCOUNT);
}

static void suggest_username(struct app *a) {
    if (a->username_edited)
        return;
    uint32_t n = 0;
    for (uint32_t i = 0; a->s.user_gecos[i] && n < 16; i++) {
        char ch = a->s.user_gecos[i];
        if (ch == ' ')
            break;
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (n && ch >= '0' && ch <= '9') ||
            (n && (ch == '-' || ch == '_')))
            a->s.user_name[n++] = ch;
    }
    a->s.user_name[n] = '\0';
}

static void page_account(struct app *a, struct ui *u) {
    header(a, u, "Create your account",
           "You will sign in with this account every day.");
    int32_t colw = (L.pw - 24) / 2;
    int32_t x1 = L.px, x2 = L.px + colw + 24;
    int32_t y = L.py + 82;
    int32_t rowh = 92;

    bool gecos_bad = a->s.user_gecos[0] &&
                     !install_account_gecos_valid(a->s.user_gecos);
    if (field(u, ID_FULLNAME, x1, y, colw, "FULL NAME", a->s.user_gecos,
              INSTALL_GECOS_MAX, false, false, "Ada Lovelace",
              gecos_bad ? "Cannot contain ':'" : 0))
        suggest_username(a);
    bool ubad = a->s.user_name[0] && !user_ok(a->s.user_name);
    if (field(u, ID_USERNAME, x2, y, colw, "USER NAME", a->s.user_name,
              INSTALL_USER_MAX, false, false, "ada",
              ubad ? "a-z, 0-9, - and _ ; start with a letter" : 0))
        a->username_edited = a->s.user_name[0] != '\0';

    y += rowh;
    field(u, ID_PASS, x1, y, colw, "PASSWORD", a->s.user_password,
          SETUP_PASSWORD_MAX, true, a->reveal, 0, 0);
    reveal_toggle(u, x1, y, colw, &a->reveal);
    strength_meter(u, x1, y, colw, a->s.user_password);
    bool mism = a->user_pass2[0] &&
                !streq(a->s.user_password, a->user_pass2);
    field(u, ID_PASS2, x2, y, colw, "CONFIRM PASSWORD", a->user_pass2,
          SETUP_PASSWORD_MAX, true, a->reveal, 0,
          mism ? "Passwords do not match" : 0);

    y += rowh;
    checkbox(u, ID_SAME, L.px, y + 2,
             "Use the same password for root",
             &a->same_root);
    if (!a->same_root) {
        y += 44;
        field(u, ID_ROOTPASS, x1, y, colw, "ROOT PASSWORD",
              a->s.root_password, SETUP_PASSWORD_MAX, true, a->reveal, 0, 0);
        strength_meter(u, x1, y, colw, a->s.root_password);
        bool rm = a->root_pass2[0] &&
                  !streq(a->s.root_password, a->root_pass2);
        field(u, ID_ROOTPASS2, x2, y, colw, "CONFIRM ROOT PASSWORD",
              a->root_pass2, SETUP_PASSWORD_MAX, true, a->reveal, 0,
              rm ? "Passwords do not match" : 0);
    }

    bool ok = account_ok(a);
    bool next, bk;
    footer(a, u, "Continue", ok, STYLE_PRIMARY, &next, &bk);
    if (u->in.key == 9)
        ui_focus_next(u, 1);
    else if (u->in.key == 13 || u->in.key == 10) {
        if (!focus_is_last(u))
            ui_focus_next(u, 1);
        else if (ok)
            next = true;
    }
    if (bk || u->in.key == 27)
        back(a, u);
    else if (next)
        go(a, u, PAGE_REVIEW);
}

static void summary_row(struct ui *u, int32_t x, int32_t y, int32_t w,
                        const char *k, const char *v) {
    ui_text_bold(&u->c, x, y, k, C_MUTED, 8);
    char shown[40];
    ui_copy(shown, v, sizeof(shown));
    ui_text(&u->c, x, y + 14, shown, C_TEXT, 16, F_BODY);
    (void)w;
}

static void page_review(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    header(a, u, "Ready to install",
           "Review your choices. Nothing is written until you confirm.");
    const struct storage_device_info *d = &a->disks[a->selected];
    int32_t y = L.py + 86;
    int32_t colw = (L.pw - 20) / 2;
    int32_t h = 176;
    ui_rrect(c, L.px, y, colw, h, 16, ui_solid(C_ROW), 255);
    ui_rrect_stroke(c, L.px, y, colw, h, 16, 1, ui_solid(C_BORDER), 255);
    ui_text_bold(c, L.px + 20, y + 16, "TARGET DISK", C_ACC0, 8);
    char size[24];
    disk_size_text(d, size, sizeof(size));
    summary_row(u, L.px + 20, y + 40, colw, "DEVICE", d->name);
    char model[40];
    ui_copy(model, d->model[0] ? d->model : "Unknown model", sizeof(model));
    summary_row(u, L.px + 20, y + 80, colw, "MODEL", model);
    summary_row(u, L.px + 20, y + 120, colw, "CAPACITY", size);

    int32_t x2 = L.px + colw + 20;
    ui_rrect(c, x2, y, colw, h, 16, ui_solid(C_ROW), 255);
    ui_rrect_stroke(c, x2, y, colw, h, 16, 1, ui_solid(C_BORDER), 255);
    ui_text_bold(c, x2 + 20, y + 16, "THIS COMPUTER", C_ACC0, 8);
    summary_row(u, x2 + 20, y + 40, colw, "COMPUTER NAME", a->s.hostname);
    char who[48] = "";
    cat(who, sizeof(who), a->s.user_gecos);
    cat(who, sizeof(who), " (");
    cat(who, sizeof(who), a->s.user_name);
    cat(who, sizeof(who), ")");
    summary_row(u, x2 + 20, y + 80, colw, "USER", who);
    summary_row(u, x2 + 20, y + 120, colw, "ADMINISTRATOR",
                a->same_root ? "root (same password)" : "root");

    int32_t wy = y + h + 18;
    ui_rrect(c, L.px, wy, L.pw, 62, 14, ui_solid(0x23121C), 255);
    ui_rrect_stroke(c, L.px, wy, L.pw, 62, 14, 2, ui_solid(0x5A2535), 255);
    ui_circle(c, L.px + 30, wy + 31, 13, ui_solid(0x3A1B28), 255);
    ui_text_center(c, L.px + 17, 26, wy + 25, "!", C_ERR, 16, F_BOLD);
    ui_text_bold(c, L.px + 56, wy + 14, "All data on this disk will be lost.",
            C_ERR, 16);
    ui_text(c, L.px + 56, wy + 38,
            "Every partition and file on the target disk is erased.", C_TEXT2,
            8, F_BODY);

    checkbox(u, ID_ERASE, L.px, wy + 80, "I understand, erase the disk",
             &a->erase_ok);

    bool next, bk;
    footer(a, u, "Erase disk & install", a->erase_ok, STYLE_DANGER, &next,
           &bk);
    if (bk || u->in.key == 27)
        back(a, u);
    else if (next) {
        a->start_install = true;
        go(a, u, PAGE_INSTALL);
    }
}

static const char *const phase_names[APP_PHASES] = {
    "Preparing the disk",        "Writing the boot partition",
    "Creating the system partition", "Copying system files",
    "Setting up accounts",       "Finishing up"};

static void page_install(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    header(a, u, "Installing PureC OS",
           "Sit back while we set things up. This can take a few minutes.");
    int32_t bx = L.px, by = L.py + 136, bw = L.pw, bh = 22;
    uint32_t p10 = a->progress_shown;
    int32_t pct = (int32_t)(p10 / 10);
    char num[8];
    fmt_u(num, sizeof(num), (uint64_t)pct);
    cat(num, sizeof(num), "%");
    ui_text(c, bx, by - 46, num, C_TEXT, 32, F_BOLD);
    ui_text(c, bx + ui_text_w(num, 32) + 28, by - 32,
            a->stage[0] ? a->stage : "Preparing...", C_TEXT2, 8, F_BODY);
    ui_rrect(c, bx, by, bw, bh, 11, ui_solid(C_FIELD), 255);
    ui_rrect_stroke(c, bx, by, bw, bh, 11, 1, ui_solid(C_BORDER), 255);
    int32_t fw = (int32_t)((int64_t)(bw - 4) * p10 / 1000);
    if (fw > 3) {
        ui_rrect(c, bx + 2, by + 2, fw, bh - 4, 9,
                 ui_hgrad(C_ACC0, C_ACC1), 255);
        int32_t sh = (int32_t)((u->frame * 6) % (uint32_t)(fw + 80)) - 40;
        for (int32_t i = 0; i < 28; i++) {
            int32_t sx = bx + 2 + sh + i;
            if (sx >= bx + 6 && sx < bx + 2 + fw - 6)
                ui_fill(c, sx, by + 4, 1, bh - 8, 0xFFFFFF,
                        (uint32_t)(36 - (i > 14 ? 28 - i : i) * 2));
        }
    }

    int32_t ly = by + 48;
    for (int32_t i = 0; i < APP_PHASES; i++) {
        int32_t y = ly + i * 38;
        bool done = i < a->phase;
        bool now = i == a->phase;
        int32_t cx = bx + 14, cy = y + 14;
        if (done) {
            ui_circle(c, cx, cy, 12, ui_solid(C_OK), 255);
            ui_check(c, cx, cy, 20, 0x0B1A12, 255);
        } else if (now) {
            ui_spinner(c, cx, cy, 11, u->frame, C_ACC0);
        } else {
            ui_ring(c, cx, cy, 12, 10, ui_solid(C_BORDER), 255);
        }
        ui_text(c, bx + 40, y + 6, phase_names[i],
                done ? C_TEXT2 : (now ? C_TEXT : C_MUTED), 16,
                now ? F_BOLD : F_BODY);
    }
    int32_t wy = L.fy + 6;
    ui_circle(c, L.px + 9, wy + 9, 9, ui_solid(0x3A2E14), 255);
    ui_text_center(c, L.px, 18, wy + 5, "!", C_WARN, 8, F_BOLD);
    ui_text(c, L.px + 28, wy + 5,
            "Do not power off or disconnect the disk.", C_WARN, 8, F_BODY);
}

static void page_done(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    int32_t mid = L.px + L.pw / 2;
    int32_t cy = L.py + 120;
    ui_circle(c, mid, cy, 64, ui_solid(C_OK), 30);
    ui_circle(c, mid, cy, 52, ui_hgrad(0x4FD99A, 0x6EE7C8), 255);
    ui_check(c, mid, cy, 74, 0xFFFFFF, 255);
    ui_text_center(c, L.px, L.pw, cy + 90, "All set!", C_TEXT, 32, F_BOLD);
    ui_text_center(c, L.px, L.pw, cy + 138,
                   "PureC OS is installed on your disk.", C_TEXT2, 16,
                   F_BODY);
    ui_text_center(c, L.px, L.pw, cy + 170,
                   "Remove the installation media, then restart.", C_MUTED, 8,
                   F_BODY);
    int32_t y = L.fy;
    int32_t w1 = ui_text_w("Restart now", 16) + 80;
    if (button(u, ID_NEXT, mid - w1 / 2, y, w1, 44, "Restart now",
               STYLE_PRIMARY, true) ||
        u->in.key == 13 || u->in.key == 10)
        a->do_reboot = true;
}

static void page_failed(struct app *a, struct ui *u) {
    struct ui_canvas *c = &u->c;
    int32_t mid = L.px + L.pw / 2;
    int32_t cy = L.py + 120;
    ui_circle(c, mid, cy, 64, ui_solid(C_ERR), 30);
    ui_circle(c, mid, cy, 52, ui_hgrad(0xFF5C7A, 0xFF8F6B), 255);
    ui_cross(c, mid, cy, 74, 0xFFFFFF, 255);
    ui_text_center(c, L.px, L.pw, cy + 90, "Install failed", C_TEXT, 32, F_BOLD);
    char line[96] = "Error ";
    char n[12];
    int32_t code = a->fail_code < 0 ? -a->fail_code : a->fail_code;
    fmt_u(n, sizeof(n), (uint64_t)code);
    if (a->fail_code < 0)
        cat(line, sizeof(line), "-");
    cat(line, sizeof(line), n);
    cat(line, sizeof(line), ": ");
    cat(line, sizeof(line), a->fail_text ? a->fail_text : "unknown error");
    ui_text_center(c, L.px, L.pw, cy + 138, line, C_ERR, 8, F_BODY);
    ui_text_center(c, L.px, L.pw, cy + 160,
                   "Your disk may be in an inconsistent state. Try again.",
                   C_TEXT2, 8, F_BODY);
    int32_t y = L.fy;
    int32_t w1 = ui_text_w("Start over", 16) + 56;
    int32_t w2 = ui_text_w("Close", 16) + 56;
    int32_t gx = mid - (w1 + w2 + 16) / 2;
    if (button(u, ID_NEXT, gx, y, w1, 44, "Start over", STYLE_PRIMARY, true) ||
        u->in.key == 13 || u->in.key == 10) {
        a->install_started = false;
        go(a, u, PAGE_DISK);
    }
    if (button(u, ID_SECOND, gx + w1 + 16, y, w2, 44, "Close",
               STYLE_SECONDARY, true))
        a->quit = true;
}

void view_background(struct ui_canvas *c, int32_t ox, int32_t oy, int32_t sw,
                     int32_t sh) {
    struct ui tmp;
    tmp.c = *c;
    layout(&tmp);
    ui_background(c, ox, oy, sw, sh);
    ui_shadow(c, L.cx, L.cy, L.cw, L.ch, 22, 26, 10, 64);
    draw_chrome(c);
}

void view_init(struct app *a) {
    a->page = PAGE_WELCOME;
    ui_copy(a->s.hostname, "purec", sizeof(a->s.hostname));
    a->same_root = true;
    a->selected = -1;
    for (int32_t i = 0; i < a->disk_count; i++)
        if (disk_usable(&a->disks[i])) {
            a->selected = i;
            break;
        }
}

void view_frame(struct app *a, struct ui *u) {
    layout(u);
    u->id_new_count = 0;
    draw_sidebar(a, u);
    switch (a->page) {
    case PAGE_WELCOME:
        page_welcome(a, u);
        break;
    case PAGE_DISK:
        page_disk(a, u);
        break;
    case PAGE_COMPUTER:
        page_computer(a, u);
        break;
    case PAGE_ACCOUNT:
        page_account(a, u);
        break;
    case PAGE_REVIEW:
        page_review(a, u);
        break;
    case PAGE_INSTALL:
        page_install(a, u);
        break;
    case PAGE_DONE:
        page_done(a, u);
        break;
    case PAGE_FAILED:
        page_failed(a, u);
        break;
    }
    if (u->in.released)
        u->active = ID_NONE;
    u->id_count = u->id_new_count;
    for (int32_t i = 0; i < u->id_new_count; i++)
        u->ids[i] = u->ids_new[i];
}
