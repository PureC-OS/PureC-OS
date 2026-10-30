#include "userspace/window_manager.h"
#include "kernel/process/process.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fake_ticks = 1000;
static struct {
    uint32_t pid, x, y, w, h;
    int updates;
    int raises;
    int destroys;
} comp;

uint32_t gop_get_width(void) { return 1280; }
uint32_t gop_get_height(void) { return 800; }
uint64_t timer_ticks(void) { return fake_ticks; }
bool scheduler_preempt_disable(void) { return true; }
void scheduler_preempt_enable(void) {}
uint64_t pmm_allocate_page(void) {
    return (uint64_t)(uintptr_t)aligned_alloc(4096, 4096);
}
void *pmm_physical_to_virtual(uint64_t physical) {
    return (void *)(uintptr_t)physical;
}
bool compositor_window_create(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h) {
    comp.pid = pid; comp.x = x; comp.y = y; comp.w = w; comp.h = h;
    return true;
}
bool compositor_window_update(uint32_t pid, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h) {
    comp.pid = pid; comp.x = x; comp.y = y; comp.w = w; comp.h = h;
    comp.updates++;
    return true;
}
void compositor_window_destroy(uint32_t pid) { (void)pid; comp.destroys++; }
void compositor_window_raise(uint32_t pid) { (void)pid; comp.raises++; }
bool compositor_window_exists(uint32_t pid) { (void)pid; return true; }
static int topmost_calls;
static bool topmost_last;
void compositor_window_set_topmost(uint32_t pid, bool topmost) {
    (void)pid;
    topmost_calls++;
    topmost_last = topmost;
}
static struct process fake_process;
struct process *process_lookup_by_pid(uint32_t pid) {
    (void)pid;
    strcpy(fake_process.name, "/usr/bin/terminal");
    return &fake_process;
}

static uint32_t ptr(int32_t x, int32_t y, uint8_t buttons) {
    bool focus;
    return window_manager_pointer(x, y, buttons, &focus);
}

static void click(int32_t x, int32_t y) {
    ptr(x, y, 1);
    ptr(x, y, 0);
}

static struct gui_window_info info(uint32_t pid) {
    struct gui_window_info out;
    assert(window_manager_info(pid, &out));
    return out;
}

int main(void) {
    window_manager_set_server_mode(true);
    window_manager_set_work_area(0, 28, 1280, 772);

    struct gui_window_request a = {100, 100, 400, 300};
    struct gui_window_request b = {300, 150, 400, 300};
    assert(window_manager_register(1, &a));
    assert(window_manager_register(2, &b));
    assert(window_manager_focused_pid() == 2);

    assert(ptr(150, 250, 0) == 1);
    click(150, 250);
    assert(window_manager_focused_pid() == 1);

    uint32_t state = window_manager_state(1);
    assert(state & GUI_WINDOW_STATE_FOCUSED);
    assert(state & GUI_WINDOW_STATE_SERVER);
    assert(!(state & GUI_WINDOW_STATE_CLOSE));

    ptr(150, 110, 1);
    assert(window_manager_state(1) & GUI_WINDOW_STATE_DRAGGING);
    ptr(250, 210, 1);
    ptr(250, 210, 0);
    struct gui_window_info frame = info(1);
    assert(frame.x == 200 && frame.y == 200);
    assert(comp.x == 200 && comp.y == 200 && comp.h == 300);
    assert(!(window_manager_state(1) & GUI_WINDOW_STATE_DRAGGING));

    ptr(300, 210, 1);
    ptr(-500, -500, 1);
    ptr(-500, -500, 0);
    frame = info(1);
    assert(frame.x == 0 && frame.y == 28);

    ptr(100, 40, 1);
    ptr(500, 300, 1);
    ptr(500, 300, 0);
    frame = info(1);
    assert(frame.x == 400 && frame.y == 288);

    uint32_t generation = window_manager_state(1) >> GUI_WINDOW_STATE_GEN_SHIFT;
    int32_t max_x = (int32_t)frame.x + (int32_t)frame.width - 57;
    int32_t title_y = (int32_t)frame.y + 14;
    click(max_x, title_y);
    frame = info(1);
    assert(frame.flags & GUI_WINDOW_INFO_MAXIMIZED);
    assert(frame.x == 0 && frame.y == 28 && frame.width == 1280
           && frame.height == 772);
    assert(comp.w == 1280 && comp.h == 772);
    assert((window_manager_state(1) >> GUI_WINDOW_STATE_GEN_SHIFT) != generation);

    click(1280 - 57, 28 + 14);
    frame = info(1);
    assert(!(frame.flags & GUI_WINDOW_INFO_MAXIMIZED));
    assert(frame.x == 400 && frame.y == 288 && frame.width == 400
           && frame.height == 300);

    click(400 + 400 - 37, 288 + 14);
    frame = info(1);
    assert(frame.flags & GUI_WINDOW_INFO_MINIMIZED);
    assert(comp.h == WM_TITLEBAR_HEIGHT + WM_BORDER * 2);
    assert(frame.height == 300);
    ptr(500, 288 + 100, 0);
    click(500, 288 + 100);
    assert(window_manager_focused_pid() != 1);
    click(400 + 400 - 37, 288 + 14);
    assert(!(info(1).flags & GUI_WINDOW_INFO_MINIMIZED));
    assert(comp.h == 300);

    fake_ticks = 5000;
    click(500, 288 + 12);
    fake_ticks = 5200;
    click(500, 288 + 12);
    assert(info(1).flags & GUI_WINDOW_INFO_MAXIMIZED);

    ptr(640, 40, 1);
    ptr(641, 41, 1);
    assert(info(1).flags & GUI_WINDOW_INFO_MAXIMIZED);
    ptr(700, 100, 1);
    frame = info(1);
    assert(!(frame.flags & GUI_WINDOW_INFO_MAXIMIZED));
    assert(frame.width == 400 && frame.height == 300);
    ptr(700, 100, 0);

    fake_ticks = 9000;
    frame = info(1);
    click((int32_t)frame.x + (int32_t)frame.width - 20, (int32_t)frame.y + 14);
    assert(window_manager_state(1) & GUI_WINDOW_STATE_CLOSE);
    assert(!(window_manager_state(2) & GUI_WINDOW_STATE_CLOSE));

    assert(window_manager_set_flags(2, GUI_WINDOW_FLAG_FIXED | GUI_WINDOW_FLAG_NOCLOSE));
    frame = info(2);
    click((int32_t)frame.x + (int32_t)frame.width - 20, (int32_t)frame.y + 14);
    assert(!(window_manager_state(2) & GUI_WINDOW_STATE_CLOSE));
    click((int32_t)frame.x + (int32_t)frame.width - 57, (int32_t)frame.y + 14);
    click((int32_t)frame.x + (int32_t)frame.width - 37, (int32_t)frame.y + 14);
    assert(info(2).flags == 0);
    assert(!window_manager_command(2, 99));
    assert(window_manager_command(2, GUI_WINDOW_CMD_MAXIMIZE));
    assert(info(2).flags == 0);

    assert(window_manager_command(1, GUI_WINDOW_CMD_MAXIMIZE));
    assert(info(1).flags & GUI_WINDOW_INFO_MAXIMIZED);
    window_manager_set_work_area(0, 28, 1024, 700);
    assert(info(1).width == 1024 && info(1).height == 700);
    assert(window_manager_command(1, GUI_WINDOW_CMD_UNMAXIMIZE));

    int destroys = comp.destroys;
    window_manager_unregister(1);
    assert(comp.destroys == destroys + 1);
    assert(window_manager_focused_pid() == 2);
    ptr(500, 500, 1);
    ptr(500, 500, 0);

    click(450, 300);
    assert(window_manager_focused_pid() == 2);
    struct gui_window_request menu = {10, 700, 200, 80};
    assert(window_manager_register(7, &menu));
    assert(window_manager_focused_pid() == 7);
    assert(window_manager_set_flags(7, GUI_WINDOW_FLAG_POPUP));
    assert(topmost_calls == 1 && topmost_last);
    assert(window_manager_focused_pid() == 2);

    struct gui_task_info tasks[8];
    assert(window_manager_tasks(tasks, 8) == 1);
    assert(tasks[0].pid == 2 && (tasks[0].flags & GUI_TASK_FOCUSED));
    assert(strcmp(tasks[0].name, "terminal") == 0);

    uint32_t popup_generation = window_manager_state(2) >> GUI_WINDOW_STATE_GEN_SHIFT;
    assert(ptr(50, 720, 1) == 0);
    assert(ptr(50, 720, 0) == 0);
    assert(window_manager_focused_pid() == 2);
    assert(window_manager_state(2) >> GUI_WINDOW_STATE_GEN_SHIFT == popup_generation);
    window_manager_unregister(7);
    assert(window_manager_focused_pid() == 2);

    assert(window_manager_command(2, GUI_WINDOW_CMD_MINIMIZE));
    assert(info(2).flags == 0 || (info(2).flags & GUI_WINDOW_INFO_MINIMIZED) == 0);
    assert(window_manager_set_flags(2, 0));
    assert(window_manager_command(2, GUI_WINDOW_CMD_MINIMIZE));
    assert(info(2).flags & GUI_WINDOW_INFO_MINIMIZED);
    assert(window_manager_tasks(tasks, 8) == 1 && (tasks[0].flags & GUI_TASK_MINIMIZED));
    assert(window_manager_activate(2));
    assert(!(info(2).flags & GUI_WINDOW_INFO_MINIMIZED));
    assert(!window_manager_activate(99));
    assert(window_manager_command(2, GUI_WINDOW_CMD_RESTORE));

    window_manager_set_server_mode(false);
    state = window_manager_state(2);
    assert(!(state & GUI_WINDOW_STATE_SERVER));
    assert(state >> GUI_WINDOW_STATE_GEN_SHIFT == 0);
    struct gui_window_request shrunk = {300, 150, 400, 30};
    assert(window_manager_update(2, &shrunk));
    assert(comp.h == 30);

    printf("window_manager_test: ok\n");
    return 0;
}
