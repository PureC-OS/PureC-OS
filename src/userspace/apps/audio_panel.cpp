#include "audio_panel.h"
#include "../audio.h"
#include "../display.h"

namespace {

constexpr uint32_t PanelBg = 0x1E1E2E;
constexpr uint32_t PanelBorder = 0x45475A;
constexpr uint32_t Text = 0xCDD6F4;
constexpr uint32_t MutedText = 0x9399B2;
constexpr uint32_t Accent = 0x89B4FA;
constexpr uint32_t Warn = 0xF38BA8;
constexpr uint32_t Good = 0xA6E3A1;
constexpr uint32_t BadgeWidth = 92;
constexpr uint32_t BadgeHeight = 22;
constexpr uint32_t PopupWidth = 198;
constexpr uint32_t PopupHeight = 150;
constexpr uint32_t SliderWidth = 142;
constexpr uint8_t KeyF1 = 1;
constexpr uint8_t KeyF2 = 2;
constexpr uint8_t KeyF3 = 3;

bool popup_visible = false;
bool dragging_slider = false;

uint32_t badge_x(uint32_t screen_width) {
    return screen_width > 140 ? screen_width - 136 : 0;
}

uint32_t popup_x(uint32_t screen_width) {
    return screen_width > PopupWidth + 8 ? screen_width - PopupWidth - 8 : 0;
}

bool point_inside(
    int32_t point_x,
    int32_t point_y,
    uint32_t left,
    uint32_t top,
    uint32_t width,
    uint32_t height
) {
    return point_x >= static_cast<int32_t>(left)
        && point_y >= static_cast<int32_t>(top)
        && point_x < static_cast<int32_t>(left + width)
        && point_y < static_cast<int32_t>(top + height);
}

void append_char(char *buffer, uint32_t *length, uint32_t capacity, char value) {
    if (*length + 1 >= capacity) {
        return;
    }

    buffer[*length] = value;
    *length += 1;
    buffer[*length] = '\0';
}

void append_text(char *buffer, uint32_t *length, uint32_t capacity, const char *text) {
    while (*text != '\0') {
        append_char(buffer, length, capacity, *text);
        text++;
    }
}

void append_uint(char *buffer, uint32_t *length, uint32_t capacity, uint32_t value) {
    char reversed[10];
    uint32_t count = 0;

    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));

    while (count > 0) {
        append_char(buffer, length, capacity, reversed[--count]);
    }
}

void make_volume_label(char *buffer, uint32_t capacity, const struct audio_status &status) {
    uint32_t length = 0;

    append_text(buffer, &length, capacity, status.muted != 0 ? "Mute " : "Vol ");
    append_uint(buffer, &length, capacity, status.volume);
    append_char(buffer, &length, capacity, '%');
}

void make_device_label(char *buffer, uint32_t capacity,
                       const struct audio_status &status) {
    uint32_t length = 0;
    append_text(buffer, &length, capacity, "Device ");
    append_uint(buffer, &length, capacity, status.selected_output_device + 1);
    append_char(buffer, &length, capacity, '/');
    append_uint(buffer, &length, capacity, status.output_device_count);
    if (status.backend == AUDIO_BACKEND_HDA) {
        append_text(buffer, &length, capacity, " D");
        append_uint(buffer, &length, capacity, status.hda_dac_node);
        append_text(buffer, &length, capacity, " P");
        append_uint(buffer, &length, capacity, status.hda_pin_node);
    } else {
        append_text(buffer, &length, capacity, " Legacy");
    }
}

const char *backend_label(const struct audio_status &status) {
    if ((status.backend & AUDIO_BACKEND_HDA) != 0) {
        return status.pcm_ready != 0 ? "HDA PCM" : "HDA found";
    }
    if ((status.backend & AUDIO_BACKEND_PC_SPEAKER) != 0) {
        return "Legacy";
    }
    return "No audio";
}

void draw_slider(uint32_t x, uint32_t y, uint8_t volume, bool muted) {
    uint32_t filled = (SliderWidth * volume) / 100;
    uint32_t knob_x = filled >= SliderWidth ? SliderWidth - 6 : filled;

    display_draw_rect(x, y, SliderWidth, 8, 0x585B70);
    if (filled > 0) {
        display_draw_rect(x, y, filled, 8, muted ? MutedText : Accent);
    }
    display_draw_rect(x + knob_x, y - 4, 6, 16, muted ? MutedText : Good);
}

void set_volume_from_x(int32_t point_x, uint32_t screen_width) {
    uint32_t left = popup_x(screen_width) + 28;
    int32_t relative = point_x - static_cast<int32_t>(left);

    if (relative < 0) {
        relative = 0;
    }
    if (relative > static_cast<int32_t>(SliderWidth)) {
        relative = static_cast<int32_t>(SliderWidth);
    }

    userspace_audio_set_volume(static_cast<uint8_t>((relative * 100) / SliderWidth));
}

} // namespace

extern "C" void audio_panel_init(void) {
    popup_visible = false;
    dragging_slider = false;
}

extern "C" void audio_panel_draw(uint32_t screen_width) {
    struct audio_status status {};
    char label[18] {};
    char device_label[28] {};
    uint32_t bx = badge_x(screen_width);

    if (!userspace_audio_get_status(&status)) {
        status.volume = 0;
        status.muted = 1;
        status.output_device_count = 1;
    }

    make_volume_label(label, sizeof(label), status);
    make_device_label(device_label, sizeof(device_label), status);
    display_draw_rect(bx, 3, BadgeWidth, BadgeHeight, popup_visible ? 0x585B70 : PanelBorder);
    display_draw_text_at(bx + 8, 9, label, status.muted != 0 ? MutedText : Text, popup_visible ? 0x585B70 : PanelBorder);

    if (!popup_visible) {
        return;
    }

    uint32_t px = popup_x(screen_width);
    display_draw_rect(px, 28, PopupWidth, PopupHeight, PanelBorder);
    display_draw_rect(px + 2, 30, PopupWidth - 4, PopupHeight - 4, PanelBg);
    display_draw_text_at(px + 12, 42, "Sound", Text, PanelBg);
    display_draw_text_at(px + 122, 42, label, status.muted != 0 ? MutedText : Good, PanelBg);
    display_draw_text_at(px + 12, 58, backend_label(status), status.pcm_ready != 0 ? Good : MutedText, PanelBg);
    draw_slider(px + 28, 74, status.volume, status.muted != 0);
    display_draw_rect(px + 12, 98, 172, 26, PanelBorder);
    display_draw_text_at(px + 20, 107, device_label, Text, PanelBorder);
    display_draw_rect(px + 12, 132, 82, 26, status.muted != 0 ? Warn : PanelBorder);
    display_draw_text_at(px + 24, 141, status.muted != 0 ? "Unmute" : "Mute", Text, status.muted != 0 ? Warn : PanelBorder);
    display_draw_rect(px + 104, 132, 80, 26, Accent);
    display_draw_text_at(px + 116, 141, "Test", 0x1E1E2E, Accent);
}

extern "C" bool audio_panel_handle_mouse(
    int32_t x,
    int32_t y,
    uint8_t buttons,
    bool pressed,
    bool released,
    uint32_t screen_width,
    bool *redraw_required
) {
    uint32_t bx = badge_x(screen_width);
    uint32_t px = popup_x(screen_width);
    bool captured = popup_visible && point_inside(x, y, px, 28, PopupWidth, PopupHeight);

    if (redraw_required != nullptr) {
        *redraw_required = false;
    }

    if (pressed && point_inside(x, y, bx, 3, BadgeWidth, BadgeHeight)) {
        popup_visible = !popup_visible;
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (popup_visible && pressed && point_inside(x, y, px + 28, 66, SliderWidth, 26)) {
        dragging_slider = true;
        set_volume_from_x(x, screen_width);
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (dragging_slider && (buttons & 1U) != 0) {
        set_volume_from_x(x, screen_width);
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (released && dragging_slider) {
        dragging_slider = false;
        return true;
    }

    if (popup_visible && pressed && point_inside(x, y, px + 12, 98, 172, 26)) {
        uint32_t count = 0;
        uint32_t selected = 0;
        struct audio_status status {};
        if (userspace_audio_get_status(&status)) {
            count = status.output_device_count;
            selected = status.selected_output_device;
        }
        if (count > 0) {
            (void)userspace_audio_select_output_device((selected + 1) % count);
        }
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (popup_visible && pressed && point_inside(x, y, px + 12, 132, 82, 26)) {
        userspace_audio_toggle_mute();
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (popup_visible && pressed && point_inside(x, y, px + 104, 132, 80, 26)) {
        userspace_audio_play_test_sound();
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return true;
    }

    if (popup_visible && pressed && !captured) {
        popup_visible = false;
        if (redraw_required != nullptr) {
            *redraw_required = true;
        }
        return false;
    }

    return captured || dragging_slider;
}

extern "C" bool audio_panel_handle_special_key(uint8_t key) {
    if (key == KeyF1) {
        userspace_audio_toggle_mute();
        return true;
    }
    if (key == KeyF2) {
        userspace_audio_adjust_volume(-8);
        return true;
    }
    if (key == KeyF3) {
        userspace_audio_adjust_volume(8);
        return true;
    }

    return false;
}

extern "C" bool audio_panel_is_popup_visible(void) {
    return popup_visible;
}
