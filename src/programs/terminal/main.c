#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pg_ui.h"
#include "../../libc/include/purec.h"
#include "../../drivers/input/keyboard.h"
#include "vt.h"

struct tty_winsize {
    uint16_t rows;
    uint16_t columns;
    uint16_t xpixel;
    uint16_t ypixel;
};

#define MAX_TABS 8
#define TAB_BAR_H 40
#define PAD 10
#define ENV_MAX 64
#define ENV_ENTRY 256

struct session {
    bool used;
    int32_t master;
    int32_t pid;
    struct vt vt;
    int scroll;
    bool exited;
    uint32_t seen_generation;
};

static struct session tabs[MAX_TABS];
static int active;
static struct pg_ui ui;
static int font_level = 1;
static uint8_t mods;
static bool focused = true;
static char app_title[96] = "Terminal";

static const int font_regular[3] = {PGF_MONO_S, PGF_MONO, PGF_MONO_L};
static const int font_bold[3] = {PGF_MONO_S_BOLD, PGF_MONO_BOLD, PGF_MONO_L_BOLD};

static const uint32_t ansi_dark[16] = {0x484f58, 0xff7b72, 0x3fb950, 0xd29922, 0x58a6ff, 0xbc8cff,
                                       0x39c5cf, 0xb1bac4, 0x6e7681, 0xffa198, 0x56d364, 0xe3b341,
                                       0x79c0ff, 0xd2a8ff, 0x56d4dd, 0xffffff};
static const uint32_t ansi_light[16] = {0x24292f, 0xcf222e, 0x116329, 0x4d2d00, 0x0969da, 0x8250df,
                                        0x1b7c83, 0x6e7781, 0x57606a, 0xa40e26, 0x1a7f37, 0x633c01,
                                        0x218bff, 0xa475f9, 0x3192aa, 0x8c959f};

static int cell_w(void) { return pgc_glyph(font_regular[font_level], 'M')->adv; }
static int cell_h(void) { return pgc_font_h(font_regular[font_level]); }

static uint64_t now_ms(void)
{
    struct time_info t;
    return pc_time_get(&t) < 0 ? 0 : t.uptime_ms;
}

static uint32_t resolve(uint32_t c, bool fg, const struct pg_palette *p, uint32_t term_bg)
{
    if (c & VT_COLOR_RGB)
        return c & 0xffffff;
    if (c & VT_COLOR_INDEX) {
        uint32_t i = c & 255;
        const uint32_t *pal = p->light ? ansi_light : ansi_dark;
        if (i < 16)
            return pal[i];
        if (i < 232) {
            i -= 16;
            static const uint8_t lv[6] = {0, 95, 135, 175, 215, 255};
            return ((uint32_t)lv[i / 36] << 16) | ((uint32_t)lv[(i / 6) % 6] << 8) | lv[i % 6];
        }
        uint32_t g = 8 + (i - 232) * 10;
        return (g << 16) | (g << 8) | g;
    }
    return fg ? p->text : term_bg;
}

static void sh_write(struct session *s, const char *data, uint32_t len)
{
    if (!s->used || s->exited || !len)
        return;
    (void)pc_syscall(SYS_FD_WRITE, (uint64_t)s->master, (uint64_t)(uintptr_t)data, len);
}

static void sh_puts(struct session *s, const char *str)
{
    uint32_t n = 0;
    while (str[n])
        n++;
    sh_write(s, str, n);
}

static void set_winsize(struct session *s)
{
    struct tty_winsize ws = {(uint16_t)s->vt.rows, (uint16_t)s->vt.cols,
                             (uint16_t)(s->vt.cols * cell_w()), (uint16_t)(s->vt.rows * cell_h())};
    (void)pc_tty_ioctl(s->master, TTY_TIOCSWINSZ, &ws);
}

static const char *find_shell(void)
{
    static const char *const cands[] = {"/bin/sh", "/usr/bin/sh", "/bin/program/sh"};
    for (unsigned i = 0; i < 3; i++)
        if (pc_file_exists(cands[i]))
            return cands[i];
    return 0;
}

static bool spawn(struct session *s, int cols, int rows)
{
    if (!vt_init(&s->vt, cols, rows))
        return false;
    int32_t fds[2];
    if (pc_syscall(SYS_OPENPTY, (uint64_t)(uintptr_t)fds, 04000U | 02000000U, 0) < 0)
        return false;
    s->master = fds[0];
    set_winsize(s);
    const char *shell = find_shell();
    static struct process_environment_variable vars[ENV_MAX];
    static char entries[ENV_MAX + 8][ENV_ENTRY];
    char *envp[ENV_MAX + 8];
    int n = 0;
    bool have_path = false, have_home = false;
    int32_t listed = pc_listenv(vars, ENV_MAX);
    for (int32_t i = 0; i < listed && n < ENV_MAX; i++) {
        if (!vars[i].used)
            continue;
        if (pc_strcmp(vars[i].name, "TERM") == 0 || pc_strcmp(vars[i].name, "COLORTERM") == 0)
            continue;
        if (pc_strcmp(vars[i].name, "PATH") == 0)
            have_path = true;
        if (pc_strcmp(vars[i].name, "HOME") == 0)
            have_home = true;
        uint32_t len = 0;
        for (uint32_t k = 0; vars[i].name[k] && len + 2 < ENV_ENTRY; k++)
            entries[n][len++] = vars[i].name[k];
        entries[n][len++] = '=';
        for (uint32_t k = 0; vars[i].value[k] && len + 1 < ENV_ENTRY; k++)
            entries[n][len++] = vars[i].value[k];
        entries[n][len] = 0;
        envp[n] = entries[n];
        n++;
    }
    pc_copy(entries[n], "TERM=xterm", ENV_ENTRY);
    envp[n] = entries[n];
    n++;
    pc_copy(entries[n], "COLORTERM=truecolor", ENV_ENTRY);
    envp[n] = entries[n];
    n++;
    if (!have_path) {
        pc_copy(entries[n], "PATH=/usr/bin:/bin:/usr/games:/sbin:/bin/program/system:/bin/program", ENV_ENTRY);
        envp[n] = entries[n];
        n++;
    }
    if (!have_home) {
        pc_copy(entries[n], "HOME=/root", ENV_ENTRY);
        envp[n] = entries[n];
        n++;
    }
    pc_copy(entries[n], "PS1=\\[\\e[1;32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\$ ", ENV_ENTRY);
    envp[n] = entries[n];
    n++;
    envp[n] = 0;
    char *argv[2] = {(char *)"sh", 0};
    if (!shell) {
        const char *msg = "\x1b[1;31mterminal: no shell found (/bin/sh)\x1b[0m\r\n";
        vt_write(&s->vt, (const uint8_t *)msg, (uint32_t)pc_strlen(msg));
        s->exited = true;
        s->used = true;
        return true;
    }
    int32_t pid = pc_fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        (void)pc_syscall(SYS_DUP2, (uint64_t)fds[1], 0, 0);
        (void)pc_syscall(SYS_DUP2, (uint64_t)fds[1], 1, 0);
        (void)pc_syscall(SYS_DUP2, (uint64_t)fds[1], 2, 0);
        if (fds[1] > 2)
            (void)pc_syscall(SYS_FILE_CLOSE, (uint64_t)fds[1], 0, 0);
        (void)pc_syscall(SYS_EXECVE, (uint64_t)(uintptr_t)shell, (uint64_t)(uintptr_t)argv,
                         (uint64_t)(uintptr_t)envp);
        pc_exit(127);
    }
    (void)pc_syscall(SYS_FILE_CLOSE, (uint64_t)fds[1], 0, 0);
    s->pid = pid;
    (void)pc_tty_ioctl(s->master, TTY_TIOCSPGRP_PID, (void *)(uintptr_t)pid);
    s->used = true;
    s->exited = false;
    s->scroll = 0;
    return true;
}

static void grid_size(const struct pg_window *w, int *cols, int *rows)
{
    int aw = (int)w->client.width - 2 * PAD - 8;
    int ah = (int)w->client.height - TAB_BAR_H - 2 * PAD;
    *cols = aw / cell_w();
    *rows = ah / cell_h();
    if (*cols < 10)
        *cols = 10;
    if (*rows < 3)
        *rows = 3;
}

static int new_tab(const struct pg_window *w)
{
    for (int i = 0; i < MAX_TABS; i++) {
        if (tabs[i].used)
            continue;
        int cols, rows;
        grid_size(w, &cols, &rows);
        if (!spawn(&tabs[i], cols, rows))
            return -1;
        return i;
    }
    return -1;
}

static void close_tab(int i)
{
    struct session *s = &tabs[i];
    if (!s->used)
        return;
    if (!s->exited && s->pid > 0)
        (void)pc_syscall(SYS_KILL, (uint64_t)s->pid, 1, 0);
    if (s->master >= 0)
        (void)pc_syscall(SYS_FILE_CLOSE, (uint64_t)s->master, 0, 0);
    if (s->pid > 0) {
        int32_t st;
        (void)pc_syscall(SYS_WAITPID, (uint64_t)s->pid, (uint64_t)(uintptr_t)&st, 1);
    }
    vt_free(&s->vt);
    s->used = false;
    if (active == i) {
        for (int k = 0; k < MAX_TABS; k++)
            if (tabs[k].used) {
                active = k;
                break;
            }
    }
}

static int tab_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_TABS; i++)
        n += tabs[i].used;
    return n;
}

static bool pump(void)
{
    static uint8_t buf[16384];
    bool any = false;
    for (int i = 0; i < MAX_TABS; i++) {
        struct session *s = &tabs[i];
        if (!s->used || s->exited)
            continue;
        for (int rounds = 0; rounds < 16; rounds++) {
            int64_t got = pc_syscall(SYS_FD_READ, (uint64_t)s->master, (uint64_t)(uintptr_t)buf, sizeof(buf));
            if (got <= 0) {
                if (got == 0)
                    s->exited = true;
                break;
            }
            vt_write(&s->vt, buf, (uint32_t)got);
            if (s->vt.reply_len) {
                sh_write(s, s->vt.reply, (uint32_t)s->vt.reply_len);
                s->vt.reply_len = 0;
            }
            any = true;
        }
        if (s->vt.title_changed && i == active)
            s->vt.title_changed = false;
        if (s->pid > 0) {
            int32_t st;
            if (pc_syscall(SYS_WAITPID, (uint64_t)s->pid, (uint64_t)(uintptr_t)&st, 1) > 0) {
                s->pid = 0;
                s->exited = true;
            }
        }
    }
    return any;
}

static void draw_tabbar(struct pg_ui *u, struct pg_window *w)
{
    const struct pg_palette *p = &u->pal;
    struct pg_canvas *c = u->c;
    pgc_fill(c, 0, 0, c->w, TAB_BAR_H, p->sidebar);
    pgc_fill_a(c, 0, TAB_BAR_H - 1, c->w, 1, p->border, 160);
    int x = 8;
    int n = tab_count();
    int tw = n ? (c->w - 60) / n : 200;
    if (tw > 220)
        tw = 220;
    for (int i = 0; i < MAX_TABS; i++) {
        struct session *s = &tabs[i];
        if (!s->used)
            continue;
        bool sel = i == active;
        uint32_t id = pgu_id("tab", i);
        if (pgu_click(u, id, x, 6, tw - 30, TAB_BAR_H - 6))
            active = i;
        bool hot = u->hot == id;
        if (sel)
            pgc_rrect(c, x, 6, tw, TAB_BAR_H - 6, 8, p->bg, 255);
        else if (hot)
            pgc_rrect(c, x, 6, tw, TAB_BAR_H - 12, 8, p->surface2, 255);
        if (sel)
            pgc_fill(c, x, TAB_BAR_H - 8, tw, 8, p->bg);
        pgc_icon(c, PGI_CHEVRON_RIGHT, PG_ICON_S, x + 10, 15, sel ? p->accent : p->text3);
        const char *title = s->vt.title[0] ? s->vt.title : (s->exited ? "[exited]" : "sh");
        pgc_text_fit(c, sel ? PGF_MEDIUM : PGF_BODY, x + 32, 13, tw - 64, title, sel ? p->text : p->text2);
        uint32_t cid = pgu_id("tabx", i);
        if (pgu_click(u, cid, x + tw - 28, 12, 20, 20)) {
            close_tab(i);
            if (!tab_count())
                pg_window_close(w);
            return;
        }
        if (u->hot == cid)
            pgc_rrect(c, x + tw - 28, 12, 20, 20, 6, p->surface3, 255);
        pgc_icon(c, PGI_CLOSE, PG_ICON_S, x + tw - 27, 13, u->hot == cid ? p->danger : p->text3);
        x += tw + 4;
    }
    uint32_t pid = pgu_id("tabplus", 0);
    if (n < MAX_TABS && pgu_click(u, pid, x + 2, 8, 30, 26)) {
        int t = new_tab(w);
        if (t >= 0)
            active = t;
    }
    if (n < MAX_TABS) {
        if (u->hot == pid)
            pgc_rrect(c, x + 2, 8, 30, 26, 8, p->surface2, 255);
        pgc_text_center(c, PGF_TITLE, x + 2, 5, 30, "+", p->text2);
    }
}

static void draw_terminal(struct pg_ui *u, struct session *s)
{
    const struct pg_palette *p = &u->pal;
    struct pg_canvas *c = u->c;
    uint32_t term_bg = p->bg;
    pgc_fill(c, 0, TAB_BAR_H, c->w, c->h - TAB_BAR_H, term_bg);
    if (!s)
        return;
    struct vt *vt = &s->vt;
    int cw = cell_w(), ch = cell_h();
    int ox = PAD, oy = TAB_BAR_H + PAD;
    int fr = font_regular[font_level], fb = font_bold[font_level];
    bool blink_on = ((u->now / 530) & 1) == 0;
    for (int row = 0; row < vt->rows; row++) {
        const struct vt_cell *line = vt_row(vt, row, s->scroll);
        if (!line)
            continue;
        int y = oy + row * ch;
        int x = ox;
        for (int col = 0; col < vt->cols; col++, x += cw) {
            const struct vt_cell *cell = &line[col];
            uint32_t fg = resolve(cell->fg, true, p, term_bg);
            uint32_t bg = resolve(cell->bg, false, p, term_bg);
            if ((cell->attr & VT_ATTR_BOLD) && (cell->fg & VT_COLOR_INDEX) && (cell->fg & 255) < 8)
                fg = resolve(VT_COLOR_INDEX | ((cell->fg & 255) + 8), true, p, term_bg);
            bool rev = ((cell->attr & VT_ATTR_REVERSE) != 0) != vt->reverse_video;
            if (rev) {
                uint32_t t = fg;
                fg = bg;
                bg = t;
            }
            if (cell->attr & VT_ATTR_DIM)
                fg = pgc_mix(fg, bg, 110);
            if (bg != term_bg)
                pgc_fill(c, x, y, cw, ch, bg);
            if (cell->ch != ' ' && cell->ch && !(cell->attr & VT_ATTR_HIDDEN) &&
                (!(cell->attr & VT_ATTR_BLINK) || blink_on))
                pgc_glyph_draw(c, (cell->attr & VT_ATTR_BOLD) ? fb : fr, x, y, cell->ch, fg);
            if (cell->attr & VT_ATTR_UNDERLINE)
                pgc_fill(c, x, y + ch - 2, cw, 1, fg);
            if (cell->attr & VT_ATTR_STRIKE)
                pgc_fill(c, x, y + ch / 2, cw, 1, fg);
        }
    }
    if (s->scroll == 0 && vt->cursor_visible && !s->exited) {
        int x = ox + vt->cx * cw, y = oy + vt->cy * ch;
        int style = vt->cursor_style;
        bool blink = style == 0 || style == 1 || style == 3 || style == 5;
        if (!focused) {
            pgc_rrect_stroke(c, x, y, cw, ch, 2, p->accent, 220);
        } else if (!blink || blink_on) {
            if (style == 5 || style == 6)
                pgc_fill(c, x, y, 2, ch, p->accent);
            else if (style == 3 || style == 4)
                pgc_fill(c, x, y + ch - 3, cw, 3, p->accent);
            else {
                pgc_fill(c, x, y, cw, ch, p->accent);
                const struct vt_cell *cell = &vt->screen[vt->cy * vt->cols + vt->cx];
                if (cell->ch != ' ' && cell->ch)
                    pgc_glyph_draw(c, (cell->attr & VT_ATTR_BOLD) ? fb : fr, x, y, cell->ch, p->on_accent);
            }
        }
        u->animating = true;
    }
    int total = vt_scrollback_lines(vt);
    if (total > 0 && s->scroll > 0) {
        int track = vt->rows * ch;
        int thumb = track * vt->rows / (total + vt->rows);
        if (thumb < 24)
            thumb = 24;
        int pos = (track - thumb) * (total - s->scroll) / total;
        pgc_rrect(c, c->w - 8, oy + pos, 4, thumb, 2, p->text3, 140);
        char badge[32];
        int n = 0;
        int v = s->scroll;
        char tmp[12];
        int t = 0;
        do {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && t < 11);
        badge[n++] = '+';
        while (t)
            badge[n++] = tmp[--t];
        const char *suffix = " lines";
        while (*suffix)
            badge[n++] = *suffix++;
        badge[n] = 0;
        int bw = pgc_text_w(PGF_SMALL, badge) + 20;
        pgc_rrect(c, c->w - bw - 18, oy + 6, bw, 24, 12, p->surface3, 230);
        pgc_text(c, PGF_SMALL, c->w - bw - 8, oy + 10, badge, p->text);
    }
    if (s->exited) {
        const char *m = "Process exited. Press Enter to restart, Ctrl+Shift+W to close.";
        int mw = pgc_text_w(PGF_BODY, m) + 32;
        int mx = (c->w - mw) / 2, my = c->h - 56;
        pgc_rrect(c, mx, my, mw, 36, 18, p->surface3, 240);
        pgc_text(c, PGF_BODY, mx + 16, my + 9, m, p->text);
    }
}

static void render(struct pg_window *w)
{
    pg_window_begin(w);
    pgu_palette_from_theme(&ui.pal, &w->theme);
    if (!pg_window_is_minimized(w)) {
        struct pg_canvas *c = pg_window_canvas(w);
        if (c) {
            pgu_frame_begin(&ui, c, now_ms());
            draw_tabbar(&ui, w);
            draw_terminal(&ui, tabs[active].used ? &tabs[active] : 0);
            pgu_frame_end(&ui);
        }
    }
    pg_window_end(w);
}

static void resize_all(const struct pg_window *w)
{
    int cols, rows;
    grid_size(w, &cols, &rows);
    for (int i = 0; i < MAX_TABS; i++) {
        struct session *s = &tabs[i];
        if (!s->used || (s->vt.cols == cols && s->vt.rows == rows))
            continue;
        vt_resize(&s->vt, cols, rows);
        if (!s->exited)
            set_winsize(s);
    }
}

static void special_key(struct session *s, int key)
{
    bool app = s->vt.app_cursor;
    bool shift = (mods & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT)) != 0;
    if (shift && (key == KEYBOARD_SPECIAL_PAGE_UP || key == KEYBOARD_SPECIAL_PAGE_DOWN)) {
        int page = s->vt.rows - 1;
        s->scroll += key == KEYBOARD_SPECIAL_PAGE_UP ? page : -page;
        if (s->scroll > vt_scrollback_lines(&s->vt))
            s->scroll = vt_scrollback_lines(&s->vt);
        if (s->scroll < 0)
            s->scroll = 0;
        return;
    }
    const char *seq = 0;
    switch (key) {
    case KEYBOARD_SPECIAL_UP:
        seq = app ? "\x1bOA" : "\x1b[A";
        break;
    case KEYBOARD_SPECIAL_DOWN:
        seq = app ? "\x1bOB" : "\x1b[B";
        break;
    case KEYBOARD_SPECIAL_RIGHT:
        seq = app ? "\x1bOC" : "\x1b[C";
        break;
    case KEYBOARD_SPECIAL_LEFT:
        seq = app ? "\x1bOD" : "\x1b[D";
        break;
    case KEYBOARD_SPECIAL_HOME:
        seq = app ? "\x1bOH" : "\x1b[H";
        break;
    case KEYBOARD_SPECIAL_END:
        seq = app ? "\x1bOF" : "\x1b[F";
        break;
    case KEYBOARD_SPECIAL_PAGE_UP:
        seq = "\x1b[5~";
        break;
    case KEYBOARD_SPECIAL_PAGE_DOWN:
        seq = "\x1b[6~";
        break;
    case KEYBOARD_SPECIAL_DELETE:
        seq = "\x1b[3~";
        break;
    case KEYBOARD_SPECIAL_F1:
        seq = "\x1bOP";
        break;
    case KEYBOARD_SPECIAL_F2:
        seq = "\x1bOQ";
        break;
    case KEYBOARD_SPECIAL_F3:
        seq = "\x1bOR";
        break;
    default:
        break;
    }
    if (seq) {
        s->scroll = 0;
        sh_puts(s, seq);
    }
}

static bool handle_key(struct pg_window *w, int key)
{
    struct session *s = &tabs[active];
    bool ctrl = (mods & (KEY_MOD_LCTRL | KEY_MOD_RCTRL)) != 0;
    bool shift = (mods & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT)) != 0;
    if (ctrl && shift && key == 20) {
        int t = new_tab(w);
        if (t >= 0)
            active = t;
        return true;
    }
    if (ctrl && shift && key == 23) {
        close_tab(active);
        if (!tab_count())
            pg_window_close(w);
        return true;
    }
    if (ctrl && (key == '=' || key == '+')) {
        if (font_level < 2) {
            font_level++;
            resize_all(w);
        }
        return true;
    }
    if (ctrl && key == '-') {
        if (font_level > 0) {
            font_level--;
            resize_all(w);
        }
        return true;
    }
    if (ctrl && key == '0') {
        font_level = 1;
        resize_all(w);
        return true;
    }
    if (ctrl && key == 9) {
        for (int k = 1; k <= MAX_TABS; k++) {
            int t = (active + (shift ? MAX_TABS - k : k)) % MAX_TABS;
            if (tabs[t].used) {
                active = t;
                break;
            }
        }
        return true;
    }
    if (!s->used)
        return false;
    if (s->exited) {
        if (key == '\n' || key == '\r') {
            vt_free(&s->vt);
            if (s->master >= 0)
                (void)pc_syscall(SYS_FILE_CLOSE, (uint64_t)s->master, 0, 0);
            int cols, rows;
            grid_size(w, &cols, &rows);
            s->used = false;
            (void)spawn(s, cols, rows);
        }
        return true;
    }
    s->scroll = 0;
    char b[2];
    if (key == '\n' || key == '\r')
        b[0] = '\r';
    else if (key == '\b')
        b[0] = 127;
    else
        b[0] = (char)key;
    sh_write(s, b, 1);
    return true;
}

static int terminal_main(void)
{
    struct pc_display_info d;
    if (!pc_display_get_info(&d) || !d.available)
        return 1;
    uint32_t width = d.width > 1100 ? 1000 : d.width - 40;
    uint32_t height = d.height > 760 ? 640 : d.height - 60;
    struct pg_window w;
    if (!pg_window_center(&w, app_title, width, height))
        return 1;
    pg_window_set_esc_closes(&w, false);
    if (new_tab(&w) < 0)
        return 1;
    active = 0;
    uint32_t last_w = w.client.width, last_h = w.client.height;
    bool dirty = true;
    uint64_t last_frame = 0;
    while (pg_window_is_open(&w)) {
        struct key_event kev;
        while (pg_window_poll_key_event(&w, &kev))
            mods = kev.mods;
        struct pg_event ev;
        int handled = 0;
        while (handled < 64 && pg_window_poll_event(&w, &ev)) {
            handled++;
            if (ev.type == PG_EVENT_CLOSE)
                goto out;
            if (ev.type == PG_EVENT_FOCUS)
                focused = true;
            if (ev.type == PG_EVENT_KEY) {
                handle_key(&w, ev.key);
                dirty = true;
                continue;
            }
            if (ev.type == PG_EVENT_SPECIAL_KEY) {
                if (tabs[active].used && !tabs[active].exited)
                    special_key(&tabs[active], ev.key);
                dirty = true;
                continue;
            }
            pgu_feed(&ui, &w, &ev);
            dirty = true;
        }
        focused = w.focused;
        if (w.client.width != last_w || w.client.height != last_h) {
            last_w = w.client.width;
            last_h = w.client.height;
            resize_all(&w);
            dirty = true;
        }
        int32_t wheel = pg_window_take_wheel(&w);
        if (wheel && tabs[active].used) {
            struct session *s = &tabs[active];
            s->scroll += wheel * 3;
            if (s->scroll > vt_scrollback_lines(&s->vt))
                s->scroll = vt_scrollback_lines(&s->vt);
            if (s->scroll < 0)
                s->scroll = 0;
            dirty = true;
        }
        if (pump())
            dirty = true;
        if (!pg_window_is_open(&w))
            break;
        uint64_t now = now_ms();
        if (ui.animating && now - last_frame >= 265)
            dirty = true;
        if (!dirty) {
            pc_sleep(8);
            continue;
        }
        dirty = false;
        last_frame = now;
        render(&w);
    }
out:
    for (int i = 0; i < MAX_TABS; i++)
        close_tab(i);
    return 0;
}

void _start(void) { pc_exit(terminal_main()); }
