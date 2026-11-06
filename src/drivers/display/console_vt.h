#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VT_MAX_COLUMNS 480
#define VT_MAX_ROWS 216
#define VT_MAX_PARAMS 16

#define VT_ATTR_FG_MASK 0x000F
#define VT_ATTR_BG_MASK 0x00F0
#define VT_ATTR_BG_SHIFT 4
#define VT_ATTR_FG_SET 0x0100
#define VT_ATTR_BG_SET 0x0200
#define VT_ATTR_REVERSE 0x0400
#define VT_ATTR_BOLD 0x0800
#define VT_ATTR_UNDERLINE 0x1000

#define VT_FLAG_NL_CR 1u
#define VT_FLAG_BS_ERASE 2u

struct vt_ops {
    void (*draw_cell)(void *ctx, uint32_t row, uint32_t column, char character,
                      uint16_t attr, bool cursor);
    void (*fill)(void *ctx, uint32_t row, uint32_t column, uint32_t count,
                 uint16_t attr);
    bool (*scroll)(void *ctx, uint32_t top, uint32_t bottom, uint32_t count,
                   bool up);
    void (*reply)(void *ctx, const char *text);
    void (*mode)(void *ctx, uint32_t mode, bool enable);
};

struct vt_term {
    char chars[VT_MAX_ROWS][VT_MAX_COLUMNS];
    uint16_t attrs[VT_MAX_ROWS][VT_MAX_COLUMNS];
    uint32_t columns;
    uint32_t rows;
    uint32_t row;
    uint32_t column;
    uint32_t top;
    uint32_t bottom;
    uint16_t attr;
    uint32_t saved_row;
    uint32_t saved_column;
    uint16_t saved_attr;
    uint8_t state;
    uint8_t prefix;
    uint8_t intermediate;
    bool have_param;
    uint8_t param_count;
    uint16_t params[VT_MAX_PARAMS];
    uint16_t string_length;
    bool autowrap;
    bool cursor_visible;
    bool cursor_drawn;
    uint32_t cursor_row;
    uint32_t cursor_column;
    const struct vt_ops *ops;
    void *ctx;
};

void vt_init(struct vt_term *term, const struct vt_ops *ops, void *ctx);
void vt_resize(struct vt_term *term, uint32_t columns, uint32_t rows);
void vt_clear(struct vt_term *term);
void vt_redraw(struct vt_term *term);
void vt_write(struct vt_term *term, const char *data, uint32_t count,
              uint32_t flags);
void vt_cursor_hide(struct vt_term *term);
void vt_cursor_show(struct vt_term *term);
