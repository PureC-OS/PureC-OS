#pragma once
#include "../types.hpp"

extern char hex_dir_path[128];
extern HexDirEntry hex_entries[64];
extern int32_t hex_entry_count;
extern int32_t hex_sel_entry;
extern uint32_t hex_tree_scroll;
extern uint32_t hex_tree_visible_rows;

bool hex_is_dir_path(const char* p);
void hex_normalize_dir(char* out, uint32_t cap, const char* src);
bool hex_join_path(char* out, uint32_t cap, const char* dir, const char* name);
void hex_parent_dir(char* out, uint32_t cap, const char* dir);
void hex_refresh_dir();
