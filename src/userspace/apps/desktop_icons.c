#include "desktop_icons.h"
#include "desktop_entries.h"
#include "../../drivers/display/gop.h"
#include "../../fs/vfs.h"
#include "../../kernel/syscall/syscall.h"
#include "../../mm/pmm.h"
#include "../../lib/string.h"

#define ICN_MAGIC 0x43494350u
#define ICN_MAX_DIM 256u
#define ICN_HEADER 8u

struct icon_slot {
    char path[128];
    bool tried;
    bool ok;
    uint32_t w, h;
    uint8_t *file;
    uint64_t file_phys, file_pages;
    uint32_t *argb;
    uint64_t argb_phys, argb_pages;
    uint32_t size, fg, bg;
};

static struct icon_slot g_slots[DESKTOP_ENTRY_MAX];

static void free_buffers(struct icon_slot *s) {
    if (s->file) pmm_free_contiguous(s->file_phys, s->file_pages);
    if (s->argb) pmm_free_contiguous(s->argb_phys, s->argb_pages);
    memset(s, 0, sizeof(*s));
}

static bool load_file(struct icon_slot *s, const char *path) {
    if (!vfs_is_root_mounted() || !filesystem_syscall_acquire()) return false;
    int32_t fd = vfs_open(path);
    bool ok = false;
    if (fd >= 0) {
        uint64_t pages = (ICN_HEADER + ICN_MAX_DIM * ICN_MAX_DIM + 4095u) / 4096u;
        uint64_t phys = pmm_allocate_contiguous(pages);
        if (phys) {
            uint8_t *buf = (uint8_t *)pmm_physical_to_virtual(phys);
            uint32_t total = 0;
            for (;;) {
                int32_t n = vfs_read(fd, buf + total,
                                     (uint32_t)(pages * 4096u - total));
                if (n <= 0) break;
                total += (uint32_t)n;
            }
            uint32_t magic = 0;
            if (total >= ICN_HEADER) memcpy(&magic, buf, 4);
            uint16_t w = 0, h = 0;
            if (total >= ICN_HEADER) {
                memcpy(&w, buf + 4, 2);
                memcpy(&h, buf + 6, 2);
            }
            if (magic == ICN_MAGIC && w && h && w <= ICN_MAX_DIM &&
                h <= ICN_MAX_DIM && total >= ICN_HEADER + (uint32_t)w * h) {
                s->file = buf;
                s->file_phys = phys;
                s->file_pages = pages;
                s->w = w;
                s->h = h;
                ok = true;
            } else {
                pmm_free_contiguous(phys, pages);
            }
        }
        (void)vfs_close(fd);
    }
    filesystem_syscall_unlock();
    return ok;
}

static uint32_t mix(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= ((ca * (255 - t) + cb * t + 127) / 255) << shift;
    }
    return out;
}

static bool render(struct icon_slot *s, uint32_t size, uint32_t fg,
                   uint32_t bg) {
    uint64_t pages = ((uint64_t)size * size * 4u + 4095u) / 4096u;
    if (!s->argb || s->argb_pages < pages) {
        if (s->argb) pmm_free_contiguous(s->argb_phys, s->argb_pages);
        s->argb = 0;
        uint64_t phys = pmm_allocate_contiguous(pages);
        if (!phys) return false;
        s->argb = (uint32_t *)pmm_physical_to_virtual(phys);
        s->argb_phys = phys;
        s->argb_pages = pages;
    }
    const uint8_t *mask = s->file + ICN_HEADER;
    for (uint32_t dy = 0; dy < size; dy++) {
        uint32_t sy0 = dy * s->h / size;
        uint32_t sy1 = (dy + 1) * s->h / size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (uint32_t dx = 0; dx < size; dx++) {
            uint32_t sx0 = dx * s->w / size;
            uint32_t sx1 = (dx + 1) * s->w / size;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t sum = 0, count = 0;
            for (uint32_t y = sy0; y < sy1 && y < s->h; y++)
                for (uint32_t x = sx0; x < sx1 && x < s->w; x++) {
                    sum += mask[y * s->w + x];
                    count++;
                }
            uint32_t a = count ? sum / count : 0;
            s->argb[dy * size + dx] = mix(bg, fg, a);
        }
    }
    s->size = size;
    s->fg = fg;
    s->bg = bg;
    return true;
}

static uint32_t foreground_for(uint32_t tile) {
    uint32_t r = (tile >> 16) & 0xFF, g = (tile >> 8) & 0xFF, b = tile & 0xFF;
    uint32_t luma = (299u * r + 587u * g + 114u * b) / 1000u;
    return luma > 90u ? 0x1E1E2Eu : 0xCDD6F4u;
}

bool desktop_icon_draw(uint32_t index, const char *icon_path, uint32_t x,
                       uint32_t y, uint32_t box_w, uint32_t box_h,
                       uint32_t tile_color) {
    if (index >= DESKTOP_ENTRY_MAX || !icon_path || icon_path[0] != '/')
        return false;
    struct icon_slot *s = &g_slots[index];
    if (strcmp(s->path, icon_path) != 0) {
        free_buffers(s);
        uint32_t len = (uint32_t)strlen(icon_path);
        if (len >= sizeof(s->path)) return false;
        memcpy(s->path, icon_path, len + 1);
    }
    if (!s->tried) {
        s->tried = true;
        s->ok = load_file(s, icon_path);
    }
    if (!s->ok) return false;
    uint32_t side = box_w < box_h ? box_w : box_h;
    uint32_t size = side * 85u / 100u;
    if (size < 12u) size = 12u;
    uint32_t fg = foreground_for(tile_color);
    if (!s->argb || s->size != size || s->fg != fg || s->bg != tile_color) {
        if (!render(s, size, fg, tile_color)) return false;
    }
    return gop_draw_pixels(x + (box_w - size) / 2u, y + (box_h - size) / 2u,
                           size, size, s->argb, size);
}
