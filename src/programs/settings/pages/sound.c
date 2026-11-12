#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"

static struct {
    struct audio_status status;
    bool have;
    uint64_t last;
    int tone;
} st;

static void refresh(struct settings_app *app)
{
    uint64_t now = settings_now();
    if (!app->refresh && now - st.last < 1000)
        return;
    st.last = now;
    st.have = pc_audio_get_status(&st.status);
    if (st.have && !app->model.muted && (int)st.status.volume != app->model.volume &&
        p_unused_drag_guard(app) == 0)
        app->model.volume = (int)st.status.volume;
}

static void commit(struct settings_app *app, bool save)
{
    (void)settings_model_apply(&app->model);
    if (save)
        (void)settings_model_save(&app->model);
    st.have = pc_audio_get_status(&st.status);
}

static int volume_icon(int v, bool muted)
{
    if (muted || v == 0)
        return SI_VOLUME_OFF;
    if (v < 34)
        return SI_VOLUME_LOW;
    if (v < 67)
        return SI_VOLUME_MEDIUM;
    return SI_VOLUME_HIGH;
}

int page_sound(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    refresh(app);
    int y = p->y;

    int h = 176;
    ui_card(u, p->x, y, p->w, h);
    bool muted = app->model.muted != 0;
    uint32_t col = muted ? pal->text3 : pal->accent;
    sg_rrect(u->c, p->x + 24, y + 24, 64, 64, 18, col, pal->light ? 36 : 40);
    sg_icon(u->c, volume_icon(app->model.volume, muted), SG_ICON_M, p->x + 42, y + 42, col);
    sg_text(u->c, SF_HEAD, p->x + 108, y + 26, "Output volume", pal->text);
    sg_text(u->c, SF_SMALL, p->x + 108, y + 48,
            muted ? "Muted - all sound is silenced" : "Applied instantly and remembered", pal->text3);
    char pct[8];
    struct fmt f;
    fmt_init(&f, pct, sizeof(pct));
    fu(&f, (uint64_t)app->model.volume);
    fc(&f, '%');
    sg_text(u->c, SF_HERO, p->x + p->w - 24 - sg_text_w(SF_HERO, pct), y + 26, pct, muted ? pal->text3 : pal->text);
    int sx = p->x + 108, sw = p->w - 108 - 30;
    int r = ui_slider(u, ui_id("vol", 0), sx, y + 84, sw, &app->model.volume, 0, 100, col);
    if (r) {
        if (app->model.muted)
            app->model.muted = 0;
        commit(app, r == 2);
    }
    ui_separator(u, p->x + 24, y + 126, p->w - 48);
    sg_text(u->c, SF_MEDIUM, p->x + 24, y + 140, "Mute", pal->text);
    bool m = muted;
    if (ui_toggle(u, ui_id("mute", 0), p->x + p->w - 70, y + 136, &m)) {
        app->model.muted = m ? 1 : 0;
        commit(app, true);
        settings_toast(app, m ? "Sound muted" : "Sound on", m ? pal->warning : pal->success);
    }
    y += h + 16;

    int count = st.have ? (int)st.status.output_device_count : 0;
    int row_h = 56;
    int dh = 84 + (count ? count * row_h : 60) + 8;
    ui_card(u, p->x, y, p->w, dh);
    const char *backend = st.have ? pa_backend_name(st.status.backend) : "No audio device";
    ui_card_title(u, p->x + 20, y + 20, SI_SPEAKER, "Output device", backend);
    int yy = y + 76;
    ui_separator(u, p->x + 20, yy, p->w - 40);
    yy += 8;
    if (!count) {
        sg_text(u->c, SF_BODY, p->x + 24, yy + 18, "No audio outputs available.", pal->text3);
    }
    for (int i = 0; i < count; i++) {
        bool sel = (uint32_t)i == st.status.selected_output_device;
        if (ui_row(u, ui_id("out", i), p->x + 10, yy, p->w - 20, row_h - 6, sel) && !sel) {
            app->model.device = i;
            commit(app, true);
        }
        ui_radio(u, p->x + 28, yy + (row_h - 6 - 18) / 2, sel);
        char name[32];
        fmt_init(&f, name, sizeof(name));
        fs(&f, "Output ");
        fu(&f, (uint64_t)i + 1);
        sg_text(u->c, SF_MEDIUM, p->x + 62, yy + 8, name, pal->text);
        char info[64];
        fmt_init(&f, info, sizeof(info));
        if (st.status.backend == 2 && sel) {
            fs(&f, "HD Audio codec ");
            fu(&f, st.status.hda_codec);
            fs(&f, ", DAC ");
            fu(&f, st.status.hda_dac_node);
            fs(&f, ", pin ");
            fu(&f, st.status.hda_pin_node);
        } else {
            fs(&f, backend);
        }
        sg_text(u->c, SF_SMALL, p->x + 62, yy + 28, info, pal->text3);
        if (sel)
            ui_badge(u, p->x + p->w - 110, yy + 14, "Active", pal->accent);
        yy += row_h;
    }
    y += dh + 16;

    int th = 156;
    ui_card(u, p->x, y, p->w, th);
    ui_card_title(u, p->x + 20, y + 20, SI_MUSIC_NOTE, "Sound check", "Make sure your speakers work");
    if (ui_button(u, ui_id("test", 0), p->x + p->w - 180, y + 22, 156, 38, "Play test sound",
                  SI_PLAY_CIRCLE_OUTLINE, UI_BTN_PRIMARY)) {
        pa_play_test_sound();
        settings_toast(app, "Playing test sound", pal->accent);
    }
    static const char *const tones[] = {"220 Hz", "440 Hz", "880 Hz", "1760 Hz"};
    static const uint32_t hz[] = {220, 440, 880, 1760};
    sg_text(u->c, SF_SMALL, p->x + 24, y + 84, "Tone generator", pal->text3);
    int sel = st.tone;
    if (ui_segmented(u, ui_id("tone", 0), p->x + 24, y + 104, p->w - 48 - 132, 38, tones, 4, &sel))
        st.tone = sel;
    if (ui_button(u, ui_id("tone-play", 0), p->x + p->w - 24 - 120, y + 104, 120, 38, "Play", SI_MUSIC_NOTE,
                  UI_BTN_SECONDARY))
        (void)pc_audio_play_tone(hz[st.tone], 400);
    y += th;
    return y - p->y;
}
