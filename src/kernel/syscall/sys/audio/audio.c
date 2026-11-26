#include <drivers/audio/audio.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <kernel/diagnostics/klog.h>
#include "../common.h"

static uint32_t parse_setting_u32(const char *text)
{
  uint32_t value = 0;
  while (*text >= '0' && *text <= '9')
  {
    uint32_t digit = (uint32_t)(*text - '0');
    if (value > (UINT32_MAX - digit) / 10U)
      return UINT32_MAX;
    value = value * 10U + digit;
    text++;
  }
  return value;
}

void audio_restore_system_settings(void)
{
  int32_t descriptor = vfs_open("/config/settings.ini");
  if (descriptor < 0)
  {
    klog(KLOG_INFO, "audio: no saved system settings; detected defaults kept");
    return;
  }
  char buffer[256] = {0};
  int32_t amount = vfs_read(descriptor, buffer, sizeof(buffer) - 1);
  (void)vfs_close(descriptor);
  if (amount <= 0)
    return;
  buffer[amount] = '\0';
  struct audio_status detected = {0};
  audio_get_status(&detected);
  uint32_t volume = detected.volume;
  uint32_t device = detected.selected_output_device;
  bool muted = detected.muted != 0;
  for (char *line = buffer; *line;)
  {
    char *end = line;
    while (*end && *end != '\n' && *end != '\r')
      end++;
    char saved = *end;
    *end = '\0';
    if (strncmp(line, "volume=", 7) == 0)
      volume = parse_setting_u32(line + 7);
    else if (strncmp(line, "muted=", 6) == 0)
      muted = parse_setting_u32(line + 6) != 0;
    else if (strncmp(line, "device=", 7) == 0)
      device = parse_setting_u32(line + 7);
    if (!saved)
      break;
    line = end + 1;
    while (*line == '\n' || *line == '\r')
      line++;
  }
  if (volume > 100)
    volume = 100;
  if (device < detected.output_device_count)
    (void)audio_select_output_device(device);
  audio_set_volume((uint8_t)volume);
  audio_set_muted(muted);
  klogf(KLOG_OK, "audio: restored settings volume=%u mute=%u device=%u", volume,
        muted ? 1 : 0, device);
}

long sys_audio_get_status(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct audio_status *out = (struct audio_status *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  audio_get_status(out);
  return 0;
}

long sys_audio_get_volume(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_update();
  return audio_get_volume();
}

long sys_audio_set_volume(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (a1 > 100)
    return -1;
  audio_set_volume((uint8_t)a1);
  return 0;
}

long sys_audio_is_muted(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_update();
  return audio_is_muted() ? 1 : 0;
}

long sys_audio_set_muted(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_set_muted(a1 != 0);
  return 0;
}

long sys_audio_adjust_volume(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_adjust_volume((int8_t)(int64_t)a1);
  return 0;
}

long sys_audio_play_test_sound(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_play_test_sound();
  return 0;
}

long sys_audio_play_tone(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint32_t freq = (uint32_t)a1;
  uint32_t dur = (uint32_t)a2;
  if (freq < 30 || freq > 8000 || dur == 0 || dur > 5000)
    return -1;
  audio_play_tone(freq, dur);
  return 0;
}

long sys_audio_stop_tone(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_stop_tone();
  return 0;
}

long sys_audio_pcm_push(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  uint32_t frames = (uint32_t)a2;
  if (frames == 0)
  {
    audio_pcm_eos();
    return 0;
  }
  const int16_t *samples = (const int16_t *)(uintptr_t)a1;
  if (frames > 4096)
    return -1;
  if (!readable(samples, (uint64_t)frames * 2U))
    return -1;
  return audio_pcm_push(samples, frames);
}

long sys_audio_pcm_start(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return audio_pcm_start() ? 0 : -1;
}

long sys_audio_pcm_stop(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_pcm_stop_stream();
  return 0;
}

long sys_audio_update(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  audio_update();
  return 0;
}

long sys_audio_select_output_device(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  klogf(KLOG_INFO, "audio: syscall select output device index=%u",
        (uint32_t)a1);
  return audio_select_output_device((uint32_t)a1) ? 0 : -1;
}
