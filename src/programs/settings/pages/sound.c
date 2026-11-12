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

static void refresh(struct settings_app *app, const struct pg_ui *u)
{
    uint64_t now = settings_now();
    if (!app->refresh && now - st.last < 1000)
        return;
    st.last = now;
    st.have = pc_audio_get_status(&st.status);
    if (st.have && u->active != pgu_id("vol", 0)) {
        app->model.volume = (int)st.status.volume;
        app->model.muted = st.status.muted ? 1 : 0;
    }
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
        return PGI_VOLUME_OFF;
    if (v < 34)
        return PGI_VOLUME_LOW;
    if (v < 67)
        return PGI_VOLUME_MEDIUM;
    return PGI_VOLUME_HIGH;
}

int page_sound(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct pg_ui *u = p->u;
    const struct pg_palette *pal = &u->pal;
    refresh(app, u);
    int y = p->y;

    int h = 176;
    pgu_card(u, p->x, y, p->w, h);
    bool muted = app->model.muted != 0;
    uint32_t col = muted ? pal->text3 : pal->accent;
    pgc_rrect(u->c, p->x + 24, y + 24, 64, 64, 18, col, pal->light ? 36 : 40);
    pgc_icon(u->c, volume_icon(app->model.volume, muted), PG_ICON_M, p->x + 42, y + 42, col);
    pgc_text(u->c, PGF_HEAD, p->x + 108, y + 26, "Output volume", pal->text);
    pgc_text(u->c, PGF_SMALL, p->x + 108, y + 48,
            muted ? "Muted - all sound is silenced" : "Applied instantly and remembered", pal->text3);
    char pct[8];
    struct fmt f;
    fmt_init(&f, pct, sizeof(pct));
    fu(&f, (uint64_t)app->model.volume);
    fc(&f, '%');
    pgc_text(u->c, PGF_HERO, p->x + p->w - 24 - pgc_text_w(PGF_HERO, pct), y + 26, pct, muted ? pal->text3 : pal->text);
    int sx = p->x + 108, sw = p->w - 108 - 30;
    int r = pgu_slider(u, pgu_id("vol", 0), sx, y + 84, sw, &app->model.volume, 0, 100, col);
    if (r) {
        if (app->model.muted)
            app->model.muted = 0;
        commit(app, r == 2);
    }
    pgu_separator(u, p->x + 24, y + 126, p->w - 48);
    pgc_text(u->c, PGF_MEDIUM, p->x + 24, y + 140, "Mute", pal->text);
    bool m = muted;
    if (pgu_toggle(u, pgu_id("mute", 0), p->x + p->w - 70, y + 136, &m)) {
        app->model.muted = m ? 1 : 0;
        commit(app, true);
        settings_toast(app, m ? "Sound muted" : "Sound on", m ? pal->warning : pal->success);
    }
    y += h + 16;

    int count = st.have ? (int)st.status.output_device_count : 0;
    int row_h = 56;
    int dh = 84 + (count ? count * row_h : 60) + 8;
    pgu_card(u, p->x, y, p->w, dh);
    const char *backend = st.have ? pa_backend_name(st.status.backend) : "No audio device";
    pgu_card_title(u, p->x + 20, y + 20, PGI_SPEAKER, "Output device", backend);
    int yy = y + 76;
    pgu_separator(u, p->x + 20, yy, p->w - 40);
    yy += 8;
    if (!count) {
        pgc_text(u->c, PGF_BODY, p->x + 24, yy + 18, "No audio outputs available.", pal->text3);
    }
    for (int i = 0; i < count; i++) {
        bool sel = (uint32_t)i == st.status.selected_output_device;
        if (pgu_row(u, pgu_id("out", i), p->x + 10, yy, p->w - 20, row_h - 6, sel) && !sel) {
            app->model.device = i;
            commit(app, true);
        }
        pgu_radio(u, p->x + 28, yy + (row_h - 6 - 18) / 2, sel);
        char name[32];
        fmt_init(&f, name, sizeof(name));
        fs(&f, "Output ");
        fu(&f, (uint64_t)i + 1);
        pgc_text(u->c, PGF_MEDIUM, p->x + 62, yy + 8, name, pal->text);
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
        pgc_text(u->c, PGF_SMALL, p->x + 62, yy + 28, info, pal->text3);
        if (sel)
            pgu_badge(u, p->x + p->w - 110, yy + 14, "Active", pal->accent);
        yy += row_h;
    }
    y += dh + 16;

    int th = 156;
    pgu_card(u, p->x, y, p->w, th);
    pgu_card_title(u, p->x + 20, y + 20, PGI_MUSIC_NOTE, "Sound check", "Make sure your speakers work");
    if (pgu_button(u, pgu_id("test", 0), p->x + p->w - 180, y + 22, 156, 38, "Play test sound",
                  PGI_PLAY_CIRCLE_OUTLINE, PGU_BTN_PRIMARY)) {
        pa_play_test_sound();
        settings_toast(app, "Playing test sound", pal->accent);
    }
    static const char *const tones[] = {"220 Hz", "440 Hz", "880 Hz", "1760 Hz"};
    static const uint32_t hz[] = {220, 440, 880, 1760};
    pgc_text(u->c, PGF_SMALL, p->x + 24, y + 84, "Tone generator", pal->text3);
    int sel = st.tone;
    if (pgu_segmented(u, pgu_id("tone", 0), p->x + 24, y + 104, p->w - 48 - 132, 38, tones, 4, &sel))
        st.tone = sel;
    if (pgu_button(u, pgu_id("tone-play", 0), p->x + p->w - 24 - 120, y + 104, 120, 38, "Play", PGI_MUSIC_NOTE,
                  PGU_BTN_SECONDARY))
        (void)pc_audio_play_tone(hz[st.tone], 400);
    y += th;
    return y - p->y;
}
