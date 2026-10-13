#include "svga.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <stdbool.h>

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

static uint32_t svga_try_ids(uint16_t io, bool log_each, uint8_t bus,
                             uint8_t slot, uint8_t func) {
    uint32_t id = SVGA_ID_2;
    for (;;) {
        svga_reg_write(io, SVGA_REG_ID, id);
        uint32_t back = svga_reg_read(io, SVGA_REG_ID);
        if (back == id)
            return id;
        if (log_each)
            klogf(KLOG_WARN, "svga: ID try 0x%x got 0x%x (io=0x%x pci=%02x:%02x.%u)",
                  id, back, io, bus, slot, func);
        if (id == SVGA_ID_0)
            return 0xFFFFFFFFu;
        id--;
    }
}

struct svga_io_used {
    uint64_t base, size;
};

static void svga_collect_visitor(const struct pci_device_info *d, void *ctx) {
    struct {
        struct svga_io_used *out;
        int *n;
        int cap;
    } *c = ctx;
    if (!d || !c || *c->n >= c->cap)
        return;
    for (uint8_t b = 0; b < 6; b++) {
        uint8_t off = (uint8_t)(0x10 + b * 4);
        uint32_t raw = pci_read_config32(d->bus, d->slot, d->function, off);
        if ((raw & 1u) == 0)
            continue;
        uint64_t base = raw & ~0x3ULL;
        if (!base)
            continue;
        uint64_t sz = pci_bar_size(d->bus, d->slot, d->function, b);
        if (!sz || sz > 0x1000)
            sz = 0x20;
        c->out[*c->n].base = base;
        c->out[*c->n].size = sz;
        (*c->n)++;
        if (*c->n >= c->cap)
            return;
    }
}

void svga_collect_io_ranges(struct svga_io_used *out, int *n, int cap) {
    struct {
        struct svga_io_used *out;
        int *n;
        int cap;
    } c = {out, n, cap};
    *n = 0;
    // legacy ISA hole: never go below 0x1000
    out[0].base = 0;
    out[0].size = 0x1000;
    *n = 1;
    pci_enumerate(svga_collect_visitor, &c);
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
    uint32_t bar0_raw = pci_read_config32(g.bus, g.slot, g.func, 0x10);
    uint32_t id_raw = pci_read_config32(g.bus, g.slot, g.func, 0x00);
    uint32_t cmd_raw = pci_read_config32(g.bus, g.slot, g.func, 0x04);
    uint32_t cls_raw = pci_read_config32(g.bus, g.slot, g.func, 0x08);
    uint32_t bar1_raw = pci_read_config32(g.bus, g.slot, g.func, 0x14);
    uint32_t bar2_raw = pci_read_config32(g.bus, g.slot, g.func, 0x18);
    klogf(KLOG_WARN, "svga: pci=%02x:%02x.%u dev=0x%x cmd=0x%x cls=0x%x bar0=0x%x bar1=0x%x bar2=0x%x",
          g.bus, g.slot, g.func, id_raw, cmd_raw & 0xFFFFu, cls_raw, bar0_raw, bar1_raw, bar2_raw);
    if (!bar0) {
        klogf(KLOG_WARN, "svga: BAR0 empty (raw=0x%x) at %02x:%02x.%u, GOP fallback",
              bar0_raw, g.bus, g.slot, g.func);
        return false;
    }
    g.io_base = (uint16_t)(bar0 & ~0x3ULL);
    if (!g.io_base) {
        klogf(KLOG_WARN, "svga: IO base 0 (bar0=0x%llx raw=0x%x), GOP fallback",
              (unsigned long long)bar0, bar0_raw);
        return false;
    }
    // version negotiate
    // self-test: same outl/inl wrappers against PCI config (known-good)
    {
        uint32_t expect = pci_read_config32(g.bus, g.slot, g.func, 0x00);
        outl16(0xCF8, (uint32_t)(0x80000000u | ((uint32_t)g.bus << 16) |
                                 ((uint32_t)g.slot << 11) | ((uint32_t)g.func << 8)));
        uint32_t probe = inl16(0xCFC);
        klogf(KLOG_WARN, "svga: io selftest cfc=0x%x expect=0x%x %s",
              probe, expect, probe == expect ? "IO_OK" : "IO_BROKEN");
    }
    uint32_t id = svga_try_ids(g.io_base, true, g.bus, g.slot, g.func);
    if (id == 0xFFFFFFFFu) {
        // IO handler may not track SeaBIOS BAR: relocate BAR0 to a free
        // address we choose ourselves, then retry. Restore on failure.
        uint32_t orig_raw = pci_read_config32(g.bus, g.slot, g.func, 0x10);
        uint64_t iosize = pci_bar_size(g.bus, g.slot, g.func, 0);
        if (iosize < 0x20)
            iosize = 0x20;
        struct svga_io_used used[72];
        int used_n = 0;
        svga_collect_io_ranges(used, &used_n, 72);
        uint64_t cand = 0;
        for (uint64_t a = 0x5000; a + iosize <= 0x10000; a += iosize) {
            bool clash = false;
            for (int i = 0; i < used_n; i++) {
                if (a < used[i].base + used[i].size && used[i].base < a + iosize) {
                    clash = true;
                    break;
                }
            }
            if (!clash) {
                cand = a;
                break;
            }
        }
        if (cand && cand <= 0xFFFFu) {
            struct pci_device_info di2 = {.bus = g.bus, .slot = g.slot, .function = g.func};
            klogf(KLOG_WARN, "svga: retry at relocated IO 0x%x (size 0x%x)",
                  (unsigned)cand, (unsigned)iosize);
            pci_update_command(&di2, 0, PCI_COMMAND_IO_SPACE);
            pci_write_config32(g.bus, g.slot, g.func, 0x10, (uint32_t)cand | 1u);
            pci_update_command(&di2, PCI_COMMAND_IO_SPACE, 0);
            id = svga_try_ids((uint16_t)cand, true, g.bus, g.slot, g.func);
            if (id != 0xFFFFFFFFu) {
                g.io_base = (uint16_t)cand;
                klogf(KLOG_OK, "svga: relocated IO works, id=0x%x", id);
            } else {
                pci_update_command(&di2, 0, PCI_COMMAND_IO_SPACE);
                pci_write_config32(g.bus, g.slot, g.func, 0x10, orig_raw);
                pci_update_command(&di2, PCI_COMMAND_IO_SPACE, 0);
            }
        }
        if (id == 0xFFFFFFFFu) {
            klog(KLOG_WARN, "svga: ID negotiate failed, GOP fallback");
            return false;
        }
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
