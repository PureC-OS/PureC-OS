#include "include/pureaudio.h"
#include "../libc/include/purec.h"
#include "../kernel/syscall/syscall.h"
#include <stddef.h>

#define STREAM_CHUNK_FRAMES 1024U
#define STREAM_LEAD_FRAMES 4096U
#define STREAM_MAX_BEHIND_FRAMES 11025U

static pa_stream_fill_fn stream_fill;
static void *stream_user;
static bool stream_open;
static bool stream_running;
static uint64_t stream_origin_ms;
static uint64_t stream_produced;

static uint64_t uptime_ms(void) {
    struct time_info info;
    if (pc_time_get(&info) != 0) {
        return 0;
    }
    return info.uptime_ms;
}

static void stream_reset_clock(void) {
    stream_origin_ms = uptime_ms();
    stream_produced = 0;
}

int32_t pa_stream_open(pa_stream_fill_fn fill, void *user) {
    if (!fill) {
        return PA_ERROR_INVALID;
    }
    if (stream_open) {
        return PA_ERROR_INVALID;
    }
    struct pa_status status;
    if (pa_get_status(&status) != 0 || !status.pcm_ready) {
        return PA_ERROR_UNSUPPORTED;
    }
    pc_audio_pcm_stop();
    stream_fill = fill;
    stream_user = user;
    stream_open = true;
    stream_running = false;
    stream_reset_clock();
    return 0;
}

void pa_stream_close(void) {
    if (!stream_open) {
        return;
    }
    if (stream_running) {
        pc_audio_pcm_stop();
    }
    stream_open = false;
    stream_running = false;
    stream_fill = 0;
    stream_user = 0;
}

bool pa_stream_is_open(void) {
    return stream_open;
}

int32_t pa_stream_pump(void) {
    if (!stream_open) {
        return PA_ERROR_INVALID;
    }
    uint64_t elapsed_ms = uptime_ms() - stream_origin_ms;
    uint64_t clock_frames = elapsed_ms * PA_WAV_SRC_RATE / 1000U;
    uint64_t target = clock_frames + STREAM_LEAD_FRAMES;

    if (stream_running && clock_frames > stream_produced + STREAM_MAX_BEHIND_FRAMES) {
        pc_audio_pcm_stop();
        stream_running = false;
        stream_reset_clock();
        elapsed_ms = 0;
        clock_frames = 0;
        target = STREAM_LEAD_FRAMES;
    }

    int16_t chunk[STREAM_CHUNK_FRAMES];
    while (stream_produced < target) {
        uint64_t need = target - stream_produced;
        uint32_t count = need > STREAM_CHUNK_FRAMES ? STREAM_CHUNK_FRAMES : (uint32_t)need;
        stream_fill(chunk, count, stream_user);
        int32_t pushed = pc_audio_pcm_push(chunk, count);
        if (pushed == -3) {
            return PA_ERROR_UNSUPPORTED;
        }
        if (pushed == -2) {
            break;
        }
        if (pushed <= 0) {
            return PA_ERROR_IO;
        }
        stream_produced += (uint32_t)pushed;
    }

    if (!stream_running && stream_produced >= STREAM_LEAD_FRAMES) {
        if (pc_audio_pcm_start() != 0) {
            return PA_ERROR_IO;
        }
        stream_running = true;
        stream_origin_ms = uptime_ms();
    }
    (void)pa_update();
    return 0;
}
