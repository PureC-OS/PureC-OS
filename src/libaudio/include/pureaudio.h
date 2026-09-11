#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PA_API_VERSION 1
#define PA_VERSION "V1.0.0"
#define PA_VERSION_MAJOR 1
#define PA_VERSION_MINOR 0
#define PA_VERSION_PATCH 0

#define PA_BACKEND_NONE 0
#define PA_BACKEND_PC_SPEAKER 1
#define PA_BACKEND_HDA 2

#define PA_ERROR_IO -1
#define PA_ERROR_INVALID -2
#define PA_ERROR_UNSUPPORTED -3
#define PA_ERROR_NOT_FOUND -4

struct pa_status {
    uint32_t volume;
    uint32_t muted;
    uint32_t backend;
    uint32_t available_backends;
    uint32_t pcm_ready;
    uint32_t test_active;
    uint32_t output_device_count;
    uint32_t selected_output_device;
    uint32_t hda_codec;
    uint32_t hda_dac_node;
    uint32_t hda_pin_node;
};

// Метаинформация и версии
const char *pa_version(void);
const char *pa_backend_name(uint32_t backend);
const char *pa_strerror(int32_t error);

// Статус и состояние
int32_t pa_get_status(struct pa_status *status);

// Управление громкостью (0 .. 100)
int32_t pa_get_volume(void);
int32_t pa_set_volume(uint8_t volume);
int32_t pa_adjust_volume(int8_t delta);
int32_t pa_volume_up(uint8_t step);
int32_t pa_volume_down(uint8_t step);

// Управление mute
bool pa_is_muted(void);
int32_t pa_set_muted(bool muted);
int32_t pa_mute(void);
int32_t pa_unmute(void);
int32_t pa_toggle_mute(void);

// Устройства вывода
uint32_t pa_get_output_device_count(void);
uint32_t pa_get_selected_output_device(void);
int32_t pa_select_output_device(uint32_t index);
int32_t pa_next_output_device(void);

// Воспроизведение
int32_t pa_play_test_sound(void);
int32_t pa_play_tone(uint16_t frequency_hz, uint32_t duration_ms);
int32_t pa_stop_tone(void);
// Sampled SFX: 22050 Hz mono 16-bit PCM.
// pa_wav_load parses a RIFF/WAVE file into a caller-provided buffer
// (no heap inside the library) and reports the frame count.
// pa_sfx_play pushes buffered frames to the kernel stream and starts
// playback; returns PA_ERROR_UNSUPPORTED when the active backend
// cannot play samples (legacy PC speaker).
#define PA_WAV_SRC_RATE 22050U
int32_t pa_wav_load(const char *path, int16_t *out_frames,
                    uint32_t capacity_frames, uint32_t *out_count);
int32_t pa_sfx_play(const int16_t *frames, uint32_t frame_count);
int32_t pa_sfx_stop(void);
int32_t pa_update(void);

#ifdef __cplusplus
}
#endif
