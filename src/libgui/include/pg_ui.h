#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pg_canvas.h"
#include "puregui.h"

struct pg_palette {
    uint32_t bg, sidebar, surface, surface2, surface3, border, text, text2, text3;
    uint32_t accent, accent2, on_accent, danger, success, warning, info;
    bool light;
};

enum pgu_btn_style { PGU_BTN_PRIMARY, PGU_BTN_SECONDARY, PGU_BTN_DANGER, PGU_BTN_GHOST };

struct pg_ui {
    struct pg_canvas *c;
    struct pg_palette pal;
    int mx, my;
    bool down, pressed, released;
    int wheel;
    int key;
    int special;
    uint32_t hot, active, focus;
    bool any_hot;
    bool animating;
    bool modal;
    uint64_t now;
    int drag_x;
    uint32_t dd_open;
    int dd_x, dd_y, dd_w, dd_count, dd_sel;
    const char *const *dd_labels;
    uint32_t dd_changed;
    int dd_result;
    int block_x, block_y, block_w, block_h;
    int prev_block_x, prev_block_y, prev_block_w, prev_block_h;
};

uint32_t pgu_id(const char *s, int n);
void pgu_palette_from_theme(struct pg_palette *p, const struct pg_theme *t);

bool pgu_hover(struct pg_ui *u, int x, int y, int w, int h);
bool pgu_click(struct pg_ui *u, uint32_t id, int x, int y, int w, int h);

void pgu_card(struct pg_ui *u, int x, int y, int w, int h);
void pgu_card_title(struct pg_ui *u, int x, int y, int icon, const char *title, const char *subtitle);
bool pgu_button(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *label, int icon,
               enum pgu_btn_style style);
int pgu_button_w(const char *label, int icon);
bool pgu_toggle(struct pg_ui *u, uint32_t id, int x, int y, bool *value);
int pgu_slider(struct pg_ui *u, uint32_t id, int x, int y, int w, int *value, int min, int max,
              uint32_t color);
bool pgu_segmented(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *const *labels,
                  int count, int *selected);
bool pgu_row(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, bool selected);
bool pgu_radio(struct pg_ui *u, int x, int y, bool on);
void pgu_check_mark(struct pg_ui *u, int x, int y, bool on);
bool pgu_textbox(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, char *buf, int cap,
                bool password, const char *placeholder);
void pgu_progress(struct pg_ui *u, int x, int y, int w, int h, uint32_t permille, uint32_t color);
int pgu_badge(struct pg_ui *u, int x, int y, const char *text, uint32_t color);
void pgu_kv(struct pg_ui *u, int x, int y, int w, const char *key, const char *value);
void pgu_separator(struct pg_ui *u, int x, int y, int w);
void pgu_icon_tile(struct pg_ui *u, int x, int y, int size, int icon, uint32_t color);
void pgu_feed(struct pg_ui *u, const struct pg_window *w, const struct pg_event *ev);
void pgu_frame_begin(struct pg_ui *u, struct pg_canvas *c, uint64_t now_ms);
void pgu_frame_end(struct pg_ui *u);
bool pgu_checkbox(struct pg_ui *u, uint32_t id, int x, int y, const char *label, bool *value);
bool pgu_dropdown(struct pg_ui *u, uint32_t id, int x, int y, int w, int h, const char *const *labels,
                  int count, int *selected);
bool pgu_tabs(struct pg_ui *u, uint32_t id, int x, int y, int w, const char *const *labels, int count,
              int *selected);
void pgu_signal_bars(struct pg_ui *u, int x, int y, int rssi, uint32_t color);
