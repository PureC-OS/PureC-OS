#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"

static struct {
    struct cpu_monitor_info cpu;
    struct memory_monitor_info mem;
    struct pc_display_info disp;
    struct pc_display_scale scale;
    char root[32];
    char fstype[16];
    bool have_cpu, have_mem, have_disp, have_scale;
    uint64_t last;
    uint8_t hist[48];
    int hist_n;
} st;

static void refresh(struct settings_app *app)
{
    uint64_t now = settings_now();
    if (!app->refresh && now - st.last < 1000)
        return;
    st.last = now;
    st.have_cpu = pc_cpu_info(&st.cpu);
    st.have_mem = pc_memory_info(&st.mem);
    st.have_disp = pc_display_get_info(&st.disp) && st.disp.available;
    st.have_scale = pc_display_scale_info(&st.scale);
    if (pc_get_root_device(st.root, sizeof(st.root)) < 0)
        fmt_copy(st.root, "unknown", sizeof(st.root));
    if (pc_get_fs_type(st.fstype, sizeof(st.fstype)) < 0)
        fmt_copy(st.fstype, "?", sizeof(st.fstype));
    if (st.have_cpu) {
        if (st.hist_n < (int)sizeof(st.hist))
            st.hist[st.hist_n++] = (uint8_t)st.cpu.usage_percent;
        else {
            for (int i = 1; i < (int)sizeof(st.hist); i++)
                st.hist[i - 1] = st.hist[i];
            st.hist[sizeof(st.hist) - 1] = (uint8_t)st.cpu.usage_percent;
        }
    }
}

static void hero(struct page_ctx *p, int y)
{
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    int h = 150;
    uint32_t c0 = sg_mix(pal->accent, 0x1e1b4b, 120);
    uint32_t c1 = sg_mix(pal->accent, 0x0f766e, 150);
    sg_rrect_grad(u->c, p->x, y, p->w, h, 16, c0, c1, true);
    struct sg_clip saved = sg_clip_push(u->c, p->x + 16, y, p->w - 16, h);
    for (int i = 0; i < 5; i++)
        sg_circle(u->c, p->x + p->w - 70 - i * 26, y + h - 10 - i * 8, 120 - i * 18, 0xffffff, 9);
    sg_clip_pop(u->c, saved);
    sg_rrect(u->c, p->x + 28, y + 35, 80, 80, 22, 0xffffff, 36);
    sg_text_center(u->c, SF_HERO, p->x + 28, y + 51, 80, "P", 0xffffff);
    sg_text(u->c, SF_HERO, p->x + 132, y + 32, "PureC OS", 0xffffff);
    sg_text(u->c, SF_BODY, p->x + 134, y + 78, "Version 1.2.29  -  x86_64", sg_mix(0xffffff, c1, 60));
    sg_text(u->c, SF_SMALL, p->x + 134, y + 102, "A 64-bit operating system written from scratch in C",
            sg_mix(0xffffff, c1, 90));
}

static void stat_tile(struct page_ctx *p, int x, int y, int w, int h, int icon, uint32_t color,
                      const char *label, const char *value, const char *sub, uint32_t permille,
                      bool ring)
{
    struct ui *u = p->u;
    ui_card(u, x, y, w, h);
    ui_icon_tile(u, x + 18, y + 18, 36, icon, color);
    sg_text(u->c, SF_SMALL, x + 66, y + 18, label, u->pal.text3);
    sg_text_fit(u->c, SF_HEAD, x + 66, y + 34, w - (ring ? 150 : 84), value, u->pal.text);
    if (sub)
        sg_text_fit(u->c, SF_SMALL, x + 18, y + h - 30, w - (ring ? 110 : 36), sub, u->pal.text3);
    if (ring) {
        int r = 34;
        sg_ring(u->c, x + w - r - 22, y + h / 2, r, 8, permille, color, u->pal.surface3);
        char pct[8];
        struct fmt f;
        fmt_init(&f, pct, sizeof(pct));
        fu(&f, permille / 10);
        fc(&f, '%');
        sg_text_center(u->c, SF_MEDIUM, x + w - 2 * r - 22, y + h / 2 - 10, 2 * r, pct, u->pal.text);
    }
}

static void cpu_graph(struct page_ctx *p, int x, int y, int w, int h)
{
    struct ui *u = p->u;
    ui_card(u, x, y, w, h);
    ui_card_title(u, x + 20, y + 18, SI_CHIP, "Processor load", "Last 48 seconds");
    char cur[16];
    struct fmt f;
    fmt_init(&f, cur, sizeof(cur));
    fu(&f, st.have_cpu ? st.cpu.usage_percent : 0);
    fc(&f, '%');
    sg_text(u->c, SF_TITLE, x + w - 20 - sg_text_w(SF_TITLE, cur), y + 18, cur, u->pal.accent);
    int gx = x + 20, gy = y + 72, gw = w - 40, gh = h - 92;
    for (int i = 0; i <= 4; i++)
        sg_fill_a(u->c, gx, gy + gh * i / 4, gw, 1, u->pal.border, 90);
    int n = (int)sizeof(st.hist);
    int bw = gw / n;
    for (int i = 0; i < st.hist_n; i++) {
        int v = st.hist[i];
        int bh = gh * v / 100;
        if (bh < 2)
            bh = 2;
        int bx = gx + (n - st.hist_n + i) * bw;
        uint32_t col = sg_mix(u->pal.accent, u->pal.info, (uint32_t)(i * 255 / n));
        sg_rrect(u->c, bx + 1, gy + gh - bh, bw - 2, bh, 2, col, 255);
    }
}

int page_about(struct page_ctx *p)
{
    struct ui *u = p->u;
    refresh(p->app);
    int y = p->y;
    hero(p, y);
    y += 170;

    int gap = 16;
    int cols = p->w > 640 ? 3 : 2;
    int tw = (p->w - gap * (cols - 1)) / cols;
    int th = 118;
    char v1[64], s1[64], v2[48], s2[64], v3[48], s3[64];
    struct fmt f;

    fmt_init(&f, v1, sizeof(v1));
    if (st.have_cpu) {
        fu(&f, st.cpu.logical_processors);
        fs(&f, st.cpu.logical_processors == 1 ? " core" : " cores");
        if (st.cpu.frequency_hz) {
            fs(&f, " @ ");
            fdec(&f, st.cpu.frequency_hz / 10000000, 100, 2);
            fs(&f, " GHz");
        }
    } else {
        fs(&f, "Unknown");
    }
    fmt_init(&f, s1, sizeof(s1));
    fs(&f, st.have_cpu && st.cpu.name[0] ? st.cpu.name : "x86_64 processor");

    uint32_t mem_pm = 0;
    fmt_init(&f, v2, sizeof(v2));
    fmt_init(&f, s2, sizeof(s2));
    if (st.have_mem && st.mem.total_bytes) {
        struct fmt g;
        fmt_init(&g, v2, sizeof(v2));
        fbytes(&g, st.mem.total_bytes);
        fmt_init(&g, s2, sizeof(s2));
        fbytes(&g, st.mem.used_bytes);
        fs(&g, " used, ");
        fbytes(&g, st.mem.available_bytes);
        fs(&g, " free");
        mem_pm = (uint32_t)(st.mem.used_bytes * 1000 / st.mem.total_bytes);
    } else {
        fs(&f, "Unknown");
    }

    fmt_init(&f, v3, sizeof(v3));
    fdur(&f, st.have_cpu ? st.cpu.uptime_ms : 0);
    fmt_init(&f, s3, sizeof(s3));
    fs(&f, "Since last boot");

    stat_tile(p, p->x, y, cols == 3 ? tw : tw, th, SI_CPU_64_BIT, u->pal.accent, "Processor", v1, s1, 0,
              false);
    stat_tile(p, p->x + tw + gap, y, tw, th, SI_MEMORY, u->pal.info, "Memory", v2, s2, mem_pm, true);
    if (cols == 3)
        stat_tile(p, p->x + 2 * (tw + gap), y, tw, th, SI_TIMER_OUTLINE, u->pal.success, "Uptime", v3, s3,
                  0, false);
    y += th + gap;
    if (cols == 2) {
        stat_tile(p, p->x, y, tw, th, SI_TIMER_OUTLINE, u->pal.success, "Uptime", v3, s3, 0, false);
        y += th + gap;
    }

    cpu_graph(p, p->x, y, p->w, 200);
    y += 200 + gap;

    int dh = 248;
    ui_card(u, p->x, y, p->w, dh);
    ui_card_title(u, p->x + 20, y + 18, SI_INFORMATION_OUTLINE, "Device specifications", 0);
    int ky = y + 72, kx = p->x + 24, kw = p->w - 48;
    char buf[96];
    fmt_init(&f, buf, sizeof(buf));
    fs(&f, "PureC OS 1.2.29 (x86_64)");
    ui_kv(u, kx, ky, kw, "Operating system", buf);
    ui_separator(u, kx, ky + 28, kw);
    ky += 38;
    ui_kv(u, kx, ky, kw, "Processor", s1);
    ui_separator(u, kx, ky + 28, kw);
    ky += 38;
    fmt_init(&f, buf, sizeof(buf));
    if (st.have_mem)
        fbytes(&f, st.mem.total_bytes);
    else
        fs(&f, "-");
    fs(&f, " RAM");
    ui_kv(u, kx, ky, kw, "Installed memory", buf);
    ui_separator(u, kx, ky + 28, kw);
    ky += 38;
    fmt_init(&f, buf, sizeof(buf));
    if (st.have_scale) {
        fu(&f, st.scale.physical_width);
        fs(&f, " x ");
        fu(&f, st.scale.physical_height);
        if (st.have_disp) {
            fs(&f, ", ");
            fu(&f, st.disp.bpp);
            fs(&f, "-bit");
        }
        fs(&f, ", scale ");
        fu(&f, st.scale.scale_percent);
        fc(&f, '%');
    } else {
        fs(&f, "-");
    }
    ui_kv(u, kx, ky, kw, "Display", buf);
    ui_separator(u, kx, ky + 28, kw);
    ky += 38;
    fmt_init(&f, buf, sizeof(buf));
    fs(&f, st.root);
    fs(&f, " (");
    fs(&f, st.fstype);
    fc(&f, ')');
    ui_kv(u, kx, ky, kw, "System drive", buf);
    y += dh;
    return y - p->y;
}
