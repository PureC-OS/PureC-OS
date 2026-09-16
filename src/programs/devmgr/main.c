#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"

#define DEVMGR_WIDTH 880
#define DEVMGR_HEIGHT 600
#define DEVMGR_CATEGORIES 11
#define DEVMGR_DISK_LIMIT 12
#define DEVMGR_CTRL_LIMIT 8
#define DEVMGR_IF_LIMIT 4
#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)

enum devmgr_category {
    DEV_CPU = 0,
    DEV_MEM,
    DEV_STORAGE_CTRL,
    DEV_DISKS,
    DEV_NET,
    DEV_WIFI,
    DEV_AUDIO,
    DEV_VIDEO,
    DEV_USB,
    DEV_POWER,
    DEV_INPUT
};

static const char *category_names[DEVMGR_CATEGORIES] = {
    "Processors",
    "Memory",
    "Storage controllers",
    "Disk drives",
    "Network adapters",
    "Wi-Fi",
    "Sound and audio",
    "Display adapters",
    "USB",
    "Batteries and power",
    "Keyboards and mice"
};

static const char *category_icons[DEVMGR_CATEGORIES] = {
    "CPU", "RAM", "AHCI", "HD", "NET", "WiFi",
    "SND", "GPU", "USB", "BAT", "IN"
};

struct devmgr_state {
    int selected;
    struct cpu_monitor_info cpu;
    bool cpu_ok;
    struct memory_monitor_info mem;
    bool mem_ok;
    struct storage_device_info disks[DEVMGR_DISK_LIMIT];
    int32_t disk_count;
    struct storage_controller_info ctrls[DEVMGR_CTRL_LIMIT];
    int32_t ctrl_count;
    struct net_if_info ifs[DEVMGR_IF_LIMIT];
    int32_t if_count;
    struct wifi_status_info wifi;
    bool wifi_ok;
    struct audio_status audio;
    bool audio_ok;
    struct pc_display_info video;
    bool video_ok;
    struct usb_scan_status usb;
    bool usb_ok;
    struct battery_info battery;
    bool battery_ok;
    char root_device[32];
    bool root_ok;
    char fs_type[16];
    bool fs_ok;
    char status[128];
    uint32_t refresh_tick;
};

static char *append_text(char *out, const char *text) {
    while (*text) *out++ = *text++;
    *out = '\0';
    return out;
}

static char *append_u64(char *out, uint64_t v) {
    char rev[24]; uint32_t c = 0;
    if (!v) rev[c++] = '0';
    while (v && c < sizeof(rev)) { rev[c++] = (char)('0' + v % 10U); v /= 10U; }
    while (c) *out++ = rev[--c];
    *out = '\0';
    return out;
}

static char *append_i32(char *out, int32_t v) {
    if (v < 0) { *out++ = '-'; v = -v; }
    return append_u64(out, (uint64_t)v);
}

static void append_ip(char *out, uint32_t ip) {
    // Stored little-endian (x86): print byte0.byte1.byte2.byte3
    for (int i = 0; i < 4; i++) {
        uint32_t b = (ip >> (i * 8)) & 0xFFU;
        char rev[4]; int c = 0;
        if (!b) rev[c++] = '0';
        while (b) { rev[c++] = (char)('0' + b % 10U); b /= 10U; }
        while (c) *out++ = rev[--c];
        if (i < 3) *out++ = '.';
    }
    *out = '\0';
}

static void append_mac(char *out, const uint8_t mac[6]) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        *out++ = hex[(mac[i] >> 4) & 0xFU];
        *out++ = hex[mac[i] & 0xFU];
        if (i < 5) *out++ = ':';
    }
    *out = '\0';
}

static const char *transport_name(uint8_t t) {
    switch (t) {
        case 1: return "ATA PIO";
        case 2: return "AHCI";
        case 3: return "USB MSC";
        case 4: return "USB EHCI";
        default: return "unknown";
    }
}

static const char *controller_type_name(uint8_t t) {
    switch (t) {
        case 1: return "AHCI";
        case 2: return "NVMe";
        case 3: return "XHCI";
        case 4: return "EHCI";
        default: return "other";
    }
}

static const char *audio_backend_name(uint32_t b) {
    if (b & 2) return "Intel HDA";
    if (b & 1) return "PC Speaker (legacy)";
    return "none";
}

static void refresh_all(struct devmgr_state *st, bool rescan_usb) {
    st->cpu_ok = pc_cpu_info(&st->cpu);
    st->mem_ok = pc_memory_info(&st->mem);
    st->disk_count = pc_list_disks(st->disks, DEVMGR_DISK_LIMIT);
    if (st->disk_count < 0) st->disk_count = 0;
    st->ctrl_count = (int32_t)pc_syscall(209, (uint64_t)(uintptr_t)st->ctrls,
                                         DEVMGR_CTRL_LIMIT, 0);
    if (st->ctrl_count < 0) st->ctrl_count = 0;
    st->if_count = pc_net_if_list(st->ifs, DEVMGR_IF_LIMIT);
    if (st->if_count < 0) st->if_count = 0;
    st->wifi_ok = pc_wifi_status(&st->wifi);
    st->audio_ok = pc_audio_get_status(&st->audio);
    st->video_ok = pc_display_get_info(&st->video);
    if (rescan_usb) {
        for (uint32_t i = 0; i < sizeof(st->usb); i++)
            ((uint8_t *)&st->usb)[i] = 0;
        int64_t r = pc_syscall(212, (uint64_t)(uintptr_t)&st->usb, 0, 0);
        st->usb_ok = r >= 0;
        if (r >= 0) {
            char *o = st->status;
            o = append_text(o, "USB rescan done, devices: ");
            o = append_u64(o, (uint64_t)r);
        } else {
            pc_copy(st->status, "USB rescan failed", sizeof(st->status));
        }
    }
    st->battery_ok = pc_syscall(220, (uint64_t)(uintptr_t)&st->battery, 0, 0) >= 0;
    st->root_ok = pc_get_root_device(st->root_device, sizeof(st->root_device)) >= 0;
    st->fs_ok = pc_get_fs_type(st->fs_type, sizeof(st->fs_type)) >= 0;
    if (!rescan_usb) {
        char *o = st->status;
        o = append_text(o, "OK  disks: ");
        o = append_u64(o, (uint64_t)st->disk_count);
        o = append_text(o, "  net: ");
        o = append_u64(o, (uint64_t)(st->if_count > 0 ? st->if_count : 0));
    }
}

static void draw_header(struct pg_window *win, struct devmgr_state *st,
                        const struct pg_event *ev) {
    pg_window_text(win, 14, 12, "Device Manager", win->theme.text);
    pg_window_text(win, 14, 30, "View hardware status. Select a category on the left.",
                   win->theme.muted_text);
    if (pg_button(win, (struct pg_rect){win->client.width - 230, 8, 100, 26},
                  "Refresh", ev)) {
        refresh_all(st, false);
    }
    if (pg_button(win, (struct pg_rect){win->client.width - 122, 8, 110, 26},
                  "Rescan USB", ev)) {
        refresh_all(st, true);
    }
}

static void draw_categories(struct pg_window *win, struct devmgr_state *st,
                            const struct pg_event *ev) {
    uint32_t pane_x = 12, pane_y = 56;
    uint32_t pane_w = 236, pane_h = win->client.height - 112;
    pg_window_rect(win, (struct pg_rect){pane_x, pane_y, pane_w, pane_h},
                   win->theme.titlebar);
    for (int i = 0; i < DEVMGR_CATEGORIES; i++) {
        uint32_t ry = pane_y + 6 + (uint32_t)i * 30;
        if (ry + 26 > pane_y + pane_h) break;
        bool sel = (st->selected == i);
        pg_window_rect(win, (struct pg_rect){pane_x + 6, ry, pane_w - 12, 26},
                       sel ? win->theme.accent : win->theme.window);
        pg_window_text(win, pane_x + 14, ry + 9, category_icons[i],
                       sel ? 0x1E1E2E : win->theme.muted_text);
        pg_window_text(win, pane_x + 56, ry + 9, category_names[i],
                       sel ? 0x1E1E2E : win->theme.text);
        if (ev && ev->type == PG_EVENT_MOUSE_UP && ev->button == 1) {
            int32_t lx = ev->x - (int32_t)win->client.x;
            int32_t ly = ev->y - (int32_t)win->client.y;
            if (lx >= (int32_t)(pane_x + 6) && lx < (int32_t)(pane_x + pane_w - 6) &&
                ly >= (int32_t)ry && ly < (int32_t)(ry + 26)) {
                st->selected = i;
            }
        }
    }
}

static uint32_t draw_line(struct pg_window *win, uint32_t x, uint32_t y,
                          const char *label, const char *value) {
    pg_window_text(win, x, y, label, win->theme.muted_text);
    if (value)
        pg_window_text(win, x + 190, y, value, win->theme.text);
    return y + 20;
}

static void draw_details(struct pg_window *win, struct devmgr_state *st) {
    uint32_t dx = 260, dy = 56;
    uint32_t dw = win->client.width > dx + 12 ? win->client.width - dx - 12 : 100;
    uint32_t dh = win->client.height > 112 ? win->client.height - 112 : 100;
    pg_window_rect(win, (struct pg_rect){dx, dy, dw, dh}, win->theme.window);
    pg_window_rect(win, (struct pg_rect){dx, dy, dw, 28}, win->theme.titlebar);
    pg_window_text(win, dx + 12, dy + 9, category_names[st->selected],
                   win->theme.accent);
    uint32_t y = dy + 44;
    uint32_t tx = dx + 14;
    char b1[128], b2[128];

    switch (st->selected) {
    case DEV_CPU: {
        if (!st->cpu_ok) {
            pg_window_text(win, tx, y, "CPU data unavailable", win->theme.danger);
            break;
        }
        char *o = b1; o = append_text(o, st->cpu.name[0] ? st->cpu.name : "Generic x86-64");
        pg_window_text(win, tx, y, "Device: CPU", win->theme.text); y += 22;
        pg_window_text(win, tx, y, b1, win->theme.muted_text); y += 24;
        b1[0] = '\0'; o = append_u64(b1, st->cpu.logical_processors);
        y = draw_line(win, tx, y, "Logical CPUs:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->cpu.usage_percent); o = append_text(o, " %");
        y = draw_line(win, tx, y, "Usage:", b1);
        b1[0] = '\0';
        if (st->cpu.frequency_hz >= 1000000ULL)
            o = append_u64(b1, st->cpu.frequency_hz / 1000000ULL),
            o = append_text(o, " MHz");
        else o = append_u64(b1, st->cpu.frequency_hz);
        y = draw_line(win, tx, y, "Frequency:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->cpu.uptime_ms / 1000ULL); o = append_text(o, " s uptime");
        y = draw_line(win, tx, y, "Uptime:", b1);
        pg_window_text(win, tx, y + 8, "Status: This device is working properly.",
                       0xA6E3A1);
        break;
    }
    case DEV_MEM: {
        if (!st->mem_ok) {
            pg_window_text(win, tx, y, "Memory data unavailable", win->theme.danger);
            break;
        }
        b1[0] = '\0'; char *o = append_u64(b1, st->mem.total_bytes / MIB);
        o = append_text(o, " MiB total");
        y = draw_line(win, tx, y, "Total RAM:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->mem.used_bytes / MIB);
        o = append_text(o, " MiB");
        y = draw_line(win, tx, y, "Used:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->mem.available_bytes / MIB);
        o = append_text(o, " MiB");
        y = draw_line(win, tx, y, "Available:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->mem.framebuffer_bytes / MIB);
        o = append_text(o, " MiB");
        y = draw_line(win, tx, y, "Framebuffer:", b1);
        break;
    }
    case DEV_STORAGE_CTRL: {
        if (!st->ctrl_count) {
            pg_window_text(win, tx, y, "No storage controllers reported",
                           win->theme.muted_text);
            break;
        }
        for (int32_t i = 0; i < st->ctrl_count && y + 40 < dy + dh; i++) {
            char *o = b1; o = append_text(o, st->ctrls[i].name[0] ? st->ctrls[i].name : "ctrl");
            o = append_text(o, "  ("); o = append_text(o, controller_type_name(st->ctrls[i].type));
            o = append_text(o, ")");
            pg_window_text(win, tx, y, b1, win->theme.text); y += 20;
            b2[0] = '\0'; o = b2;
            o = append_text(o, "PCI ");
            // vendor:device in hex-ish decimal (decimal fallback)
            o = append_u64(o, st->ctrls[i].vendor_id); o = append_text(o, ":");
            o = append_u64(o, st->ctrls[i].device_id); o = append_text(o, "  bus ");
            o = append_u64(o, st->ctrls[i].bus); o = append_text(o, " slot ");
            o = append_u64(o, st->ctrls[i].slot);
            pg_window_text(win, tx + 16, y, b2, win->theme.muted_text); y += 22;
        }
        break;
    }
    case DEV_DISKS: {
        if (!st->disk_count) {
            pg_window_text(win, tx, y, "No disks found", win->theme.muted_text);
            break;
        }
        for (int32_t i = 0; i < st->disk_count && y + 40 < dy + dh; i++) {
            char *o = b1; o = append_text(o, st->disks[i].name);
            o = append_text(o, "  "); o = append_text(o, st->disks[i].model[0] ? st->disks[i].model : "Unknown");
            pg_window_text(win, tx, y, b1, win->theme.text); y += 20;
            b2[0] = '\0'; o = b2;
            uint64_t bytes = st->disks[i].sector_count * st->disks[i].sector_size;
            if (bytes >= GIB) { o = append_u64(o, bytes / GIB); o = append_text(o, " GiB  "); }
            else { o = append_u64(o, bytes / MIB); o = append_text(o, " MiB  "); }
            o = append_text(o, transport_name(st->disks[i].transport));
            o = append_text(o, st->disks[i].operational ? "  online" : "  offline");
            o = append_text(o, st->disks[i].writable ? "  writable" : "  read-only");
            pg_window_text(win, tx + 16, y, b2, win->theme.muted_text); y += 22;
        }
        if (st->root_ok) {
            y += 4;
            b1[0] = '\0'; char *o = append_text(b1, "System disk: ");
            o = append_text(o, st->root_device);
            if (st->fs_ok) { o = append_text(o, "  ("); o = append_text(o, st->fs_type); o = append_text(o, ")"); }
            pg_window_text(win, tx, y, b1, win->theme.accent);
        }
        break;
    }
    case DEV_NET: {
        if (!st->if_count) {
            pg_window_text(win, tx, y, "No network adapters found", win->theme.muted_text);
            break;
        }
        for (int32_t i = 0; i < st->if_count && y + 60 < dy + dh; i++) {
            pg_window_text(win, tx, y, st->ifs[i].name, win->theme.text); y += 20;
            b1[0] = '\0'; append_mac(b1, st->ifs[i].mac);
            y = draw_line(win, tx + 16, y, "MAC:", b1);
            b1[0] = '\0';
            if (st->ifs[i].has_ip) append_ip(b1, st->ifs[i].ip_address);
            else append_text(b1, "- (no IP)");
            y = draw_line(win, tx + 16, y, "IP:", b1);
            b1[0] = '\0'; char *o = append_text(b1, st->ifs[i].link_up ? "up" : "down");
            if (st->ifs[i].dhcp_bound) o = append_text(o, "  DHCP");
            b2[0] = '\0'; o = b2; o = append_text(o, "rx ");
            o = append_u64(o, st->ifs[i].rx_packets); o = append_text(o, " / tx ");
            o = append_u64(o, st->ifs[i].tx_packets);
            pg_window_text(win, tx + 16, y, b1, win->theme.muted_text);
            pg_window_text(win, tx + 260, y, b2, win->theme.muted_text);
            y += 22;
        }
        break;
    }
    case DEV_WIFI: {
        if (!st->wifi_ok) {
            pg_window_text(win, tx, y, "No Wi-Fi device", win->theme.muted_text);
            break;
        }
        y = draw_line(win, tx, y, "Interface:", st->wifi.interface_name[0] ? st->wifi.interface_name : "-");
        const char *s = "disconnected";
        if (st->wifi.state == 3) s = "connected";
        else if (st->wifi.state == 1) s = "scanning";
        else if (st->wifi.state == 2) s = "connecting";
        y = draw_line(win, tx, y, "State:", s);
        y = draw_line(win, tx, y, "SSID:", st->wifi.ssid[0] ? st->wifi.ssid : "-");
        b1[0] = '\0'; char *o = append_i32(b1, st->wifi.rssi); o = append_text(o, " dBm");
        y = draw_line(win, tx, y, "Signal:", b1);
        b1[0] = '\0';
        if (st->wifi.ip_address) append_ip(b1, st->wifi.ip_address);
        else append_text(b1, "-");
        y = draw_line(win, tx, y, "IP:", b1);
        break;
    }
    case DEV_AUDIO: {
        if (!st->audio_ok) {
            pg_window_text(win, tx, y, "Audio data unavailable", win->theme.danger);
            break;
        }
        y = draw_line(win, tx, y, "Backend:", audio_backend_name(st->audio.backend));
        b1[0] = '\0'; char *o = append_u64(b1, st->audio.volume); o = append_text(o, st->audio.muted ? " % (muted)" : " %");
        y = draw_line(win, tx, y, "Volume:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->audio.selected_output_device + 1);
        o = append_text(o, " / "); o = append_u64(o, st->audio.output_device_count);
        y = draw_line(win, tx, y, "Device:", b1);
        if (st->audio.backend & 2) {
            b1[0] = '\0'; o = append_u64(b1, st->audio.hda_codec);
            y = draw_line(win, tx, y, "HDA codec:", b1);
            b1[0] = '\0'; o = append_u64(b1, st->audio.hda_dac_node);
            o = append_text(o, "  pin "); o = append_u64(o, st->audio.hda_pin_node);
            y = draw_line(win, tx, y, "DAC/PIN:", b1);
        }
        break;
    }
    case DEV_VIDEO: {
        if (!st->video_ok || !st->video.available) {
            pg_window_text(win, tx, y, "No display adapter data", win->theme.muted_text);
            break;
        }
        b1[0] = '\0'; char *o = append_u64(b1, st->video.width);
        o = append_text(o, "x"); o = append_u64(o, st->video.height);
        o = append_text(o, "x"); o = append_u64(o, st->video.bpp);
        y = draw_line(win, tx, y, "Mode:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->video.size_bytes / MIB); o = append_text(o, " MiB");
        y = draw_line(win, tx, y, "Framebuffer:", b1);
        break;
    }
    case DEV_USB: {
        if (!st->usb_ok) {
            pg_window_text(win, tx, y, "Press Rescan USB to probe controllers",
                           win->theme.muted_text);
            y += 24;
            pg_window_text(win, tx, y, "XHCI/EHCI mice, hubs and mass storage",
                           win->theme.muted_text);
            break;
        }
        b1[0] = '\0'; char *o = append_u64(b1, st->usb.xhci_controllers);
        y = draw_line(win, tx, y, "XHCI ctrls:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->usb.xhci_connected_ports);
        o = append_text(o, " ports");
        y = draw_line(win, tx, y, "Connected:", b1);
        // addressed devices
        b1[0] = '\0'; o = append_u64(b1, st->usb.xhci_addressed_devices);
        y = draw_line(win, tx, y, "Addressed:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->usb.xhci_disks);
        y = draw_line(win, tx, y, "USB disks:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->usb.xhci_hid_mice);
        y = draw_line(win, tx, y, "USB mice:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->usb.xhci_hubs);
        y = draw_line(win, tx, y, "Hubs:", b1);
        b1[0] = '\0'; o = append_u64(b1, st->usb.ehci_disks);
        y = draw_line(win, tx, y, "EHCI disks:", b1);
        break;
    }
    case DEV_POWER: {
        if (!st->battery_ok) {
            pg_window_text(win, tx, y, "No battery (desktop / ACPI unavailable)",
                           win->theme.muted_text);
            break;
        }
        y = draw_line(win, tx, y, "Name:", st->battery.name[0] ? st->battery.name : "BAT");
        b1[0] = '\0';
        if (st->battery.percent == 0xFFFFFFFFu) append_text(b1, "unknown");
        else { char *o = append_u64(b1, st->battery.percent); o = append_text(o, " %"); }
        y = draw_line(win, tx, y, "Charge:", b1);
        y = draw_line(win, tx, y, "State:", st->battery.charging ? "charging" : "discharging");
        y = draw_line(win, tx, y, "Info:", st->battery.status_text[0] ? st->battery.status_text : "-");
        break;
    }
    case DEV_INPUT: {
        pg_window_text(win, tx, y, "PS/2 keyboard", win->theme.text); y += 20;
        pg_window_text(win, tx + 16, y, "Status: working properly", win->theme.muted_text); y += 24;
        pg_window_text(win, tx, y, "PS/2 mouse", win->theme.text); y += 20;
        pg_window_text(win, tx + 16, y, "Status: working properly", win->theme.muted_text); y += 24;
        if (st->usb_ok && st->usb.xhci_hid_mice)
            pg_window_text(win, tx, y, "USB HID mice detected", 0xA6E3A1);
        else
            pg_window_text(win, tx, y, "USB HID: none / press Rescan USB",
                           win->theme.muted_text);
        break;
    }
    }
}

static void draw_status(struct pg_window *win, struct devmgr_state *st) {
    uint32_t y = win->client.height - 34;
    pg_window_rect(win, (struct pg_rect){12, y, win->client.width - 24, 24},
                   win->theme.titlebar);
    pg_window_text(win, 22, y + 8, st->status[0] ? st->status : "Ready",
                   win->theme.muted_text);
}

static void draw_all(struct pg_window *win, struct devmgr_state *st,
                     const struct pg_event *ev) {
    pg_window_begin(win);
    draw_header(win, st, ev);
    // Category clicks handled inside draw_categories via event coords.
    draw_categories(win, st, ev);
    draw_details(win, st);
    draw_status(win, st);
    pg_window_end(win);
}

static int devmgr_main(void) {
    struct pg_window win;
    struct pc_display_info di;
    if (!pc_display_get_info(&di) || !di.available) return 1;
    uint32_t w = di.width > DEVMGR_WIDTH + 20 ? DEVMGR_WIDTH : di.width - 20;
    uint32_t h = di.height > DEVMGR_HEIGHT + 40 ? DEVMGR_HEIGHT : di.height - 40;
    if (!pg_window_center(&win, "Device Manager", w, h)) return 1;
    struct devmgr_state st = {0};
    st.selected = DEV_CPU;
    refresh_all(&st, false);
    struct pg_event ev = {0};
    draw_all(&win, &st, &ev);
    uint32_t elapsed = 0;
    while (pg_window_is_open(&win)) {
        if (pg_window_poll_event(&win, &ev)) {
            if (ev.type == PG_EVENT_CLOSE) break;
            if (ev.type != PG_EVENT_MOUSE_MOVE) draw_all(&win, &st, &ev);
        }
        pc_sleep(20);
        elapsed += 20;
        if (elapsed >= 2000) {
            elapsed = 0;
            // Light auto-refresh for CPU/MEM only to avoid USB spam.
            struct cpu_monitor_info cpu;
            struct memory_monitor_info mem;
            if (pc_cpu_info(&cpu)) st.cpu = cpu;
            if (pc_memory_info(&mem)) st.mem = mem;
            if (!pg_window_is_minimized(&win)) draw_all(&win, &st, 0);
        }
    }
    pg_window_close(&win);
    return 0;
}

void _start(void) { pc_exit(devmgr_main()); }
