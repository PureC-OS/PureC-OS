#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings/gfx.h"

struct ui_palette {
    uint32_t bg, sidebar, surface, surface2, surface3, border, text, text2, text3;
    uint32_t accent, accent2, on_accent, danger, success, warning, info;
    bool light;
};

enum ui_btn_style { UI_BTN_PRIMARY, UI_BTN_SECONDARY, UI_BTN_DANGER, UI_BTN_GHOST };

struct ui {
    struct sg_canvas *c;
    struct ui_palette pal;
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
};

uint32_t ui_id(const char *s, int n);
void ui_palette_from_theme(struct ui_palette *p, uint32_t window, uint32_t text, uint32_t muted,
                           uint32_t accent, uint32_t danger);

bool ui_hover(struct ui *u, int x, int y, int w, int h);
bool ui_click(struct ui *u, uint32_t id, int x, int y, int w, int h);

void ui_card(struct ui *u, int x, int y, int w, int h);
void ui_card_title(struct ui *u, int x, int y, int icon, const char *title, const char *subtitle);
bool ui_button(struct ui *u, uint32_t id, int x, int y, int w, int h, const char *label, int icon,
               enum ui_btn_style style);
int ui_button_w(const char *label, int icon);
bool ui_toggle(struct ui *u, uint32_t id, int x, int y, bool *value);
int ui_slider(struct ui *u, uint32_t id, int x, int y, int w, int *value, int min, int max,
              uint32_t color);
bool ui_segmented(struct ui *u, uint32_t id, int x, int y, int w, int h, const char *const *labels,
                  int count, int *selected);
bool ui_row(struct ui *u, uint32_t id, int x, int y, int w, int h, bool selected);
bool ui_radio(struct ui *u, int x, int y, bool on);
void ui_check_mark(struct ui *u, int x, int y, bool on);
bool ui_textbox(struct ui *u, uint32_t id, int x, int y, int w, int h, char *buf, int cap,
                bool password, const char *placeholder);
void ui_progress(struct ui *u, int x, int y, int w, int h, uint32_t permille, uint32_t color);
int ui_badge(struct ui *u, int x, int y, const char *text, uint32_t color);
void ui_kv(struct ui *u, int x, int y, int w, const char *key, const char *value);
void ui_separator(struct ui *u, int x, int y, int w);
void ui_icon_tile(struct ui *u, int x, int y, int size, int icon, uint32_t color);
void ui_signal_bars(struct ui *u, int x, int y, int rssi, uint32_t color);
