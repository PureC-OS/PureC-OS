#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "canvas.h"
#include "model.h"

enum { TAB_PROCS, TAB_SYSTEM, TAB_PROC, TAB_COUNT };
enum { SORT_CPU, SORT_MEM, SORT_PID, SORT_NAME, SORT_TIME, SORT_COUNT };

#define UI_COLUMNS 8
#define UI_LINES_MAX 2048

struct ui_state {
  struct mon_model model;
  int tab;
  int order[MON_MAX_PROCS];
  int visible;
  uint32_t sel_pid;
  int sel;
  int scroll;
  int sort_key;
  bool sort_desc;
  bool tree;
  char filter[32];
  int filter_len;
  bool filter_mode;
  bool details;
  bool kill_prompt;
  uint32_t kill_pid;
  char message[96];
  int message_age;
  int sys_scroll;
  int sys_content_h;
  struct mon_entry browse[MON_BROWSE_MAX];
  int browse_count;
  int browse_sel;
  int browse_scroll;
  char text[MON_TEXT_MAX];
  int text_len;
  int line_start[UI_LINES_MAX];
  int line_count;
  int text_scroll;
  int (*kill_hook)(uint32_t pid);
  bool quit;
  int width;
  int height;
  int tab_x[TAB_COUNT + 1];
  int hdr_y;
  int col_x[UI_COLUMNS + 1];
  int row_y0;
  int row_h;
  int rows_visible;
  int list_x;
  int list_y;
  int list_w;
  int list_rows;
  int content_x;
};

void ui_init(struct ui_state *u);
void ui_refresh(struct ui_state *u);
void ui_render(struct ui_state *u, struct canvas *c);
bool ui_key(struct ui_state *u, int key);
bool ui_special(struct ui_state *u, int code);
bool ui_click(struct ui_state *u, int x, int y);
bool ui_wheel(struct ui_state *u, int delta, int x, int y);
