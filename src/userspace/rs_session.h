#pragma once
#include <stdbool.h>
#include <stdint.h>

struct fb_blit_request;

#define RS_DESKTOP_PATH "/bin/program/desktop-rs"

#define RS_WAIT_MOUSE 1u
#define RS_WAIT_KEY 2u
#define RS_WAIT_REDRAW 4u

#define RS_WM_POINTER 1
#define RS_WM_HAS_FOCUS 2
#define RS_WM_REQUEST_REPAINT 3
#define RS_WM_FOCUSED_PID 4
#define RS_WM_SET_WORK_AREA 5
#define RS_WM_TASKS 6
#define RS_WM_ACTIVATE 7
#define RS_WM_COMMAND 8

bool rs_session_start(void);
void rs_session_input_thread(void *arg);
bool rs_session_active(void);
void rs_session_display_changed(void);
void rs_session_request_redraw(void);
int64_t rs_session_claim(uint32_t pid, uint32_t uid);
int64_t rs_session_blit(uint32_t pid, const struct fb_blit_request *request);
int64_t rs_session_wait(uint32_t pid, uint32_t timeout_ms);
int64_t rs_session_wm(uint32_t pid, uint64_t op, uint64_t arg1, uint64_t arg2);
