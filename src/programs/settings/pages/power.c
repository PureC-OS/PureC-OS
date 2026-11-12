#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"

static struct {
    struct battery_info bat;
    struct thermal_info th;
    bool have_th;
    bool have;
    uint64_t last;
} st;

static void refresh(struct settings_app *app)
{
    uint64_t now = settings_now();
    if (!app->refresh && now - st.last < 2000)
        return;
    st.last = now;
    for (unsigned i = 0; i < sizeof(st.bat); i++)
        ((char *)&st.bat)[i] = 0;
    st.have = pc_syscall(SYS_BATTERY_INFO, (uint64_t)(uintptr_t)&st.bat, 0, 0) >= 0 && st.bat.present;
    for (unsigned i = 0; i < sizeof(st.th); i++)
        ((char *)&st.th)[i] = 0;
    st.have_th = pc_syscall(SYS_THERMAL_INFO, (uint64_t)(uintptr_t)&st.th, 0, 0) >= 0 && st.th.available;
}

static void battery_shape(struct pg_ui *u, int x, int y, uint32_t pct, bool charging, uint32_t col)
{
    const struct pg_palette *pal = &u->pal;
    int w = 120, h = 58;
    pgc_rrect(u->c, x, y, w, h, 12, pal->text3, 255);
    pgc_rrect(u->c, x + 3, y + 3, w - 6, h - 6, 10, pal->surface, 255);
    pgc_rrect(u->c, x + w + 2, y + 18, 8, 22, 3, pal->text3, 255);
    int fill = (int)((w - 12) * (pct > 100 ? 100 : pct) / 100);
    if (fill > 0)
        pgc_rrect_grad(u->c, x + 6, y + 6, fill < 14 ? 14 : fill, h - 12, 8, pgc_mix(col, pal->surface, 60), col,
                      true);
    if (charging)
        pgc_icon(u->c, PGI_FLASH, PG_ICON_M, x + (w - 28) / 2, y + (h - 28) / 2, 0xffffff);
}

int page_power(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct pg_ui *u = p->u;
    const struct pg_palette *pal = &u->pal;
    refresh(app);
    int y = p->y;
    struct fmt f;
    char a[64], b[96];

    int h = 172;
    pgu_card(u, p->x, y, p->w, h);
    if (st.have) {
        uint32_t pct = st.bat.percent == 0xffffffffu ? 0 : st.bat.percent;
        uint32_t col = pct <= 15 ? pal->danger : pct <= 35 ? pal->warning : pal->success;
        battery_shape(u, p->x + 32, y + 56, pct, st.bat.charging != 0, col);
        fmt_init(&f, a, sizeof(a));
        if (st.bat.percent == 0xffffffffu)
            fs(&f, "--");
        else
            fu(&f, pct);
        fc(&f, '%');
        pgc_text(u->c, PGF_HERO, p->x + 190, y + 34, a, pal->text);
        fmt_init(&f, b, sizeof(b));
        if (st.bat.charging)
            fs(&f, "Charging");
        else if (st.bat.status_text[0])
            fs(&f, st.bat.status_text);
        else
            fs(&f, "On battery");
        if (st.bat.remaining_minutes && st.bat.remaining_minutes != 0xffffffffu) {
            fs(&f, "  -  ");
            if (st.bat.remaining_minutes >= 60) {
                fu(&f, st.bat.remaining_minutes / 60);
                fs(&f, " h ");
            }
            fu(&f, st.bat.remaining_minutes % 60);
            fs(&f, st.bat.charging ? " min until full" : " min left");
        }
        pgc_text(u->c, PGF_BODY, p->x + 192, y + 82, b, pal->text2);
        fmt_init(&f, b, sizeof(b));
        fs(&f, st.bat.name[0] ? st.bat.name : "Battery");
        if (st.bat.voltage_mv) {
            fs(&f, "  -  ");
            fdec(&f, st.bat.voltage_mv, 1000, 2);
            fs(&f, " V");
        }
        if (st.bat.current_ma) {
            fs(&f, "  -  ");
            fu(&f, st.bat.current_ma);
            fs(&f, " mA");
        }
        pgc_text(u->c, PGF_SMALL, p->x + 192, y + 108, b, pal->text3);
        pgu_badge(u, p->x + 192, y + 130, st.bat.charging ? "Plugged in" : "Battery power",
                 st.bat.charging ? pal->success : pal->info);
    } else {
        pgu_icon_tile(u, p->x + 32, y + 50, 64, PGI_FLASH, pal->info);
        pgc_text(u->c, PGF_TITLE, p->x + 120, y + 52, "Plugged in", pal->text);
        pgc_text(u->c, PGF_BODY, p->x + 120, y + 88, "No battery detected - this computer runs on AC power.",
                pal->text3);
    }
    y += h + 16;

    int bh = 150;
    int gap = 16;
    int bw = (p->w - gap) / 2;
    const struct {
        int icon;
        const char *title;
        const char *text;
        int confirm;
        uint32_t color;
    } acts[2] = {
        {PGI_RESTART, "Restart", "Close everything and boot again", CONFIRM_REBOOT, pal->accent},
        {PGI_POWER, "Shut down", "Turn the computer off completely", CONFIRM_SHUTDOWN, pal->danger},
    };
    for (int i = 0; i < 2; i++) {
        int x = p->x + i * (bw + gap);
        uint32_t id = pgu_id("power", i);
        bool clicked = pgu_click(u, id, x, y, bw, bh);
        bool hot = u->hot == id;
        pgu_card(u, x, y, bw, bh);
        if (hot) {
            pgc_rrect(u->c, x, y, bw, bh, 12, acts[i].color, pal->light ? 18 : 22);
            pgc_rrect_stroke(u->c, x, y, bw, bh, 12, acts[i].color, 200);
        }
        pgc_rrect(u->c, x + 24, y + 24, 56, 56, 16, acts[i].color, pal->light ? 36 : 44);
        pgc_icon(u->c, acts[i].icon, PG_ICON_M, x + 38, y + 38, acts[i].color);
        pgc_text(u->c, PGF_HEAD, x + 24, y + 96, acts[i].title, pal->text);
        pgc_text_fit(u->c, PGF_SMALL, x + 24, y + 118, bw - 48, acts[i].text, pal->text3);
        pgc_icon(u->c, PGI_CHEVRON_RIGHT, PG_ICON_S, x + bw - 40, y + 43, pal->text3);
        if (clicked)
            app->confirm = acts[i].confirm;
    }
    y += bh + 16;

    int th = 128;
    pgu_card(u, p->x, y, p->w, th);
    pgu_card_title(u, p->x + 20, y + 20, PGI_THERMOMETER, "Thermals",
                  st.have_th ? "Reported by ACPI thermal zones" : "No thermal sensors reported");
    if (st.have_th) {
        int t10 = st.th.temperature_deci_c;
        fmt_init(&f, a, sizeof(a));
        fi(&f, t10 / 10);
        fc(&f, '.');
        fu(&f, (uint64_t)((t10 < 0 ? -t10 : t10) % 10));
        fs(&f, " C");
        uint32_t tc = t10 >= 850 ? pal->danger : t10 >= 700 ? pal->warning : pal->success;
        pgc_text(u->c, PGF_TITLE, p->x + 24, y + 78, a, tc);
        fmt_init(&f, b, sizeof(b));
        fs(&f, st.th.thermal_zone[0] ? st.th.thermal_zone : "CPU");
        pgc_text(u->c, PGF_SMALL, p->x + 34 + pgc_text_w(PGF_TITLE, a), y + 88, b, pal->text3);
        if (st.th.fan_count) {
            fmt_init(&f, b, sizeof(b));
            fu(&f, st.th.fan_rpm);
            fs(&f, " RPM");
            int fx = p->x + p->w / 2;
            pgc_icon(u->c, PGI_FAN, PG_ICON_S, fx, y + 82, pal->info);
            pgc_text(u->c, PGF_TITLE, fx + 26, y + 78, b, pal->text);
        }
    } else {
        pgc_text(u->c, PGF_BODY, p->x + 24, y + 80, "Temperature readings are not available on this machine.",
                pal->text3);
    }
    y += th;
    return y - p->y;
}
