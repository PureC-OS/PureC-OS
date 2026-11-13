#include "display_owner.h"
#include "gop.h"
#include "../input/keyboard.h"
#include "../mouse/ps2_mouse.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/ipc/inputdev.h"
#include "../../kernel/syscall/syscall.h"
#include "../../lib/string.h"


static volatile uint32_t owner_pid;
static struct mouse_state last_mouse;
static bool have_last;

int32_t display_owner_claim(uint32_t pid)
{
  if (!pid)
    return -KERR_INVAL;
  uint32_t current = owner_pid;
  if (current && current != pid)
    return -KERR_BUSY;
  if (current == pid)
    return 0;
  owner_pid = pid;
  have_last = false;
  klog_set_screen_enabled(false);
  gop_set_user_owned(true);
  klogf(KLOG_INFO, "display: screen claimed by pid=%u", pid);
  return 0;
}

void display_owner_release(uint32_t pid)
{
  if (!pid || owner_pid != pid)
    return;
  owner_pid = 0;
  gop_set_user_owned(false);
  klogf(KLOG_INFO, "display: screen returned to the kernel (pid=%u)", pid);
}

bool display_owner_active(void) { return owner_pid != 0; }

uint32_t display_owner_pid(void) { return owner_pid; }

void display_owner_pump(void)
{
  if (!owner_pid)
    return;
  struct key_event key;
  while (keyboard_try_get_event(&key))
  {
    if (!inputdev_active())
      continue;
    struct input_record r;
    memset(&r, 0, sizeof(r));
    r.type = INPUT_RECORD_KEY;
    r.scancode = key.scancode;
    r.pressed = key.pressed;
    r.repeat = key.repeat;
    r.mods = key.mods;
    inputdev_push(&r);
  }
  struct mouse_state m = mouse_get_state();
  if (!have_last)
  {
    last_mouse = m;
    have_last = true;
    return;
  }
  if (m.x == last_mouse.x && m.y == last_mouse.y &&
      m.buttons == last_mouse.buttons && m.wheel == last_mouse.wheel)
    return;
  if (inputdev_active())
  {
    struct input_record r;
    memset(&r, 0, sizeof(r));
    r.type = INPUT_RECORD_MOUSE;
    r.x = m.x;
    r.y = m.y;
    r.dx = m.x - last_mouse.x;
    r.dy = m.y - last_mouse.y;
    r.wheel = m.wheel - last_mouse.wheel;
    r.buttons = m.buttons;
    inputdev_push(&r);
  }
  last_mouse = m;
}
