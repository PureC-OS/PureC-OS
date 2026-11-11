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
#define CMD_DX_DEFINE_CONTEXT 1143
#define CMD_DX_BIND_CONTEXT 1145
#define CMD_DX_SET_SHADER_RESOURCES 1149
#define CMD_DX_SET_SHADER 1150
#define CMD_DX_SET_SAMPLERS 1151
#define CMD_DX_DRAW 1152
#define CMD_DX_SET_INPUT_LAYOUT 1157
#define CMD_DX_SET_VERTEX_BUFFERS 1158
#define CMD_DX_SET_TOPOLOGY 1160
#define CMD_DX_SET_RENDERTARGETS 1161
#define CMD_DX_SET_BLEND_STATE 1162
#define CMD_DX_SET_DEPTHSTENCIL_STATE 1163
#define CMD_DX_SET_RASTERIZER_STATE 1164
#define CMD_DX_SET_VIEWPORTS 1174
#define CMD_DX_SET_SCISSORRECTS 1175
#define CMD_DX_DEFINE_SRV 1185
#define CMD_DX_DESTROY_SRV 1186
#define CMD_DX_DEFINE_RTV 1187
#define CMD_DX_DESTROY_RTV 1188
#define CMD_DX_DEFINE_ELEMENTLAYOUT 1191
#define CMD_DX_DEFINE_BLEND_STATE 1193
#define CMD_DX_DEFINE_DEPTHSTENCIL_STATE 1195
#define CMD_DX_DEFINE_RASTERIZER_STATE 1197
#define CMD_DX_DEFINE_SAMPLER_STATE 1199
#define CMD_DX_DEFINE_SHADER 1201
#define CMD_DX_BIND_SHADER 1203
#define CMD_DX_SET_COTABLE 1207

#define FMT_BUFFER 37u
#define FMT_R8G8B8A8_UNORM 68u
#define FMT_R32G32_FLOAT 125u
#define FMT_B8G8R8A8_UNORM 141u
#define SURF_HINT_DYNAMIC (1ULL << 2)
#define SURF_HINT_VERTEXBUFFER (1ULL << 4)
#define SURF_BIND_VERTEX_BUFFER (1ULL << 20)
#define RESOURCE_TEXTURE2D 3u
#define SHADER_VS 1u
#define SHADER_PS 2u
#define COTABLE_COUNT 12u
#define COTABLE_SRVIEW 2u
#define MAX_TEXTURES 96u
#define MAX_BATCHES 256u
#define VERTEX_BYTES 20u
#define VB_BYTES 65536u
#define MAX_VERTICES (VB_BYTES / VERTEX_BYTES)
#define F32_ONE 0x3F800000u
#define TEX_FRONT 0u
#define TEX_WHITE 1u
#define TEX_FIRST_USER 2u

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

struct texture {
    bool used;
    uint32_t sid;
    uint32_t mob;
    uint32_t width;
    uint32_t height;
};

struct batch {
    uint32_t first;
    uint32_t count;
    uint32_t texture;
    uint32_t mode;
    bool linear;
};

struct vertex {
    uint32_t x;
    uint32_t y;
    uint32_t u;
    uint32_t v;
    uint8_t r, g, b, a;
} PACKED;

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
    bool dx_context;
    bool dx;
    struct mem ctx_mem;
    struct mem cot_mem[COTABLE_COUNT];
    struct mem shader_mem;
    struct mem vb_mem;
    struct mem white_mem;
    struct mem rt_mem;
    uint32_t vb_sid;
    uint32_t rt_sid;
    uint32_t rt_mob;
    struct texture tex[MAX_TEXTURES];
    struct batch batches[MAX_BATCHES];
    uint32_t batch_count;
    uint32_t vertex_count;
    bool in_frame;
    void (*frame_request)(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
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
    if (d.dx_context && context == CB_CONTEXT_0)
        h->flags |= CB_FLAG_DX_CONTEXT;
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


static const uint32_t shader_vs[] = {
    0x00010040, 45,
    0x0300005F, 0x00101032, 0,
    0x0300005F, 0x00101032, 1,
    0x0300005F, 0x001010F2, 2,
    0x04000067, 0x001020F2, 0, 1,
    0x03000065, 0x00102032, 1,
    0x03000065, 0x001020F2, 2,
    0x05000036, 0x00102032, 0, 0x00101046, 0,
    0x08000036, 0x001020C2, 0, 0x00004002, 0, 0, 0, F32_ONE,
    0x05000036, 0x00102032, 1, 0x00101046, 1,
    0x05000036, 0x001020F2, 2, 0x00101E46, 2,
    0x0100003E};

static const uint32_t shader_ps_tex[] = {
    0x00000040, 41,
    0x0300005A, 0x00106000, 0,
    0x04001858, 0x00107000, 0, 0x00005555,
    0x04002064, 0x001010F2, 0, 1,
    0x03001062, 0x00101032, 1,
    0x03001062, 0x001010F2, 2,
    0x03000065, 0x001020F2, 0,
    0x02000068, 1,
    0x09000045, 0x001000F2, 0, 0x00101046, 1, 0x00107E46, 0, 0x00106000, 0,
    0x07000038, 0x001020F2, 0, 0x00100E46, 0, 0x00101E46, 2,
    0x0100003E};

static const uint32_t shader_ps_mask[] = {
    0x00000040, 49,
    0x0300005A, 0x00106000, 0,
    0x04001858, 0x00107000, 0, 0x00005555,
    0x04002064, 0x001010F2, 0, 1,
    0x03001062, 0x00101032, 1,
    0x03001062, 0x001010F2, 2,
    0x03000065, 0x001020F2, 0,
    0x02000068, 1,
    0x09000045, 0x001000F2, 0, 0x00101046, 1, 0x00107E46, 0, 0x00106000, 0,
    0x0300000D, 0x0010003A, 0,
    0x05000036, 0x00100082, 0, 0x00004001, F32_ONE,
    0x07000038, 0x001020F2, 0, 0x00100E46, 0, 0x00101E46, 2,
    0x0100003E};

static uint32_t f32_ratio(int64_t num, int64_t den) {
    if (!num || den <= 0)
        return 0;
    uint32_t sign = 0;
    if (num < 0) {
        sign = 0x80000000u;
        num = -num;
    }
    int nb = 64 - __builtin_clzll((uint64_t)num);
    int db = 64 - __builtin_clzll((uint64_t)den);
    int shift = 26 - (nb - db);
    uint64_t q = shift >= 0 ? ((uint64_t)num << shift) / (uint64_t)den
                            : ((uint64_t)num / (uint64_t)den) >> -shift;
    int exp = -shift;
    while (q >= (1ULL << 25)) {
        q >>= 1;
        exp++;
    }
    q = (q + 1) >> 1;
    exp++;
    if (q >= (1ULL << 24)) {
        q >>= 1;
        exp++;
    }
    if (q < (1ULL << 23))
        return 0;
    return sign | ((uint32_t)(exp + 150) << 23) | ((uint32_t)q & 0x7FFFFFu);
}

static uint32_t f32_int(int64_t value) { return f32_ratio(value, 1); }

static bool dx_stage(const char *what) {
    if (cb_flush(true))
        return true;
    klogf(KLOG_WARN, "svga3d: DX setup failed at %s", what);
    d.failed = false;
    return false;
}

static bool dx_texture_define(uint32_t index, uint64_t phys, uint32_t w,
                              uint32_t h, uint32_t format) {
    struct texture *t = &d.tex[index];
    uint64_t bytes = (uint64_t)w * h * 4;
    t->mob = mob_create(phys, bytes);
    t->sid = surface_create(SURF_BIND_SHADER_RESOURCE | SURF_HINT_TEXTURE,
                            format, w, h, t->mob);
    if (t->mob == INVALID_ID || t->sid == INVALID_ID) {
        if (t->sid != INVALID_ID)
            surface_destroy(t->sid);
        mob_destroy(t->mob);
        return false;
    }
    surface_upload(t->sid, 0, 0, w, h);
    uint32_t *c = cb_cmd(CMD_DX_DEFINE_SRV, 32);
    c[0] = index;
    c[1] = t->sid;
    c[2] = format;
    c[3] = RESOURCE_TEXTURE2D;
    c[6] = 1;
    c[7] = 1;
    t->width = w;
    t->height = h;
    t->used = true;
    return true;
}

static void dx_texture_release(uint32_t index) {
    struct texture *t = &d.tex[index];
    if (!t->used)
        return;
    uint32_t *c = cb_cmd(CMD_DX_DESTROY_SRV, 4);
    c[0] = index;
    surface_destroy(t->sid);
    mob_destroy(t->mob);
    t->used = false;
}

static bool dx_setup(void) {
    if (!mem_alloc(&d.ctx_mem, 65536) || !mem_alloc(&d.shader_mem, 4096) ||
        !mem_alloc(&d.vb_mem, VB_BYTES) || !mem_alloc(&d.white_mem, 4096))
        return false;
    uint32_t ctx_mob = mob_create(d.ctx_mem.phys, 65536);
    uint32_t *c = cb_cmd(CMD_DX_DEFINE_CONTEXT, 4);
    c[0] = 0;
    c = cb_cmd(CMD_DX_BIND_CONTEXT, 12);
    c[0] = 0;
    c[1] = ctx_mob;
    if (ctx_mob == INVALID_ID || !dx_stage("context"))
        return false;
    d.dx_context = true;
    for (uint32_t i = 0; i < COTABLE_COUNT; i++) {
        uint32_t bytes = i == COTABLE_SRVIEW ? 16384u : 8192u;
        if (!mem_alloc(&d.cot_mem[i], bytes))
            return false;
        uint32_t mob = mob_create(d.cot_mem[i].phys, bytes);
        if (mob == INVALID_ID)
            return false;
        c = cb_cmd(CMD_DX_SET_COTABLE, 16);
        c[0] = 0;
        c[1] = mob;
        c[2] = i;
    }
    if (!dx_stage("cotables"))
        return false;

    memcpy(d.shader_mem.virt, shader_vs, sizeof(shader_vs));
    memcpy(d.shader_mem.virt + 512, shader_ps_tex, sizeof(shader_ps_tex));
    memcpy(d.shader_mem.virt + 1024, shader_ps_mask, sizeof(shader_ps_mask));
    uint32_t shader_mob = mob_create(d.shader_mem.phys, 4096);
    if (shader_mob == INVALID_ID)
        return false;
    static const uint32_t shader_size[3] = {sizeof(shader_vs),
                                            sizeof(shader_ps_tex),
                                            sizeof(shader_ps_mask)};
    for (uint32_t i = 0; i < 3; i++) {
        c = cb_cmd(CMD_DX_DEFINE_SHADER, 12);
        c[0] = i;
        c[1] = i ? SHADER_PS : SHADER_VS;
        c[2] = shader_size[i];
        c = cb_cmd(CMD_DX_BIND_SHADER, 16);
        c[0] = 0;
        c[1] = i;
        c[2] = shader_mob;
        c[3] = i * 512;
    }
    if (!dx_stage("shaders"))
        return false;

    c = cb_cmd(CMD_DX_DEFINE_ELEMENTLAYOUT, 4 + 3 * 24);
    c[0] = 0;
    static const uint32_t layout[3][3] = {{0, FMT_R32G32_FLOAT, 0},
                                          {8, FMT_R32G32_FLOAT, 1},
                                          {16, FMT_R8G8B8A8_UNORM, 2}};
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t *e = c + 1 + i * 6;
        e[1] = layout[i][0];
        e[2] = layout[i][1];
        e[5] = layout[i][2];
    }
    for (uint32_t id = 0; id < 2; id++) {
        uint8_t *b = cb_cmd(CMD_DX_DEFINE_BLEND_STATE, 104);
        *(uint32_t *)b = id;
        for (uint32_t rt = 0; rt < 8; rt++) {
            uint8_t *r = b + 8 + rt * 12;
            r[0] = (uint8_t)(id && rt == 0);
            r[1] = id ? 5 : 2;
            r[2] = id ? 6 : 1;
            r[3] = 1;
            r[4] = 2;
            r[5] = id ? 6 : 1;
            r[6] = 1;
            r[7] = 0x0F;
        }
    }
    uint8_t *ds = cb_cmd(CMD_DX_DEFINE_DEPTHSTENCIL_STATE, 20);
    ds[6] = 8;
    ds[10] = 0xFF;
    ds[11] = 0xFF;
    ds[12] = 1;
    ds[13] = 1;
    ds[14] = 1;
    ds[15] = 8;
    ds[16] = 1;
    ds[17] = 1;
    ds[18] = 1;
    ds[19] = 8;
    uint8_t *rs = cb_cmd(CMD_DX_DEFINE_RASTERIZER_STATE, 32);
    rs[4] = 3;
    rs[5] = 1;
    rs[20] = 1;
    rs[21] = 1;
    *(uint32_t *)(rs + 24) = F32_ONE;
    for (uint32_t id = 0; id < 2; id++) {
        uint8_t *sm = cb_cmd(CMD_DX_DEFINE_SAMPLER_STATE, 44);
        *(uint32_t *)sm = id;
        *(uint32_t *)(sm + 4) = id ? 0x14u : 0u;
        sm[8] = 3;
        sm[9] = 3;
        sm[10] = 3;
        sm[16] = 1;
        sm[17] = 1;
        *(uint32_t *)(sm + 40) = 0x7F7FFFFFu;
    }
    if (!dx_stage("state objects"))
        return false;

    uint32_t vb_mob = mob_create(d.vb_mem.phys, VB_BYTES);
    d.vb_sid = surface_create(SURF_BIND_VERTEX_BUFFER | SURF_HINT_VERTEXBUFFER |
                                  SURF_HINT_DYNAMIC,
                              FMT_BUFFER, VB_BYTES, 1, vb_mob);
    if (vb_mob == INVALID_ID || d.vb_sid == INVALID_ID)
        return false;
    for (uint32_t i = 0; i < 1024; i++)
        ((uint32_t *)d.white_mem.virt)[i] = 0xFFFFFFFFu;
    if (!dx_texture_define(TEX_WHITE, d.white_mem.phys, 16, 16,
                           FMT_B8G8R8A8_UNORM))
        return false;
    return dx_stage("buffers");
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
    d.rt_sid = INVALID_ID;
    d.rt_mob = INVALID_ID;
    d.dx = dx_setup();
    if (d.dx)
        klog(KLOG_OK, "svga3d: DX pipeline ready (shaders, states, vertex buffer)");
    else
        klog(KLOG_WARN, "svga3d: DX pipeline unavailable, screen target only");
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
    if (d.tex[TEX_FRONT].used) {
        dx_texture_release(TEX_FRONT);
    } else {
        surface_destroy(d.front_sid);
        mob_destroy(d.front_mob);
    }
    if (d.rt_sid != INVALID_ID) {
        uint32_t *c = cb_cmd(CMD_DX_DESTROY_RTV, 4);
        c[0] = 0;
        surface_destroy(d.rt_sid);
        mob_destroy(d.rt_mob);
    }
    d.front_sid = INVALID_ID;
    d.front_mob = INVALID_ID;
    d.rt_sid = INVALID_ID;
    d.rt_mob = INVALID_ID;
    cb_flush(true);
    mem_free(&d.front);
    mem_free(&d.rt_mem);
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
    if (d.dx && mem_alloc(&d.rt_mem, bytes)) {
        d.rt_mob = mob_create(d.rt_mem.phys, bytes);
        d.rt_sid = surface_create(SURF_SCREENTARGET | SURF_BIND_RENDER_TARGET |
                                      SURF_BIND_SHADER_RESOURCE |
                                      SURF_HINT_RENDERTARGET | SURF_HINT_TEXTURE,
                                  FMT_B8G8R8X8_UNORM, width, height, d.rt_mob);
        bool ok = d.rt_mob != INVALID_ID && d.rt_sid != INVALID_ID &&
                  dx_texture_define(TEX_FRONT, d.front.phys, width, height,
                                    FMT_B8G8R8X8_UNORM);
        if (ok) {
            uint32_t *c = cb_cmd(CMD_DX_DEFINE_RTV, 28);
            c[0] = 0;
            c[1] = d.rt_sid;
            c[2] = FMT_B8G8R8X8_UNORM;
            c[3] = RESOURCE_TEXTURE2D;
            c[6] = 1;
            struct cmd_define_st *def =
                cb_cmd(CMD_DEFINE_GB_SCREENTARGET, sizeof(*def));
            def->stid = 0;
            def->width = width;
            def->height = height;
            def->flags = 1;
            struct cmd_bind_st *bind =
                cb_cmd(CMD_BIND_GB_SCREENTARGET, sizeof(*bind));
            bind->stid = 0;
            bind->sid = d.rt_sid;
            ok = cb_flush(true);
        }
        if (ok) {
            d.front_sid = d.tex[TEX_FRONT].sid;
            d.front_mob = d.tex[TEX_FRONT].mob;
            d.screen_defined = true;
            d.active = true;
            status_set("svga3d: GPU compositing active");
            svga3d_frame_begin();
            svga3d_frame_end(0, 0, width, height);
            if (!d.failed)
                return d.front.virt;
        }
        klogf(KLOG_WARN, "svga3d: render target %ux%u rejected", width, height);
        d.active = false;
        d.screen_defined = false;
        d.dx = false;
        svga_legacy_restore(width, height);
        mem_free(&d.front);
        mem_free(&d.rt_mem);
        return 0;
    }
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
    if (d.dx) {
        if (d.frame_request) {
            d.frame_request(0, 0, 0, 0);
            return;
        }
        if (__atomic_test_and_set(&render_busy, __ATOMIC_ACQUIRE))
            return;
        while (d.active && dirty_valid) {
            svga3d_frame_begin();
            svga3d_frame_end(0, 0, d.width, d.height);
        }
        __atomic_clear(&render_busy, __ATOMIC_RELEASE);
        return;
    }
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
    if (d.dx && d.frame_request) {
        d.frame_request(x, y, w, h);
        return;
    }
    svga3d_flush();
}


bool svga3d_gpu(void) { return d.active && d.dx; }

void svga3d_set_frame_request(void (*fn)(uint32_t, uint32_t, uint32_t,
                                         uint32_t)) {
    d.frame_request = fn;
}

uint32_t svga3d_texture_create(uint64_t phys, uint32_t width, uint32_t height) {
    if (!svga3d_gpu() || !phys || !width || !height)
        return 0;
    for (uint32_t i = TEX_FIRST_USER; i < MAX_TEXTURES; i++) {
        if (d.tex[i].used)
            continue;
        if (!dx_texture_define(i, phys, width, height, FMT_B8G8R8A8_UNORM))
            return 0;
        return i;
    }
    return 0;
}

void svga3d_texture_destroy(uint32_t texture) {
    if (!d.started || d.failed || texture < TEX_FIRST_USER ||
        texture >= MAX_TEXTURES)
        return;
    dx_texture_release(texture);
    cb_flush(true);
}

void svga3d_texture_update(uint32_t texture, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h) {
    if (!svga3d_gpu() || texture >= MAX_TEXTURES || !d.tex[texture].used)
        return;
    struct texture *t = &d.tex[texture];
    if (x >= t->width || y >= t->height || !w || !h)
        return;
    if (w > t->width - x)
        w = t->width - x;
    if (h > t->height - y)
        h = t->height - y;
    surface_upload(t->sid, x, y, w, h);
}

static void put_vertex(int32_t px, int32_t py, int64_t tu, int64_t tv,
                       uint32_t tw, uint32_t th, uint32_t argb) {
    struct vertex *v =
        (struct vertex *)(d.vb_mem.virt + (uint64_t)d.vertex_count * VERTEX_BYTES);
    v->x = f32_ratio(2 * (int64_t)px - d.width, d.width);
    v->y = f32_ratio((int64_t)d.height - 2 * (int64_t)py, d.height);
    v->u = f32_ratio(tu, tw);
    v->v = f32_ratio(tv, th);
    v->r = (uint8_t)(argb >> 16);
    v->g = (uint8_t)(argb >> 8);
    v->b = (uint8_t)argb;
    v->a = (uint8_t)(argb >> 24);
    d.vertex_count++;
}

static struct batch *batch_for(uint32_t texture, uint32_t mode, bool linear,
                               uint32_t vertices) {
    if (!d.in_frame || d.vertex_count + vertices > MAX_VERTICES)
        return 0;
    if (d.batch_count) {
        struct batch *last = &d.batches[d.batch_count - 1];
        if (last->texture == texture && last->mode == mode &&
            last->linear == linear)
            return last;
    }
    if (d.batch_count >= MAX_BATCHES)
        return 0;
    struct batch *b = &d.batches[d.batch_count++];
    b->first = d.vertex_count;
    b->count = 0;
    b->texture = texture;
    b->mode = mode;
    b->linear = linear;
    return b;
}

void svga3d_quad(uint32_t texture, int32_t dx, int32_t dy, uint32_t dw,
                 uint32_t dh, uint32_t sx, uint32_t sy, uint32_t sw,
                 uint32_t sh, uint32_t argb, uint32_t mode) {
    if (texture >= MAX_TEXTURES || !d.tex[texture].used || !dw || !dh)
        return;
    struct texture *t = &d.tex[texture];
    bool linear = dw != sw || dh != sh;
    struct batch *b = batch_for(texture, mode, linear, 6);
    if (!b)
        return;
    int32_t x1 = dx + (int32_t)dw;
    int32_t y1 = dy + (int32_t)dh;
    int64_t u1 = (int64_t)sx + sw;
    int64_t v1 = (int64_t)sy + sh;
    put_vertex(dx, dy, sx, sy, t->width, t->height, argb);
    put_vertex(x1, dy, u1, sy, t->width, t->height, argb);
    put_vertex(dx, y1, sx, v1, t->width, t->height, argb);
    put_vertex(dx, y1, sx, v1, t->width, t->height, argb);
    put_vertex(x1, dy, u1, sy, t->width, t->height, argb);
    put_vertex(x1, y1, u1, v1, t->width, t->height, argb);
    b->count += 6;
}

void svga3d_triangle(int32_t x0, int32_t y0, uint32_t c0, int32_t x1,
                     int32_t y1, uint32_t c1, int32_t x2, int32_t y2,
                     uint32_t c2) {
    struct batch *b = batch_for(TEX_WHITE, SVGA3D_MODE_BLEND, false, 3);
    if (!b)
        return;
    put_vertex(x0, y0, 8, 8, 16, 16, c0);
    put_vertex(x1, y1, 8, 8, 16, 16, c1);
    put_vertex(x2, y2, 8, 8, 16, 16, c2);
    b->count += 3;
}

void svga3d_frame_begin(void) {
    if (!svga3d_gpu() || d.in_frame)
        return;
    d.in_frame = true;
    d.batch_count = 0;
    d.vertex_count = 0;
    uint32_t x, y, w, h;
    if (dirty_take(&x, &y, &w, &h))
        surface_upload(d.front_sid, x, y, w, h);
    svga3d_quad(TEX_FRONT, 0, 0, d.width, d.height, 0, 0, d.width, d.height,
                0xFFFFFFFFu, SVGA3D_MODE_OPAQUE);
}

void svga3d_frame_end(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!d.in_frame)
        return;
    d.in_frame = false;
    if (x >= d.width || y >= d.height || !w || !h) {
        x = 0;
        y = 0;
        w = d.width;
        h = d.height;
    }
    if (w > d.width - x)
        w = d.width - x;
    if (h > d.height - y)
        h = d.height - y;
    uint32_t *c = cb_cmd(CMD_DX_SET_RENDERTARGETS, 8);
    c[0] = INVALID_ID;
    c[1] = 0;
    c = cb_cmd(CMD_DX_SET_VIEWPORTS, 28);
    c[3] = f32_int(d.width);
    c[4] = f32_int(d.height);
    c[6] = F32_ONE;
    c = cb_cmd(CMD_DX_SET_SCISSORRECTS, 20);
    c[1] = x;
    c[2] = y;
    c[3] = x + w;
    c[4] = y + h;
    c = cb_cmd(CMD_DX_SET_TOPOLOGY, 4);
    c[0] = 1;
    c = cb_cmd(CMD_DX_SET_INPUT_LAYOUT, 4);
    c[0] = 0;
    c = cb_cmd(CMD_DX_SET_VERTEX_BUFFERS, 16);
    c[1] = d.vb_sid;
    c[2] = VERTEX_BYTES;
    c = cb_cmd(CMD_DX_SET_SHADER, 8);
    c[0] = 0;
    c[1] = SHADER_VS;
    c = cb_cmd(CMD_DX_SET_DEPTHSTENCIL_STATE, 8);
    c = cb_cmd(CMD_DX_SET_RASTERIZER_STATE, 4);
    surface_upload(d.vb_sid, 0, 0, VB_BYTES, 1);
    for (uint32_t i = 0; i < d.batch_count; i++) {
        struct batch *b = &d.batches[i];
        if (!b->count)
            continue;
        c = cb_cmd(CMD_DX_SET_SHADER, 8);
        c[0] = b->mode == SVGA3D_MODE_MASK ? 2 : 1;
        c[1] = SHADER_PS;
        c = cb_cmd(CMD_DX_SET_BLEND_STATE, 24);
        c[0] = b->mode == SVGA3D_MODE_BLEND ? 1 : 0;
        c[1] = F32_ONE;
        c[2] = F32_ONE;
        c[3] = F32_ONE;
        c[4] = F32_ONE;
        c[5] = 0xFFFFFFFFu;
        c = cb_cmd(CMD_DX_SET_SAMPLERS, 12);
        c[1] = SHADER_PS;
        c[2] = b->linear ? 1 : 0;
        c = cb_cmd(CMD_DX_SET_SHADER_RESOURCES, 12);
        c[1] = SHADER_PS;
        c[2] = b->texture;
        c = cb_cmd(CMD_DX_DRAW, 8);
        c[0] = b->count;
        c[1] = b->first;
    }
    struct cmd_update_st *upd = cb_cmd(CMD_UPDATE_GB_SCREENTARGET, sizeof(*upd));
    upd->stid = 0;
    upd->x = x;
    upd->y = y;
    upd->w = w;
    upd->h = h;
    cb_flush(true);
    if (d.failed)
        fail_to_legacy();
}

void svga3d_panic(void) {
    if (!d.active)
        return;
    d.active = false;
    void *legacy = svga_fb_virt();
    if (legacy && svga_legacy_restore(d.width, d.height))
        gop_retarget_front(legacy, svga_pitch_pixels());
}
