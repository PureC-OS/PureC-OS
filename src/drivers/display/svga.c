#include "svga.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define SVGA_PCI_VENDOR 0x15ADu
#define SVGA_PCI_DEVICE 0x0405u

#define SVGA_REG_ID 0
#define SVGA_REG_ENABLE 1
#define SVGA_REG_WIDTH 2
#define SVGA_REG_HEIGHT 3
#define SVGA_REG_MAX_WIDTH 4
#define SVGA_REG_MAX_HEIGHT 5
#define SVGA_REG_BPP 7
#define SVGA_REG_FB_OFFSET 14
#define SVGA_REG_VRAM_SIZE 15
#define SVGA_REG_FB_SIZE 16
#define SVGA_REG_CAPS 17
#define SVGA_REG_MEM_SIZE 19
#define SVGA_REG_CONFIG_DONE 20
#define SVGA_REG_SYNC 21
#define SVGA_REG_BUSY 22
#define SVGA_REG_BYTES_PER_LINE 12

#define SVGA_ID_0 0x90000000u
#define SVGA_ID_2 0x90000002u

#define SVGA_CAP_RECT_FILL (1u << 0)
#define SVGA_CAP_RECT_COPY (1u << 1)

#define SVGA_FIFO_MIN 0
#define SVGA_FIFO_MAX 1
#define SVGA_FIFO_NEXT_CMD 2
#define SVGA_FIFO_STOP 3
#define SVGA_FIFO_NUM_REGS_MIN 4

#define SVGA_CMD_UPDATE 1
#define SVGA_CMD_RECT_FILL 2
#define SVGA_CMD_RECT_COPY 3

static inline void outl16(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(port));
}
static inline uint32_t inl16(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

struct svga_dev {
    bool pci_found;
    uint8_t bus, slot, func;
    uint16_t io_base;
    uint64_t fb_phys, fifo_phys;
    uint64_t fb_size, fifo_size;
    volatile uint32_t *fb;
    volatile uint32_t *fifo;
    uint32_t width, height, pitch_px;
    uint8_t bpp;
    uint32_t caps;
    uint32_t dev_id;
    bool ready;
};
static struct svga_dev g;

struct svga_probe_ctx {
    bool found;
    uint8_t bus, slot, func;
};

static void svga_visitor(const struct pci_device_info *d, void *ctx) {
    struct svga_probe_ctx *c = ctx;
    if (!d || !c || c->found)
        return;
    if (d->vendor_id == SVGA_PCI_VENDOR && d->device_id == SVGA_PCI_DEVICE) {
        c->found = true;
        c->bus = d->bus;
        c->slot = d->slot;
        c->func = d->function;
    }
}

bool svga_probe_only(void) {
    struct svga_probe_ctx c = {0};
    pci_enumerate(svga_visitor, &c);
    return c.found;
}

static uint32_t svga_reg_read(uint16_t io, uint32_t idx) {
    outl16(io, idx);
    return inl16((uint16_t)(io + 4));
}
static void svga_reg_write(uint16_t io, uint32_t idx, uint32_t val) {
    outl16(io, idx);
    outl16((uint16_t)(io + 4), val);
}

static void fifo_write_word(uint32_t word) {
    volatile uint32_t *fifo = g.fifo;
    uint32_t next = fifo[SVGA_FIFO_NEXT_CMD];
    uint32_t max = fifo[SVGA_FIFO_MAX];
    uint32_t min = fifo[SVGA_FIFO_MIN];
    uint32_t stop = fifo[SVGA_FIFO_STOP];
    // wait for slot: next+4 == stop (modulo) means full
    for (int spin = 0; spin < 1000000; spin++) {
        stop = fifo[SVGA_FIFO_STOP];
        uint32_t used;
        if (next >= stop)
            used = next - stop;
        else
            used = (max - stop) + (next - min);
        uint32_t free = (max - min) - used;
        if (free > 4)
            break;
        // kick device, let it drain
        svga_reg_write(g.io_base, SVGA_REG_SYNC, 1);
        while (svga_reg_read(g.io_base, SVGA_REG_BUSY))
            ;
        stop = fifo[SVGA_FIFO_STOP];
    }
    fifo[next / 4] = word;
    next += 4;
    if (next >= max)
        next = min + (next - max);
    fifo[SVGA_FIFO_NEXT_CMD] = next;
}

static void fifo_cmd(uint32_t id, const uint32_t *args, uint32_t n) {
    fifo_write_word(id);
    for (uint32_t i = 0; i < n; i++)
        fifo_write_word(args[i]);
}

void svga_sync(void) {
    if (!g.ready)
        return;
    svga_reg_write(g.io_base, SVGA_REG_SYNC, 1);
    for (int i = 0; i < 1000000; i++) {
        if (!svga_reg_read(g.io_base, SVGA_REG_BUSY))
            return;
    }
}

void svga_update(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g.ready || !w || !h)
        return;
    if (x >= g.width || y >= g.height)
        return;
    if (x + w > g.width)
        w = g.width - x;
    if (y + h > g.height)
        h = g.height - y;
    uint32_t a[4] = {x, y, w, h};
    fifo_cmd(SVGA_CMD_UPDATE, a, 4);
}

bool svga_fill(uint32_t color, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g.ready || !w || !h)
        return false;
    if (!(g.caps & SVGA_CAP_RECT_FILL))
        return false;
    if (x >= g.width || y >= g.height)
        return false;
    if (x + w > g.width)
        w = g.width - x;
    if (y + h > g.height)
        h = g.height - y;
    uint32_t a[5] = {color, x, y, w, h};
    fifo_cmd(SVGA_CMD_RECT_FILL, a, 5);
    return true;
}

bool svga_copy(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    if (!g.ready || !w || !h)
        return false;
    if (!(g.caps & SVGA_CAP_RECT_COPY))
        return false;
    uint32_t a[6] = {sx, sy, dx, dy, w, h};
    fifo_cmd(SVGA_CMD_RECT_COPY, a, 6);
    return true;
}

bool svga_init(uint32_t want_w, uint32_t want_h, uint8_t want_bpp) {
    memset(&g, 0, sizeof(g));
    struct svga_probe_ctx c = {0};
    pci_enumerate(svga_visitor, &c);
    if (!c.found) {
        klog(KLOG_WARN, "svga: 15AD:0405 not found, GOP fallback");
        return false;
    }
    g.bus = c.bus;
    g.slot = c.slot;
    g.func = c.func;
    struct pci_device_info di = {.bus = g.bus, .slot = g.slot, .function = g.func};
    if (!pci_update_command(&di, PCI_COMMAND_IO_SPACE | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0)) {
        klog(KLOG_WARN, "svga: PCI enable failed, GOP fallback");
        return false;
    }
    uint64_t bar0 = pci_read_bar(g.bus, g.slot, g.func, 0);
    if (!bar0) {
        klog(KLOG_WARN, "svga: BAR0 empty, GOP fallback");
        return false;
    }
    g.io_base = (uint16_t)(bar0 & ~0x3ULL);
    if (!g.io_base) {
        klog(KLOG_WARN, "svga: IO base 0, GOP fallback");
        return false;
    }
    // version negotiate
    uint32_t id = SVGA_ID_2;
    for (;;) {
        svga_reg_write(g.io_base, SVGA_REG_ID, id);
        if (svga_reg_read(g.io_base, SVGA_REG_ID) == id)
            break;
        if (id == SVGA_ID_0) {
            klog(KLOG_WARN, "svga: ID negotiate failed, GOP fallback");
            return false;
        }
        id--;
    }
    g.dev_id = id;
    if (!want_w)
        want_w = 1024;
    if (!want_h)
        want_h = 768;
    if (want_bpp != 32)
        want_bpp = 32;
    svga_reg_write(g.io_base, SVGA_REG_ENABLE, 0);
    svga_reg_write(g.io_base, SVGA_REG_WIDTH, want_w);
    svga_reg_write(g.io_base, SVGA_REG_HEIGHT, want_h);
    svga_reg_write(g.io_base, SVGA_REG_BPP, want_bpp);
    svga_reg_write(g.io_base, SVGA_REG_ENABLE, 1);
    uint32_t bpl = svga_reg_read(g.io_base, SVGA_REG_BYTES_PER_LINE);
    if (!bpl)
        bpl = want_w * 4;
    g.width = want_w;
    g.height = want_h;
    g.pitch_px = bpl / 4;
    g.bpp = want_bpp;
    g.caps = 0;
    // CAPS valid from ID_1+
    if (id >= SVGA_ID_0 + 1)
        g.caps = svga_reg_read(g.io_base, SVGA_REG_CAPS);
    uint64_t fb_sz = svga_reg_read(g.io_base, SVGA_REG_FB_SIZE);
    uint64_t fifo_sz = svga_reg_read(g.io_base, SVGA_REG_MEM_SIZE);
    if (!fb_sz || !fifo_sz) {
        klog(KLOG_WARN, "svga: zero FB/FIFO size, GOP fallback");
        return false;
    }
    g.fb_size = fb_sz;
    g.fifo_size = fifo_sz;
    uint64_t fb_phys = pci_read_bar(g.bus, g.slot, g.func, 1);
    uint64_t fifo_phys = pci_read_bar(g.bus, g.slot, g.func, 2);
    if (!fb_phys || !fifo_phys) {
        klog(KLOG_WARN, "svga: BAR1/2 empty, GOP fallback");
        return false;
    }
    g.fb_phys = fb_phys & ~0xFULL;
    g.fifo_phys = fifo_phys & ~0xFULL;
    if (!mmio_is_ready()) {
        klog(KLOG_WARN, "svga: mmio not ready, GOP fallback");
        return false;
    }
    uint64_t need_fb = (uint64_t)g.height * bpl;
    if (need_fb > fb_sz)
        need_fb = fb_sz;
    volatile void *fb = mmio_map(g.fb_phys, need_fb);
    volatile void *fifo = mmio_map(g.fifo_phys, fifo_sz);
    if (!fb || !fifo) {
        klog(KLOG_WARN, "svga: BAR map failed, GOP fallback");
        return false;
    }
    g.fb = (volatile uint32_t *)fb;
    g.fifo = (volatile uint32_t *)fifo;
    // fifo init: reserve registers
    uint32_t nregs = svga_reg_read(g.io_base, 30); // MEM_REGS
    if (nregs < SVGA_FIFO_NUM_REGS_MIN || nregs > 1024)
        nregs = 293 * 1; // modern default; clamp
    if (nregs > 293)
        nregs = 293;
    uint32_t min = nregs * 4;
    if (min + 10 * 1024 > fifo_sz) {
        klog(KLOG_WARN, "svga: FIFO too small, GOP fallback");
        return false;
    }
    g.fifo[SVGA_FIFO_MIN] = min;
    g.fifo[SVGA_FIFO_MAX] = (uint32_t)fifo_sz;
    g.fifo[SVGA_FIFO_NEXT_CMD] = min;
    g.fifo[SVGA_FIFO_STOP] = min;
    svga_reg_write(g.io_base, SVGA_REG_CONFIG_DONE, 1);
    g.ready = true;
    klogf(KLOG_OK, "svga: %ux%ux%u io=0x%x fb=0x%llx (%lluK) fifo=0x%llx (%lluK) caps=0x%x id=0x%x",
          g.width, g.height, g.bpp, g.io_base,
          (unsigned long long)g.fb_phys, (unsigned long long)(fb_sz / 1024),
          (unsigned long long)g.fifo_phys, (unsigned long long)(fifo_sz / 1024),
          g.caps, g.dev_id);
    return true;
}

bool svga_is_available(void) { return g.ready; }
void *svga_fb_virt(void) { return (void *)g.fb; }
uint32_t svga_width(void) { return g.width; }
uint32_t svga_height(void) { return g.height; }
uint32_t svga_pitch_pixels(void) { return g.pitch_px; }
uint8_t svga_bpp(void) { return g.bpp; }
uint64_t svga_fb_size(void) { return g.fb_size; }
uint32_t svga_caps(void) { return g.caps; }
