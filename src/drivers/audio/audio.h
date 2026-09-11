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
void audio_update(void);
