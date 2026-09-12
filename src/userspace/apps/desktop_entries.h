#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DESKTOP_ENTRY_MAX 24
#define DESKTOP_ENTRY_SCAN_DIR "/bin/program"

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
    bool hidden;
};

void desktop_entries_init(void);
void desktop_entries_rescan(void);
uint32_t desktop_entries_count(void);
const struct desktop_entry *desktop_entries_get(uint32_t index);
void desktop_entries_set_position(uint32_t index, uint32_t x, uint32_t y);
void desktop_entries_set_installer_visible(bool visible);
// Parses INI content; true if Name + (Exec|Builtin) found.
bool desktop_entry_parse(const char *data, uint32_t size,
                         struct desktop_entry *out);
