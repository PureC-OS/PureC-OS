#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"

struct tz_entry {
    const char *name;
    const char *city;
    int offset;
};

static const struct tz_entry zones[] = {
    {"UTC", "UTC", 0},
    {"Pacific/Honolulu", "Honolulu", -600},
    {"America/Los_Angeles", "Los Angeles", -480},
    {"America/Denver", "Denver", -420},
    {"America/Chicago", "Chicago", -360},
    {"America/New_York", "New York", -300},
    {"America/Sao_Paulo", "Sao Paulo", -180},
    {"Europe/London", "London", 0},
    {"Europe/Berlin", "Berlin", 60},
    {"Europe/Paris", "Paris", 60},
    {"Europe/Warsaw", "Warsaw", 60},
    {"Europe/Stockholm", "Stockholm", 60},
    {"Europe/Kyiv", "Kyiv", 120},
    {"Europe/Chisinau", "Chisinau", 120},
    {"Europe/Helsinki", "Helsinki", 120},
    {"Europe/Istanbul", "Istanbul", 180},
    {"Europe/Moscow", "Moscow", 180},
    {"Asia/Dubai", "Dubai", 240},
    {"Asia/Tashkent", "Tashkent", 300},
    {"Asia/Kolkata", "Kolkata", 330},
    {"Asia/Novosibirsk", "Novosibirsk", 420},
    {"Asia/Shanghai", "Shanghai", 480},
    {"Asia/Tokyo", "Tokyo", 540},
    {"Asia/Vladivostok", "Vladivostok", 600},
    {"Australia/Sydney", "Sydney", 600},
    {"Pacific/Auckland", "Auckland", 720},
};
#define ZONES ((int)(sizeof(zones) / sizeof(zones[0])))

static const char *months[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};
static const char *wdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static struct {
    char zone[48];
    uint64_t last;
} st;

static void offset_str(struct fmt *f, int minutes)
{
    fs(f, "UTC");
    fc(f, minutes < 0 ? '-' : '+');
    int m = minutes < 0 ? -minutes : minutes;
    fu2(f, (uint32_t)(m / 60));
    fc(f, ':');
    fu2(f, (uint32_t)(m % 60));
}

int page_datetime(struct page_ctx *p)
{
    struct settings_app *app = p->app;
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    int y = p->y;
    struct fmt f;
    uint64_t now = settings_now();
    if (app->refresh || now - st.last > 2000) {
        st.last = now;
        if (pc_time_zone_get(st.zone, sizeof(st.zone)) < 0)
            fmt_copy(st.zone, "UTC", sizeof(st.zone));
    }

    struct time_info t;
    bool have = pc_time_get(&t) >= 0;
    int64_t local = have ? t.unix_seconds + (int64_t)t.tz_minutes * 60 : 0;
    int yr, mo, dy, hh, mi, ss, wd;
    civil_from_unix(local, &yr, &mo, &dy, &hh, &mi, &ss, &wd);

    int h = 220;
    sg_rrect_grad(u->c, p->x, y, p->w, h, 16, sg_mix(pal->accent, 0x0c0a1d, 140),
                  sg_mix(pal->info, 0x0c0a1d, 150), true);
    char clock[16];
    fmt_init(&f, clock, sizeof(clock));
    fu2(&f, (uint32_t)hh);
    fc(&f, ':');
    fu2(&f, (uint32_t)mi);
    sg_text(u->c, SF_DIGITS, p->x + 36, y + 30, clock, 0xffffff);
    int cw = sg_text_w(SF_DIGITS, clock);
    char sec[4];
    fmt_init(&f, sec, sizeof(sec));
    fu2(&f, (uint32_t)ss);
    sg_text(u->c, SF_TITLE, p->x + 44 + cw, y + 72, sec, sg_mix(0xffffff, pal->accent, 90));
    char date[64];
    fmt_init(&f, date, sizeof(date));
    fs(&f, wdays[wd]);
    fs(&f, ", ");
    fu(&f, (uint64_t)dy);
    fc(&f, ' ');
    fs(&f, months[(mo - 1) % 12]);
    fc(&f, ' ');
    fu(&f, (uint64_t)yr);
    sg_text(u->c, SF_TITLE, p->x + 40, y + 124, date, 0xffffff);
    char zl[96];
    fmt_init(&f, zl, sizeof(zl));
    fs(&f, st.zone);
    fs(&f, "  -  ");
    offset_str(&f, have ? t.tz_minutes : 0);
    sg_text(u->c, SF_BODY, p->x + 40, y + 162, zl, sg_mix(0xffffff, pal->accent, 70));
    int ax = p->x + p->w - 130, ay = y + 110;
    sg_circle(u->c, ax, ay, 74, 0xffffff, 26);
    sg_circle(u->c, ax, ay, 66, 0xffffff, 18);
    for (int i = 0; i < 12; i++) {
        static const int dx[12] = {0, 29, 50, 58, 50, 29, 0, -29, -50, -58, -50, -29};
        static const int dyy[12] = {-58, -50, -29, 0, 29, 50, 58, 50, 29, 0, -29, -50};
        sg_circle(u->c, ax + dx[i], ay + dyy[i], i % 3 == 0 ? 3 : 2, 0xffffff, 200);
    }
    sg_ring(u->c, ax, ay, 46, 5, (uint32_t)(((hh % 12) * 60 + mi) * 1000 / 720), 0xffffff, 0x000000);
    sg_ring(u->c, ax, ay, 36, 4, (uint32_t)(mi * 1000 / 60), sg_mix(0xffffff, pal->accent, 120),
            0x000000);
    sg_circle(u->c, ax, ay, 26, sg_mix(pal->accent, 0x0c0a1d, 140), 255);
    u->animating = true;
    y += h + 16;

    int ih = 128;
    ui_card(u, p->x, y, p->w, ih);
    ui_card_title(u, p->x + 20, y + 20, SI_TIMER_OUTLINE, "System clock", 0);
    char buf[64];
    fmt_init(&f, buf, sizeof(buf));
    fdur(&f, have ? t.uptime_ms : 0);
    ui_kv(u, p->x + 24, y + 70, p->w - 48, "Uptime", buf);
    ui_separator(u, p->x + 24, y + 96, p->w - 48);
    fmt_init(&f, buf, sizeof(buf));
    fs(&f, have && t.source ? "Hardware RTC" : "Software clock");
    ui_kv(u, p->x + 24, y + 102, p->w - 48, "Time source", buf);
    y += ih + 16;

    int cols = p->w > 700 ? 3 : 2;
    int gap = 8;
    int cw2 = (p->w - 40 - gap * (cols - 1)) / cols;
    int rh = 50;
    int rows = (ZONES + cols - 1) / cols;
    int zh = 84 + rows * (rh + gap) + 12;
    ui_card(u, p->x, y, p->w, zh);
    ui_card_title(u, p->x + 20, y + 20, SI_EARTH, "Time zone", "Daylight saving is handled automatically");
    for (int i = 0; i < ZONES; i++) {
        int zx = p->x + 20 + (i % cols) * (cw2 + gap);
        int zy = y + 84 + (i / cols) * (rh + gap);
        bool sel = fmt_streq(st.zone, zones[i].name);
        if (ui_row(u, ui_id("tz", i), zx, zy, cw2, rh, sel) && !sel) {
            if (pc_time_zone_set(zones[i].name) >= 0) {
                fmt_copy(st.zone, zones[i].name, sizeof(st.zone));
                settings_toast(app, "Time zone updated", pal->success);
            } else {
                settings_toast(app, "Could not change time zone", pal->danger);
            }
        }
        sg_text(u->c, sel ? SF_MEDIUM : SF_BODY, zx + 14, zy + 7, zones[i].city, sel ? pal->text : pal->text2);
        fmt_init(&f, buf, sizeof(buf));
        offset_str(&f, zones[i].offset);
        sg_text(u->c, SF_SMALL, zx + 14, zy + 27, buf, pal->text3);
        if (sel)
            sg_icon(u->c, SI_CHECK_CIRCLE, SG_ICON_S, zx + cw2 - 30, zy + 16, pal->accent);
    }
    y += zh;
    return y - p->y;
}
