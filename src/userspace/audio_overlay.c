#include "audio_overlay.h"
#include "audio.h"
#include "display.h"

#define AUDIO_OVERLAY_X 12
#define AUDIO_OVERLAY_Y 132
#define AUDIO_OVERLAY_W 430
#define AUDIO_OVERLAY_H 84
#define AUDIO_OVERLAY_BG 0x313244
#define AUDIO_OVERLAY_TEXT 0xCDD6F4
#define AUDIO_OVERLAY_ACCENT 0x89DCEB
#define AUDIO_OVERLAY_GOOD 0xA6E3A1
#define AUDIO_OVERLAY_WARN 0xF9E2AF
#define AUDIO_OVERLAY_BAD 0xF38BA8

static void append_char(char *buffer, uint32_t *length, uint32_t capacity, char value) {
    if (*length + 1 >= capacity) {
        return;
    }
    buffer[*length] = value;
    *length += 1;
    buffer[*length] = '\0';
}

static void append_text(char *buffer, uint32_t *length, uint32_t capacity, const char *text) {
    while (*text != '\0') {
        append_char(buffer, length, capacity, *text);
        text++;
    }
}

static void append_uint(char *buffer, uint32_t *length, uint32_t capacity, uint32_t value) {
    char reversed[10];
    uint32_t count = 0;

    do {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));

    while (count > 0) {
        append_char(buffer, length, capacity, reversed[--count]);
    }
}

static const char *backend_name(const struct audio_status *status) {
    if (status->backend == AUDIO_BACKEND_HDA) {
        return "HDA";
    }
    if (status->backend == AUDIO_BACKEND_PC_SPEAKER) {
        return "PCSPK";
    }
    return "NONE";
}

static uint32_t backend_color(const struct audio_status *status) {
    if (status->backend == AUDIO_BACKEND_HDA && status->pcm_ready != 0) {
        return AUDIO_OVERLAY_GOOD;
    }
    if (status->backend != AUDIO_BACKEND_NONE) {
        return AUDIO_OVERLAY_WARN;
    }
    return AUDIO_OVERLAY_BAD;
}

static void make_master_label(char *buffer, uint32_t capacity,
                              const struct audio_status *status) {
    uint32_t length = 0;
    append_text(buffer, &length, capacity,
                status->muted != 0 ? "MASTER MUTE " : "MASTER ");
    append_uint(buffer, &length, capacity, status->volume);
    append_char(buffer, &length, capacity, '%');
}

static void make_pcm_label(char *buffer, uint32_t capacity,
                           const struct audio_status *status) {
    uint32_t length = 0;
    append_text(buffer, &length, capacity, "PCM ");
    append_text(buffer, &length, capacity,
                status->pcm_ready != 0 ? "READY" : "NOT READY");
}

static void make_route_label(char *buffer, uint32_t capacity,
                             const struct audio_status *status) {
    uint32_t length = 0;
    append_text(buffer, &length, capacity, "ROUTE ");
    append_uint(buffer, &length, capacity, status->selected_output_device + 1);
    append_char(buffer, &length, capacity, '/');
    append_uint(buffer, &length, capacity, status->output_device_count);
    if (status->backend == AUDIO_BACKEND_HDA) {
        append_text(buffer, &length, capacity, " C");
        append_uint(buffer, &length, capacity, status->hda_codec);
        append_text(buffer, &length, capacity, " D");
        append_uint(buffer, &length, capacity, status->hda_dac_node);
        append_text(buffer, &length, capacity, " P");
        append_uint(buffer, &length, capacity, status->hda_pin_node);
    } else {
        append_text(buffer, &length, capacity, " LEGACY");
    }
}

void audio_overlay_draw(void) {
    struct audio_status status = {0};
    char master[24] = {0};
    char pcm[18] = {0};
    char route[40] = {0};

    if (!userspace_audio_get_status(&status)) {
        status.muted = 1;
    }

    make_master_label(master, sizeof(master), &status);
    make_pcm_label(pcm, sizeof(pcm), &status);
    make_route_label(route, sizeof(route), &status);

    display_draw_rect(AUDIO_OVERLAY_X, AUDIO_OVERLAY_Y,
                      AUDIO_OVERLAY_W, AUDIO_OVERLAY_H,
                      AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 6, AUDIO_OVERLAY_Y + 5,
                         "AUDIO DEBUG", AUDIO_OVERLAY_ACCENT,
                         AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 6, AUDIO_OVERLAY_Y + 20,
                         master,
                         status.muted != 0 ? AUDIO_OVERLAY_BAD : AUDIO_OVERLAY_TEXT,
                         AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 180, AUDIO_OVERLAY_Y + 20,
                         "BACKEND", AUDIO_OVERLAY_TEXT, AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 244, AUDIO_OVERLAY_Y + 20,
                         backend_name(&status), backend_color(&status),
                         AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 6, AUDIO_OVERLAY_Y + 38,
                         pcm,
                         status.pcm_ready != 0 ? AUDIO_OVERLAY_GOOD : AUDIO_OVERLAY_WARN,
                         AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 180, AUDIO_OVERLAY_Y + 38,
                         "TEST", AUDIO_OVERLAY_TEXT, AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 228, AUDIO_OVERLAY_Y + 38,
                         status.test_active != 0 ? "ACTIVE" : "IDLE",
                         status.test_active != 0 ? AUDIO_OVERLAY_GOOD : AUDIO_OVERLAY_TEXT,
                         AUDIO_OVERLAY_BG);
    display_draw_text_at(AUDIO_OVERLAY_X + 6, AUDIO_OVERLAY_Y + 56,
                         route,
                         status.pcm_ready != 0 ? AUDIO_OVERLAY_GOOD : AUDIO_OVERLAY_WARN,
                         AUDIO_OVERLAY_BG);
}
