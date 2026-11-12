#include "vt.h"
#include <stddef.h>

void *malloc(size_t size);
void free(void *pointer);

enum { S_GROUND, S_ESC, S_CSI, S_OSC, S_OSC_ESC, S_STR, S_STR_ESC, S_CHARSET };

static const uint16_t dec_graphics[31] = {
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518,
    0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524,
    0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

static void cell_clear(struct vt *vt, struct vt_cell *c)
{
    c->ch = ' ';
    c->fg = VT_COLOR_DEFAULT;
    c->bg = vt->bg;
    c->attr = 0;
}

static struct vt_cell *cell_at(struct vt *vt, int x, int y)
{
    return &vt->screen[y * vt->cols + x];
}

static void clear_range(struct vt *vt, int y, int x0, int x1)
{
    for (int x = x0; x < x1; x++)
        cell_clear(vt, cell_at(vt, x, y));
}

static void reset_tabs(struct vt *vt)
{
    for (int i = 0; i < VT_MAX_COLS; i++)
        vt->tabs[i] = i && i % 8 == 0;
}

static void soft_reset(struct vt *vt)
{
    vt->fg = VT_COLOR_DEFAULT;
    vt->bg = VT_COLOR_DEFAULT;
    vt->attr = 0;
    vt->top = 0;
    vt->bottom = vt->rows - 1;
    vt->autowrap = true;
    vt->origin = false;
    vt->insert = false;
    vt->app_cursor = false;
    vt->app_keypad = false;
    vt->cursor_visible = true;
    vt->bracketed = false;
    vt->lnm = false;
    vt->cursor_style = 0;
    vt->g0_graphics = false;
    vt->g1_graphics = false;
    vt->gl = 0;
    vt->wrap_pending = false;
}

bool vt_init(struct vt *vt, int cols, int rows)
{
    for (size_t i = 0; i < sizeof(*vt); i++)
        ((char *)vt)[i] = 0;
    size_t screen = (size_t)VT_MAX_COLS * VT_MAX_ROWS * sizeof(struct vt_cell);
    vt->main = malloc(screen);
    vt->alt = malloc(screen);
    vt->sb = malloc((size_t)VT_SCROLLBACK * VT_MAX_COLS * sizeof(struct vt_cell));
    if (!vt->main || !vt->alt || !vt->sb)
        return false;
    vt->cols = cols < 2 ? 2 : cols > VT_MAX_COLS ? VT_MAX_COLS : cols;
    vt->rows = rows < 2 ? 2 : rows > VT_MAX_ROWS ? VT_MAX_ROWS : rows;
    vt->screen = vt->main;
    soft_reset(vt);
    reset_tabs(vt);
    for (int y = 0; y < vt->rows; y++)
        clear_range(vt, y, 0, vt->cols);
    vt->screen = vt->alt;
    for (int y = 0; y < vt->rows; y++)
        clear_range(vt, y, 0, vt->cols);
    vt->screen = vt->main;
    vt->dirty = true;
    return true;
}

void vt_free(struct vt *vt)
{
    free(vt->main);
    free(vt->alt);
    free(vt->sb);
    vt->main = vt->alt = vt->sb = 0;
}

int vt_scrollback_lines(const struct vt *vt)
{
    return vt->alt_active ? 0 : vt->sb_count;
}

const struct vt_cell *vt_row(const struct vt *vt, int row, int scroll)
{
    int line = row - scroll;
    if (line >= 0)
        return &vt->screen[line * vt->cols];
    int back = -line;
    if (back > vt->sb_count)
        return 0;
    int idx = (vt->sb_head - back + VT_SCROLLBACK) % VT_SCROLLBACK;
    return &vt->sb[(size_t)idx * VT_MAX_COLS];
}

static void push_scrollback(struct vt *vt, const struct vt_cell *row)
{
    struct vt_cell *dst = &vt->sb[(size_t)vt->sb_head * VT_MAX_COLS];
    for (int x = 0; x < VT_MAX_COLS; x++) {
        if (x < vt->cols)
            dst[x] = row[x];
        else
            cell_clear(vt, &dst[x]);
    }
    vt->sb_head = (vt->sb_head + 1) % VT_SCROLLBACK;
    if (vt->sb_count < VT_SCROLLBACK)
        vt->sb_count++;
}

static void scroll_up(struct vt *vt, int top, int bottom, int n)
{
    if (n <= 0)
        return;
    if (n > bottom - top + 1)
        n = bottom - top + 1;
    if (top == 0 && !vt->alt_active)
        for (int i = 0; i < n; i++)
            push_scrollback(vt, &vt->screen[(top + i) * vt->cols]);
    for (int y = top; y <= bottom - n; y++)
        for (int x = 0; x < vt->cols; x++)
            *cell_at(vt, x, y) = *cell_at(vt, x, y + n);
    for (int y = bottom - n + 1; y <= bottom; y++)
        clear_range(vt, y, 0, vt->cols);
}

static void scroll_down(struct vt *vt, int top, int bottom, int n)
{
    if (n <= 0)
        return;
    if (n > bottom - top + 1)
        n = bottom - top + 1;
    for (int y = bottom; y >= top + n; y--)
        for (int x = 0; x < vt->cols; x++)
            *cell_at(vt, x, y) = *cell_at(vt, x, y - n);
    for (int y = top; y < top + n; y++)
        clear_range(vt, y, 0, vt->cols);
}

void vt_resize(struct vt *vt, int cols, int rows)
{
    cols = cols < 2 ? 2 : cols > VT_MAX_COLS ? VT_MAX_COLS : cols;
    rows = rows < 2 ? 2 : rows > VT_MAX_ROWS ? VT_MAX_ROWS : rows;
    if (cols == vt->cols && rows == vt->rows)
        return;
    struct vt_cell *bufs[2] = {vt->main, vt->alt};
    int shift = 0;
    if (!vt->alt_active && vt->cy >= rows)
        shift = vt->cy - rows + 1;
    for (int b = 0; b < 2; b++) {
        struct vt_cell *s = bufs[b];
        int sh = b == 0 ? shift : 0;
        if (b == 0 && sh) {
            struct vt_cell *saved = vt->screen;
            vt->screen = s;
            for (int i = 0; i < sh; i++)
                push_scrollback(vt, &s[i * vt->cols]);
            vt->screen = saved;
        }
        static struct vt_cell old[VT_MAX_COLS * VT_MAX_ROWS];
        for (int i = 0; i < vt->cols * vt->rows; i++)
            old[i] = s[i];
        for (int y = 0; y < rows; y++) {
            int sy = y + sh;
            for (int x = 0; x < cols; x++) {
                if (sy < vt->rows && x < vt->cols)
                    s[y * cols + x] = old[sy * vt->cols + x];
                else
                    cell_clear(vt, &s[y * cols + x]);
            }
        }
    }
    vt->cy -= shift;
    vt->cols = cols;
    vt->rows = rows;
    if (vt->cx >= cols)
        vt->cx = cols - 1;
    if (vt->cy >= rows)
        vt->cy = rows - 1;
    if (vt->cy < 0)
        vt->cy = 0;
    vt->top = 0;
    vt->bottom = rows - 1;
    vt->wrap_pending = false;
    vt->dirty = true;
}

static void reply(struct vt *vt, const char *s)
{
    while (*s && vt->reply_len < (int)sizeof(vt->reply))
        vt->reply[vt->reply_len++] = *s++;
}

static void reply_num(struct vt *vt, int v)
{
    char b[12];
    int n = 0;
    if (v == 0)
        b[n++] = '0';
    while (v > 0 && n < 11) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        if (vt->reply_len < (int)sizeof(vt->reply))
            vt->reply[vt->reply_len++] = b[--n];
}

static void linefeed(struct vt *vt)
{
    vt->wrap_pending = false;
    if (vt->cy == vt->bottom)
        scroll_up(vt, vt->top, vt->bottom, 1);
    else if (vt->cy < vt->rows - 1)
        vt->cy++;
}

static void reverse_index(struct vt *vt)
{
    vt->wrap_pending = false;
    if (vt->cy == vt->top)
        scroll_down(vt, vt->top, vt->bottom, 1);
    else if (vt->cy > 0)
        vt->cy--;
}

static void put_char(struct vt *vt, uint32_t ch)
{
    bool graphics = vt->gl ? vt->g1_graphics : vt->g0_graphics;
    if (graphics && ch == 0x5f)
        ch = ' ';
    else if (graphics && ch >= 0x60 && ch <= 0x7e)
        ch = dec_graphics[ch - 0x60];
    if (vt->wrap_pending) {
        if (vt->autowrap) {
            vt->cx = 0;
            linefeed(vt);
        }
        vt->wrap_pending = false;
    }
    if (vt->insert) {
        for (int x = vt->cols - 1; x > vt->cx; x--)
            *cell_at(vt, x, vt->cy) = *cell_at(vt, x - 1, vt->cy);
    }
    struct vt_cell *c = cell_at(vt, vt->cx, vt->cy);
    c->ch = ch;
    c->fg = vt->fg;
    c->bg = vt->bg;
    c->attr = vt->attr;
    vt->last_ch = ch;
    if (vt->cx == vt->cols - 1)
        vt->wrap_pending = true;
    else
        vt->cx++;
}

static int param(const struct vt *vt, int i, int def)
{
    if (i >= vt->nparams || vt->params[i] <= 0)
        return def;
    return vt->params[i];
}

static void clamp_cursor(struct vt *vt)
{
    if (vt->cx < 0)
        vt->cx = 0;
    if (vt->cx >= vt->cols)
        vt->cx = vt->cols - 1;
    int lo = vt->origin ? vt->top : 0, hi = vt->origin ? vt->bottom : vt->rows - 1;
    if (vt->cy < lo)
        vt->cy = lo;
    if (vt->cy > hi)
        vt->cy = hi;
    vt->wrap_pending = false;
}

static void save_cursor(struct vt *vt)
{
    struct vt_saved *s = vt->alt_active ? &vt->saved_alt : &vt->saved;
    s->cx = vt->cx;
    s->cy = vt->cy;
    s->fg = vt->fg;
    s->bg = vt->bg;
    s->attr = vt->attr;
    s->origin = vt->origin;
    s->g0_graphics = vt->g0_graphics;
    s->g1_graphics = vt->g1_graphics;
    s->gl = vt->gl;
}

static void restore_cursor(struct vt *vt)
{
    struct vt_saved *s = vt->alt_active ? &vt->saved_alt : &vt->saved;
    vt->cx = s->cx;
    vt->cy = s->cy;
    vt->fg = s->fg ? s->fg : VT_COLOR_DEFAULT;
    vt->bg = s->bg ? s->bg : VT_COLOR_DEFAULT;
    vt->attr = s->attr;
    vt->origin = s->origin;
    vt->g0_graphics = s->g0_graphics;
    vt->g1_graphics = s->g1_graphics;
    vt->gl = s->gl;
    clamp_cursor(vt);
}

static void set_alt(struct vt *vt, bool on, bool save, bool clear)
{
    if (on == vt->alt_active)
        return;
    if (on) {
        if (save)
            save_cursor(vt);
        vt->alt_active = true;
        vt->screen = vt->alt;
        if (clear)
            for (int y = 0; y < vt->rows; y++)
                clear_range(vt, y, 0, vt->cols);
    } else {
        vt->alt_active = false;
        vt->screen = vt->main;
        if (save)
            restore_cursor(vt);
    }
    vt->dirty = true;
}

static void sgr(struct vt *vt)
{
    if (vt->nparams == 0) {
        vt->fg = vt->bg = VT_COLOR_DEFAULT;
        vt->attr = 0;
        return;
    }
    for (int i = 0; i < vt->nparams; i++) {
        int p = vt->params[i] < 0 ? 0 : vt->params[i];
        if (p == 0) {
            vt->fg = vt->bg = VT_COLOR_DEFAULT;
            vt->attr = 0;
        } else if (p == 1)
            vt->attr |= VT_ATTR_BOLD;
        else if (p == 2)
            vt->attr |= VT_ATTR_DIM;
        else if (p == 3)
            vt->attr |= VT_ATTR_ITALIC;
        else if (p == 4)
            vt->attr |= VT_ATTR_UNDERLINE;
        else if (p == 5 || p == 6)
            vt->attr |= VT_ATTR_BLINK;
        else if (p == 7)
            vt->attr |= VT_ATTR_REVERSE;
        else if (p == 8)
            vt->attr |= VT_ATTR_HIDDEN;
        else if (p == 9)
            vt->attr |= VT_ATTR_STRIKE;
        else if (p == 21 || p == 22)
            vt->attr &= (uint16_t)~(VT_ATTR_BOLD | VT_ATTR_DIM);
        else if (p == 23)
            vt->attr &= (uint16_t)~VT_ATTR_ITALIC;
        else if (p == 24)
            vt->attr &= (uint16_t)~VT_ATTR_UNDERLINE;
        else if (p == 25)
            vt->attr &= (uint16_t)~VT_ATTR_BLINK;
        else if (p == 27)
            vt->attr &= (uint16_t)~VT_ATTR_REVERSE;
        else if (p == 28)
            vt->attr &= (uint16_t)~VT_ATTR_HIDDEN;
        else if (p == 29)
            vt->attr &= (uint16_t)~VT_ATTR_STRIKE;
        else if (p >= 30 && p <= 37)
            vt->fg = VT_COLOR_INDEX | (uint32_t)(p - 30);
        else if (p == 39)
            vt->fg = VT_COLOR_DEFAULT;
        else if (p >= 40 && p <= 47)
            vt->bg = VT_COLOR_INDEX | (uint32_t)(p - 40);
        else if (p == 49)
            vt->bg = VT_COLOR_DEFAULT;
        else if (p >= 90 && p <= 97)
            vt->fg = VT_COLOR_INDEX | (uint32_t)(p - 90 + 8);
        else if (p >= 100 && p <= 107)
            vt->bg = VT_COLOR_INDEX | (uint32_t)(p - 100 + 8);
        else if (p == 38 || p == 48) {
            uint32_t *dst = p == 38 ? &vt->fg : &vt->bg;
            int mode = i + 1 < vt->nparams ? vt->params[i + 1] : -1;
            if (mode == 5 && i + 2 < vt->nparams) {
                *dst = VT_COLOR_INDEX | (uint32_t)(vt->params[i + 2] & 255);
                i += 2;
            } else if (mode == 2 && i + 4 < vt->nparams) {
                *dst = VT_COLOR_RGB | ((uint32_t)(vt->params[i + 2] & 255) << 16) |
                       ((uint32_t)(vt->params[i + 3] & 255) << 8) | (uint32_t)(vt->params[i + 4] & 255);
                i += 4;
            } else {
                i = vt->nparams;
            }
        }
    }
}

static void set_mode(struct vt *vt, bool on)
{
    for (int i = 0; i < (vt->nparams ? vt->nparams : 1); i++) {
        int p = vt->nparams ? vt->params[i] : 0;
        if (vt->priv == '?') {
            switch (p) {
            case 1:
                vt->app_cursor = on;
                break;
            case 5:
                vt->reverse_video = on;
                vt->dirty = true;
                break;
            case 6:
                vt->origin = on;
                vt->cx = 0;
                vt->cy = on ? vt->top : 0;
                break;
            case 7:
                vt->autowrap = on;
                break;
            case 25:
                vt->cursor_visible = on;
                break;
            case 47:
            case 1047:
                set_alt(vt, on, false, on);
                break;
            case 1048:
                if (on)
                    save_cursor(vt);
                else
                    restore_cursor(vt);
                break;
            case 1049:
                set_alt(vt, on, true, true);
                break;
            case 2004:
                vt->bracketed = on;
                break;
            default:
                break;
            }
        } else {
            if (p == 4)
                vt->insert = on;
            else if (p == 20)
                vt->lnm = on;
        }
    }
}

static void csi_dispatch(struct vt *vt, char f)
{
    int n;
    switch (f) {
    case 'A':
        vt->cy -= param(vt, 0, 1);
        if (vt->cy < vt->top && vt->cy + param(vt, 0, 1) >= vt->top)
            vt->cy = vt->top;
        clamp_cursor(vt);
        break;
    case 'B':
    case 'e':
        vt->cy += param(vt, 0, 1);
        if (vt->cy > vt->bottom && vt->cy - param(vt, 0, 1) <= vt->bottom)
            vt->cy = vt->bottom;
        clamp_cursor(vt);
        break;
    case 'C':
    case 'a':
        vt->cx += param(vt, 0, 1);
        clamp_cursor(vt);
        break;
    case 'D':
        vt->cx -= param(vt, 0, 1);
        clamp_cursor(vt);
        break;
    case 'E':
        vt->cy += param(vt, 0, 1);
        vt->cx = 0;
        clamp_cursor(vt);
        break;
    case 'F':
        vt->cy -= param(vt, 0, 1);
        vt->cx = 0;
        clamp_cursor(vt);
        break;
    case 'G':
    case '`':
        vt->cx = param(vt, 0, 1) - 1;
        clamp_cursor(vt);
        break;
    case 'd':
        vt->cy = param(vt, 0, 1) - 1 + (vt->origin ? vt->top : 0);
        clamp_cursor(vt);
        break;
    case 'H':
    case 'f':
        vt->cy = param(vt, 0, 1) - 1 + (vt->origin ? vt->top : 0);
        vt->cx = param(vt, 1, 1) - 1;
        clamp_cursor(vt);
        break;
    case 'J':
        n = param(vt, 0, 0);
        if (vt->nparams == 0)
            n = 0;
        else
            n = vt->params[0] < 0 ? 0 : vt->params[0];
        if (n == 0) {
            clear_range(vt, vt->cy, vt->cx, vt->cols);
            for (int y = vt->cy + 1; y < vt->rows; y++)
                clear_range(vt, y, 0, vt->cols);
        } else if (n == 1) {
            clear_range(vt, vt->cy, 0, vt->cx + 1);
            for (int y = 0; y < vt->cy; y++)
                clear_range(vt, y, 0, vt->cols);
        } else if (n == 2 || n == 3) {
            for (int y = 0; y < vt->rows; y++)
                clear_range(vt, y, 0, vt->cols);
            if (n == 3)
                vt->sb_count = 0;
        }
        vt->wrap_pending = false;
        break;
    case 'K':
        n = vt->nparams ? (vt->params[0] < 0 ? 0 : vt->params[0]) : 0;
        if (n == 0)
            clear_range(vt, vt->cy, vt->cx, vt->cols);
        else if (n == 1)
            clear_range(vt, vt->cy, 0, vt->cx + 1);
        else
            clear_range(vt, vt->cy, 0, vt->cols);
        vt->wrap_pending = false;
        break;
    case 'L':
        if (vt->cy >= vt->top && vt->cy <= vt->bottom)
            scroll_down(vt, vt->cy, vt->bottom, param(vt, 0, 1));
        vt->cx = 0;
        break;
    case 'M':
        if (vt->cy >= vt->top && vt->cy <= vt->bottom)
            scroll_up(vt, vt->cy, vt->bottom, param(vt, 0, 1));
        vt->cx = 0;
        break;
    case '@':
        n = param(vt, 0, 1);
        for (int x = vt->cols - 1; x >= vt->cx; x--) {
            if (x - n >= vt->cx)
                *cell_at(vt, x, vt->cy) = *cell_at(vt, x - n, vt->cy);
            else
                cell_clear(vt, cell_at(vt, x, vt->cy));
        }
        break;
    case 'P':
        n = param(vt, 0, 1);
        for (int x = vt->cx; x < vt->cols; x++) {
            if (x + n < vt->cols)
                *cell_at(vt, x, vt->cy) = *cell_at(vt, x + n, vt->cy);
            else
                cell_clear(vt, cell_at(vt, x, vt->cy));
        }
        vt->wrap_pending = false;
        break;
    case 'X':
        n = param(vt, 0, 1);
        clear_range(vt, vt->cy, vt->cx, vt->cx + n > vt->cols ? vt->cols : vt->cx + n);
        vt->wrap_pending = false;
        break;
    case 'S':
        scroll_up(vt, vt->top, vt->bottom, param(vt, 0, 1));
        break;
    case 'T':
        scroll_down(vt, vt->top, vt->bottom, param(vt, 0, 1));
        break;
    case 'b':
        n = param(vt, 0, 1);
        for (int i = 0; i < n && i < 1000; i++)
            put_char(vt, vt->last_ch ? vt->last_ch : ' ');
        break;
    case 'm':
        if (vt->priv)
            break;
        sgr(vt);
        break;
    case 'r':
        if (vt->priv)
            break;
        vt->top = param(vt, 0, 1) - 1;
        vt->bottom = param(vt, 1, vt->rows) - 1;
        if (vt->top < 0)
            vt->top = 0;
        if (vt->bottom >= vt->rows)
            vt->bottom = vt->rows - 1;
        if (vt->top >= vt->bottom) {
            vt->top = 0;
            vt->bottom = vt->rows - 1;
        }
        vt->cx = 0;
        vt->cy = vt->origin ? vt->top : 0;
        vt->wrap_pending = false;
        break;
    case 's':
        if (!vt->priv)
            save_cursor(vt);
        break;
    case 'u':
        if (!vt->priv)
            restore_cursor(vt);
        break;
    case 'h':
        set_mode(vt, true);
        break;
    case 'l':
        set_mode(vt, false);
        break;
    case 'n':
        if (param(vt, 0, 0) == 6) {
            reply(vt, "\x1b[");
            reply_num(vt, vt->cy + 1 - (vt->origin ? vt->top : 0));
            reply(vt, ";");
            reply_num(vt, vt->cx + 1);
            reply(vt, "R");
        } else if (param(vt, 0, 0) == 5) {
            reply(vt, "\x1b[0n");
        }
        break;
    case 'c':
        if (vt->priv == '>')
            reply(vt, "\x1b[>0;10;0c");
        else if (!vt->priv)
            reply(vt, "\x1b[?62;22c");
        break;
    case 'g':
        if (param(vt, 0, 0) == 3 || (vt->nparams && vt->params[0] == 3))
            for (int i = 0; i < VT_MAX_COLS; i++)
                vt->tabs[i] = false;
        else if (vt->cx < VT_MAX_COLS)
            vt->tabs[vt->cx] = false;
        break;
    case 'q':
        if (vt->inter == ' ')
            vt->cursor_style = param(vt, 0, 0);
        break;
    case 'I':
        for (int i = 0; i < param(vt, 0, 1); i++) {
            do
                vt->cx++;
            while (vt->cx < vt->cols - 1 && !vt->tabs[vt->cx]);
        }
        clamp_cursor(vt);
        break;
    case 'Z':
        for (int i = 0; i < param(vt, 0, 1); i++) {
            do
                vt->cx--;
            while (vt->cx > 0 && !vt->tabs[vt->cx]);
        }
        clamp_cursor(vt);
        break;
    default:
        break;
    }
}

static void osc_dispatch(struct vt *vt)
{
    vt->osc[vt->osc_len] = 0;
    int code = 0, i = 0;
    while (vt->osc[i] >= '0' && vt->osc[i] <= '9')
        code = code * 10 + (vt->osc[i++] - '0');
    if (vt->osc[i] != ';')
        return;
    i++;
    if (code == 0 || code == 2) {
        int n = 0;
        while (vt->osc[i] && n < (int)sizeof(vt->title) - 1)
            vt->title[n++] = vt->osc[i++];
        vt->title[n] = 0;
        vt->title_changed = true;
    }
}

static void esc_dispatch(struct vt *vt, uint8_t c)
{
    switch (c) {
    case '7':
        save_cursor(vt);
        break;
    case '8':
        restore_cursor(vt);
        break;
    case 'D':
        linefeed(vt);
        break;
    case 'E':
        vt->cx = 0;
        linefeed(vt);
        break;
    case 'M':
        reverse_index(vt);
        break;
    case 'H':
        if (vt->cx < VT_MAX_COLS)
            vt->tabs[vt->cx] = true;
        break;
    case 'c':
        set_alt(vt, false, false, false);
        soft_reset(vt);
        reset_tabs(vt);
        for (int y = 0; y < vt->rows; y++)
            clear_range(vt, y, 0, vt->cols);
        vt->cx = vt->cy = 0;
        vt->sb_count = 0;
        break;
    case '=':
        vt->app_keypad = true;
        break;
    case '>':
        vt->app_keypad = false;
        break;
    default:
        break;
    }
}

static void control(struct vt *vt, uint8_t c)
{
    switch (c) {
    case 7:
        vt->bell++;
        break;
    case 8:
        if (vt->cx > 0)
            vt->cx--;
        vt->wrap_pending = false;
        break;
    case 9:
        while (vt->cx < vt->cols - 1) {
            vt->cx++;
            if (vt->tabs[vt->cx])
                break;
        }
        break;
    case 10:
    case 11:
    case 12:
        linefeed(vt);
        if (vt->lnm)
            vt->cx = 0;
        break;
    case 13:
        vt->cx = 0;
        vt->wrap_pending = false;
        break;
    case 14:
        vt->gl = 1;
        break;
    case 15:
        vt->gl = 0;
        break;
    default:
        break;
    }
}

static void feed(struct vt *vt, uint8_t c)
{
    if (vt->state == S_OSC) {
        if (c == 7) {
            osc_dispatch(vt);
            vt->state = S_GROUND;
        } else if (c == 0x1b) {
            vt->state = S_OSC_ESC;
        } else if (vt->osc_len < (int)sizeof(vt->osc) - 1) {
            vt->osc[vt->osc_len++] = (char)c;
        }
        return;
    }
    if (vt->state == S_OSC_ESC) {
        osc_dispatch(vt);
        vt->state = S_GROUND;
        if (c != '\\')
            feed(vt, c);
        return;
    }
    if (vt->state == S_STR) {
        if (c == 0x1b)
            vt->state = S_STR_ESC;
        else if (c == 7)
            vt->state = S_GROUND;
        return;
    }
    if (vt->state == S_STR_ESC) {
        vt->state = c == '\\' ? S_GROUND : S_STR;
        return;
    }
    if (vt->state == S_CHARSET) {
        bool graphics = c == '0';
        if (vt->inter == '(')
            vt->g0_graphics = graphics;
        else if (vt->inter == ')')
            vt->g1_graphics = graphics;
        vt->state = S_GROUND;
        return;
    }
    if (c == 0x1b) {
        vt->state = S_ESC;
        vt->utf_need = 0;
        return;
    }
    if (c < 0x20 || c == 0x7f) {
        if (c != 0x7f)
            control(vt, c);
        return;
    }
    if (vt->state == S_ESC) {
        if (c == '[') {
            vt->state = S_CSI;
            vt->nparams = 0;
            vt->param_started = false;
            vt->priv = 0;
            vt->inter = 0;
            for (int i = 0; i < 16; i++)
                vt->params[i] = -1;
            return;
        }
        if (c == ']') {
            vt->state = S_OSC;
            vt->osc_len = 0;
            return;
        }
        if (c == 'P' || c == 'X' || c == '^' || c == '_') {
            vt->state = S_STR;
            return;
        }
        if (c == '(' || c == ')' || c == '*' || c == '+') {
            vt->inter = (char)c;
            vt->state = S_CHARSET;
            return;
        }
        if (c == '#' || c == '%' || c == ' ') {
            vt->inter = (char)c;
            vt->state = S_CHARSET;
            return;
        }
        vt->state = S_GROUND;
        esc_dispatch(vt, c);
        return;
    }
    if (vt->state == S_CSI) {
        if (c >= '0' && c <= '9') {
            if (!vt->param_started) {
                if (vt->nparams < 16)
                    vt->params[vt->nparams++] = 0;
                vt->param_started = true;
            }
            if (vt->nparams && vt->params[vt->nparams - 1] < 100000)
                vt->params[vt->nparams - 1] = vt->params[vt->nparams - 1] * 10 + (c - '0');
            return;
        }
        if (c == ';' || c == ':') {
            if (!vt->param_started && vt->nparams < 16)
                vt->params[vt->nparams++] = -1;
            vt->param_started = false;
            return;
        }
        if (c == '?' || c == '>' || c == '<' || c == '=') {
            vt->priv = (char)c;
            return;
        }
        if (c >= 0x20 && c <= 0x2f) {
            vt->inter = (char)c;
            return;
        }
        if (c >= 0x40 && c <= 0x7e) {
            csi_dispatch(vt, (char)c);
            vt->state = S_GROUND;
        }
        return;
    }
    if (c < 0x80) {
        vt->utf_need = 0;
        put_char(vt, c);
        return;
    }
    if (vt->utf_need) {
        if ((c & 0xC0) == 0x80) {
            vt->utf_cp = (vt->utf_cp << 6) | (c & 0x3F);
            if (--vt->utf_need == 0)
                put_char(vt, vt->utf_cp);
            return;
        }
        vt->utf_need = 0;
    }
    if ((c & 0xE0) == 0xC0) {
        vt->utf_cp = c & 0x1F;
        vt->utf_need = 1;
    } else if ((c & 0xF0) == 0xE0) {
        vt->utf_cp = c & 0x0F;
        vt->utf_need = 2;
    } else if ((c & 0xF8) == 0xF0) {
        vt->utf_cp = c & 0x07;
        vt->utf_need = 3;
    } else {
        put_char(vt, 0xFFFD);
    }
}

void vt_write(struct vt *vt, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        feed(vt, data[i]);
    if (len) {
        vt->dirty = true;
        vt->generation++;
    }
}
