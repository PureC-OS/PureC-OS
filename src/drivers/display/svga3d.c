#include "svga3d.h"
#include "svga.h"
#include "gop.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/sync/spinlock.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"

#define PACKED __attribute__((packed))

#define REG_COMMAND_LOW 48
#define REG_COMMAND_HIGH 49
#define REG_DEV_CAP 52
#define REG_SCREENTARGET_MAX_WIDTH 55
#define REG_SCREENTARGET_MAX_HEIGHT 56
#define REG_CAP2 59

#define CAP_COMMAND_BUFFERS 0x01000000u
#define CAP_GBOBJECTS 0x08000000u
#define CAP_DX 0x10000000u
#define CAP_CAP2 0x80000000u
#define DEVCAP_DXCONTEXT 95

#define CB_STATUS_NONE 0
#define CB_STATUS_COMPLETED 1
#define CB_FLAG_NO_IRQ 1u
#define CB_FLAG_DX_CONTEXT 2u
#define CB_CONTEXT_0 0u
#define CB_CONTEXT_DEVICE 0x3Fu
#define DC_CMD_START_STOP_CONTEXT 1u

#define CMD_DESTROY_GB_MOB 1094
#define CMD_DESTROY_GB_SURFACE 1098
#define CMD_BIND_GB_SURFACE 1099
#define CMD_UPDATE_GB_IMAGE 1101
#define CMD_SET_OTABLE_BASE64 1115
#define CMD_DEFINE_GB_SCREENTARGET 1124
#define CMD_DESTROY_GB_SCREENTARGET 1125
#define CMD_BIND_GB_SCREENTARGET 1126
#define CMD_UPDATE_GB_SCREENTARGET 1127
#define CMD_DEFINE_GB_MOB64 1135
#define CMD_DEFINE_GB_SURFACE_V3 1239

#define MOBFMT_RANGE 3u
#define INVALID_ID 0xFFFFFFFFu

#define FMT_B8G8R8X8_UNORM 142u
#define SURF_HINT_TEXTURE (1ULL << 5)
#define SURF_HINT_RENDERTARGET (1ULL << 6)
#define SURF_SCREENTARGET (1ULL << 16)
#define SURF_BIND_SHADER_RESOURCE (1ULL << 23)
#define SURF_BIND_RENDER_TARGET (1ULL << 24)

#define OTABLE_COUNT 6
#define MAX_MOBS 512u
#define MAX_SURFACES 256u
#define CB_COUNT 4u
#define CB_DATA_PAGES 8u
#define CB_DATA_BYTES (CB_DATA_PAGES * 4096u)
#define CB_TIMEOUT_US 3000000ULL

struct cb_header {
    volatile uint32_t status;
    volatile uint32_t error_offset;
    uint64_t id;
    uint32_t flags;
    uint32_t length;
    uint64_t pa;
    uint32_t offset;
    uint32_t dx_context;
    uint32_t zero[6];
} PACKED;

struct cmd_otable {
    uint32_t type;
    uint64_t base;
    uint32_t size;
    uint32_t valid;
    uint32_t depth;
} PACKED;

struct cmd_mob {
    uint32_t mobid;
    uint32_t depth;
    uint64_t base;
    uint32_t size;
} PACKED;

struct cmd_surface_v3 {
    uint32_t sid;
    uint64_t flags;
    uint32_t format;
    uint32_t mips;
    uint32_t samples;
    uint32_t ms_pattern;
    uint32_t ms_quality;
    uint32_t autogen;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
} PACKED;

struct cmd_update_image {
    uint32_t sid;
    uint32_t face;
    uint32_t mip;
    uint32_t x, y, z, w, h, d;
} PACKED;

struct cmd_define_st {
    uint32_t stid;
    uint32_t width;
    uint32_t height;
    int32_t xroot;
    int32_t yroot;
    uint32_t flags;
    uint32_t dpi;
} PACKED;

struct cmd_bind_st {
    uint32_t stid;
    uint32_t sid;
    uint32_t face;
    uint32_t mip;
} PACKED;

struct cmd_update_st {
    uint32_t stid;
    uint32_t x, y, w, h;
} PACKED;

struct mem {
    uint64_t phys;
    uint64_t pages;
    uint8_t *virt;
};

static struct {
    bool started;
    bool active;
    bool failed;
    uint32_t caps;
    uint32_t caps2;
    struct mem cb[CB_COUNT];
    bool cb_sent[CB_COUNT];
    uint32_t cb_cur;
    uint32_t cb_len;
    struct mem otable[OTABLE_COUNT];
    uint8_t mob_used[MAX_MOBS / 8];
    uint8_t sid_used[MAX_SURFACES / 8];
    uint32_t width;
    uint32_t height;
    struct mem front;
    uint32_t front_mob;
    uint32_t front_sid;
    bool screen_defined;
} d;

static char status_text[96] = "svga3d: off";
static spinlock_t dirty_lock = SPINLOCK_INIT;
static volatile bool render_busy;
static bool dirty_valid;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static const uint32_t otable_bytes[OTABLE_COUNT] = {
    MAX_MOBS * 16u, MAX_SURFACES * 64u, 4096u, 4096u, 4096u, 4096u};

static void status_set(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1 < sizeof(status_text)) {
        status_text[i] = text[i];
        i++;
    }
    status_text[i] = 0;
}

const char *svga3d_status(void) { return status_text; }
bool svga3d_active(void) { return d.active; }

static bool mem_alloc(struct mem *m, uint64_t bytes) {
    uint64_t pages = (bytes + 4095ULL) / 4096ULL;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if (!phys)
        return false;
    m->phys = phys;
    m->pages = pages;
    m->virt = (uint8_t *)pmm_physical_to_virtual(phys);
    memset(m->virt, 0, pages * 4096ULL);
    return true;
}

static void mem_free(struct mem *m) {
    if (m->phys)
        pmm_free_contiguous(m->phys, m->pages);
    m->phys = 0;
    m->pages = 0;
    m->virt = 0;
}

static uint32_t id_alloc(uint8_t *map, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!(map[i / 8] & (1u << (i % 8)))) {
            map[i / 8] |= (uint8_t)(1u << (i % 8));
            return i;
        }
    }
    return INVALID_ID;
}

static void id_free(uint8_t *map, uint32_t count, uint32_t id) {
    if (id < count)
        map[id / 8] &= (uint8_t)~(1u << (id % 8));
}

static struct cb_header *cb_hdr(uint32_t index) {
    return (struct cb_header *)d.cb[index].virt;
}

static bool cb_wait(uint32_t index) {
    if (!d.cb_sent[index])
        return true;
    struct cb_header *h = cb_hdr(index);
    uint64_t start = klog_uptime_us();
    while (h->status == CB_STATUS_NONE) {
        __asm__ volatile("pause");
        if (klog_uptime_us() - start > CB_TIMEOUT_US) {
            d.failed = true;
            status_set("svga3d: command buffer timeout");
            return false;
        }
    }
    d.cb_sent[index] = false;
    if (h->status != CB_STATUS_COMPLETED) {
        uint32_t off = h->error_offset;
        uint32_t cmd = off + 4 <= CB_DATA_BYTES
                           ? *(uint32_t *)(d.cb[index].virt + 4096 + off)
                           : 0;
        d.failed = true;
        status_set("svga3d: command rejected");
        klogf(KLOG_ERROR, "svga3d: buffer status=%u offset=%u cmd=%u len=%u",
              h->status, off, cmd, h->length);
        return false;
    }
    return true;
}

static void cb_send(uint32_t index, uint32_t length, uint32_t context) {
    struct cb_header *h = cb_hdr(index);
    memset(h, 0, sizeof(*h));
    h->flags = CB_FLAG_NO_IRQ;
    h->length = length;
    h->pa = d.cb[index].phys + 4096;
    d.cb_sent[index] = true;
    svga_reg_set(REG_COMMAND_HIGH, (uint32_t)(d.cb[index].phys >> 32));
    svga_reg_set(REG_COMMAND_LOW, (uint32_t)d.cb[index].phys | context);
}

static bool cb_flush(bool wait) {
    if (d.failed)
        return false;
    if (!d.cb_len)
        return wait ? cb_wait((d.cb_cur + CB_COUNT - 1) % CB_COUNT) : true;
    uint32_t index = d.cb_cur;
    cb_send(index, d.cb_len, CB_CONTEXT_0);
    d.cb_len = 0;
    if (wait && !cb_wait(index))
        return false;
    d.cb_cur = (index + 1) % CB_COUNT;
    return cb_wait(d.cb_cur);
}

static void *cb_cmd(uint32_t id, uint32_t size) {
    static uint8_t sink[256];
    if (d.failed || size + 8 > CB_DATA_BYTES)
        return sink;
    if (d.cb_len + size + 8 > CB_DATA_BYTES && !cb_flush(false))
        return sink;
    uint32_t *head = (uint32_t *)(d.cb[d.cb_cur].virt + 4096 + d.cb_len);
    head[0] = id;
    head[1] = size;
    d.cb_len += size + 8;
    memset(head + 2, 0, size);
    return head + 2;
}

static uint32_t mob_create(uint64_t phys, uint64_t bytes) {
    uint32_t id = id_alloc(d.mob_used, MAX_MOBS);
    if (id == INVALID_ID)
        return id;
    struct cmd_mob *c = cb_cmd(CMD_DEFINE_GB_MOB64, sizeof(*c));
    c->mobid = id;
    c->depth = MOBFMT_RANGE;
    c->base = phys >> 12;
    c->size = (uint32_t)bytes;
    return id;
}

static void mob_destroy(uint32_t id) {
    if (id == INVALID_ID)
        return;
    uint32_t *c = cb_cmd(CMD_DESTROY_GB_MOB, 4);
    c[0] = id;
    id_free(d.mob_used, MAX_MOBS, id);
}

static uint32_t surface_create(uint64_t flags, uint32_t format, uint32_t w,
                               uint32_t h, uint32_t mob) {
    uint32_t sid = id_alloc(d.sid_used, MAX_SURFACES);
    if (sid == INVALID_ID)
        return sid;
    struct cmd_surface_v3 *c = cb_cmd(CMD_DEFINE_GB_SURFACE_V3, sizeof(*c));
    c->sid = sid;
    c->flags = flags;
    c->format = format;
    c->mips = 1;
    c->width = w;
    c->height = h;
    c->depth = 1;
    uint32_t *b = cb_cmd(CMD_BIND_GB_SURFACE, 8);
    b[0] = sid;
    b[1] = mob;
    return sid;
}

static void surface_destroy(uint32_t sid) {
    if (sid == INVALID_ID)
        return;
    uint32_t *b = cb_cmd(CMD_BIND_GB_SURFACE, 8);
    b[0] = sid;
    b[1] = INVALID_ID;
    uint32_t *c = cb_cmd(CMD_DESTROY_GB_SURFACE, 4);
    c[0] = sid;
    id_free(d.sid_used, MAX_SURFACES, sid);
}

static void surface_upload(uint32_t sid, uint32_t x, uint32_t y, uint32_t w,
                           uint32_t h) {
    struct cmd_update_image *c = cb_cmd(CMD_UPDATE_GB_IMAGE, sizeof(*c));
    c->sid = sid;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->d = 1;
}

static void fail_to_legacy(void) {
    if (!d.active)
        return;
    d.active = false;
    void *legacy = svga_fb_virt();
    if (!legacy || !svga_legacy_restore(d.width, d.height))
        return;
    uint32_t pitch = svga_pitch_pixels();
    if (d.front.virt) {
        for (uint32_t y = 0; y < d.height; y++)
            memcpy((uint32_t *)legacy + (uint64_t)y * pitch,
                   d.front.virt + (uint64_t)y * d.width * 4,
                   (uint64_t)d.width * 4);
    }
    gop_retarget_front(legacy, pitch);
    svga_update(0, 0, d.width, d.height);
    klogf(KLOG_WARN, "svga3d: disabled (%s), back to framebuffer mode",
          status_text);
}

bool svga3d_start(void) {
    if (d.started)
        return !d.failed;
    if (!svga_is_available())
        return false;
    d.caps = svga_caps();
    if ((d.caps & (CAP_COMMAND_BUFFERS | CAP_GBOBJECTS | CAP_DX)) !=
        (CAP_COMMAND_BUFFERS | CAP_GBOBJECTS | CAP_DX)) {
        status_set("svga3d: device has no DX support");
        klogf(KLOG_INFO, "svga3d: not available, caps=0x%x", d.caps);
        return false;
    }
    d.caps2 = (d.caps & CAP_CAP2) ? svga_reg_get(REG_CAP2) : 0;
    svga_reg_set(REG_DEV_CAP, DEVCAP_DXCONTEXT);
    uint32_t dx = svga_reg_get(REG_DEV_CAP);
    uint32_t max_w = svga_reg_get(REG_SCREENTARGET_MAX_WIDTH);
    uint32_t max_h = svga_reg_get(REG_SCREENTARGET_MAX_HEIGHT);
    klogf(KLOG_INFO, "svga3d: caps=0x%x caps2=0x%x dxcontext=%u screentarget max %ux%u",
          d.caps, d.caps2, dx, max_w, max_h);
    if (!dx) {
        status_set("svga3d: DX contexts disabled on host");
        return false;
    }
    d.started = true;
    d.front_mob = INVALID_ID;
    d.front_sid = INVALID_ID;
    for (uint32_t i = 0; i < CB_COUNT; i++) {
        if (!mem_alloc(&d.cb[i], 4096u + CB_DATA_BYTES)) {
            d.failed = true;
            status_set("svga3d: no memory for command buffers");
            return false;
        }
    }
    uint32_t *dc = (uint32_t *)(d.cb[0].virt + 4096);
    dc[0] = DC_CMD_START_STOP_CONTEXT;
    dc[1] = 1;
    dc[2] = CB_CONTEXT_0;
    cb_send(0, 12, CB_CONTEXT_DEVICE);
    if (!cb_wait(0)) {
        klog(KLOG_WARN, "svga3d: cannot start command buffer context");
        return false;
    }
    for (uint32_t i = 0; i < OTABLE_COUNT; i++) {
        if (!mem_alloc(&d.otable[i], otable_bytes[i])) {
            d.failed = true;
            status_set("svga3d: no memory for object tables");
            return false;
        }
        struct cmd_otable *c = cb_cmd(CMD_SET_OTABLE_BASE64, sizeof(*c));
        c->type = i;
        c->base = d.otable[i].phys >> 12;
        c->size = otable_bytes[i];
        c->depth = MOBFMT_RANGE;
    }
    if (!cb_flush(true)) {
        klog(KLOG_WARN, "svga3d: object tables rejected");
        return false;
    }
    status_set("svga3d: ready");
    klog(KLOG_OK, "svga3d: command buffers and object tables ready");
    return true;
}

void svga3d_screen_teardown(void) {
    if (!d.started || d.failed)
        return;
    uint64_t flags = spin_lock_irqsave(&dirty_lock);
    dirty_valid = false;
    spin_unlock_irqrestore(&dirty_lock, flags);
    if (d.active) {
        d.active = false;
        void *legacy = svga_fb_virt();
        if (legacy)
            gop_retarget_front(legacy, svga_pitch_pixels());
    }
    if (d.screen_defined) {
        uint32_t *c = cb_cmd(CMD_DESTROY_GB_SCREENTARGET, 4);
        c[0] = 0;
        d.screen_defined = false;
    }
    surface_destroy(d.front_sid);
    mob_destroy(d.front_mob);
    d.front_sid = INVALID_ID;
    d.front_mob = INVALID_ID;
    cb_flush(true);
    mem_free(&d.front);
}

void *svga3d_screen_setup(uint32_t width, uint32_t height) {
    if (!d.started || d.failed || !width || !height)
        return 0;
    svga3d_screen_teardown();
    if (d.failed)
        return 0;
    uint64_t bytes = (uint64_t)width * height * 4;
    if (!mem_alloc(&d.front, bytes)) {
        status_set("svga3d: no memory for front buffer");
        return 0;
    }
    d.width = width;
    d.height = height;
    d.front_mob = mob_create(d.front.phys, bytes);
    d.front_sid = surface_create(SURF_SCREENTARGET | SURF_BIND_RENDER_TARGET |
                                     SURF_BIND_SHADER_RESOURCE |
                                     SURF_HINT_RENDERTARGET | SURF_HINT_TEXTURE,
                                 FMT_B8G8R8X8_UNORM, width, height, d.front_mob);
    if (d.front_mob == INVALID_ID || d.front_sid == INVALID_ID) {
        status_set("svga3d: out of object ids");
        mem_free(&d.front);
        return 0;
    }
    surface_upload(d.front_sid, 0, 0, width, height);
    struct cmd_define_st *def = cb_cmd(CMD_DEFINE_GB_SCREENTARGET, sizeof(*def));
    def->stid = 0;
    def->width = width;
    def->height = height;
    def->flags = 1;
    struct cmd_bind_st *bind = cb_cmd(CMD_BIND_GB_SCREENTARGET, sizeof(*bind));
    bind->stid = 0;
    bind->sid = d.front_sid;
    struct cmd_update_st *upd = cb_cmd(CMD_UPDATE_GB_SCREENTARGET, sizeof(*upd));
    upd->stid = 0;
    upd->w = width;
    upd->h = height;
    if (!cb_flush(true)) {
        klogf(KLOG_WARN, "svga3d: screen target %ux%u rejected", width, height);
        svga_legacy_restore(width, height);
        mem_free(&d.front);
        return 0;
    }
    d.screen_defined = true;
    d.active = true;
    status_set("svga3d: screen target active");
    return d.front.virt;
}

static bool dirty_take(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint64_t flags = spin_lock_irqsave(&dirty_lock);
    bool have = dirty_valid;
    if (have) {
        *x = dirty_x0;
        *y = dirty_y0;
        *w = dirty_x1 - dirty_x0;
        *h = dirty_y1 - dirty_y0;
        dirty_valid = false;
    }
    spin_unlock_irqrestore(&dirty_lock, flags);
    return have;
}

void svga3d_flush(void) {
    if (!d.active)
        return;
    for (;;) {
        if (__atomic_test_and_set(&render_busy, __ATOMIC_ACQUIRE))
            return;
        uint32_t x, y, w, h;
        bool drew = false;
        while (d.active && dirty_take(&x, &y, &w, &h)) {
            surface_upload(d.front_sid, x, y, w, h);
            struct cmd_update_st *upd =
                cb_cmd(CMD_UPDATE_GB_SCREENTARGET, sizeof(*upd));
            upd->stid = 0;
            upd->x = x;
            upd->y = y;
            upd->w = w;
            upd->h = h;
            cb_flush(false);
            drew = true;
        }
        if (d.failed)
            fail_to_legacy();
        __atomic_clear(&render_busy, __ATOMIC_RELEASE);
        if (!drew || !dirty_valid)
            return;
    }
}

void svga3d_front_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!d.active || !w || !h || x >= d.width || y >= d.height)
        return;
    if (w > d.width - x)
        w = d.width - x;
    if (h > d.height - y)
        h = d.height - y;
    uint64_t flags = spin_lock_irqsave(&dirty_lock);
    if (!dirty_valid) {
        dirty_x0 = x;
        dirty_y0 = y;
        dirty_x1 = x + w;
        dirty_y1 = y + h;
        dirty_valid = true;
    } else {
        if (x < dirty_x0)
            dirty_x0 = x;
        if (y < dirty_y0)
            dirty_y0 = y;
        if (x + w > dirty_x1)
            dirty_x1 = x + w;
        if (y + h > dirty_y1)
            dirty_y1 = y + h;
    }
    spin_unlock_irqrestore(&dirty_lock, flags);
    svga3d_flush();
}

void svga3d_panic(void) {
    if (!d.active)
        return;
    d.active = false;
    void *legacy = svga_fb_virt();
    if (legacy && svga_legacy_restore(d.width, d.height))
        gop_retarget_front(legacy, svga_pitch_pixels());
}
