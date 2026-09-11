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

int32_t pa_update(void) {
    (void)pc_syscall(SYS_AUDIO_UPDATE, 0, 0, 0);
    return 0;
}
