#pragma once
#include <stdbool.h>
#include <stdint.h>

#define VT_COLOR_DEFAULT 0x01000000u
#define VT_COLOR_INDEX 0x02000000u
#define VT_COLOR_RGB 0x04000000u

#define VT_ATTR_BOLD 0x0001u
#define VT_ATTR_DIM 0x0002u
#define VT_ATTR_ITALIC 0x0004u
#define VT_ATTR_UNDERLINE 0x0008u
#define VT_ATTR_BLINK 0x0010u
#define VT_ATTR_REVERSE 0x0020u
#define VT_ATTR_HIDDEN 0x0040u
#define VT_ATTR_STRIKE 0x0080u

#define VT_MAX_COLS 400
#define VT_MAX_ROWS 200
#define VT_SCROLLBACK 3000

struct vt_cell {
    uint32_t ch;
    uint32_t fg;
    uint32_t bg;
    uint16_t attr;
    uint16_t pad;
};

struct vt_saved {
    int cx, cy;
    uint32_t fg, bg;
    uint16_t attr;
    bool origin;
    bool g0_graphics, g1_graphics;
    int gl;
};

struct vt {
    int cols, rows;
    struct vt_cell *main;
    struct vt_cell *alt;
    struct vt_cell *screen;
    struct vt_cell *sb;
    int sb_count, sb_head;
    bool alt_active;
    int cx, cy;
    bool wrap_pending;
    uint32_t fg, bg;
    uint16_t attr;
    int top, bottom;
    bool autowrap, origin, insert, app_cursor, app_keypad, cursor_visible, bracketed, reverse_video, lnm;
    int cursor_style;
    bool g0_graphics, g1_graphics;
    int gl;
    bool tabs[VT_MAX_COLS];
    struct vt_saved saved, saved_alt;
    int state;
    int params[16];
    int nparams;
    bool param_started;
    char priv;
    char inter;
    uint32_t utf_cp;
    int utf_need;
    char osc[256];
    int osc_len;
    char title[128];
    bool title_changed;
    char reply[128];
    int reply_len;
    bool dirty;
    uint32_t last_ch;
    uint32_t bell;
    uint64_t generation;
};

bool vt_init(struct vt *vt, int cols, int rows);
void vt_free(struct vt *vt);
void vt_resize(struct vt *vt, int cols, int rows);
void vt_write(struct vt *vt, const uint8_t *data, uint32_t len);
const struct vt_cell *vt_row(const struct vt *vt, int row, int scroll);
int vt_scrollback_lines(const struct vt *vt);
