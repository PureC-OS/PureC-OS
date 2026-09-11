#include "include/pureaudio.h"
#include "../libc/include/purec.h"
#include "../kernel/syscall/syscall.h"
#include <stddef.h>

const char *pa_version(void) {
    return PA_VERSION;
}

const char *pa_backend_name(uint32_t backend) {
    switch (backend) {
        case PA_BACKEND_NONE:
            return "None";
        case PA_BACKEND_PC_SPEAKER:
            return "Legacy PC speaker";
        case PA_BACKEND_HDA:
            return "High Definition Audio";
        default:
            return "Unknown";
    }
}

const char *pa_strerror(int32_t error) {
    switch (error) {
        case 0:
            return "Success";
        case PA_ERROR_IO:
            return "I/O Error";
        case PA_ERROR_INVALID:
            return "Invalid Argument";
        case PA_ERROR_UNSUPPORTED:
            return "Unsupported Operation";
        case PA_ERROR_NOT_FOUND:
            return "Device Not Found";
        default:
            return "Unknown Error";
    }
}

int32_t pa_get_status(struct pa_status *status) {
    if (!status) {
        return PA_ERROR_INVALID;
    }
    struct audio_status kstatus;
    if (!pc_audio_get_status(&kstatus)) {
        return PA_ERROR_IO;
    }
    status->volume = kstatus.volume;
    status->muted = kstatus.muted;
    status->backend = kstatus.backend;
    status->available_backends = kstatus.available_backends;
    status->pcm_ready = kstatus.pcm_ready;
    status->test_active = kstatus.test_active;
    status->output_device_count = kstatus.output_device_count;
    status->selected_output_device = kstatus.selected_output_device;
    status->hda_codec = kstatus.hda_codec;
    status->hda_dac_node = kstatus.hda_dac_node;
    status->hda_pin_node = kstatus.hda_pin_node;
    return 0;
}

int32_t pa_get_volume(void) {
    int32_t vol = pc_audio_get_volume();
    if (vol < 0) {
        return 0;
    }
    if (vol > 100) {
        return 100;
    }
    return vol;
}

int32_t pa_set_volume(uint8_t volume) {
    if (volume > 100) {
        volume = 100;
    }
    pc_audio_set_volume(volume);
    return 0;
}

int32_t pa_adjust_volume(int8_t delta) {
    pc_audio_adjust_volume((int32_t)delta);
    return 0;
}

int32_t pa_volume_up(uint8_t step) {
    return pa_adjust_volume((int8_t)step);
}

int32_t pa_volume_down(uint8_t step) {
    return pa_adjust_volume(-(int8_t)step);
}

bool pa_is_muted(void) {
    return pc_audio_is_muted();
}

int32_t pa_set_muted(bool muted) {
    pc_audio_set_muted(muted);
    return 0;
}

int32_t pa_mute(void) {
    return pa_set_muted(true);
}

int32_t pa_unmute(void) {
    return pa_set_muted(false);
}

int32_t pa_toggle_mute(void) {
    return pa_set_muted(!pa_is_muted());
}

uint32_t pa_get_output_device_count(void) {
    struct pa_status status;
    if (pa_get_status(&status) == 0) {
        return status.output_device_count;
    }
    return 0;
}

uint32_t pa_get_selected_output_device(void) {
    struct pa_status status;
    if (pa_get_status(&status) == 0) {
        return status.selected_output_device;
    }
    return 0;
}

int32_t pa_select_output_device(uint32_t index) {
    if (!pc_audio_select_output(index)) {
        return PA_ERROR_IO;
    }
    return 0;
}

int32_t pa_next_output_device(void) {
    struct pa_status status;
    if (pa_get_status(&status) != 0) {
        return PA_ERROR_IO;
    }
    if (status.output_device_count <= 1) {
        return 0;
    }
    uint32_t next = (status.selected_output_device + 1) % status.output_device_count;
    return pa_select_output_device(next);
}

int32_t pa_play_test_sound(void) {
    pc_audio_play_test();
    return 0;
}

int32_t pa_play_tone(uint16_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz < 30 || frequency_hz > 8000 || duration_ms == 0
        || duration_ms > 5000) {
        return PA_ERROR_INVALID;
    }
    if (pc_audio_play_tone(frequency_hz, duration_ms) != 0) {
        return PA_ERROR_IO;
    }
    return 0;
}

int32_t pa_stop_tone(void) {
    pc_audio_stop_tone();
    return 0;
}

static uint16_t wav_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wav_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t wav_read_full(int32_t fd, uint8_t *buffer, uint32_t size) {
    uint32_t done = 0;
    while (done < size) {
        int32_t got = pc_file_read(fd, buffer + done, size - done);
        if (got <= 0) {
            return -1;
        }
        done += (uint32_t)got;
    }
    return 0;
}

static int32_t wav_skip(int32_t fd, uint32_t size) {
    uint8_t discard[128];
    while (size > 0) {
        uint32_t chunk = size > sizeof(discard) ? sizeof(discard) : size;
        if (wav_read_full(fd, discard, chunk) != 0) {
            return -1;
        }
        size -= chunk;
    }
    return 0;
}

int32_t pa_wav_load(const char *path, int16_t *out_frames,
                    uint32_t capacity_frames, uint32_t *out_count) {
    if (!path || !out_frames || capacity_frames == 0) {
        return PA_ERROR_INVALID;
    }
    int32_t fd = pc_file_open(path);
    if (fd < 0) {
        return PA_ERROR_NOT_FOUND;
    }
    int32_t result = PA_ERROR_INVALID;
    uint32_t data_size = 0;
    bool have_fmt = false;
    uint8_t header[12];
    if (wav_read_full(fd, header, sizeof(header)) != 0) {
        result = PA_ERROR_IO;
    } else if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F'
        || header[3] != 'F' || header[8] != 'W' || header[9] != 'A'
        || header[10] != 'V' || header[11] != 'E') {
        result = PA_ERROR_INVALID;
    } else {
        result = 0;
        for (uint32_t guard = 0; guard < 16; guard++) {
            uint8_t chunk[8];
            if (wav_read_full(fd, chunk, sizeof(chunk)) != 0) {
                result = PA_ERROR_IO;
                break;
            }
            uint32_t size = wav_u32(chunk + 4);
            if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't'
                && chunk[3] == ' ') {
                uint8_t fmt[16];
                if (size < 16 || wav_read_full(fd, fmt, sizeof(fmt)) != 0) {
                    result = PA_ERROR_IO;
                    break;
                }
                if (wav_u16(fmt) != 1 || wav_u16(fmt + 2) != 1
                    || wav_u32(fmt + 4) != PA_WAV_SRC_RATE
                    || wav_u16(fmt + 14) != 16) {
                    result = PA_ERROR_UNSUPPORTED;
                    break;
                }
                have_fmt = true;
                if (wav_skip(fd, size - 16 + (size & 1U)) != 0) {
                    result = PA_ERROR_IO;
                    break;
                }
            } else if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't'
                && chunk[3] == 'a') {
                data_size = size;
                break;
            } else {
                if (size > 1024U * 1024U
                    || wav_skip(fd, size + (size & 1U)) != 0) {
                    result = PA_ERROR_IO;
                    break;
                }
            }
        }
        if (result == 0 && (!have_fmt || data_size == 0)) {
            result = PA_ERROR_INVALID;
        }
    }
    if (result == 0) {
        uint32_t want = data_size;
        if (want > capacity_frames * 2U) {
            want = capacity_frames * 2U;
        }
        want &= ~1U;
        if (wav_read_full(fd, (uint8_t *)out_frames, want) != 0) {
            result = PA_ERROR_IO;
        } else {
            if (out_count) {
                *out_count = want / 2U;
            }
        }
    }
    pc_file_close(fd);
    return result;
}

int32_t pa_sfx_play(const int16_t *frames, uint32_t frame_count) {
    if (!frames || frame_count == 0 || frame_count > 24576U) {
        return PA_ERROR_INVALID;
    }
    pc_audio_pcm_stop();
    uint32_t offset = 0;
    while (offset < frame_count) {
        uint32_t chunk = frame_count - offset;
        if (chunk > 4096U) {
            chunk = 4096U;
        }
        int32_t pushed = pc_audio_pcm_push(frames + offset, chunk);
        if (pushed == -3) {
            return PA_ERROR_UNSUPPORTED;
        }
        if (pushed <= 0) {
            pc_audio_pcm_stop();
            return PA_ERROR_IO;
        }
        offset += (uint32_t)pushed;
    }
    pc_audio_pcm_eos();
    if (pc_audio_pcm_start() != 0) {
        pc_audio_pcm_stop();
        return PA_ERROR_IO;
    }
    return 0;
}

int32_t pa_sfx_stop(void) {
    pc_audio_pcm_stop();
    return 0;
}

int32_t pa_update(void) {
    (void)pc_syscall(SYS_AUDIO_UPDATE, 0, 0, 0);
    return 0;
}
