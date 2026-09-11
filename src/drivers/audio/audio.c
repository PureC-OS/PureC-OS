#include "audio.h"
#include "audio_hda.h"
#include "../../kernel/core/system_info.h"
#include "../../kernel/diagnostics/klog.h"

#define PIT_CHANNEL2_PORT 0x42
#define PIT_COMMAND_PORT  0x43
#define SPEAKER_PORT      0x61
#define PIT_FREQUENCY     1193180U
#define AUDIO_DEFAULT_VOLUME 65

struct audio_master_bus {
    uint8_t volume;
    bool muted;
    uint32_t active_backend;
    uint32_t available_backends;
    bool pcm_ready;
    bool test_active;
    uint8_t test_step;
    uint64_t next_step_ms;
    bool sfx_active;
    uint64_t sfx_end_ms;
};

#define AUDIO_TONE_FREQ_MIN_HZ 30U
#define AUDIO_TONE_FREQ_MAX_HZ 8000U
#define AUDIO_TONE_MAX_DURATION_MS 5000U

// Sampled SFX streaming: userspace pushes S16 mono frames at
// PCM_SRC_RATE_HZ; the engine upsamples x2 to 44.1 kHz stereo into
// the HDA DMA double buffer, refilling the played-out half from
// audio_update() (LPIB-polled, no audio IRQ wired up yet).
#define PCM_SRC_RATE_HZ 22050U
#define PCM_HALF_STEREO_FRAMES 4096U
#define PCM_HALF_MONO_FRAMES 2048U
#define PCM_STAGE_FRAMES 24576U
#define PCM_PUSH_MAX_FRAMES 4096U
#define PCM_DRAIN_TAIL_MS 200U
#define PCM_DMA_HALF_BYTES 16384U

static int16_t pcm_stage[PCM_STAGE_FRAMES];
static int16_t pcm_scratch[PCM_HALF_STEREO_FRAMES * 2];
static uint32_t pcm_read_idx;
static uint32_t pcm_count;
static bool pcm_eos;
static bool pcm_playing;
static uint8_t pcm_last_half;
static uint64_t pcm_stop_at;

static struct audio_master_bus master_bus;

static const uint16_t test_frequencies[] = {
    880,
    1175,
    1568
};

static const uint16_t test_durations_ms[] = {
    90,
    90,
    120
};

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static uint8_t clamp_volume(uint8_t volume) {
    return volume > 100 ? 100 : volume;
}

static void pc_speaker_off(void) {
    uint8_t state = inb(SPEAKER_PORT);
    klogf(KLOG_DEBUG, "audio: PC speaker off port=0x61 before=0x%02x", state);
    outb(SPEAKER_PORT, state & (uint8_t)~0x03);
}

static void pc_speaker_tone(uint32_t frequency_hz) {
    klogf(KLOG_DEBUG, "audio: PC speaker tone request freq=%u volume=%u mute=%u",
          frequency_hz, master_bus.volume, master_bus.muted ? 1 : 0);
    if (frequency_hz == 0 || master_bus.muted || master_bus.volume == 0) {
        pc_speaker_off();
        return;
    }

    uint32_t divisor = PIT_FREQUENCY / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }

    outb(PIT_COMMAND_PORT, 0xB6);
    outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t state = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, state | 0x03);
}

static void stop_test_sound(void) {
    klogf(KLOG_DEBUG, "audio: test stop backend=%u step=%u",
          master_bus.active_backend, master_bus.test_step);
    master_bus.test_active = false;
    master_bus.test_step = 0;
    master_bus.next_step_ms = 0;
    hda_stop_tone();
    pc_speaker_off();
}

static void start_pc_speaker_test(void) {
    klog(KLOG_DEBUG, "audio: PC speaker test sequence begin");
    master_bus.test_active = true;
    master_bus.test_step = 0;
    master_bus.next_step_ms = system_info_uptime_ms() + test_durations_ms[0];
    pc_speaker_tone(test_frequencies[0]);
}

void audio_stop_tone(void) {
    if (master_bus.sfx_active) {
        klog(KLOG_DEBUG, "audio: sfx tone stop");
    }
    master_bus.sfx_active = false;
    master_bus.sfx_end_ms = 0;
    hda_stop_tone();
    pc_speaker_off();
}

void audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz < AUDIO_TONE_FREQ_MIN_HZ
        || frequency_hz > AUDIO_TONE_FREQ_MAX_HZ
        || duration_ms == 0) {
        klogf(KLOG_ERROR,
              "audio: sfx tone rejected reason=BAD_ARGUMENT freq=%u dur=%u",
              frequency_hz, duration_ms);
        audio_stop_tone();
        return;
    }
    if (duration_ms > AUDIO_TONE_MAX_DURATION_MS) {
        duration_ms = AUDIO_TONE_MAX_DURATION_MS;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        klog(KLOG_WARN, "audio: sfx tone ignored while master bus is muted or zero");
        audio_stop_tone();
        return;
    }
    stop_test_sound();
    audio_pcm_stop_stream();
    if (master_bus.pcm_ready) {
        if (!hda_play_tone((uint16_t)frequency_hz, master_bus.volume)) {
            klog(KLOG_WARN, "audio: HDA sfx tone failed, legacy fallback");
            pc_speaker_tone(frequency_hz);
        }
    } else {
        pc_speaker_tone(frequency_hz);
    }
    master_bus.sfx_active = true;
    master_bus.sfx_end_ms = system_info_uptime_ms() + duration_ms;
    klogf(KLOG_DEBUG, "audio: sfx tone start freq=%u dur=%u", frequency_hz, duration_ms);
}

static void pcm_reset_locked(void) {
    pcm_read_idx = 0;
    pcm_count = 0;
    pcm_eos = false;
    pcm_playing = false;
    pcm_last_half = 0;
    pcm_stop_at = 0;
}

static void pcm_fill_half(uint32_t half) {
    uint32_t take = pcm_count >= PCM_HALF_MONO_FRAMES
        ? PCM_HALF_MONO_FRAMES : pcm_count;
    bool audible = !master_bus.muted && master_bus.volume != 0;
    int32_t gain = (int32_t)master_bus.volume;
    for (uint32_t i = 0; i < PCM_HALF_MONO_FRAMES; i++) {
        int16_t sample = 0;
        if (i < take) {
            sample = pcm_stage[(pcm_read_idx + i) % PCM_STAGE_FRAMES];
        }
        int32_t scaled = audible ? ((int32_t)sample * gain) / 100 : 0;
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        pcm_scratch[i * 2] = (int16_t)scaled;
        pcm_scratch[i * 2 + 1] = (int16_t)scaled;
    }
    pcm_read_idx = (pcm_read_idx + take) % PCM_STAGE_FRAMES;
    pcm_count -= take;
    hda_pcm_fill_half(half, pcm_scratch, PCM_HALF_STEREO_FRAMES);
}

// Returns frames accepted, or negative: -1 invalid, -2 staging full,
// -3 PCM backend not available (PC speaker cannot play samples).
int32_t audio_pcm_push(const int16_t *samples, uint32_t frames) {
    if (!samples || frames == 0 || frames > PCM_PUSH_MAX_FRAMES) {
        return -1;
    }
    if (!master_bus.pcm_ready) {
        return -3;
    }
    uint32_t free = PCM_STAGE_FRAMES - pcm_count;
    if (frames > free) {
        klogf(KLOG_WARN, "audio: PCM push rejected reason=STAGING_FULL frames=%u free=%u",
              frames, free);
        return -2;
    }
    uint32_t write_idx = (pcm_read_idx + pcm_count) % PCM_STAGE_FRAMES;
    for (uint32_t i = 0; i < frames; i++) {
        pcm_stage[write_idx] = samples[i];
        write_idx++;
        if (write_idx >= PCM_STAGE_FRAMES) write_idx = 0;
    }
    pcm_count += frames;
    return (int32_t)frames;
}

void audio_pcm_eos(void) {
    pcm_eos = true;
}

bool audio_pcm_start(void) {
    bool eos = pcm_eos;
    stop_test_sound();
    audio_stop_tone();
    audio_pcm_stop_stream();
    pcm_eos = eos;
    if (!master_bus.pcm_ready) {
        klog(KLOG_WARN, "audio: PCM start rejected reason=PCM_NOT_READY");
        return false;
    }
    if (pcm_count == 0) {
        klog(KLOG_WARN, "audio: PCM start rejected reason=EMPTY_STAGE");
        return false;
    }
    pcm_fill_half(0);
    pcm_fill_half(1);
    pcm_last_half = 0;
    pcm_stop_at = 0;
    if (!hda_pcm_start()) {
        pcm_reset_locked();
        return false;
    }
    pcm_playing = true;
    klogf(KLOG_DEBUG, "audio: PCM stream start staged=%u eos=%u",
          pcm_count, pcm_eos ? 1 : 0);
    return true;
}

void audio_pcm_stop_stream(void) {
    if (pcm_playing) {
        klog(KLOG_DEBUG, "audio: PCM stream stop");
    }
    hda_pcm_stop();
    pcm_reset_locked();
}

static void audio_pcm_update(void) {
    if (!pcm_playing) {
        return;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        // Keep timing: refill path already outputs silence.
    }
    uint32_t lpib = hda_pcm_position();
    uint8_t current_half = (lpib < PCM_DMA_HALF_BYTES) ? 0 : 1;
    if (current_half != pcm_last_half) {
        pcm_fill_half(pcm_last_half);
        pcm_last_half = current_half;
    }
    if (pcm_eos && pcm_count == 0) {
        uint64_t now = system_info_uptime_ms();
        if (pcm_stop_at == 0) {
            pcm_stop_at = now + PCM_DRAIN_TAIL_MS;
        } else if (now >= pcm_stop_at) {
            klog(KLOG_DEBUG, "audio: PCM stream drained");
            hda_pcm_stop();
            pcm_reset_locked();
        }
    }
}

void audio_init(void) {
    klog(KLOG_INFO, "audio: init begin");
    master_bus.volume = AUDIO_DEFAULT_VOLUME;
    master_bus.muted = false;
    master_bus.active_backend = AUDIO_BACKEND_PC_SPEAKER;
    master_bus.available_backends = AUDIO_BACKEND_PC_SPEAKER;
    master_bus.pcm_ready = false;
    master_bus.test_active = false;
    master_bus.test_step = 0;
    master_bus.next_step_ms = 0;
    master_bus.sfx_active = false;
    master_bus.sfx_end_ms = 0;
    pcm_reset_locked();
    pc_speaker_off();
    klog(KLOG_INFO, "audio: probing HDA devices before backend selection");
    hda_init();
    if (hda_is_present()) {
        master_bus.available_backends |= AUDIO_BACKEND_HDA;
        master_bus.pcm_ready = hda_pcm_output_ready();
        if (master_bus.pcm_ready) {
            master_bus.active_backend = AUDIO_BACKEND_HDA;
            klogf(KLOG_OK, "audio: automatic HDA selection succeeded device=%u",
                  hda_selected_output_device());
        } else {
            klogf(KLOG_WARN,
                  "audio: legacy fallback reason=HDA_PCM_NOT_READY discovered_outputs=%u",
                  hda_output_device_count());
        }
        klogf(KLOG_INFO,
              "audio: HDA probe result present=1 pcm_ready=%u outputs=%u selected=%u",
              master_bus.pcm_ready ? 1 : 0, hda_output_device_count(),
              hda_selected_output_device());
    } else {
        klog(KLOG_WARN,
             "audio: legacy fallback reason=HDA_CONTROLLER_NOT_FOUND");
    }
    if(master_bus.pcm_ready)
        (void)hda_set_master_volume(master_bus.volume,master_bus.muted);
    klogf(KLOG_INFO, "audio: master bus volume=%u mute=%u backend=%u available=0x%x pcm=%u",
          master_bus.volume, master_bus.muted ? 1 : 0,
          master_bus.active_backend, master_bus.available_backends,
          master_bus.pcm_ready ? 1 : 0);
}

void audio_get_status(struct audio_status *status) {
    if (!status) {
        klog(KLOG_ERROR, "audio: status request rejected reason=NULL_OUTPUT");
        return;
    }

    audio_update();
    struct hda_output_device_info device = {0};
    uint32_t hda_selected = hda_selected_output_device();
    bool have_hda_device = hda_get_output_device(hda_selected, &device);
    klogf(KLOG_DEBUG, "audio: status volume=%u mute=%u backend=%u available=0x%x pcm=%u test=%u devices=%u selected=%u codec=%u dac=%u pin=%u",
          master_bus.volume, master_bus.muted ? 1 : 0, master_bus.active_backend,
          master_bus.available_backends, master_bus.pcm_ready ? 1 : 0,
          master_bus.test_active ? 1 : 0, 1U + hda_output_device_count(),
          master_bus.active_backend == AUDIO_BACKEND_HDA ? hda_selected + 1U : 0U,
          have_hda_device ? device.codec_address : 0,
          have_hda_device ? device.dac_node : 0,
          have_hda_device ? device.pin_node : 0);
    status->volume = master_bus.volume;
    status->muted = master_bus.muted ? 1 : 0;
    status->backend = master_bus.active_backend;
    status->available_backends = master_bus.available_backends;
    status->pcm_ready = master_bus.pcm_ready ? 1 : 0;
    status->test_active = master_bus.test_active ? 1 : 0;
    status->output_device_count = 1U + hda_output_device_count();
    status->selected_output_device = master_bus.active_backend == AUDIO_BACKEND_HDA
        ? hda_selected + 1U : 0U;
    status->hda_codec = have_hda_device ? device.codec_address : 0;
    status->hda_dac_node = have_hda_device ? device.dac_node : 0;
    status->hda_pin_node = have_hda_device ? device.pin_node : 0;
}

uint8_t audio_get_volume(void) {
    return master_bus.volume;
}

bool audio_is_muted(void) {
    return master_bus.muted;
}

void audio_set_volume(uint8_t volume) {
    uint8_t next_volume = clamp_volume(volume);
    if (next_volume != master_bus.volume) {
        klogf(KLOG_INFO, "audio: master volume %u -> %u",
              master_bus.volume, next_volume);
    }
    master_bus.volume = next_volume;
    klogf(KLOG_DEBUG, "audio: master state volume=%u mute=%u test=%u",
          master_bus.volume, master_bus.muted ? 1 : 0,
          master_bus.test_active ? 1 : 0);
    if (master_bus.volume > 0) {
        master_bus.muted = false;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        stop_test_sound();
        audio_stop_tone();
    }
    if(master_bus.pcm_ready)
        (void)hda_set_master_volume(master_bus.volume,master_bus.muted);
}

void audio_set_muted(bool muted) {
    if (master_bus.muted != muted) {
        klogf(KLOG_INFO, "audio: master mute=%u", muted ? 1 : 0);
    }
    master_bus.muted = muted;
    klogf(KLOG_DEBUG, "audio: master state volume=%u mute=%u test=%u",
          master_bus.volume, master_bus.muted ? 1 : 0,
          master_bus.test_active ? 1 : 0);
    if (master_bus.muted) {
        stop_test_sound();
        audio_stop_tone();
    }
    if(master_bus.pcm_ready)
        (void)hda_set_master_volume(master_bus.volume,master_bus.muted);
}

void audio_adjust_volume(int8_t delta) {
    int16_t next_volume = (int16_t)master_bus.volume + delta;

    if (next_volume < 0) {
        next_volume = 0;
    }
    if (next_volume > 100) {
        next_volume = 100;
    }

    audio_set_volume((uint8_t)next_volume);
}

bool audio_select_output_device(uint32_t index) {
    uint32_t count = 1U + hda_output_device_count();
    klogf(KLOG_INFO,
          "audio: output selection request index=%u count=%u current_backend=%u",
          index, count, master_bus.active_backend);
    if (index >= count) {
        klogf(KLOG_ERROR,
              "audio: output selection rejected reason=INDEX_OUT_OF_RANGE index=%u count=%u",
              index, count);
        return false;
    }
    stop_test_sound();
    audio_stop_tone();
    audio_pcm_stop_stream();
    if (index == 0) {
        master_bus.active_backend = AUDIO_BACKEND_PC_SPEAKER;
        master_bus.pcm_ready = false;
        klog(KLOG_OK, "audio: output device selected index=0 backend=PC_SPEAKER");
        return true;
    }
    uint32_t hda_index = index - 1U;
    if (!hda_select_output_device(hda_index)) {
        master_bus.active_backend = AUDIO_BACKEND_PC_SPEAKER;
        master_bus.pcm_ready = false;
        klogf(KLOG_ERROR,
              "audio: HDA output selection failed index=%u hda_index=%u fallback=PC_SPEAKER",
              index, hda_index);
        return false;
    }
    master_bus.active_backend = AUDIO_BACKEND_HDA;
    master_bus.pcm_ready = true;
    (void)hda_set_master_volume(master_bus.volume,master_bus.muted);
    klogf(KLOG_OK, "audio: output device selected index=%u backend=HDA hda_index=%u",
          index, hda_index);
    return true;
}

void audio_play_test_sound(void) {
    klogf(KLOG_INFO, "audio: test request mute=%u volume=%u backend=%u pcm=%u active=%u",
          master_bus.muted ? 1 : 0, master_bus.volume, master_bus.active_backend,
          master_bus.pcm_ready ? 1 : 0, master_bus.test_active ? 1 : 0);
    audio_stop_tone();
    audio_pcm_stop_stream();
    if (master_bus.muted || master_bus.volume == 0) {
        klog(KLOG_WARN, "audio: test sound ignored while master bus is muted or zero");
        return;
    }

    if (master_bus.pcm_ready) {
        klog(KLOG_INFO, "audio: starting HDA PCM test path");
        if (!hda_play_tone(test_frequencies[0], master_bus.volume)) {
            klog(KLOG_WARN, "audio: HDA PCM test could not start");
        } else {
            master_bus.test_active = true;
            master_bus.test_step = 0;
            master_bus.next_step_ms = system_info_uptime_ms() + test_durations_ms[0];
        }
        return;
    }

    klogf(KLOG_WARN,
          "audio: test using legacy path reason=PCM_NOT_READY backend=%u hda_present=%u hda_outputs=%u",
          master_bus.active_backend, hda_is_present() ? 1 : 0,
          hda_output_device_count());
    start_pc_speaker_test();
}

void audio_update(void) {
    audio_pcm_update();
    if (master_bus.sfx_active) {
        if (master_bus.muted || master_bus.volume == 0) {
            audio_stop_tone();
        } else if (system_info_uptime_ms() >= master_bus.sfx_end_ms) {
            audio_stop_tone();
        }
    }
    if (!master_bus.test_active) {
        return;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        stop_test_sound();
        return;
    }

    uint64_t now = system_info_uptime_ms();
    if (now < master_bus.next_step_ms) {
        return;
    }

    master_bus.test_step++;
    if (master_bus.test_step >= sizeof(test_frequencies) / sizeof(test_frequencies[0])) {
        klog(KLOG_INFO, "audio: test sequence complete");
        stop_test_sound();
        return;
    }

    master_bus.next_step_ms = now + test_durations_ms[master_bus.test_step];
    if (master_bus.pcm_ready) {
        if (!hda_play_tone(test_frequencies[master_bus.test_step], master_bus.volume)) {
            klog(KLOG_WARN, "audio: HDA PCM tone step failed");
            stop_test_sound();
        }
    } else {
        pc_speaker_tone(test_frequencies[master_bus.test_step]);
    }
}
