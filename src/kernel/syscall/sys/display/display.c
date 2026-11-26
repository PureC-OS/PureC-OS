#include <drivers/display/gop.h>
#include <drivers/display/svga.h>
#include <drivers/display/svga3d.h>
#include <drivers/mouse/ps2_mouse.h>
#include <drivers/display/display_owner.h>
#include <lib/string.h>
#include <kernel/diagnostics/klog.h>
#include <kernel/process/process.h>
#include "../common.h"

long sys_fb_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct framebuffer_info *info = (struct framebuffer_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  memset(info, 0, sizeof(*info));
  info->width = gop_get_width();
  info->height = gop_get_height();
  info->pitch = gop_get_pitch();
  info->size_bytes = gop_get_framebuffer_size_bytes();
  info->bpp = gop_get_bpp();
  info->available = gop_is_available() ? 1 : 0;
  strncpy(info->protocol_name, gop_get_protocol_name(),
          sizeof(info->protocol_name) - 1);
  return 0;
}

long sys_fb_map(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct fb_map_info *out = (struct fb_map_info *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -KERR_FAULT;
  if (display_owner_pid() != (uint32_t)process_current_pid())
    return -KERR_PERM;
  struct gop_front_info front;
  if (!gop_front_physical(&front) || front.bpp != 32)
    return -KERR_NODEV;
  uint64_t base = front.physical & ~0xFFFULL;
  uint64_t skew = front.physical - base;
  int64_t mapped = process_map_device(base, front.bytes + skew, true);
  if (mapped < 0)
    return mapped;
  struct fb_map_info info;
  memset(&info, 0, sizeof(info));
  info.address = (uint64_t)mapped + skew;
  info.bytes = front.bytes;
  info.width = front.width;
  info.height = front.height;
  info.pitch_bytes = front.pitch_bytes;
  info.bpp = front.bpp;
  info.generation = front.generation;
  *out = info;
  return 0;
}

long sys_fb_damage(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a5;
  (void)a6;
  if (display_owner_pid() != (uint32_t)process_current_pid())
    return -KERR_PERM;
  gop_user_damage((uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4);
  return 0;
}

long sys_display_scale_info(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct display_scale_info *info =
      (struct display_scale_info *)(uintptr_t)a1;
  if (!writable(info, sizeof(*info)))
    return -1;
  info->physical_width = gop_get_physical_width();
  info->physical_height = gop_get_physical_height();
  info->scale_percent = gop_get_scale_percent();
  info->reserved = 0;
  return 0;
}

long sys_display_claim(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!process_has_capability(PROCESS_CAP_DISPLAY) && process_current_uid() != 0)
    return -KERR_PERM;
  return display_owner_claim((uint32_t)process_current_pid());
}

long sys_display_release(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  display_owner_release((uint32_t)process_current_pid());
  return 0;
}

long sys_display_set_mode(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (display_owner_pid() != (uint32_t)process_current_pid())
    return -KERR_PERM;
  uint32_t width = (uint32_t)a1, height = (uint32_t)a2;
  if (width < 640 || width > 7680 || height < 400 || height > 4320)
    return -KERR_INVAL;
  if (!svga_is_available())
    return -KERR_NODEV;
  uint32_t actual_w = 0, actual_h = 0;
  bool was3d = svga3d_active();
  svga3d_screen_teardown();
  if (!svga_set_mode(width, height, &actual_w, &actual_h))
    return -KERR_INVAL;
  void *front3d = was3d ? svga3d_screen_setup(actual_w, actual_h) : 0;
  bool applied = front3d ? gop_apply_live(front3d, actual_w, actual_h, actual_w, 32)
                         : gop_apply_live(svga_fb_virt(), actual_w, actual_h, svga_pitch_pixels(), 32);
  if (!applied)
    return -KERR_NODEV;
  mouse_set_bounds((int32_t)actual_w, (int32_t)actual_h);
  klogf(KLOG_OK, "display: mode %ux%u", actual_w, actual_h);
  return 0;
}
