#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VTCON_COUNT 6
#define VTCON_GRAPHICS VTCON_COUNT

void vtcon_init(void);
bool vtcon_ready(void);
void vtcon_start_thread(void);
void vtcon_display_changed(void);

bool vtcon_keyboard_captured(void);
void vtcon_key_char(char character, bool alt);
void vtcon_key_special(uint8_t key);
void vtcon_key_sequence(const char *text);
void vtcon_key_function(uint8_t number);
bool vtcon_switch_combo(uint8_t function_number, bool ctrl, bool alt);

int32_t vtcon_activate(int32_t index);
int32_t vtcon_active(void);
int32_t vtcon_pty(int32_t index);
int32_t vtcon_index_of_pty(int32_t pty_id);
int32_t vtcon_open_index(const char *path);

void vtcon_console_write(const char *text, uint32_t count);
void vtcon_klog_putc(char character);
bool vtcon_klog_wants(int level);
void vtcon_set_quiet(bool quiet);

int32_t vtcon_gui_start(void);
bool vtcon_gui_running(void);
void vtcon_set_gui_starter(void (*starter)(void));
