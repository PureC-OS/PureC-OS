#include "rs_session.h"
#include "display.h"
#include "display_mode.h"
#include "window_manager.h"
#include "syscall.h"
#include "../arch/x86_64/idt/include/idt.h"
#include "../drivers/display/gop.h"
#include "../drivers/input/keyboard.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/mouse/usb_mouse.h"
#include "../drivers/storage/block_device.h"
#include "../fs/vfs.h"
#include "../gfx/compositor.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/process/process.h"
#include "../kernel/process/scheduler.h"
#include "../kernel/syscall/syscall.h"

#define RS_DESKTOP_BG 0x181825u
#define RS_WAIT_MAX_MS 1000u

static volatile bool session_active;
static volatile uint32_t session_pid;
static volatile uint32_t redraw_flag;
static struct mouse_state last_mouse;
static bool last_mouse_valid;

bool rs_session_active(void) {
  return __atomic_load_n(&session_active, __ATOMIC_ACQUIRE);
}

void rs_session_request_redraw(void) {
  __atomic_store_n(&redraw_flag, 1, __ATOMIC_RELEASE);
}

void rs_session_display_changed(void) { rs_session_request_redraw(); }

static bool desktop_installed(void) {
  struct file_stat_info info;
  return vfs_stat(RS_DESKTOP_PATH, &info) == 0;
}

bool rs_session_start(void) {
  if (!display_is_available() || !desktop_installed()) {
    klog(KLOG_WARN, "desktop-rs: not available, using in-kernel desktop");
    return false;
  }
  keyboard_init();
  klog_set_screen_enabled(false);
  display_mode_boot_apply();
  uint32_t width = gop_get_width();
  uint32_t height = gop_get_height();
  mouse_set_bounds((int32_t)width, (int32_t)height);
  gop_begin_compose();
  gop_draw_rect(0, 0, width, height, RS_DESKTOP_BG);
  gop_end_compose();
  window_manager_set_server_mode(true);
  __atomic_store_n(&session_active, true, __ATOMIC_RELEASE);
  int64_t pid = userspace_syscall(SYS_EXEC, (uint64_t)RS_DESKTOP_PATH, 0, 0);
  if (pid < 0) {
    klogf(KLOG_ERROR, "desktop-rs: exec failed rc=%d", (int)pid);
    return true;
  }
  klogf(KLOG_OK, "desktop-rs: started pid=%d", (int)pid);
  return true;
}

void rs_session_input_thread(void *arg) {
  (void)arg;
  klog(KLOG_INFO, "sched: input thread started (desktop-rs session)");
  for (;;) {
    idt_check_interrupt_rates();
    ps2_mouse_poll();
    block_device_poll_usb_hotplug();
    usb_mouse_poll();
    mouse_flush_pending();
    keyboard_poll();
    display_mode_poll();
    scheduler_sleep(4);
  }
}

static bool owner_ok(uint32_t pid) {
  return pid != 0 && __atomic_load_n(&session_pid, __ATOMIC_ACQUIRE) == pid;
}

int64_t rs_session_claim(uint32_t pid, uint32_t uid) {
  if (!rs_session_active() || uid != 0 || pid == 0)
    return -1;
  uint32_t current = __atomic_load_n(&session_pid, __ATOMIC_ACQUIRE);
  if (current != 0 && current != pid) {
    process_table_lock();
    bool alive = process_lookup_by_pid(current) != 0;
    process_table_unlock();
    if (alive)
      return -2;
  }
  __atomic_store_n(&session_pid, pid, __ATOMIC_RELEASE);
  last_mouse_valid = false;
  rs_session_request_redraw();
  klogf(KLOG_INFO, "desktop-rs: session claimed by pid=%u", pid);
  return 0;
}

int64_t rs_session_blit(uint32_t pid, const struct fb_blit_request *request) {
  if (!owner_ok(pid) || !request)
    return -1;
  uint32_t width = gop_get_width();
  uint32_t height = gop_get_height();
  if (request->x < 0 || request->y < 0 || (uint32_t)request->x >= width ||
      (uint32_t)request->y >= height)
    return -1;
  uint32_t x = (uint32_t)request->x;
  uint32_t y = (uint32_t)request->y;
  uint32_t w = request->width;
  uint32_t h = request->height;
  if (w > width - x)
    w = width - x;
  if (h > height - y)
    h = height - y;
  static bool first_blit_logged;
  if (!first_blit_logged) {
    first_blit_logged = true;
    klogf(KLOG_INFO, "desktop-rs: first blit %ux%u at %u,%u", w, h, x, y);
  }
  if (!gop_backbuffer_put(x, y, w, h,
                          (const uint32_t *)(uintptr_t)request->pixels,
                          request->stride))
    return -2;
  return compositor_present(x, y, x + w, y + h) ? 0 : -3;
}

static uint32_t pending_events(void) {
  uint32_t bits = 0;
  struct mouse_state now = mouse_get_state();
  if (!last_mouse_valid || now.x != last_mouse.x || now.y != last_mouse.y ||
      now.buttons != last_mouse.buttons || now.wheel != last_mouse.wheel) {
    last_mouse = now;
    last_mouse_valid = true;
    bits |= RS_WAIT_MOUSE;
  }
  if (keyboard_has_key())
    bits |= RS_WAIT_KEY;
  if (__atomic_exchange_n(&redraw_flag, 0, __ATOMIC_ACQ_REL))
    bits |= RS_WAIT_REDRAW;
  return bits;
}

int64_t rs_session_wait(uint32_t pid, uint32_t timeout_ms) {
  if (!owner_ok(pid))
    return -1;
  if (timeout_ms > RS_WAIT_MAX_MS)
    timeout_ms = RS_WAIT_MAX_MS;
  for (uint32_t waited = 0;; waited++) {
    uint32_t bits = pending_events();
    if (bits || waited >= timeout_ms)
      return (int64_t)bits;
    scheduler_sleep(1);
  }
}

int64_t rs_session_wm(uint32_t pid, uint64_t op, uint64_t arg1,
                      uint64_t arg2) {
  if (!owner_ok(pid))
    return -1;
  switch (op) {
  case RS_WM_POINTER: {
    bool focus_changed = false;
    int32_t x = (int32_t)(uint32_t)(arg1 & 0xFFFFFFFFu);
    int32_t y = (int32_t)(uint32_t)(arg1 >> 32);
    uint32_t consumed =
        window_manager_pointer(x, y, (uint8_t)arg2, &focus_changed);
    return (int64_t)(consumed | (focus_changed ? 2u : 0u));
  }
  case RS_WM_SET_WORK_AREA:
    window_manager_set_work_area((int32_t)(uint32_t)(arg1 & 0xFFFFFFFFu),
                                 (int32_t)(uint32_t)(arg1 >> 32),
                                 (uint32_t)(arg2 & 0xFFFFFFFFu),
                                 (uint32_t)(arg2 >> 32));
    return 0;
  case RS_WM_HAS_FOCUS:
    return window_manager_has_focus() ? 1 : 0;
  case RS_WM_REQUEST_REPAINT:
    window_manager_request_repaint((uint32_t)arg1);
    return 0;
  case RS_WM_FOCUSED_PID:
    return (int64_t)window_manager_focused_pid();
  case RS_WM_TASKS: {
    uint32_t capacity = (uint32_t)arg2;
    if (capacity > 32)
      capacity = 32;
    if (!capacity ||
        !process_user_buffer((void *)(uintptr_t)arg1,
                             (uint64_t)capacity * sizeof(struct gui_task_info),
                             true))
      return -1;
    return (int64_t)window_manager_tasks(
        (struct gui_task_info *)(uintptr_t)arg1, capacity);
  }
  case RS_WM_ACTIVATE:
    return window_manager_activate((uint32_t)arg1) ? 0 : -1;
  case RS_WM_COMMAND:
    return window_manager_command((uint32_t)arg1, (uint32_t)arg2) ? 0 : -1;
  default:
    return -1;
  }
}
