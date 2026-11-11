#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"
#include "../../../drivers/input/keyboard.h"

#define BOARD_W 10
#define BOARD_H 20
#define BOARD_HIDDEN 2
#define BOARD_ROWS (BOARD_H + BOARD_HIDDEN)
#define QUEUE_LEN 3
#define LOCK_DELAY_MS 500
#define LOCK_RESETS 15
#define CLEAR_FLASH_MS 220
#define SIDE_CELLS 6

#define C_BG 0x0B0D17U
#define C_BG_GLOW 0x10142AU
#define C_PANEL 0x141830U
#define C_PANEL_EDGE 0x262B4AU
#define C_WELL 0x080A12U
#define C_GRID 0x121628U
#define C_TEXT 0xE8EBFAU
#define C_MUTED 0x8A92B8U
#define C_ACCENT 0x6C8CFFU
#define C_WARN 0xFF6B8BU

enum { P_I = 0, P_O, P_T, P_S, P_Z, P_J, P_L, P_COUNT };

static const uint32_t piece_color[P_COUNT] = {
    0x4DD0E1U, 0xFFD54FU, 0xB388FFU, 0x81C784U, 0xEF6F6FU, 0x5C9DFFU, 0xFFA552U};

static const uint16_t shapes[P_COUNT][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    {0xC600, 0x2640, 0x0C60, 0x4C80},
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    {0x2E00, 0x4460, 0x0E80, 0xC440}};

static const int8_t kicks_jlstz[8][5][2] = {
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}};

static const int8_t kicks_i[8][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}},
    {{0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}}};

static const uint16_t gravity_ms[] = {1000, 793, 618, 473, 355, 262, 190, 135,
                                      94,   64,  43,  28,  18,  11,  7};

#define SFX_LEN(a) (sizeof(a)/sizeof((a)[0]))

struct sfx_note {
    uint16_t freq_hz;
    uint16_t tone_ms;
    uint16_t gap_ms;
};
static const struct sfx_note SFX_MOVE[]={
    {1050,40,0}
};
static const struct sfx_note SFX_CLEAR[]={
    {1767,60,40},
    {2188,80,40},
    {2625,80,40},
    {3538,80,40},
    {1771,80,0}
};
static const struct sfx_note SFX_GAMEOVER[]={
    {260,150,0},
    {400,100,0},
    {300,80,0},
    {250,90,0},
    {220,80,0}
};

static const struct sfx_note *sfx_seq=0;
static uint32_t sfx_len=0;
static uint32_t sfx_idx=0;
static uint32_t sfx_timer=0;
static bool sfx_playing=false;
static int16_t sfx_move_buf[1000];
static uint32_t sfx_move_n;
static int16_t sfx_clear_buf[12200];
static uint32_t sfx_clear_n;
static int16_t sfx_over_buf[11600];
static uint32_t sfx_over_n;
static uint32_t sfx_cool;

static uint32_t sfx_preload(const char *name,int16_t *buffer,uint32_t capacity){
    static const char *dirs[]={"/game/sound/","/bin/sound/"};
    for(uint32_t d=0;d<sizeof(dirs)/sizeof(dirs[0]);d++){
        char path[64];
        uint32_t pos=0;
        const char *dir=dirs[d];
        while(dir[pos] && pos+1<sizeof(path)){
            path[pos]=dir[pos];
            pos++;
        }
        for(uint32_t i=0;name[i] && pos+1<sizeof(path);i++){
            path[pos++]=name[i];
        }
        path[pos]='\0';
        uint32_t frames=0;
        if(pa_wav_load(path,buffer,capacity,&frames)==0 && frames>0){
            return frames;
        }
    }
    return 0;
}

static void sfx_preload_all(void){
    sfx_move_n=sfx_preload("move.wav",sfx_move_buf,
                           sizeof(sfx_move_buf)/sizeof(sfx_move_buf[0]));
    sfx_clear_n=sfx_preload("clear.wav",sfx_clear_buf,
                            sizeof(sfx_clear_buf)/sizeof(sfx_clear_buf[0]));
    sfx_over_n=sfx_preload("over.wav",sfx_over_buf,
                           sizeof(sfx_over_buf)/sizeof(sfx_over_buf[0]));
}

static void sfx_start(const struct sfx_note *seq,uint32_t len){
    if(!seq || len==0) return;
    sfx_seq=seq;
    sfx_len=len;
    sfx_idx=0;
    sfx_timer=seq[0].tone_ms+seq[0].gap_ms;
    sfx_playing=true;
    pa_play_tone(seq[0].freq_hz,seq[0].tone_ms);
}

static void sfx_stop(void){
    sfx_playing=false;
    sfx_seq=0;
    sfx_len=0;
    sfx_idx=0;
    sfx_timer=0;
    sfx_cool=0;
    pa_stop_tone();
    pa_sfx_stop();
}

static void sfx_tick(uint32_t elapsed_ms){
    if(sfx_cool>0) sfx_cool--;
    while(sfx_playing && elapsed_ms>0){
        if(elapsed_ms<sfx_timer){
            sfx_timer-=elapsed_ms;
            break;
        }
        elapsed_ms-=sfx_timer;
        sfx_idx++;
        if(sfx_idx>=sfx_len){
            sfx_playing=false;
            sfx_seq=0;
            break;
        }
        sfx_timer=sfx_seq[sfx_idx].tone_ms+sfx_seq[sfx_idx].gap_ms;
        pa_play_tone(sfx_seq[sfx_idx].freq_hz,sfx_seq[sfx_idx].tone_ms);
    }
}

static void sfx_move_blip(void){
    if(sfx_playing || sfx_cool>0) return;
    if(sfx_move_n>0 && pa_sfx_play(sfx_move_buf,sfx_move_n)==0) return;
    pa_play_tone(SFX_MOVE[0].freq_hz,SFX_MOVE[0].tone_ms);
}

static void sfx_jingle(const int16_t *buffer,uint32_t frames,uint32_t cool,
                       const struct sfx_note *seq,uint32_t len){
    sfx_stop();
    if(frames>0 && pa_sfx_play(buffer,frames)==0){
        sfx_cool=cool;
        return;
    }
    sfx_start(seq,len);
}


struct game {
    int8_t cells[BOARD_ROWS][BOARD_W];
    int32_t type, rot, x, y;
    int32_t queue[QUEUE_LEN];
    int32_t bag[P_COUNT];
    int32_t bag_left;
    int32_t hold;
    bool hold_used;
    uint32_t score, best, lines, level;
    uint32_t combo;
    bool over, paused;
    uint64_t fall_at;
    bool grounded;
    uint64_t lock_at;
    uint32_t lock_resets;
    uint32_t clear_mask;
    uint64_t clear_until;
    uint32_t last_clear;
    uint64_t banner_until;
    const char *banner;
};

static struct game g;
static uint32_t rng_state = 0x9E3779B9U;

static uint64_t now_ms(void) {
    struct time_info info;
    if (pc_time_get(&info) < 0)
        return 0;
    return info.uptime_ms;
}

static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int32_t bag_next(void) {
    if (g.bag_left == 0) {
        for (int32_t i = 0; i < P_COUNT; i++)
            g.bag[i] = i;
        for (int32_t i = P_COUNT - 1; i > 0; i--) {
            int32_t j = (int32_t)(rng() % (uint32_t)(i + 1));
            int32_t t = g.bag[i];
            g.bag[i] = g.bag[j];
            g.bag[j] = t;
        }
        g.bag_left = P_COUNT;
    }
    return g.bag[--g.bag_left];
}

static bool cell_of(int32_t type, int32_t rot, int32_t cx, int32_t cy) {
    return (shapes[type][rot & 3] >> (15 - (cy * 4 + cx))) & 1;
}

static bool fits(int32_t type, int32_t rot, int32_t x, int32_t y) {
    for (int32_t cy = 0; cy < 4; cy++)
        for (int32_t cx = 0; cx < 4; cx++) {
            if (!cell_of(type, rot, cx, cy))
                continue;
            int32_t bx = x + cx, by = y + cy;
            if (bx < 0 || bx >= BOARD_W || by >= BOARD_ROWS)
                return false;
            if (by >= 0 && g.cells[by][bx])
                return false;
        }
    return true;
}

static uint32_t gravity(void) {
    uint32_t i = g.level ? g.level - 1 : 0;
    uint32_t n = sizeof(gravity_ms) / sizeof(gravity_ms[0]);
    return i < n ? gravity_ms[i] : 5;
}

static bool on_ground(void) { return !fits(g.type, g.rot, g.x, g.y + 1); }

static void spawn(int32_t type, uint64_t now) {
    g.type = type;
    g.rot = 0;
    g.x = 3;
    g.y = 1;
    g.grounded = false;
    g.lock_resets = 0;
    g.fall_at = now + gravity();
    if (!fits(g.type, g.rot, g.x, g.y)) {
        g.over = true;
        if (g.score > g.best)
            g.best = g.score;
        sfx_jingle(sfx_over_buf, sfx_over_n, 40, SFX_GAMEOVER,
                   SFX_LEN(SFX_GAMEOVER));
    }
}

static void spawn_next(uint64_t now) {
    int32_t type = g.queue[0];
    for (int32_t i = 0; i + 1 < QUEUE_LEN; i++)
        g.queue[i] = g.queue[i + 1];
    g.queue[QUEUE_LEN - 1] = bag_next();
    g.hold_used = false;
    spawn(type, now);
}

static void reset_game(void) {
    uint32_t best = g.best;
    for (uint32_t i = 0; i < sizeof(g); i++)
        ((uint8_t *)&g)[i] = 0;
    g.best = best;
    g.level = 1;
    g.hold = -1;
    rng_state ^= (uint32_t)now_ms() * 2654435761U;
    if (!rng_state)
        rng_state = 0x9E3779B9U;
    for (int32_t i = 0; i < QUEUE_LEN; i++)
        g.queue[i] = bag_next();
    spawn_next(now_ms());
}

static void touch_lock(uint64_t now) {
    if (!g.grounded)
        return;
    if (g.lock_resets < LOCK_RESETS) {
        g.lock_resets++;
        g.lock_at = now + LOCK_DELAY_MS;
    }
}

static void update_ground(uint64_t now) {
    bool ground = on_ground();
    if (ground && !g.grounded)
        g.lock_at = now + LOCK_DELAY_MS;
    g.grounded = ground;
}

static bool shift(int32_t dx, uint64_t now) {
    if (!fits(g.type, g.rot, g.x + dx, g.y))
        return false;
    g.x += dx;
    touch_lock(now);
    update_ground(now);
    return true;
}

static bool rotate(int32_t dir, uint64_t now) {
    if (g.type == P_O)
        return false;
    int32_t from = g.rot & 3;
    int32_t to = (from + (dir > 0 ? 1 : 3)) & 3;
    int32_t idx = dir > 0 ? from * 2 : ((to * 2) + 1) & 7;
    const int8_t(*table)[5][2] = g.type == P_I ? kicks_i : kicks_jlstz;
    for (int32_t k = 0; k < 5; k++) {
        int32_t dx = table[idx][k][0];
        int32_t dy = -table[idx][k][1];
        if (fits(g.type, to, g.x + dx, g.y + dy)) {
            g.x += dx;
            g.y += dy;
            g.rot = to;
            touch_lock(now);
            update_ground(now);
            return true;
        }
    }
    return false;
}

static int32_t ghost_y(void) {
    int32_t y = g.y;
    while (fits(g.type, g.rot, g.x, y + 1))
        y++;
    return y;
}

static void lock_piece(uint64_t now) {
    bool above = true;
    for (int32_t cy = 0; cy < 4; cy++)
        for (int32_t cx = 0; cx < 4; cx++) {
            if (!cell_of(g.type, g.rot, cx, cy))
                continue;
            int32_t by = g.y + cy, bx = g.x + cx;
            if (by >= 0 && by < BOARD_ROWS)
                g.cells[by][bx] = (int8_t)(g.type + 1);
            if (by >= BOARD_HIDDEN)
                above = false;
        }
    uint32_t mask = 0, count = 0;
    for (int32_t y = 0; y < BOARD_ROWS; y++) {
        bool full = true;
        for (int32_t x = 0; x < BOARD_W; x++)
            if (!g.cells[y][x])
                full = false;
        if (full) {
            mask |= 1U << y;
            count++;
        }
    }
    if (count) {
        static const uint32_t base[5] = {0, 100, 300, 500, 800};
        g.score += base[count] * g.level + 50 * g.combo * g.level;
        g.combo++;
        g.lines += count;
        g.level = 1 + g.lines / 10;
        g.clear_mask = mask;
        g.clear_until = now + CLEAR_FLASH_MS;
        g.last_clear = count;
        static const char *const names[5] = {"", "SINGLE", "DOUBLE", "TRIPLE",
                                             "TETRIS!"};
        g.banner = names[count];
        g.banner_until = now + 1200;
        sfx_jingle(sfx_clear_buf, sfx_clear_n, 25, SFX_CLEAR,
                   SFX_LEN(SFX_CLEAR));
    } else {
        g.combo = 0;
        sfx_move_blip();
    }
    if (g.score > g.best)
        g.best = g.score;
    if (above && !count) {
        g.over = true;
        sfx_jingle(sfx_over_buf, sfx_over_n, 40, SFX_GAMEOVER,
                   SFX_LEN(SFX_GAMEOVER));
        return;
    }
    if (!count)
        spawn_next(now);
}

static void finish_clear(uint64_t now) {
    int32_t dst = BOARD_ROWS - 1;
    for (int32_t y = BOARD_ROWS - 1; y >= 0; y--) {
        if (g.clear_mask & (1U << y))
            continue;
        if (dst != y)
            for (int32_t x = 0; x < BOARD_W; x++)
                g.cells[dst][x] = g.cells[y][x];
        dst--;
    }
    for (; dst >= 0; dst--)
        for (int32_t x = 0; x < BOARD_W; x++)
            g.cells[dst][x] = 0;
    g.clear_mask = 0;
    spawn_next(now);
}

static void hard_drop(uint64_t now) {
    int32_t gy = ghost_y();
    g.score += (uint32_t)(gy - g.y) * 2;
    g.y = gy;
    lock_piece(now);
}

static bool soft_drop(uint64_t now) {
    if (!fits(g.type, g.rot, g.x, g.y + 1))
        return false;
    g.y++;
    g.score++;
    g.fall_at = now + gravity();
    update_ground(now);
    return true;
}

static void hold_piece(uint64_t now) {
    if (g.hold_used)
        return;
    int32_t current = g.type;
    if (g.hold < 0) {
        g.hold = current;
        spawn_next(now);
    } else {
        int32_t t = g.hold;
        g.hold = current;
        spawn(t, now);
    }
    g.hold_used = true;
}

static bool step(uint64_t now) {
    if (g.over || g.paused)
        return false;
    if (g.clear_mask) {
        if (now >= g.clear_until) {
            finish_clear(now);
            return true;
        }
        return false;
    }
    bool changed = false;
    if (g.grounded) {
        if (!on_ground()) {
            g.grounded = false;
            g.fall_at = now + gravity();
        } else if (now >= g.lock_at) {
            lock_piece(now);
            return true;
        }
    }
    uint32_t guard = 0;
    while (!g.grounded && now >= g.fall_at && guard++ < 22) {
        if (fits(g.type, g.rot, g.x, g.y + 1)) {
            g.y++;
            changed = true;
        }
        g.fall_at += gravity();
        update_ground(now);
    }
    if (g.grounded && g.fall_at < now)
        g.fall_at = now + gravity();
    return changed;
}

struct layout {
    uint32_t cell;
    uint32_t bx, by;
    uint32_t lx, rx, side_w;
};

static struct layout layout_of(const struct pg_window *w) {
    struct layout l;
    uint32_t cw = w->client.width, ch = w->client.height;
    uint32_t by_h = ch > 40 ? (ch - 40) / BOARD_H : 8;
    uint32_t total = BOARD_W + 2 * SIDE_CELLS + 2;
    uint32_t by_w = cw > 32 ? (cw - 32) / total : 8;
    l.cell = by_h < by_w ? by_h : by_w;
    if (l.cell < 8)
        l.cell = 8;
    if (l.cell > 40)
        l.cell = 40;
    uint32_t board_w = l.cell * BOARD_W;
    uint32_t board_h = l.cell * BOARD_H;
    l.side_w = l.cell * SIDE_CELLS;
    uint32_t gap = l.cell;
    uint32_t group = board_w + 2 * (l.side_w + gap);
    uint32_t gx = cw > group ? (cw - group) / 2 : 0;
    l.lx = gx;
    l.bx = gx + l.side_w + gap;
    l.rx = l.bx + board_w + gap;
    l.by = ch > board_h ? (ch - board_h) / 2 : 0;
    return l;
}

static uint32_t shade(uint32_t c, int32_t amount) {
    int32_t r = (int32_t)((c >> 16) & 0xFF), gr = (int32_t)((c >> 8) & 0xFF),
            b = (int32_t)(c & 0xFF);
    if (amount >= 0) {
        r += (255 - r) * amount / 100;
        gr += (255 - gr) * amount / 100;
        b += (255 - b) * amount / 100;
    } else {
        r = r * (100 + amount) / 100;
        gr = gr * (100 + amount) / 100;
        b = b * (100 + amount) / 100;
    }
    return ((uint32_t)r << 16) | ((uint32_t)gr << 8) | (uint32_t)b;
}

static void rect(struct pg_window *w, uint32_t x, uint32_t y, uint32_t ww,
                 uint32_t hh, uint32_t color) {
    if (ww && hh)
        pg_window_rect(w, (struct pg_rect){x, y, ww, hh}, color);
}

static void block(struct pg_window *w, uint32_t x, uint32_t y, uint32_t s,
                  uint32_t color) {
    uint32_t e = s >= 16 ? 3 : 2;
    rect(w, x, y, s - 1, s - 1, shade(color, -45));
    rect(w, x, y, s - 1 - e, s - 1 - e, shade(color, 35));
    rect(w, x + e, y + e, s - 1 - 2 * e, s - 1 - 2 * e, color);
    if (s >= 14)
        rect(w, x + e + 1, y + e + 1, (s - 2 * e) / 3, 2, shade(color, 60));
}

static void ghost(struct pg_window *w, uint32_t x, uint32_t y, uint32_t s,
                  uint32_t color) {
    uint32_t c = shade(color, -55);
    rect(w, x + 1, y + 1, s - 3, 2, c);
    rect(w, x + 1, y + s - 4, s - 3, 2, c);
    rect(w, x + 1, y + 1, 2, s - 3, c);
    rect(w, x + s - 4, y + 1, 2, s - 3, c);
}

static void text(struct pg_window *w, uint32_t x, uint32_t y, const char *t,
                 uint32_t color, uint32_t size) {
    if (size <= 8)
        pg_window_text(w, x, y, t, color);
    else
        pg_window_text_sized(w, x, y, t, color, size);
}

static void text_center(struct pg_window *w, uint32_t x, uint32_t width,
                        uint32_t y, const char *t, uint32_t color,
                        uint32_t size) {
    uint32_t tw = pc_strlen(t) * (size <= 8 ? 8 : size);
    text(w, x + (width > tw ? (width - tw) / 2 : 0), y, t, color, size);
}

static void number(char *out, uint32_t v) {
    char tmp[12];
    uint32_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (uint32_t i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void panel(struct pg_window *w, uint32_t x, uint32_t y, uint32_t ww,
                  uint32_t hh, const char *title) {
    rect(w, x, y, ww, hh, C_PANEL_EDGE);
    rect(w, x + 1, y + 1, ww - 2, hh - 2, C_PANEL);
    if (title) {
        rect(w, x + 1, y + 1, ww - 2, 2, C_ACCENT);
        text(w, x + 12, y + 12, title, C_MUTED, 8);
    }
}

static void mini_piece(struct pg_window *w, int32_t type, uint32_t x,
                       uint32_t y, uint32_t box_w, uint32_t box_h, uint32_t s,
                       bool dim) {
    if (type < 0)
        return;
    int32_t minx = 4, maxx = -1, miny = 4, maxy = -1;
    for (int32_t cy = 0; cy < 4; cy++)
        for (int32_t cx = 0; cx < 4; cx++)
            if (cell_of(type, 0, cx, cy)) {
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
            }
    uint32_t pw = (uint32_t)(maxx - minx + 1) * s;
    uint32_t ph = (uint32_t)(maxy - miny + 1) * s;
    uint32_t ox = x + (box_w > pw ? (box_w - pw) / 2 : 0);
    uint32_t oy = y + (box_h > ph ? (box_h - ph) / 2 : 0);
    uint32_t color = dim ? shade(piece_color[type], -55) : piece_color[type];
    for (int32_t cy = miny; cy <= maxy; cy++)
        for (int32_t cx = minx; cx <= maxx; cx++)
            if (cell_of(type, 0, cx, cy))
                block(w, ox + (uint32_t)(cx - minx) * s,
                      oy + (uint32_t)(cy - miny) * s, s, color);
}

static void stat(struct pg_window *w, uint32_t x, uint32_t y, uint32_t width,
                 const char *label, uint32_t value, uint32_t size) {
    char buf[12];
    number(buf, value);
    text(w, x, y, label, C_MUTED, 8);
    text(w, x, y + 14, buf, C_TEXT, size);
    (void)width;
}

static void draw(struct pg_window *w, uint64_t now) {
    struct layout l = layout_of(w);
    uint32_t s = l.cell;
    uint32_t cw = w->client.width, ch = w->client.height;
    pg_window_begin(w);
    rect(w, 0, 0, cw, ch, C_BG);
    rect(w, 0, 0, cw, ch / 3, C_BG_GLOW);

    uint32_t bw = s * BOARD_W, bh = s * BOARD_H;
    rect(w, l.bx - 3, l.by - 3, bw + 6, bh + 6, C_PANEL_EDGE);
    rect(w, l.bx - 1, l.by - 1, bw + 2, bh + 2, C_WELL);
    for (int32_t y = 0; y < BOARD_H; y++)
        for (int32_t x = 0; x < BOARD_W; x++)
            rect(w, l.bx + (uint32_t)x * s + s / 2 - 1,
                 l.by + (uint32_t)y * s + s / 2 - 1, 2, 2, C_GRID);

    bool flash = g.clear_mask && ((now / 55) & 1) == 0;
    for (int32_t y = BOARD_HIDDEN; y < BOARD_ROWS; y++)
        for (int32_t x = 0; x < BOARD_W; x++) {
            int32_t v = g.cells[y][x];
            if (!v)
                continue;
            uint32_t px = l.bx + (uint32_t)x * s;
            uint32_t py = l.by + (uint32_t)(y - BOARD_HIDDEN) * s;
            if (g.clear_mask & (1U << y))
                block(w, px, py, s, flash ? 0xFFFFFFU : shade(piece_color[v - 1], 40));
            else
                block(w, px, py, s,
                      g.over ? shade(piece_color[v - 1], -50) : piece_color[v - 1]);
        }

    if (!g.over && !g.clear_mask) {
        int32_t gy = ghost_y();
        for (int32_t cy = 0; cy < 4; cy++)
            for (int32_t cx = 0; cx < 4; cx++) {
                if (!cell_of(g.type, g.rot, cx, cy))
                    continue;
                int32_t by = gy + cy - BOARD_HIDDEN;
                if (by >= 0 && gy != g.y)
                    ghost(w, l.bx + (uint32_t)(g.x + cx) * s, l.by + (uint32_t)by * s,
                          s, piece_color[g.type]);
            }
        for (int32_t cy = 0; cy < 4; cy++)
            for (int32_t cx = 0; cx < 4; cx++) {
                if (!cell_of(g.type, g.rot, cx, cy))
                    continue;
                int32_t by = g.y + cy - BOARD_HIDDEN;
                if (by >= 0)
                    block(w, l.bx + (uint32_t)(g.x + cx) * s, l.by + (uint32_t)by * s,
                          s, piece_color[g.type]);
            }
    }

    uint32_t mini = s * 2 / 3;
    if (mini < 8)
        mini = 8;
    uint32_t big = s >= 28 ? 24 : 16;

    uint32_t hold_h = s * 4;
    panel(w, l.lx, l.by, l.side_w, hold_h, "HOLD");
    mini_piece(w, g.hold, l.lx, l.by + 24, l.side_w, hold_h - 28, mini,
               g.hold_used);

    uint32_t sy = l.by + hold_h + s / 2;
    uint32_t stats_h = bh > hold_h + s / 2 ? bh - hold_h - s / 2 : s * 8;
    panel(w, l.lx, sy, l.side_w, stats_h, 0);
    uint32_t row = (big + 30);
    uint32_t tx = l.lx + 14;
    stat(w, tx, sy + 14, l.side_w, "SCORE", g.score, big);
    stat(w, tx, sy + 14 + row, l.side_w, "BEST", g.best, 16);
    stat(w, tx, sy + 14 + row * 2, l.side_w, "LEVEL", g.level, big);
    stat(w, tx, sy + 14 + row * 3, l.side_w, "LINES", g.lines, big);
    if (sy + 14 + row * 4 + 24 < sy + stats_h) {
        text(w, tx, sy + 14 + row * 4, "SOUND", C_MUTED, 8);
        text(w, tx, sy + 28 + row * 4, pa_is_muted() ? "off  (M)" : "on   (M)",
             C_TEXT, 8);
    }

    uint32_t next_h = s * 3 * QUEUE_LEN + 30;
    if (next_h > bh / 2 + s * 3)
        next_h = bh / 2 + s * 3;
    panel(w, l.rx, l.by, l.side_w, next_h, "NEXT");
    uint32_t slot = (next_h - 30) / QUEUE_LEN;
    for (int32_t i = 0; i < QUEUE_LEN; i++)
        mini_piece(w, g.queue[i], l.rx, l.by + 26 + (uint32_t)i * slot, l.side_w,
                   slot, i == 0 ? mini : mini * 3 / 4, false);

    uint32_t ky = l.by + next_h + s / 2;
    uint32_t keys_h = bh > next_h + s / 2 ? bh - next_h - s / 2 : 0;
    if (keys_h > 60) {
        panel(w, l.rx, ky, l.side_w, keys_h, "KEYS");
        static const char *const help[][2] = {
            {"<- ->", "move"},  {"Up / X", "rotate"}, {"Z", "rotate back"},
            {"Down", "soft drop"}, {"Space", "hard drop"}, {"C", "hold"},
            {"P / Esc", "pause"}, {"R", "restart"},     {"Q", "quit"}};
        uint32_t yy = ky + 30;
        for (uint32_t i = 0; i < sizeof(help) / sizeof(help[0]); i++) {
            if (yy + 12 > ky + keys_h)
                break;
            text(w, l.rx + 12, yy, help[i][0], C_TEXT, 8);
            if (l.side_w > 150)
                text(w, l.rx + 12 + 72, yy, help[i][1], C_MUTED, 8);
            yy += 16;
        }
    }

    if (g.banner && now < g.banner_until && !g.over) {
        uint32_t size = g.last_clear == 4 ? (s >= 24 ? 24 : 16) : 16;
        text_center(w, l.bx, bw, l.by + bh / 3, g.banner,
                    g.last_clear == 4 ? piece_color[P_I] : C_TEXT, size);
    }
    if (g.paused || g.over) {
        uint32_t oh = 120;
        uint32_t oy = l.by + (bh - oh) / 2;
        rect(w, l.bx, oy, bw, oh, 0x05060CU);
        rect(w, l.bx, oy, bw, 2, g.over ? C_WARN : C_ACCENT);
        rect(w, l.bx, oy + oh - 2, bw, 2, g.over ? C_WARN : C_ACCENT);
        text_center(w, l.bx, bw, oy + 26, g.over ? "GAME OVER" : "PAUSED",
                    g.over ? C_WARN : C_TEXT, s >= 24 ? 24 : 16);
        if (g.over) {
            char line[32] = "Score ";
            number(line + 6, g.score);
            text_center(w, l.bx, bw, oy + 64, line, C_TEXT, 8);
            text_center(w, l.bx, bw, oy + 86, "R - play again", C_MUTED, 8);
        } else {
            text_center(w, l.bx, bw, oy + 70, "P to continue", C_MUTED, 8);
        }
    }
    pg_window_end(w);
}

static bool handle_key(struct pg_window *w, const struct pg_event *e,
                       uint64_t now) {
    if (e->type == PG_EVENT_KEY) {
        int32_t k = e->key;
        if (k == 'q' || k == 'Q') {
            pg_window_close(w);
            return false;
        }
        if (k == 'r' || k == 'R') {
            reset_game();
            return true;
        }
        if (k == 'm' || k == 'M') {
            pa_toggle_mute();
            return true;
        }
        if (g.over)
            return false;
        if (k == 'p' || k == 'P' || k == 27) {
            g.paused = !g.paused;
            if (!g.paused) {
                g.fall_at = now + gravity();
                if (g.grounded)
                    g.lock_at = now + LOCK_DELAY_MS;
            }
            return true;
        }
        if (g.paused || g.clear_mask)
            return false;
        switch (k) {
        case 'a': case 'A':
            return shift(-1, now);
        case 'd': case 'D':
            return shift(1, now);
        case 's': case 'S':
            return soft_drop(now);
        case 'w': case 'W': case 'x': case 'X':
            return rotate(1, now);
        case 'z': case 'Z':
            return rotate(-1, now);
        case 'c': case 'C':
            hold_piece(now);
            return true;
        case ' ':
            hard_drop(now);
            return true;
        default:
            return false;
        }
    }
    if (e->type == PG_EVENT_SPECIAL_KEY) {
        if (g.over || g.paused || g.clear_mask)
            return false;
        switch (e->key) {
        case KEYBOARD_SPECIAL_LEFT:
            return shift(-1, now);
        case KEYBOARD_SPECIAL_RIGHT:
            return shift(1, now);
        case KEYBOARD_SPECIAL_DOWN:
            return soft_drop(now);
        case KEYBOARD_SPECIAL_UP:
            return rotate(1, now);
        default:
            return false;
        }
    }
    return false;
}

static int tetris_main(void) {
    struct pc_display_info info;
    if (!pc_display_get_info(&info) || !info.width || !info.height)
        return 1;
    uint32_t cell = info.height > 200 ? (info.height - 200) / BOARD_H : 16;
    if (cell > 32)
        cell = 32;
    if (cell < 14)
        cell = 14;
    uint32_t win_w = cell * (BOARD_W + 2 * SIDE_CELLS + 2) + 48;
    uint32_t win_h = cell * BOARD_H + 80;
    struct pg_window window;
    bool opened = false;
    for (int32_t attempt = 0; attempt < 8 && !opened; attempt++) {
        if (win_w > info.width)
            win_w = info.width;
        if (win_h > info.height - 40)
            win_h = info.height - 40;
        opened = pg_window_center(&window, "Tetris", win_w, win_h);
        if (!opened) {
            win_w = win_w * 7 / 8;
            win_h = win_h * 7 / 8;
        }
    }
    if (!opened)
        return 1;
    pg_window_set_esc_closes(&window, false);
    sfx_preload_all();
    reset_game();
    uint64_t last = now_ms();
    draw(&window, last);
    bool banner_shown = false;
    while (pg_window_is_open(&window)) {
        struct pg_event event;
        bool dirty = false;
        uint64_t now = now_ms();
        uint32_t polled = 0;
        while (polled++ < 16 && pg_window_poll_event(&window, &event)) {
            if (event.type == PG_EVENT_CLOSE) {
                pg_window_close(&window);
                break;
            }
            if (event.type == PG_EVENT_REPAINT || event.type == PG_EVENT_MOVE ||
                event.type == PG_EVENT_FOCUS)
                dirty = true;
            else if (handle_key(&window, &event, now))
                dirty = true;
        }
        if (!pg_window_is_open(&window))
            break;
        pa_update();
        sfx_tick((uint32_t)(now - last));
        last = now;
        if (!pg_window_is_minimized(&window)) {
            if (step(now))
                dirty = true;
            if (g.clear_mask)
                dirty = true;
            bool banner = g.banner && now < g.banner_until;
            if (banner != banner_shown) {
                banner_shown = banner;
                dirty = true;
            }
            if (dirty)
                draw(&window, now);
        }
        pc_sleep(8);
    }
    sfx_stop();
    pg_window_close(&window);
    return 0;
}

void _start(void) { pc_exit(tetris_main()); }
