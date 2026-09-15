#if __has_include("../../../libc/include/purec.h")
#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"
#include "../../../drivers/input/keyboard.h"
#elif __has_include("src/libc/include/purec.h")
#include "src/libgui/include/puregui.h"
#include "src/libgui/include/pguiw.h"
#include "src/libc/include/purec.h"
#include "src/libaudio/include/pureaudio.h"
#include "src/drivers/input/keyboard.h"
#else
#include <puregui.h>
#include <pguiw.h>
#include <purec.h>
#include <pureaudio.h>
#endif

#ifndef KEYBOARD_SPECIAL_LEFT
#define KEYBOARD_SPECIAL_LEFT  8
#define KEYBOARD_SPECIAL_RIGHT 9
#define KEYBOARD_SPECIAL_UP    10
#define KEYBOARD_SPECIAL_DOWN  11
#endif

#define WIN_WIDTH   380
#define WIN_HEIGHT  460

#define BOARD_ROWS  4
#define BOARD_COLS  4
#define TILE_SIZE   72
#define GAP         6
#define BOARD_PIXELS (BOARD_COLS * TILE_SIZE + (BOARD_COLS + 1) * GAP)

#define COLOR_WIN_BG      0x1E1E2E
#define COLOR_BOARD_BG    0x181825
#define COLOR_BOARD_EDGE  0x45475A
#define COLOR_TILE_BASE   0x89B4FA
#define COLOR_TILE_HOVER  0xB4BEFE
#define COLOR_TILE_DONE   0xA6E3A1
#define COLOR_TILE_TEXT   0x11111B
#define COLOR_SLOT_EMPTY  0x242438
#define COLOR_TEXT_MAIN   0xCDD6F4
#define COLOR_TEXT_MUTED  0x6C7086

static int board[BOARD_ROWS][BOARD_COLS];
static int empty_r = 3;
static int empty_c = 3;
static uint32_t moves = 0;
static bool won = false;
static uint32_t elapsed_sec = 0;
static uint32_t timer_ms = 0;
static int hover_r = -1;
static int hover_c = -1;

static uint32_t rng_state = 0x51A7E123;

static int16_t sfx_move_buf[2048];
static uint32_t sfx_move_len = 0;
static int16_t sfx_win_buf[14000];
static uint32_t sfx_win_len = 0;

static uint32_t sfx_preload(const char *name, int16_t *buffer, uint32_t capacity) {
    static const char *dirs[] = {"/game/sound/", "/bin/sound/"};
    for (uint32_t d = 0; d < sizeof(dirs)/sizeof(dirs[0]); d++) {
        char path[64];
        uint32_t pos = 0;
        const char *dir = dirs[d];
        while (dir[pos] && pos + 1 < sizeof(path)) {
            path[pos] = dir[pos];
            pos++;
        }
        for (uint32_t i = 0; name[i] && pos + 1 < sizeof(path); i++) {
            path[pos++] = name[i];
        }
        path[pos] = '\0';
        uint32_t frames = 0;
        if (pa_wav_load(path, buffer, capacity, &frames) == 0 && frames > 0) {
            return frames;
        }
    }
    return 0;
}

static void sfx_init(void) {
    sfx_move_len = sfx_preload("move.wav", sfx_move_buf, sizeof(sfx_move_buf)/sizeof(sfx_move_buf[0]));
    sfx_win_len  = sfx_preload("clear.wav", sfx_win_buf, sizeof(sfx_win_buf)/sizeof(sfx_win_buf[0]));
}

static void play_move_sound(void) {
    if (sfx_move_len > 0 && pa_sfx_play(sfx_move_buf, sfx_move_len) == 0) return;
    pa_play_tone(950, 35);
}

static void play_win_sound(void) {
    if (sfx_win_len > 0 && pa_sfx_play(sfx_win_buf, sfx_win_len) == 0) return;
    pa_play_tone(1600, 220);
}

static uint32_t rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void format_u32(uint32_t val, char *dst) {
    if (val == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        return;
    }
    char tmp[12];
    int len = 0;
    while (val > 0) {
        tmp[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int pos = 0;
    while (len > 0) {
        dst[pos++] = tmp[--len];
    }
    dst[pos] = '\0';
}

static int str_length(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static bool check_win(void) {
    int expected = 1;
    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c < BOARD_COLS; c++) {
            if (r == BOARD_ROWS - 1 && c == BOARD_COLS - 1) {
                if (board[r][c] != 0) return false;
            } else {
                if (board[r][c] != expected++) return false;
            }
        }
    }
    return true;
}

static void shuffle_board(void) {
    int val = 1;
    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c < BOARD_COLS; c++) {
            board[r][c] = (r == BOARD_ROWS - 1 && c == BOARD_COLS - 1) ? 0 : val++;
        }
    }
    empty_r = BOARD_ROWS - 1;
    empty_c = BOARD_COLS - 1;

    struct cpu_monitor_info cpu;
    if (pc_cpu_info(&cpu)) {
        rng_state = (uint32_t)cpu.uptime_ms ^ 0x9B1C5D37u;
    } else {
        rng_state ^= 0xA5A55A5Au;
    }

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};
    const int opp[4] = {1, 0, 3, 2};
    int last_dir = -1;

    for (int step = 0; step < 260; step++) {
        int dirs[4];
        int count = 0;
        for (int d = 0; d < 4; d++) {
            if (d == last_dir) continue;
            int nr = empty_r + dr[d];
            int nc = empty_c + dc[d];
            if (nr >= 0 && nr < BOARD_ROWS && nc >= 0 && nc < BOARD_COLS) {
                dirs[count++] = d;
            }
        }
        if (count == 0) continue;
        int choice = dirs[rng_next() % (uint32_t)count];
        int nr = empty_r + dr[choice];
        int nc = empty_c + dc[choice];
        board[empty_r][empty_c] = board[nr][nc];
        board[nr][nc] = 0;
        empty_r = nr;
        empty_c = nc;
        last_dir = opp[choice];
    }

    if (check_win()) {
        int nr = empty_r > 0 ? empty_r - 1 : empty_r + 1;
        board[empty_r][empty_c] = board[nr][empty_c];
        board[nr][empty_c] = 0;
        empty_r = nr;
    }

    moves = 0;
    won = false;
    elapsed_sec = 0;
    timer_ms = 0;
    hover_r = -1;
    hover_c = -1;
}

static bool can_slide(int r, int c) {
    if (r < 0 || r >= BOARD_ROWS || c < 0 || c >= BOARD_COLS) return false;
    if (board[r][c] == 0) return false;
    return (r == empty_r && c != empty_c) || (c == empty_c && r != empty_r);
}

static bool try_slide(int r, int c) {
    if (!can_slide(r, c)) return false;

    if (r == empty_r) {
        if (c < empty_c) {
            for (int i = empty_c; i > c; i--) {
                board[r][i] = board[r][i - 1];
            }
            board[r][c] = 0;
            empty_c = c;
        } else {
            for (int i = empty_c; i < c; i++) {
                board[r][i] = board[r][i + 1];
            }
            board[r][c] = 0;
            empty_c = c;
        }
    } else if (c == empty_c) {
        if (r < empty_r) {
            for (int i = empty_r; i > r; i--) {
                board[i][c] = board[i - 1][c];
            }
            board[r][c] = 0;
            empty_r = r;
        } else {
            for (int i = empty_r; i < r; i++) {
                board[i][c] = board[i + 1][c];
            }
            board[r][c] = 0;
            empty_r = r;
        }
    }

    moves++;
    play_move_sound();

    if (check_win()) {
        won = true;
        play_win_sound();
    }
    return true;
}

static void draw_hud(struct pg_window *window, uint32_t board_x) {
    char buf[48];

    buf[0] = '\0';
    char moves_val[12];
    format_u32(moves, moves_val);
    const char *mv_hdr = "Moves: ";
    int p = 0;
    for (int i = 0; mv_hdr[i]; i++) buf[p++] = mv_hdr[i];
    for (int i = 0; moves_val[i]; i++) buf[p++] = moves_val[i];
    buf[p] = '\0';
    pg_window_text(window, board_x, 12, buf, COLOR_TEXT_MAIN);

    uint32_t mm = elapsed_sec / 60;
    uint32_t ss = elapsed_sec % 60;
    buf[0] = 'T'; buf[1] = 'i'; buf[2] = 'm'; buf[3] = 'e'; buf[4] = ':'; buf[5] = ' ';
    buf[6] = (char)('0' + (mm / 10));
    buf[7] = (char)('0' + (mm % 10));
    buf[8] = ':';
    buf[9] = (char)('0' + (ss / 10));
    buf[10] = (char)('0' + (ss % 10));
    buf[11] = '\0';
    pg_window_text(window, board_x + 115, 12, buf, COLOR_TEXT_MAIN);

    const char *snd_txt = pa_is_muted() ? "Sound: OFF" : "Sound: ON";
    pg_window_text(window, board_x + 230, 12, snd_txt, COLOR_TEXT_MAIN);

    pg_window_text(window, board_x, 32, "[R] Restart   [M] Sound   [Q] Exit", COLOR_TEXT_MUTED);
}

static void draw_board(struct pg_window *window, uint32_t board_x, uint32_t board_y) {
    pg_window_rect(window, (struct pg_rect){board_x - 3, board_y - 3, BOARD_PIXELS + 6, BOARD_PIXELS + 6}, COLOR_BOARD_EDGE);
    pg_window_rect(window, (struct pg_rect){board_x, board_y, BOARD_PIXELS, BOARD_PIXELS}, COLOR_BOARD_BG);

    for (int r = 0; r < BOARD_ROWS; r++) {
        for (int c = 0; c < BOARD_COLS; c++) {
            uint32_t tx = board_x + GAP + (uint32_t)c * (TILE_SIZE + GAP);
            uint32_t ty = board_y + GAP + (uint32_t)r * (TILE_SIZE + GAP);
            int val = board[r][c];

            if (val == 0) {
                pg_window_rect(window, (struct pg_rect){tx, ty, TILE_SIZE, TILE_SIZE}, COLOR_SLOT_EMPTY);
                pg_window_rect(window, (struct pg_rect){tx + 1, ty + 1, TILE_SIZE - 2, TILE_SIZE - 2}, COLOR_BOARD_BG);
            } else {
                uint32_t col = COLOR_TILE_BASE;
                if (won) {
                    col = COLOR_TILE_DONE;
                } else if (r == hover_r && c == hover_c && can_slide(r, c)) {
                    col = COLOR_TILE_HOVER;
                }

                pg_window_rect(window, (struct pg_rect){tx, ty, TILE_SIZE, TILE_SIZE}, col);
                pg_window_rect(window, (struct pg_rect){tx, ty, TILE_SIZE, 3}, col + 0x151515);
                pg_window_rect(window, (struct pg_rect){tx, ty, 3, TILE_SIZE}, col + 0x151515);
                pg_window_rect(window, (struct pg_rect){tx, ty + TILE_SIZE - 3, TILE_SIZE, 3}, 0x181825);
                pg_window_rect(window, (struct pg_rect){tx + TILE_SIZE - 3, ty, 3, TILE_SIZE}, 0x181825);

                char num_str[8];
                format_u32((uint32_t)val, num_str);
                int len = str_length(num_str);
                uint32_t tw = (uint32_t)len * 24;
                uint32_t nx = tx + (TILE_SIZE > tw ? (TILE_SIZE - tw) / 2 : 0);
                uint32_t ny = ty + (TILE_SIZE > 24 ? (TILE_SIZE - 24) / 2 : 0);
                pg_window_text_sized(window, nx, ny, num_str, COLOR_TILE_TEXT, 24);
            }
        }
    }
}

static void draw_footer(struct pg_window *window, uint32_t board_x, uint32_t board_y) {
    uint32_t footer_y = board_y + BOARD_PIXELS + 12;
    if (won) {
        pg_window_rect(window, (struct pg_rect){board_x, footer_y - 2, BOARD_PIXELS, 32}, COLOR_TILE_DONE);
        pg_window_text(window, board_x + 18, footer_y + 8, "VICTORY! Solved! Press R to replay", COLOR_TILE_TEXT);
    } else {
        pg_window_rect(window, (struct pg_rect){board_x, footer_y - 2, BOARD_PIXELS, 32}, COLOR_WIN_BG);
        pg_window_text(window, board_x + 22, footer_y + 8, "Click tile or use Arrows / WASD", COLOR_TEXT_MUTED);
    }
}

static void redraw(struct pg_window *window) {
    pg_window_begin(window);
    pg_window_clear(window, COLOR_WIN_BG);

    uint32_t client_w = window->client.width;
    uint32_t board_x = client_w > BOARD_PIXELS ? (client_w - BOARD_PIXELS) / 2 : 10;
    uint32_t board_y = 56;

    draw_hud(window, board_x);
    draw_board(window, board_x, board_y);
    draw_footer(window, board_x, board_y);

    pg_window_end(window);
}

static bool screen_to_cell(const struct pg_window *window, int32_t sx, int32_t sy, int *out_r, int *out_c) {
    int32_t cx = sx - (int32_t)window->client.x;
    int32_t cy = sy - (int32_t)window->client.y;

    uint32_t client_w = window->client.width;
    int32_t bx = (int32_t)(client_w > BOARD_PIXELS ? (client_w - BOARD_PIXELS) / 2 : 10);
    int32_t by = 56;

    int32_t rx = cx - bx;
    int32_t ry = cy - by;

    if (rx < 0 || rx >= (int32_t)BOARD_PIXELS || ry < 0 || ry >= (int32_t)BOARD_PIXELS) {
        return false;
    }

    int c = rx / (TILE_SIZE + GAP);
    int r = ry / (TILE_SIZE + GAP);
    if (r >= 0 && r < BOARD_ROWS && c >= 0 && c < BOARD_COLS) {
        *out_r = r;
        *out_c = c;
        return true;
    }
    return false;
}

static void handle_input(struct pg_window *window, struct pg_event *event) {
    if (event->type == PG_EVENT_CLOSE) {
        pg_window_close(window);
        return;
    }

    if (event->type == PG_EVENT_KEY) {
        int32_t k = event->key;
        if (k == 'q' || k == 'Q') {
            pg_window_close(window);
            return;
        }
        if (k == 'r' || k == 'R') {
            shuffle_board();
            redraw(window);
            return;
        }
        if (k == 'm' || k == 'M') {
            pa_toggle_mute();
            redraw(window);
            return;
        }
        if (!won) {
            bool moved = false;
            if (k == 'w' || k == 'W') {
                moved = try_slide(empty_r - 1, empty_c);
            } else if (k == 's' || k == 'S') {
                moved = try_slide(empty_r + 1, empty_c);
            } else if (k == 'a' || k == 'A') {
                moved = try_slide(empty_r, empty_c - 1);
            } else if (k == 'd' || k == 'D') {
                moved = try_slide(empty_r, empty_c + 1);
            }
            if (moved) redraw(window);
        }
    } else if (event->type == PG_EVENT_SPECIAL_KEY) {
        if (!won) {
            bool moved = false;
            if (event->key == KEYBOARD_SPECIAL_UP) {
                moved = try_slide(empty_r - 1, empty_c);
            } else if (event->key == KEYBOARD_SPECIAL_DOWN) {
                moved = try_slide(empty_r + 1, empty_c);
            } else if (event->key == KEYBOARD_SPECIAL_LEFT) {
                moved = try_slide(empty_r, empty_c - 1);
            } else if (event->key == KEYBOARD_SPECIAL_RIGHT) {
                moved = try_slide(empty_r, empty_c + 1);
            }
            if (moved) redraw(window);
        }
    } else if (event->type == PG_EVENT_MOUSE_MOVE) {
        int cr = -1, cc = -1;
        bool on_board = screen_to_cell(window, event->x, event->y, &cr, &cc);
        int new_hr = on_board ? cr : -1;
        int new_hc = on_board ? cc : -1;
        if (new_hr != hover_r || new_hc != hover_c) {
            hover_r = new_hr;
            hover_c = new_hc;
            redraw(window);
        }
    } else if (event->type == PG_EVENT_MOUSE_DOWN && event->button == 1) {
        if (!won) {
            int cr = -1, cc = -1;
            if (screen_to_cell(window, event->x, event->y, &cr, &cc)) {
                if (try_slide(cr, cc)) {
                    redraw(window);
                }
            }
        }
    }
}

int puzzle15_main(void) {
    struct pg_window window;
    if (!pg_window_center(&window, "15-Puzzle", WIN_WIDTH, WIN_HEIGHT)) {
        return 1;
    }

    sfx_init();
    shuffle_board();
    redraw(&window);

    while (pg_window_is_open(&window)) {
        struct pg_event event;
        bool has_event = pg_window_poll_event(&window, &event);

        if (has_event) {
            if (event.type == PG_EVENT_REPAINT || event.type == PG_EVENT_MOVE || event.type == PG_EVENT_FOCUS) {
                redraw(&window);
            } else {
                handle_input(&window, &event);
            }
            if (!pg_window_is_open(&window)) break;
        } else {
            pc_sleep(16);
        }

        pa_update();

        if (pg_window_is_minimized(&window)) continue;

        if (!won) {
            timer_ms += 16;
            if (timer_ms >= 1000) {
                timer_ms -= 1000;
                elapsed_sec++;
                redraw(&window);
            }
        }
    }

    pg_window_close(&window);
    return 0;
}

void _start(void) {
    pc_exit(puzzle15_main());
}