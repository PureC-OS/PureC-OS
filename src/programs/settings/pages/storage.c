#include "settings/app.h"
#include "settings/fmt.h"
#include "../../../libc/include/purec.h"
#include "../../../io/storage/storage_types.h"

#define DISK_MAX 8

static struct {
    struct storage_device_info disks[DISK_MAX];
    int count;
    struct ext2_super_info sb;
    bool have_sb;
    char root[32];
    char fstype[16];
    uint64_t last;
} st;

static void refresh(struct settings_app *app)
{
    uint64_t now = settings_now();
    if (!app->refresh && now - st.last < 3000)
        return;
    st.last = now;
    st.count = pc_list_disks(st.disks, DISK_MAX);
    if (st.count < 0)
        st.count = 0;
    st.have_sb = pc_ext2_super(&st.sb) >= 0 && st.sb.total_blocks;
    if (pc_get_root_device(st.root, sizeof(st.root)) < 0)
        fmt_copy(st.root, "?", sizeof(st.root));
    if (pc_get_fs_type(st.fstype, sizeof(st.fstype)) < 0)
        fmt_copy(st.fstype, "?", sizeof(st.fstype));
}

static const char *transport_name(uint8_t t)
{
    switch (t) {
    case STORAGE_TRANSPORT_ATA_PIO:
        return "IDE / ATA";
    case STORAGE_TRANSPORT_AHCI:
        return "SATA (AHCI)";
    case STORAGE_TRANSPORT_USB_MSC:
    case STORAGE_TRANSPORT_USB_EHCI:
        return "USB";
    default:
        return "Disk";
    }
}

int page_storage(struct page_ctx *p)
{
    struct ui *u = p->u;
    const struct ui_palette *pal = &u->pal;
    refresh(p->app);
    int y = p->y;
    struct fmt f;
    char a[64], b[64];

    int h = 190;
    ui_card(u, p->x, y, p->w, h);
    sg_rrect(u->c, p->x + 24, y + 24, 64, 64, 18, pal->accent, pal->light ? 36 : 40);
    sg_icon(u->c, SI_HARDDISK, SG_ICON_M, p->x + 42, y + 42, pal->accent);
    fmt_init(&f, a, sizeof(a));
    fs(&f, "System drive (");
    fs(&f, st.root);
    fc(&f, ')');
    sg_text(u->c, SF_HEAD, p->x + 108, y + 28, a, pal->text);
    fmt_init(&f, b, sizeof(b));
    fs(&f, st.fstype);
    fs(&f, " file system, mounted at /");
    sg_text(u->c, SF_SMALL, p->x + 108, y + 50, b, pal->text3);
    if (st.have_sb) {
        uint64_t bs = st.sb.block_size ? st.sb.block_size : 1024;
        uint64_t total = (uint64_t)st.sb.total_blocks * bs;
        uint64_t freeb = (uint64_t)st.sb.free_blocks * bs;
        uint64_t used = total - freeb;
        uint32_t pm = total ? (uint32_t)(used * 1000 / total) : 0;
        uint32_t col = pm > 900 ? pal->danger : pm > 750 ? pal->warning : pal->accent;
        ui_progress(u, p->x + 24, y + 108, p->w - 48, 14, pm, col);
        fmt_init(&f, a, sizeof(a));
        fbytes(&f, used);
        fs(&f, " used");
        sg_text(u->c, SF_MEDIUM, p->x + 24, y + 134, a, pal->text);
        fmt_init(&f, b, sizeof(b));
        fbytes(&f, freeb);
        fs(&f, " free of ");
        fbytes(&f, total);
        sg_text(u->c, SF_BODY, p->x + p->w - 24 - sg_text_w(SF_BODY, b), y + 134, b, pal->text2);
        fmt_init(&f, a, sizeof(a));
        fu(&f, st.sb.total_inodes - st.sb.free_inodes);
        fs(&f, " files and folders  -  ");
        fu(&f, st.sb.block_size);
        fs(&f, "-byte blocks  -  ");
        fu(&f, st.sb.groups_count);
        fs(&f, " block groups");
        sg_text(u->c, SF_SMALL, p->x + 24, y + 160, a, pal->text3);
    } else {
        sg_text(u->c, SF_BODY, p->x + 24, y + 112, "Usage information is not available for this file system.",
                pal->text3);
    }
    y += h + 16;

    int row_h = 88;
    int dh = 84 + (st.count ? st.count * row_h : 60);
    ui_card(u, p->x, y, p->w, dh);
    fmt_init(&f, a, sizeof(a));
    fu(&f, (uint64_t)st.count);
    fs(&f, st.count == 1 ? " device connected" : " devices connected");
    ui_card_title(u, p->x + 20, y + 20, SI_SERVER, "Disks", a);
    int yy = y + 76;
    ui_separator(u, p->x + 20, yy, p->w - 40);
    yy += 14;
    if (!st.count)
        sg_text(u->c, SF_BODY, p->x + 24, yy + 10, "No storage devices detected.", pal->text3);
    for (int i = 0; i < st.count; i++) {
        const struct storage_device_info *d = &st.disks[i];
        bool usb = d->transport == STORAGE_TRANSPORT_USB_MSC || d->transport == STORAGE_TRANSPORT_USB_EHCI;
        ui_icon_tile(u, p->x + 24, yy, 40, usb ? SI_USB : SI_HARDDISK, usb ? pal->info : pal->accent);
        const char *model = d->model[0] ? d->model : "Unknown disk";
        sg_text_fit(u->c, SF_MEDIUM, p->x + 80, yy, p->w - 300, model, pal->text);
        fmt_init(&f, a, sizeof(a));
        fs(&f, d->name);
        fs(&f, "  -  ");
        fs(&f, transport_name(d->transport));
        if (d->sector_size) {
            fs(&f, "  -  ");
            fu(&f, d->sector_size);
            fs(&f, " B sectors");
        }
        sg_text(u->c, SF_SMALL, p->x + 80, yy + 21, a, pal->text3);
        fmt_init(&f, b, sizeof(b));
        fbytes(&f, d->sector_count * (d->sector_size ? d->sector_size : 512));
        sg_text(u->c, SF_HEAD, p->x + p->w - 24 - sg_text_w(SF_HEAD, b), yy + 2, b, pal->text);
        int bx = p->x + 80;
        bx += ui_badge(u, bx, yy + 44, d->operational ? "Healthy" : "Offline",
                       d->operational ? pal->success : pal->danger) + 8;
        bx += ui_badge(u, bx, yy + 44, d->writable ? "Read / write" : "Read only",
                       d->writable ? pal->info : pal->warning) + 8;
        if (fmt_streq(d->name, st.root))
            ui_badge(u, bx, yy + 44, "System", pal->accent);
        yy += row_h;
        if (i + 1 < st.count)
            ui_separator(u, p->x + 80, yy - 12, p->w - 104);
    }
    y += dh + 16;

    int nh = 96;
    ui_card(u, p->x, y, p->w, nh);
    ui_icon_tile(u, p->x + 24, y + 28, 40, SI_SHIELD_CHECK, pal->success);
    sg_text(u->c, SF_MEDIUM, p->x + 80, y + 26, "Formatting and partitioning live in the Disks app", pal->text);
    sg_text(u->c, SF_SMALL, p->x + 80, y + 48, "Destructive actions always ask for a separate confirmation.",
            pal->text3);
    y += nh;
    return y - p->y;
}
