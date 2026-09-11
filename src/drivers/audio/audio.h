#pragma once

#include "../../kernel/syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

void audio_init(void);
void audio_get_status(struct audio_status *status);
uint8_t audio_get_volume(void);
bool audio_is_muted(void);
void audio_set_volume(uint8_t volume);
void audio_set_muted(bool muted);
void audio_adjust_volume(int8_t delta);
bool audio_select_output_device(uint32_t index);
void audio_play_test_sound(void);
void audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
void audio_stop_tone(void);
// Sampled PCM streaming: S16 mono frames at 22050 Hz from userspace.
// push returns frames accepted or negative (-1 invalid, -2 staging
// full, -3 PCM backend unavailable). eos marks end-of-stream so the
// engine stops after draining. start/stop control playback.
int32_t audio_pcm_push(const int16_t *samples, uint32_t frames);
void audio_pcm_eos(void);
bool audio_pcm_start(void);
void audio_pcm_stop_stream(void);
void audio_update(void);
