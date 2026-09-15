/*
 * game2048.c — Игра «2048» для PureC OS
 *
 * Написано в том же стиле, что и tetris/main.c:
 *   - puregui / pguiw для окна и событий
 *   - pureaudio для звуков
 *   - purec stdlib для системных вызовов
 *
 * Управление:
 *   Стрелки / WASD — сдвиг плиток
 *   R              — новая игра
 *   Q              — выход
 *
 * Сборка (аналогично тетрису):
 *   x86_64-elf-gcc -O2 -ffreestanding -nostdlib \
 *       -I src/libgui/include -I src/libc/include -I src/libaudio/include \
 *       -I src/drivers/input \
 *       -o game2048.elf src/programs/game/2048/main.c \
 *       -lgui -laudio -lpurec
 */

#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"
#include "../../../drivers/input/keyboard.h"

/* ─────────────────────────────────────────
   Параметры игры
───────────────────────────────────────── */
#define GRID_N   4          /* 4×4 поле */
#define TARGET   2048       /* цель */

/* ─────────────────────────────────────────
   Цветовая схема (Catppuccin Mocha)
───────────────────────────────────────── */
#define C_BG      0x1E1E2E
#define C_BOARD   0x181825
#define C_BORDER  0x45475A
#define C_EMPTY   0x313244
#define C_TEXT_HI 0xCDD6F4
#define C_TEXT_LO 0x1E1E2E

/* Цвета плиток по показателю log2(value)-1  (индексы 0..16) */
static const uint32_t TILE_COLORS[] = {
    0x313244,  /*    2 */
    0x45475A,  /*    4 */
    0x89B4FA,  /*    8 */
    0x74C7EC,  /*   16 */
    0x89DCEB,  /*   32 */
    0xA6E3A1,  /*   64 */
    0xF9E2AF,  /*  128 */
    0xFAB387,  /*  256 */
    0xEBA0AC,  /*  512 */
    0xF38BA8,  /* 1024 */
    0xCBA6F7,  /* 2048 */
    0xB4BEFE,  /* 4096 */
    0x89B4FA,  /* 8192+ */
};
#define TILE_COLORS_N 13

/* ─────────────────────────────────────────
   Звуки (тон-последовательности как в тетрисе)
───────────────────────────────────────── */
struct sfx_note { uint16_t freq_hz; uint16_t tone_ms; uint16_t gap_ms; };

static const struct sfx_note SFX_MOVE[]   = { {900,  30, 0} };
static const struct sfx_note SFX_MERGE[]  = { {1200, 50, 0}, {1600, 60, 0} };
static const struct sfx_note SFX_WIN[]    = { {1047,80,30},{1319,80,30},{1568,80,30},{2093,120,0} };
static const struct sfx_note SFX_OVER[]   = { {400,120,0},{300,100,0},{240,150,0} };

static const struct sfx_note *sfx_seq = 0;
static uint32_t sfx_len   = 0;
static uint32_t sfx_idx   = 0;
static uint32_t sfx_timer = 0;
static bool     sfx_playing = false;

static void sfx_stop(void) {
    sfx_playing = false; sfx_seq = 0; sfx_len = sfx_idx = sfx_timer = 0;
    pa_stop_tone(); pa_sfx_stop();
}

static void sfx_start(const struct sfx_note *seq, uint32_t len) {
    if (!seq || !len) return;
    sfx_stop();
    sfx_seq = seq; sfx_len = len; sfx_idx = 0;
    sfx_timer = seq[0].tone_ms + seq[0].gap_ms;
    sfx_playing = true;
    pa_play_tone(seq[0].freq_hz, seq[0].tone_ms);
}

static void sfx_tick(uint32_t elapsed_ms) {
    while (sfx_playing && elapsed_ms > 0) {
        if (elapsed_ms < sfx_timer) { sfx_timer -= elapsed_ms; break; }
        elapsed_ms -= sfx_timer;
        sfx_idx++;
        if (sfx_idx >= sfx_len) { sfx_playing = false; sfx_seq = 0; break; }
        sfx_timer = sfx_seq[sfx_idx].tone_ms + sfx_seq[sfx_idx].gap_ms;
        pa_play_tone(sfx_seq[sfx_idx].freq_hz, sfx_seq[sfx_idx].tone_ms);
    }
}

/* ─────────────────────────────────────────
   Состояние игры
───────────────────────────────────────── */
static uint32_t board[GRID_N][GRID_N]; /* 0 = пусто, иначе степень двойки */
static uint32_t score;
static uint32_t best;
static bool     game_over;
static bool     game_won;        /* достигли 2048 (но можно продолжать) */
static bool     won_shown;       /* баннер победы уже был показан */
static uint32_t rng;

/* ─────────────────────────────────────────
   Layout (адаптивный, как в тетрисе)
───────────────────────────────────────── */
static uint32_t g_win_w   = 480;
static uint32_t g_win_h   = 580;
static uint32_t g_cell    = 90;   /* размер ячейки */
static uint32_t g_gap     = 8;    /* зазор между ячейками */
static uint32_t g_board_x = 20;   /* позиция поля в клиентской области */
static uint32_t g_board_y = 100;
static uint32_t g_hud_x   = 20;
static uint32_t g_hud_y   = 12;

static void compute_layout(void) {
    struct pc_display_info info;
    uint32_t dw = 1024, dh = 768;
    if (pc_display_get_info(&info) && info.available && info.width >= 640) {
        dw = info.width; dh = info.height;
    }

    /* Окно ≈ 70% экрана по наименьшей стороне */
    uint32_t budget = dw < dh ? dw : dh;
    budget = budget * 70 / 100;
    if (budget < 300) budget = 300;
    if (budget > 560) budget = 560;

    g_gap  = budget / 60;
    if (g_gap < 4)  g_gap = 4;
    if (g_gap > 12) g_gap = 12;

    /* Поле занимает budget × budget, разбиваем на 4 ячейки + 5 отступов */
    uint32_t field = budget;
    g_cell = (field - g_gap * 5) / 4;
    if (g_cell < 50) g_cell = 50;

    field = g_cell * 4 + g_gap * 5;

    uint32_t border = (uint32_t)PG_WINDOW_BORDER;
    uint32_t tb     = (uint32_t)PG_TITLEBAR_HEIGHT;

    g_win_w = field + 40 + border * 2;
    g_win_h = field + 120 + tb + border * 2;

    if (g_win_w > dw) g_win_w = dw;
    if (g_win_h > dh) g_win_h = dh;

    g_board_x = 20;
    g_board_y = 100;
    g_hud_x   = 20;
    g_hud_y   = 12;
}

/* ─────────────────────────────────────────
   RNG (XorShift32, seed из uptime как в тетрисе)
───────────────────────────────────────── */
static uint32_t rng_next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* ─────────────────────────────────────────
   Логика игры
───────────────────────────────────────── */

/* Добавляет случайную плитку (90% → 2, 10% → 4) */
static void spawn_tile(void) {
    /* Считаем пустые */
    uint32_t empty[GRID_N * GRID_N];
    uint32_t n = 0;
    for (uint32_t r = 0; r < GRID_N; r++)
        for (uint32_t c = 0; c < GRID_N; c++)
            if (!board[r][c]) empty[n++] = r * GRID_N + c;
    if (!n) return;
    uint32_t idx = rng_next() % n;
    uint32_t val = (rng_next() % 10 == 0) ? 4 : 2;
    board[empty[idx] / GRID_N][empty[idx] % GRID_N] = val;
}

static void reset_game(void) {
    sfx_stop();
    for (uint32_t r = 0; r < GRID_N; r++)
        for (uint32_t c = 0; c < GRID_N; c++)
            board[r][c] = 0;
    score     = 0;
    game_over = false;
    game_won  = false;
    won_shown = false;

    /* seed */
    struct cpu_monitor_info cpu;
    rng = 0xDEADBEEF;
    if (pc_cpu_info(&cpu)) rng ^= (uint32_t)cpu.uptime_ms;

    spawn_tile();
    spawn_tile();
}

static bool has_moves(void) {
    for (uint32_t r = 0; r < GRID_N; r++)
        for (uint32_t c = 0; c < GRID_N; c++) {
            if (!board[r][c]) return true;
            if (c + 1 < GRID_N && board[r][c] == board[r][c+1]) return true;
            if (r + 1 < GRID_N && board[r][c] == board[r+1][c]) return true;
        }
    return false;
}

/* Сдвигает одну строку влево, возвращает true если что-то изменилось.
   merged[] помечает, какие позиции уже слились (чтобы не сливать дважды). */
static bool slide_left_row(uint32_t row[GRID_N]) {
    bool changed = false;
    /* 1. Убираем нули: сжимаем влево */
    uint32_t tmp[GRID_N] = {0,0,0,0};
    uint32_t pos = 0;
    for (uint32_t c = 0; c < GRID_N; c++)
        if (row[c]) tmp[pos++] = row[c];
    /* 2. Слияние */
    for (uint32_t c = 0; c + 1 < GRID_N; c++) {
        if (tmp[c] && tmp[c] == tmp[c+1]) {
            tmp[c]  *= 2;
            score   += tmp[c];
            if (tmp[c] > best) best = tmp[c];
            if (tmp[c] >= TARGET) game_won = true;
            tmp[c+1] = 0;
            c++; /* пропускаем только что слитую */
        }
    }
    /* 3. Снова сжимаем */
    pos = 0;
    uint32_t out[GRID_N] = {0,0,0,0};
    for (uint32_t c = 0; c < GRID_N; c++)
        if (tmp[c]) out[pos++] = tmp[c];
    /* 4. Записываем и проверяем изменения */
    for (uint32_t c = 0; c < GRID_N; c++) {
        if (row[c] != out[c]) changed = true;
        row[c] = out[c];
    }
    return changed;
}

/* Направления: 0=Left 1=Right 2=Up 3=Down */
static bool do_move(int dir) {
    if (game_over) return false;
    bool changed = false;
    bool merged  = false;

    uint32_t old_score = score;

    if (dir == 0) { /* LEFT */
        for (uint32_t r = 0; r < GRID_N; r++)
            if (slide_left_row(board[r])) changed = true;
    } else if (dir == 1) { /* RIGHT — переворачиваем строку, слайдим, переворачиваем */
        for (uint32_t r = 0; r < GRID_N; r++) {
            uint32_t rev[GRID_N];
            for (uint32_t c = 0; c < GRID_N; c++) rev[c] = board[r][GRID_N-1-c];
            if (slide_left_row(rev)) changed = true;
            for (uint32_t c = 0; c < GRID_N; c++) board[r][GRID_N-1-c] = rev[c];
        }
    } else if (dir == 2) { /* UP — транспонируем, слайдим, транспонируем */
        for (uint32_t c = 0; c < GRID_N; c++) {
            uint32_t col[GRID_N];
            for (uint32_t r = 0; r < GRID_N; r++) col[r] = board[r][c];
            if (slide_left_row(col)) changed = true;
            for (uint32_t r = 0; r < GRID_N; r++) board[r][c] = col[r];
        }
    } else { /* DOWN */
        for (uint32_t c = 0; c < GRID_N; c++) {
            uint32_t col[GRID_N];
            for (uint32_t r = 0; r < GRID_N; r++) col[r] = board[GRID_N-1-r][c];
            if (slide_left_row(col)) changed = true;
            for (uint32_t r = 0; r < GRID_N; r++) board[GRID_N-1-r][c] = col[r];
        }
    }

    if (!changed) return false;

    merged = (score > old_score);
    spawn_tile();

    if (!has_moves()) game_over = true;

    if (!pa_is_muted()) {
        if (game_over)       sfx_start(SFX_OVER,  3);
        else if (game_won && !won_shown) sfx_start(SFX_WIN, 4);
        else if (merged)     sfx_start(SFX_MERGE, 2);
        else                 sfx_start(SFX_MOVE,  1);
    }

    if (game_won && !won_shown) won_shown = true;

    return true;
}

/* ─────────────────────────────────────────
   Утилиты рисования (текст)
───────────────────────────────────────── */
static char *append_str(char *d, const char *s) {
    while (*s) *d++ = *s++;
    *d = '\0';
    return d;
}

static char *append_u32(char *d, uint32_t v) {
    char rev[12]; uint32_t n = 0;
    if (!v) rev[n++] = '0';
    while (v) { rev[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *d++ = rev[--n];
    *d = '\0';
    return d;
}

/* log2 для степеней двойки (2→1, 4→2, …) */
static uint32_t log2_u32(uint32_t v) {
    uint32_t n = 0;
    while (v > 1) { v >>= 1; n++; }
    return n;
}

/* ─────────────────────────────────────────
   Рендеринг
───────────────────────────────────────── */
static void draw_tile(struct pg_window *win,
                      uint32_t px, uint32_t py, uint32_t val) {
    uint32_t cs = g_cell;

    if (!val) {
        pg_window_rect(win, (struct pg_rect){px, py, cs, cs}, C_EMPTY);
        return;
    }

    uint32_t idx = log2_u32(val) - 1;
    if (idx >= TILE_COLORS_N) idx = TILE_COLORS_N - 1;
    uint32_t bg = TILE_COLORS[idx];

    /* Плитка */
    pg_window_rect(win, (struct pg_rect){px, py, cs, cs}, bg);
    /* Тонкий блик сверху */
    uint32_t hl = cs >= 60 ? 4 : 2;
    pg_window_rect(win, (struct pg_rect){px+2, py+2, cs-4, hl}, bg + 0x181818);

    /* Цвет текста: тёмный для светлых плиток, светлый для тёмных */
    uint32_t fg = (idx <= 1) ? C_TEXT_HI : C_TEXT_LO;

    /* Число по центру */
    char buf[8]; char *p = buf;
    p = append_u32(p, val);

    /* Выбираем размер шрифта в зависимости от числа цифр и размера клетки */
    uint32_t len = (uint32_t)(p - buf);
    uint32_t font = cs >= 80 ? 24 : (cs >= 60 ? 18 : 14);
    if (len >= 4) font = cs >= 80 ? 18 : (cs >= 60 ? 14 : 10);
    if (len >= 5) font = cs >= 80 ? 14 : 10;

    /* Примерная ширина текста: каждый символ ≈ font px / 1.6 */
    uint32_t tw = len * font * 10 / 16;
    uint32_t th = font;
    uint32_t tx = px + (cs > tw ? (cs - tw) / 2 : 2);
    uint32_t ty = py + (cs > th ? (cs - th) / 2 : 2);

    pc_draw_text_sized(tx, ty, buf, fg, bg, font);
}

static void draw_board(struct pg_window *win) {
    uint32_t field = g_cell * GRID_N + g_gap * (GRID_N + 1);

    /* Рамка поля */
    pg_window_rect(win, (struct pg_rect){g_board_x-2, g_board_y-2,
                                         field+4, field+4}, C_BORDER);
    pg_window_rect(win, (struct pg_rect){g_board_x, g_board_y,
                                         field, field}, C_BOARD);

    for (uint32_t r = 0; r < GRID_N; r++) {
        for (uint32_t c = 0; c < GRID_N; c++) {
            uint32_t px = g_board_x + g_gap + c * (g_cell + g_gap);
            uint32_t py = g_board_y + g_gap + r * (g_cell + g_gap);
            draw_tile(win, px, py, board[r][c]);
        }
    }
}

static void draw_hud(struct pg_window *win) {
    uint32_t field = g_cell * GRID_N + g_gap * (GRID_N + 1);

    /* Заголовок */
    pg_window_rect(win, (struct pg_rect){g_hud_x, g_hud_y,
                                         field, 80}, win->theme.titlebar);
    pg_window_rect(win, (struct pg_rect){g_hud_x-1, g_hud_y-1,
                                         field+2, 82}, C_BORDER);

    /* Название */
    uint32_t title_font = (field >= 350) ? 32 : 24;
    pc_draw_text_sized(g_hud_x + 10, g_hud_y + 10,
                       "2048", C_TEXT_HI, win->theme.titlebar, title_font);

    /* Score */
    char buf[32];
    char *p;

    uint32_t sx = g_hud_x + field / 2 - 60;
    uint32_t sy = g_hud_y + 8;

    /* Score box */
    pg_window_rect(win, (struct pg_rect){sx, sy, 110, 32}, C_EMPTY);
    p = buf; p = append_str(p, "SCORE: "); p = append_u32(p, score);
    pg_window_text(win, sx + 6, sy + 8, buf, C_TEXT_HI);

    /* Best box */
    pg_window_rect(win, (struct pg_rect){sx + 118, sy, 110, 32}, C_EMPTY);
    p = buf; p = append_str(p, "BEST:  "); p = append_u32(p, best);
    pg_window_text(win, sx + 118 + 6, sy + 8, buf, C_TEXT_HI);

    /* Подсказки */
    pg_window_text(win, g_hud_x + 10, g_hud_y + 52,
                   "Arrows/WASD: move   R: new game   Q: quit",
                   win->theme.text);
}

static void draw_overlay(struct pg_window *win) {
    if (!game_over && !(game_won && won_shown)) return;

    uint32_t field = g_cell * GRID_N + g_gap * (GRID_N + 1);
    uint32_t ow = 220, oh = 60;
    uint32_t ox = g_board_x + (field - ow) / 2;
    uint32_t oy = g_board_y + (field - oh) / 2;

    if (game_over) {
        pg_window_rect(win, (struct pg_rect){ox-2, oy-2, ow+4, oh+4}, C_BORDER);
        pg_window_rect(win, (struct pg_rect){ox, oy, ow, oh}, 0xF38BA8);
        pg_window_text(win, ox + 60, oy + 12, "GAME OVER", C_TEXT_LO);
        pg_window_text(win, ox + 30, oy + 38, "Press R to start over", C_TEXT_LO);
    } else if (game_won && won_shown) {
        pg_window_rect(win, (struct pg_rect){ox-2, oy-2, ow+4, oh+4}, C_BORDER);
        pg_window_rect(win, (struct pg_rect){ox, oy, ow, oh}, 0xA6E3A1);
        pg_window_text(win, ox + 60, oy + 12, "YOU WIN!", C_TEXT_LO);
        pg_window_text(win, ox + 18, oy + 38, "Keep going or press R", C_TEXT_LO);
    }
}

static void redraw(struct pg_window *win) {
    pg_window_begin(win);
    draw_hud(win);
    draw_board(win);
    draw_overlay(win);
    pg_window_end(win);
}

/* ─────────────────────────────────────────
   Обработка ввода
───────────────────────────────────────── */
static void handle_input(struct pg_window *win, struct pg_event *event) {
    if (event->type == PG_EVENT_CLOSE) {
        pg_window_close(win);
        return;
    }

    if (event->type == PG_EVENT_KEY) {
        int32_t k = event->key;
        if (k == 'q' || k == 'Q') { pg_window_close(win); return; }
        if (k == 'r' || k == 'R') { reset_game(); redraw(win); return; }

        if (game_over) return;

        bool moved = false;
        if      (k == 'a' || k == 'A') moved = do_move(0);
        else if (k == 'd' || k == 'D') moved = do_move(1);
        else if (k == 'w' || k == 'W') moved = do_move(2);
        else if (k == 's' || k == 'S') moved = do_move(3);

        if (moved) redraw(win);
        return;
    }

    if (event->type == PG_EVENT_SPECIAL_KEY) {
        if (game_over) return;
        bool moved = false;
        if      (event->key == KEYBOARD_SPECIAL_LEFT)  moved = do_move(0);
        else if (event->key == KEYBOARD_SPECIAL_RIGHT) moved = do_move(1);
        else if (event->key == KEYBOARD_SPECIAL_UP)    moved = do_move(2);
        else if (event->key == KEYBOARD_SPECIAL_DOWN)  moved = do_move(3);
        if (moved) redraw(win);
    }
}

/* ─────────────────────────────────────────
   main
───────────────────────────────────────── */
static int game2048_main(void) {
    compute_layout();

    struct pg_window window;
    bool opened = false;

    /* Пробуем открыть окно, уменьшая при неудаче (как в тетрисе) */
    for (int32_t attempt = 0; attempt < 6; attempt++) {
        if (pg_window_center(&window, "2048", g_win_w, g_win_h)) {
            opened = true;
            break;
        }
        if (g_win_w > 320) { g_win_w -= 40; g_win_h -= 40; compute_layout(); }
    }
    if (!opened) return 1;

    reset_game();
    redraw(&window);

    while (pg_window_is_open(&window)) {
        struct pg_event event;
        bool has = pg_window_poll_event(&window, &event);

        if (has) {
            if (event.type == PG_EVENT_REPAINT ||
                event.type == PG_EVENT_MOVE    ||
                event.type == PG_EVENT_FOCUS) {
                redraw(&window);
            } else {
                handle_input(&window, &event);
            }
            if (!pg_window_is_open(&window)) break;
        } else {
            pc_sleep(16);
        }

        pa_update();
        sfx_tick(16);
    }

    sfx_stop();
    pg_window_close(&window);
    return 0;
}

void _start(void) {
    pc_exit(game2048_main());
}
