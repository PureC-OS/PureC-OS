#pragma once
#include "../types.hpp"

extern uint8_t* hex_buf;
extern uint32_t hex_buf_len;
extern uint32_t hex_buf_cap;
extern uint32_t hex_cursor;
extern bool hex_high_nibble;
extern bool hex_mode;
extern bool hex_insert_mode;
extern bool hex_dirty;
extern uint32_t hex_view_offset;
extern char hex_file_path[128];

bool hex_reserve(uint32_t need);
bool hex_insert_byte(uint32_t off, uint8_t v);
void hex_delete_byte(uint32_t off);
bool hex_load_file(const char* path);
bool hex_save_file();
uint32_t hex_visible_rows();
uint32_t hex_total_rows();
void hex_clamp_view();
void hex_ensure_visible();
void hex_move_cursor(int32_t d);
char hex_digit(uint8_t v);
int hex_val(char c);
void hex_u32_hex(char* out, uint32_t v, uint32_t d);
void hex_u32_dec(char* out, uint32_t v);
void hex_append_txt(char* dst, const char* src, uint32_t cap);
void hex_set_status(const char* m, bool err);
uint32_t hex_parse_hex(const char* s, bool* ok);
