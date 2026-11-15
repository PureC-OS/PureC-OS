#include "vtcon.h"
#include "../../drivers/display/console_vt.h"
#include "../../drivers/display/display_owner.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/input/keyboard.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../ipc/pty.h"
#include "../process/scheduler.h"
#include "../sync/mutex.h"
#include "../sync/spinlock.h"
#include "../diagnostics/klog.h"

#define CELL_W 8u
#define CELL_H 16u
#define INPUT_RING 1024u
#define KLOG_RING 16384u
#define REPLY_MAX 64u
#define FLUSH_ROWS 64u
#define DEFAULT_FG 7u

extern const uint8_t vt_font_8x16[256][16];

static const uint32_t palette[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF};

struct vtcon {
    struct vt_term *term;
    uint64_t term_phys;
    uint64_t term_pages;
    int32_t pty;
    int32_t index;
    bool app_cursor;
    char reply[REPLY_MAX];
    uint32_t reply_len;
    volatile uint32_t draining;
    volatile uint32_t pending;
};

static struct vtcon cons[VTCON_COUNT];
static spinlock_t render_lock = SPINLOCK_INIT;
static mutex_t switch_mutex;
static volatile int32_t active;
static volatile bool ready;
static volatile bool thread_running;
static volatile bool gui_running;
static volatile bool gui_starting;
static volatile bool quiet;
static volatile int32_t pending_switch;
static void (*gui_starter)(void);

static uint32_t *shadow;
static uint64_t shadow_phys;
static uint64_t shadow_pages;
static uint32_t screen_w, screen_h;
static uint32_t scale = 1;
static uint32_t cell_w = CELL_W, cell_h = CELL_H;
static uint32_t columns, rows;
static uint32_t generation;
static bool dirty_valid;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static uint8_t input_ring[INPUT_RING];
static volatile uint32_t input_head, input_tail;
static char klog_ring[KLOG_RING];
static volatile uint32_t klog_head, klog_tail;

static bool try_lock(spinlock_t *lock, uint64_t *flags)
{
    *flags = irq_save();
    if (!__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE))
        return true;
    irq_restore(*flags);
    return false;
}

static void mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h)
        return;
    uint32_t x1 = x + w, y1 = y + h;
    if (!dirty_valid) {
        dirty_x0 = x;
        dirty_y0 = y;
        dirty_x1 = x1;
        dirty_y1 = y1;
        dirty_valid = true;
        return;
    }
    if (x < dirty_x0)
        dirty_x0 = x;
    if (y < dirty_y0)
        dirty_y0 = y;
    if (x1 > dirty_x1)
        dirty_x1 = x1;
    if (y1 > dirty_y1)
        dirty_y1 = y1;
}

static bool visible(const struct vtcon *c)
{
    return shadow && c->index == active;
}

static void colors(uint16_t attr, uint32_t *fg, uint32_t *bg)
{
    uint32_t f = DEFAULT_FG, b = 0;
    if (attr & VT_ATTR_FG_SET)
        f = attr & VT_ATTR_FG_MASK;
    if ((attr & VT_ATTR_BOLD) && f < 8)
        f |= 8;
    if (attr & VT_ATTR_BG_SET)
        b = (attr & VT_ATTR_BG_MASK) >> VT_ATTR_BG_SHIFT;
    *fg = palette[f & 15];
    *bg = palette[b & 15];
    if (attr & VT_ATTR_REVERSE) {
        uint32_t t = *fg;
        *fg = *bg;
        *bg = t;
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (x >= screen_w || y >= screen_h)
        return;
    if (w > screen_w - x)
        w = screen_w - x;
    if (h > screen_h - y)
        h = screen_h - y;
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *line = &shadow[(uint64_t)(y + row) * screen_w + x];
        for (uint32_t i = 0; i < w; i++)
            line[i] = color;
    }
    mark_dirty(x, y, w, h);
}

static void op_draw_cell(void *ctx, uint32_t row, uint32_t column, char character, uint16_t attr, bool cursor)
{
    struct vtcon *c = ctx;
    if (!visible(c))
        return;
    uint32_t x = column * cell_w, y = row * cell_h;
    if (x + cell_w > screen_w || y + cell_h > screen_h)
        return;
    uint32_t fg, bg;
    colors(attr, &fg, &bg);
    const uint8_t *glyph = vt_font_8x16[(uint8_t)character];
    for (uint32_t gy = 0; gy < CELL_H; gy++) {
        uint8_t bits = glyph[gy];
        if ((attr & VT_ATTR_UNDERLINE) && gy == CELL_H - 2)
            bits = 0xFF;
        if (cursor && gy >= CELL_H - 2)
            bits = 0xFF;
        for (uint32_t sy = 0; sy < scale; sy++) {
            uint32_t *line = &shadow[(uint64_t)(y + gy * scale + sy) * screen_w + x];
            for (uint32_t gx = 0; gx < CELL_W; gx++) {
                uint32_t color = (bits & (0x80u >> gx)) ? fg : bg;
                for (uint32_t sx = 0; sx < scale; sx++)
                    line[gx * scale + sx] = color;
            }
        }
    }
    mark_dirty(x, y, cell_w, cell_h);
}

static void op_fill(void *ctx, uint32_t row, uint32_t column, uint32_t count, uint16_t attr)
{
    struct vtcon *c = ctx;
    if (!visible(c))
        return;
    uint32_t fg, bg;
    colors(attr, &fg, &bg);
    fill_rect(column * cell_w, row * cell_h, count * cell_w, cell_h, bg);
}

static bool op_scroll(void *ctx, uint32_t top, uint32_t bottom, uint32_t count, bool up)
{
    struct vtcon *c = ctx;
    if (!visible(c))
        return true;
    uint32_t y0 = top * cell_h, y1 = (bottom + 1) * cell_h, shift = count * cell_h;
    if (y1 > screen_h || y0 >= y1 || shift >= y1 - y0)
        return false;
    uint64_t span = (uint64_t)(y1 - y0 - shift) * screen_w * sizeof(uint32_t);
    if (up)
        memmove(&shadow[(uint64_t)y0 * screen_w], &shadow[(uint64_t)(y0 + shift) * screen_w], span);
    else
        memmove(&shadow[(uint64_t)(y0 + shift) * screen_w], &shadow[(uint64_t)y0 * screen_w], span);
    mark_dirty(0, y0, screen_w, y1 - y0);
    return true;
}

static void op_reply(void *ctx, const char *text)
{
    struct vtcon *c = ctx;
    while (*text && c->reply_len < REPLY_MAX)
        c->reply[c->reply_len++] = *text++;
}

static void op_mode(void *ctx, uint32_t mode, bool enable)
{
    struct vtcon *c = ctx;
    if (mode == 1)
        c->app_cursor = enable;
}

static const struct vt_ops ops = {
    .draw_cell = op_draw_cell,
    .fill = op_fill,
    .scroll = op_scroll,
    .reply = op_reply,
    .mode = op_mode,
};

static void repaint_locked(void)
{
    if (!shadow || active < 0 || active >= VTCON_COUNT)
        return;
    for (uint64_t i = 0; i < (uint64_t)screen_w * screen_h; i++)
        shadow[i] = 0;
    mark_dirty(0, 0, screen_w, screen_h);
    vt_redraw(cons[active].term);
}

static void flush(void)
{
    uint64_t flags = spin_lock_irqsave(&render_lock);
    if (!dirty_valid || !shadow || active >= VTCON_COUNT || gop_user_owned()) {
        spin_unlock_irqrestore(&render_lock, flags);
        return;
    }
    uint32_t x0 = dirty_x0, y0 = dirty_y0, x1 = dirty_x1, y1 = dirty_y1;
    uint32_t gen = generation;
    dirty_valid = false;
    spin_unlock_irqrestore(&render_lock, flags);
    if (x1 > screen_w)
        x1 = screen_w;
    if (y1 > screen_h)
        y1 = screen_h;
    for (uint32_t y = y0; y < y1; y += FLUSH_ROWS) {
        uint32_t h = y1 - y < FLUSH_ROWS ? y1 - y : FLUSH_ROWS;
        flags = spin_lock_irqsave(&render_lock);
        bool ok = gen == generation && shadow && active < VTCON_COUNT;
        if (ok)
            gop_vt_blit(x0, y, x1 - x0, h, shadow, screen_w);
        spin_unlock_irqrestore(&render_lock, flags);
        if (!ok)
            return;
    }
}

static void send_replies(struct vtcon *c)
{
    char buf[REPLY_MAX];
    uint64_t flags = spin_lock_irqsave(&render_lock);
    uint32_t n = c->reply_len;
    memcpy(buf, c->reply, n);
    c->reply_len = 0;
    spin_unlock_irqrestore(&render_lock, flags);
    if (n)
        (void)pty_write(c->pty, true, buf, n, true);
}

static void render(struct vtcon *c, const char *data, uint32_t count, uint32_t vt_flags)
{
    uint64_t flags = spin_lock_irqsave(&render_lock);
    vt_write(c->term, data, count, vt_flags);
    spin_unlock_irqrestore(&render_lock, flags);
}

static void drain(int32_t pty_id)
{
    int32_t index = vtcon_index_of_pty(pty_id);
    if (index < 0)
        return;
    struct vtcon *c = &cons[index];
    __atomic_store_n(&c->pending, 1, __ATOMIC_RELEASE);
    if (__atomic_exchange_n(&c->draining, 1, __ATOMIC_ACQUIRE))
        return;
    for (;;) {
        __atomic_store_n(&c->pending, 0, __ATOMIC_RELEASE);
        char buf[256];
        for (;;) {
            int32_t n = pty_read(c->pty, true, buf, sizeof(buf), true);
            if (n <= 0)
                break;
            render(c, buf, (uint32_t)n, 0);
        }
        __atomic_store_n(&c->draining, 0, __ATOMIC_RELEASE);
        if (!__atomic_load_n(&c->pending, __ATOMIC_ACQUIRE))
            break;
        if (__atomic_exchange_n(&c->draining, 1, __ATOMIC_ACQUIRE))
            break;
    }
    send_replies(c);
    if (index == active)
        flush();
}

static void drain_klog_locked(void)
{
    char buf[256];
    for (;;) {
        uint32_t tail = klog_tail;
        uint32_t head = __atomic_load_n(&klog_head, __ATOMIC_ACQUIRE);
        if (tail == head)
            break;
        uint32_t n = 0;
        while (tail != head && n < sizeof(buf)) {
            buf[n++] = klog_ring[tail % KLOG_RING];
            tail++;
        }
        __atomic_store_n(&klog_tail, tail, __ATOMIC_RELEASE);
        vt_write(cons[0].term, buf, n, VT_FLAG_NL_CR);
    }
}

static void drain_klog(void)
{
    uint64_t flags = spin_lock_irqsave(&render_lock);
    drain_klog_locked();
    spin_unlock_irqrestore(&render_lock, flags);
}

static bool compute_geometry(uint32_t *w, uint32_t *h, uint32_t *s, uint32_t *cols, uint32_t *rws)
{
    gop_vt_geometry(w, h);
    if (*w < CELL_W * 20 || *h < CELL_H * 5)
        return false;
    *s = (*w >= 2560 && *h >= 1440) ? 2 : 1;
    *cols = *w / (CELL_W * *s);
    *rws = *h / (CELL_H * *s);
    if (*cols > VT_MAX_COLUMNS)
        *cols = VT_MAX_COLUMNS;
    if (*rws > VT_MAX_ROWS)
        *rws = VT_MAX_ROWS;
    return true;
}

static bool alloc_shadow(uint32_t w, uint32_t h, uint32_t **out, uint64_t *phys, uint64_t *pages)
{
    *pages = ((uint64_t)w * h * sizeof(uint32_t) + 4095) / 4096;
    *phys = pmm_allocate_contiguous(*pages);
    if (!*phys)
        return false;
    *out = pmm_physical_to_virtual(*phys);
    memset(*out, 0, (uint64_t)w * h * sizeof(uint32_t));
    return true;
}

static void replay_boot_log(void)
{
    char buf[512];
    uint64_t pos = 0;
    for (;;) {
        uint32_t n = klog_read_ring(pos, buf, sizeof(buf));
        if (!n)
            break;
        pos += n;
        for (uint32_t i = 0; i < n; i++)
            vtcon_klog_putc(buf[i]);
    }
}

void vtcon_init(void)
{
    if (ready)
        return;
    uint32_t w, h, s, cols, rws;
    if (!compute_geometry(&w, &h, &s, &cols, &rws))
        return;
    uint32_t *buf;
    uint64_t phys, pages;
    if (!alloc_shadow(w, h, &buf, &phys, &pages))
        return;
    uint64_t term_pages = (sizeof(struct vt_term) + 4095) / 4096;
    for (int32_t i = 0; i < VTCON_COUNT; i++) {
        struct vtcon *c = &cons[i];
        memset(c, 0, sizeof(*c));
        c->index = i;
        c->term_pages = term_pages;
        c->term_phys = pmm_allocate_contiguous(term_pages);
        if (!c->term_phys) {
            klog(KLOG_ERROR, "vtcon: out of memory for consoles");
            return;
        }
        c->term = pmm_physical_to_virtual(c->term_phys);
        memset(c->term, 0, sizeof(struct vt_term));
        if (pty_create_kernel(&c->pty, drain, (uint16_t)cols, (uint16_t)rws) < 0) {
            klog(KLOG_ERROR, "vtcon: cannot create tty pty");
            return;
        }
    }
    uint64_t flags = spin_lock_irqsave(&render_lock);
    shadow = buf;
    shadow_phys = phys;
    shadow_pages = pages;
    screen_w = w;
    screen_h = h;
    scale = s;
    cell_w = CELL_W * s;
    cell_h = CELL_H * s;
    columns = cols;
    rows = rws;
    active = 0;
    for (int32_t i = 0; i < VTCON_COUNT; i++) {
        vt_init(cons[i].term, &ops, &cons[i]);
        vt_resize(cons[i].term, cols, rws);
    }
    repaint_locked();
    spin_unlock_irqrestore(&render_lock, flags);
    gop_set_vt_owned(true);
    keyboard_init();
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
    replay_boot_log();
    drain_klog();
    flush();
    klogf(KLOG_OK, "vtcon: %u consoles %ux%u (%ux%u px, font 8x16%s)", VTCON_COUNT, cols, rws, w, h,
          s > 1 ? " x2" : "");
}

bool vtcon_ready(void)
{
    return __atomic_load_n(&ready, __ATOMIC_ACQUIRE);
}

void vtcon_display_changed(void)
{
    if (!vtcon_ready())
        return;
    uint32_t w, h, s, cols, rws;
    if (!compute_geometry(&w, &h, &s, &cols, &rws))
        return;
    uint32_t *buf = 0;
    uint64_t phys = 0, pages = 0;
    if (w != screen_w || h != screen_h) {
        if (!alloc_shadow(w, h, &buf, &phys, &pages))
            return;
    }
    uint64_t old_phys = 0, old_pages = 0;
    uint64_t flags = spin_lock_irqsave(&render_lock);
    if (buf) {
        old_phys = shadow_phys;
        old_pages = shadow_pages;
        shadow = buf;
        shadow_phys = phys;
        shadow_pages = pages;
    }
    screen_w = w;
    screen_h = h;
    scale = s;
    cell_w = CELL_W * s;
    cell_h = CELL_H * s;
    columns = cols;
    rows = rws;
    generation++;
    dirty_valid = false;
    int32_t saved = active;
    active = -1;
    for (int32_t i = 0; i < VTCON_COUNT; i++)
        vt_resize(cons[i].term, cols, rws);
    active = saved;
    repaint_locked();
    spin_unlock_irqrestore(&render_lock, flags);
    if (old_phys)
        pmm_free_contiguous(old_phys, old_pages);
    for (int32_t i = 0; i < VTCON_COUNT; i++)
        pty_set_kernel_winsize(cons[i].pty, (uint16_t)cols, (uint16_t)rws);
    if (active < VTCON_COUNT)
        gop_set_vt_owned(true);
    flush();
}

static int32_t do_switch(int32_t index)
{
    if (index < 0 || index > VTCON_GRAPHICS)
        return -1;
    int32_t rc = 0;
    mutex_lock(&switch_mutex);
    if (index == active) {
        mutex_unlock(&switch_mutex);
        return 0;
    }
    if (index == VTCON_GRAPHICS) {
        if (!gui_running) {
            rc = -1;
        } else {
            uint64_t flags = spin_lock_irqsave(&render_lock);
            active = VTCON_GRAPHICS;
            spin_unlock_irqrestore(&render_lock, flags);
            keyboard_flush_legacy();
            gop_set_vt_owned(false);
        }
    } else if (display_owner_active()) {
        rc = -1;
    } else {
        if (active == VTCON_GRAPHICS)
            gop_set_vt_owned(true);
        uint64_t flags = spin_lock_irqsave(&render_lock);
        active = index;
        repaint_locked();
        spin_unlock_irqrestore(&render_lock, flags);
        flush();
    }
    mutex_unlock(&switch_mutex);
    return rc;
}

int32_t vtcon_activate(int32_t index)
{
    if (!vtcon_ready())
        return -1;
    return do_switch(index);
}

int32_t vtcon_active(void)
{
    return vtcon_ready() ? active : -1;
}

int32_t vtcon_pty(int32_t index)
{
    if (!vtcon_ready() || index < 0 || index >= VTCON_COUNT)
        return -1;
    return cons[index].pty;
}

int32_t vtcon_index_of_pty(int32_t pty_id)
{
    if (!vtcon_ready() && !cons[0].term)
        return -1;
    for (int32_t i = 0; i < VTCON_COUNT; i++)
        if (cons[i].term && cons[i].pty == pty_id)
            return i;
    return -1;
}

int32_t vtcon_open_index(const char *path)
{
    if (!vtcon_ready() || !path)
        return -2;
    if (strcmp(path, "/dev/console") == 0)
        return 0;
    if (strcmp(path, "/dev/tty0") == 0)
        return active < VTCON_COUNT ? active : 0;
    if (strncmp(path, "/dev/tty", 8) != 0)
        return -2;
    const char *n = path + 8;
    if (n[0] < '1' || n[0] > '9' || n[1])
        return -2;
    int32_t index = n[0] - '1';
    return index < VTCON_COUNT ? index : -1;
}

bool vtcon_keyboard_captured(void)
{
    return vtcon_ready() && active >= 0 && active < VTCON_COUNT;
}

static void input_put(uint8_t byte)
{
    uint32_t head = input_head;
    if (head - __atomic_load_n(&input_tail, __ATOMIC_ACQUIRE) >= INPUT_RING)
        return;
    input_ring[head % INPUT_RING] = byte;
    __atomic_store_n(&input_head, head + 1, __ATOMIC_RELEASE);
}

static void input_puts(const char *text)
{
    while (*text)
        input_put((uint8_t)*text++);
}

void vtcon_key_char(char character, bool alt)
{
    if (character == '\b')
        character = 127;
    else if (character == '\n')
        character = '\r';
    if (alt)
        input_put(27);
    input_put((uint8_t)character);
}

void vtcon_key_special(uint8_t key)
{
    bool app = active >= 0 && active < VTCON_COUNT && cons[active].app_cursor;
    switch (key) {
    case KEYBOARD_SPECIAL_F1: input_puts("\033OP"); break;
    case KEYBOARD_SPECIAL_F2: input_puts("\033OQ"); break;
    case KEYBOARD_SPECIAL_F3: input_puts("\033OR"); break;
    case KEYBOARD_SPECIAL_HOME: input_puts(app ? "\033OH" : "\033[H"); break;
    case KEYBOARD_SPECIAL_END: input_puts(app ? "\033OF" : "\033[F"); break;
    case KEYBOARD_SPECIAL_PAGE_UP: input_puts("\033[5~"); break;
    case KEYBOARD_SPECIAL_PAGE_DOWN: input_puts("\033[6~"); break;
    case KEYBOARD_SPECIAL_LEFT: input_puts(app ? "\033OD" : "\033[D"); break;
    case KEYBOARD_SPECIAL_RIGHT: input_puts(app ? "\033OC" : "\033[C"); break;
    case KEYBOARD_SPECIAL_UP: input_puts(app ? "\033OA" : "\033[A"); break;
    case KEYBOARD_SPECIAL_DOWN: input_puts(app ? "\033OB" : "\033[B"); break;
    case KEYBOARD_SPECIAL_DELETE: input_puts("\033[3~"); break;
    default: break;
    }
}

void vtcon_key_sequence(const char *text)
{
    input_puts(text);
}

bool vtcon_switch_combo(uint8_t function_number, bool ctrl, bool alt)
{
    if (!vtcon_ready() || !alt || function_number < 1 || function_number > VTCON_COUNT + 1)
        return false;
    if (!ctrl && !vtcon_keyboard_captured())
        return false;
    __atomic_store_n(&pending_switch, (int32_t)function_number, __ATOMIC_RELEASE);
    return true;
}

static void pump_input(void)
{
    uint8_t buf[128];
    for (;;) {
        uint32_t tail = input_tail;
        uint32_t head = __atomic_load_n(&input_head, __ATOMIC_ACQUIRE);
        if (tail == head)
            return;
        uint32_t n = 0;
        while (tail != head && n < sizeof(buf))
            buf[n++] = input_ring[tail++ % INPUT_RING];
        __atomic_store_n(&input_tail, tail, __ATOMIC_RELEASE);
        int32_t target = active;
        if (target >= 0 && target < VTCON_COUNT)
            (void)pty_write(cons[target].pty, true, buf, n, true);
    }
}

static void vt_thread(void *arg)
{
    (void)arg;
    __atomic_store_n(&thread_running, true, __ATOMIC_RELEASE);
    for (;;) {
        if (vtcon_keyboard_captured())
            keyboard_poll();
        pump_input();
        int32_t request = __atomic_exchange_n(&pending_switch, 0, __ATOMIC_ACQ_REL);
        if (request)
            (void)do_switch(request - 1);
        drain_klog();
        flush();
        scheduler_sleep(10);
    }
}

void vtcon_start_thread(void)
{
    if (!vtcon_ready())
        return;
    int tid = scheduler_create_thread(vt_thread, 0, "vtcon", 1, -1);
    if (tid < 0)
        klog(KLOG_ERROR, "vtcon: cannot start console thread");
}

void vtcon_console_write(const char *text, uint32_t count)
{
    if (!vtcon_ready())
        return;
    (void)pty_write(cons[0].pty, false, text, count, true);
}

void vtcon_set_quiet(bool value)
{
    quiet = value;
}

bool vtcon_klog_wants(int level)
{
    if (!vtcon_ready())
        return false;
    if (!quiet)
        return level != KLOG_DEBUG;
    return level == KLOG_WARN || level == KLOG_ERROR;
}

void vtcon_klog_putc(char character)
{
    if (!vtcon_ready())
        return;
    uint32_t head = klog_head;
    if (head - __atomic_load_n(&klog_tail, __ATOMIC_ACQUIRE) >= KLOG_RING)
        return;
    klog_ring[head % KLOG_RING] = character;
    __atomic_store_n(&klog_head, head + 1, __ATOMIC_RELEASE);
    if (character != '\n' || thread_running)
        return;
    uint64_t flags;
    if (!try_lock(&render_lock, &flags))
        return;
    drain_klog_locked();
    spin_unlock_irqrestore(&render_lock, flags);
    flush();
}

void vtcon_set_gui_starter(void (*starter)(void))
{
    gui_starter = starter;
}

bool vtcon_gui_running(void)
{
    return gui_running;
}

static void gui_thread(void *arg)
{
    (void)arg;
    gui_starter();
    gui_running = true;
    gui_starting = false;
    (void)do_switch(VTCON_GRAPHICS);
}

int32_t vtcon_gui_start(void)
{
    if (gui_running)
        return do_switch(VTCON_GRAPHICS);
    if (!gui_starter || !gop_is_available())
        return -1;
    if (__atomic_exchange_n(&gui_starting, true, __ATOMIC_ACQ_REL))
        return 0;
    if (scheduler_create_thread(gui_thread, 0, "gui-start", 1, -1) < 0) {
        gui_starting = false;
        return -1;
    }
    return 0;
}
