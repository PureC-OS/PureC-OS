#include "audio.h"
#include "syscall.h"

bool userspace_audio_get_status(struct audio_status *status) {
    return userspace_syscall(SYS_AUDIO_GET_STATUS, (uint64_t)status, 0, 0) >= 0;
}

uint8_t userspace_audio_get_volume(void) {
    int64_t volume = userspace_syscall(SYS_AUDIO_GET_VOLUME, 0, 0, 0);
    if (volume < 0) {
        return 0;
    }
    if (volume > 100) {
        return 100;
    }
    return (uint8_t)volume;
}

bool userspace_audio_is_muted(void) {
    return userspace_syscall(SYS_AUDIO_IS_MUTED, 0, 0, 0) != 0;
}

void userspace_audio_set_volume(uint8_t volume) {
    (void)userspace_syscall(SYS_AUDIO_SET_VOLUME, volume, 0, 0);
}

void userspace_audio_set_muted(bool muted) {
    (void)userspace_syscall(SYS_AUDIO_SET_MUTED, muted ? 1 : 0, 0, 0);
}

void userspace_audio_toggle_mute(void) {
    userspace_audio_set_muted(!userspace_audio_is_muted());
}

void userspace_audio_adjust_volume(int8_t delta) {
    (void)userspace_syscall(SYS_AUDIO_ADJUST_VOLUME, (uint64_t)(int64_t)delta, 0, 0);
}

bool userspace_audio_select_output_device(uint32_t index) {
    return userspace_syscall(SYS_AUDIO_SELECT_OUTPUT_DEVICE, index, 0, 0) == 0;
}

void userspace_audio_play_test_sound(void) {
    (void)userspace_syscall(SYS_AUDIO_PLAY_TEST_SOUND, 0, 0, 0);
}

void userspace_audio_update(void) {
    (void)userspace_syscall(SYS_AUDIO_UPDATE, 0, 0, 0);
}
