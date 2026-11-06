#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DESKTOP_ENTRY_MAX 24
#define DESKTOP_ENTRY_SCAN_DIR "/usr/share/applications/"

struct desktop_entry {
    char name[32];
    char exec[128];
    char icon_path[128];
    char builtin[16];
    char icon_text[8];
    uint32_t icon_color;
    char id[32];
    uint32_t x;
    uint32_t y;
    uint16_t col;
    uint16_t row;
    bool placed;
    bool hidden;
};

void desktop_entries_init(void);
void desktop_entries_rescan(void);
uint32_t desktop_entries_count(void);
const struct desktop_entry *desktop_entries_get(uint32_t index);
void desktop_entries_set_position(uint32_t index, uint32_t x, uint32_t y);
/* Icon metrics follow the label font size; grid cells follow the metrics. */
void desktop_entries_configure(uint32_t label_px, uint32_t desktop_width,
                               uint32_t desktop_height, uint32_t top,
                               uint32_t bottom);
uint32_t desktop_entries_icon_w(void);
uint32_t desktop_entries_icon_h(void);
uint32_t desktop_entries_cell_w(void);
/* Grid cell the icon would snap to if dropped now. */
void desktop_entries_target_cell(uint32_t index, uint32_t *x, uint32_t *y);
/* Drop: move the icon into the nearest free cell and save the layout. */
void desktop_entries_snap(uint32_t index);
void desktop_entries_set_installer_visible(bool visible);
// Parses INI content; true if Name + (Exec|Builtin) found.
bool desktop_entry_parse(const char *data, uint32_t size,
                         struct desktop_entry *out);
