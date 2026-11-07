#include <stdint.h>
#include <string.h>

#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"
#include "ui.h"

#define WINDOW_WIDTH 1040
#define WINDOW_HEIGHT 700
#define MIN_CLIENT_WIDTH 640
#define MIN_CLIENT_HEIGHT 360
#define REFRESH_MS 1000
#define SIGTERM_NUMBER 15

static struct ui_state ui;
static struct pg_window window;
static uint32_t *pixels;
static uint64_t pixels_capacity;

static int kill_process(uint32_t pid) {
  return (int)pc_syscall(SYS_KILL, pid, SIGTERM_NUMBER, 0);
}

static uint64_t uptime_ms(void) {
  struct cpu_monitor_info info = {0};
  if (!pc_cpu_info(&info))
    return 0;
  return info.uptime_ms;
}

static bool ensure_pixels(uint32_t width, uint32_t height) {
  uint64_t need = (uint64_t)width * height;
  if (need <= pixels_capacity)
    return true;
  void *memory = pc_heap_grow(need * sizeof(uint32_t));
  if (!memory)
    return false;
  pixels = (uint32_t *)memory;
  pixels_capacity = need;
  return true;
}

static void redraw(void) {
  uint32_t width = window.client.width;
  uint32_t height = window.client.height;
  if (width < MIN_CLIENT_WIDTH || height < MIN_CLIENT_HEIGHT || !ensure_pixels(width, height))
    return;
  struct canvas canvas;
  cv_init(&canvas, pixels, (int)width, (int)height, (int)width);
  ui_render(&ui, &canvas);
  pg_window_begin(&window);
  (void)pg_window_blit(&window, 0, 0, width, height, pixels, width);
  pg_window_end(&window);
}

int main(void) {
  struct pc_display_info display;
  if (!pc_display_get_info(&display) || !display.available)
    return 1;
  uint32_t width = display.width > WINDOW_WIDTH + 20 ? WINDOW_WIDTH : display.width - 20;
  uint32_t height = display.height > WINDOW_HEIGHT + 40 ? WINDOW_HEIGHT : display.height - 40;
  if (!pg_window_center(&window, "htop", width, height))
    return 1;
  pg_window_set_esc_closes(&window, false);
  ui_init(&ui);
  ui.kill_hook = kill_process;
  redraw();
  uint64_t last_refresh = uptime_ms();
  int32_t mouse_x = 0, mouse_y = 0;
  while (pg_window_is_open(&window) && !ui.quit) {
    bool dirty = false;
    struct pg_event event;
    while (pg_window_poll_event(&window, &event)) {
      switch (event.type) {
      case PG_EVENT_CLOSE:
        ui.quit = true;
        break;
      case PG_EVENT_KEY:
        dirty |= ui_key(&ui, event.key);
        break;
      case PG_EVENT_SPECIAL_KEY:
        dirty |= ui_special(&ui, event.key);
        break;
      case PG_EVENT_MOUSE_MOVE:
        mouse_x = event.x - (int32_t)window.client.x;
        mouse_y = event.y - (int32_t)window.client.y;
        break;
      case PG_EVENT_MOUSE_DOWN: {
        int32_t x = event.x - (int32_t)window.client.x;
        int32_t y = event.y - (int32_t)window.client.y;
        mouse_x = x;
        mouse_y = y;
        if (event.button == 1 && x >= 0 && y >= 0 && x < (int32_t)window.client.width &&
            y < (int32_t)window.client.height)
          dirty |= ui_click(&ui, x, y);
        break;
      }
      case PG_EVENT_REPAINT:
      case PG_EVENT_MOVE:
      case PG_EVENT_FOCUS:
        dirty = true;
        break;
      default:
        break;
      }
    }
    int32_t wheel = pg_window_take_wheel(&window);
    if (wheel)
      dirty |= ui_wheel(&ui, wheel, mouse_x, mouse_y);
    uint64_t now = uptime_ms();
    if (now - last_refresh >= REFRESH_MS) {
      last_refresh = now;
      if (!pg_window_is_minimized(&window)) {
        ui_refresh(&ui);
        dirty = true;
      }
    }
    if (dirty && !pg_window_is_minimized(&window))
      redraw();
    pc_sleep(16);
  }
  pg_window_close(&window);
  return 0;
}
