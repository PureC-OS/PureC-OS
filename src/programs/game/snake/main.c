#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"

#define BOARD_COLS 20
#define BOARD_ROWS 15
#define MAX_LENGTH (BOARD_COLS * BOARD_ROWS)

#define COLOR_WIN_BG     0x1E1E2E
#define COLOR_BOARD_BG   0x181825
#define COLOR_BOARD_EDGE 0x45475A
#define COLOR_SNAKE_HEAD 0x89B4FA
#define COLOR_SNAKE_BODY 0xA6E3A1
#define COLOR_SNAKE_EYE  0x11111B
#define COLOR_FOOD       0xF38BA8
#define COLOR_FOOD_IN    0xF9E2AF
#define COLOR_TEXT_MAIN  0xCDD6F4
#define COLOR_TEXT_MUTED 0x6C7086
#define COLOR_BTN_BG     0x313244
#define COLOR_BTN_SEL    0x89B4FA
#define COLOR_BTN_TEXT   0x11111B

enum game_mode {
    MODE_MENU = 0,
    MODE_PLAYING,
    MODE_PAUSED,
    MODE_GAMEOVER
};

enum menu_action {
    ACT_PLAY = 0,
    ACT_DIFF,
    ACT_WALLS,
    ACT_EXIT,
    ACT_COUNT
};

struct point {
    int16_t x;
    int16_t y;
};

static struct point snake[MAX_LENGTH];
static uint16_t snake_len = 3;
static struct point food_pos;
static int16_t dir_x = 1;
static int16_t dir_y = 0;
static int16_t next_dx = 1;
static int16_t next_dy = 0;

static uint32_t score = 0;
static uint32_t high_score = 0;
static enum game_mode mode = MODE_MENU;
static enum menu_action menu_sel = ACT_PLAY;
static uint32_t diff_level = 1;
static bool wrap_walls = false;

static uint32_t rng_state = 0x51A7E123;
static uint32_t step_timer_ms = 0;

static uint32_t g_win_w = 420;
static uint32_t g_win_h = 560;
static uint32_t g_cell = 20;
static uint32_t g_board_x = 10;
static uint32_t g_board_y = 70;

static const char *DIFF_LABELS[] = {
    "Easy",
    "Normal",
    "Hard"
};
static const uint32_t DIFF_SPEEDS[] = {
    140,
    95,
    60
};

static int16_t sfx_turn[1100];
static uint32_t sfx_turn_frames = 0;
static int16_t sfx_eat[2700];
static uint32_t sfx_eat_frames = 0;
static int16_t sfx_die[11100];
static uint32_t sfx_die_frames = 0;
static uint32_t sfx_cool = 0;

static uint32_t sfx_preload(const char *name, int16_t *buf, uint32_t cap) {
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
        if (pa_wav_load(path, buf, cap, &frames) == 0 && frames > 0) return frames;
    }
    return 0;
}

static void sfx_play_buf(const int16_t *buf, uint32_t frames, uint16_t hz, uint32_t ms) {
    if (pa_is_muted()) return;
    if (frames > 0 && pa_sfx_play(buf, frames) == 0) return;
    pa_play_tone(hz, ms);
}

static void sfx_turn_blip(void) {
    if (sfx_cool > 0) return;
    sfx_play_buf(sfx_turn, sfx_turn_frames, 1200, 35);
}

static void sfx_eat_sound(void) {
    sfx_cool = 1;
    sfx_play_buf(sfx_eat, sfx_eat_frames, 950, 70);
}

static void sfx_die_sound(void) {
    sfx_cool = 3;
    sfx_play_buf(sfx_die, sfx_die_frames, 280, 260);
}

static void sfx_init_all(void) {
    sfx_turn_frames = sfx_preload("turn.wav", sfx_turn, sizeof(sfx_turn)/sizeof(sfx_turn[0]));
    sfx_eat_frames  = sfx_preload("eat.wav",  sfx_eat,  sizeof(sfx_eat)/sizeof(sfx_eat[0]));
    sfx_die_frames  = sfx_preload("die.wav",  sfx_die,  sizeof(sfx_die)/sizeof(sfx_die[0]));
}

static uint32_t random_val(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static char *append_text(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = '\0';
    return dest;
}

static char *append_u32(char *dest, uint32_t val) {
    if (val == 0) {
        *dest++ = '0';
        *dest = '\0';
        return dest;
    }
    char tmp[12];
    int pos = 0;
    while (val > 0) {
        tmp[pos++] = '0' + (val % 10);
        val /= 10;
    }
    while (pos > 0) *dest++ = tmp[--pos];
    *dest = '\0';
    return dest;
}

static bool is_cell_occupied(int16_t x, int16_t y) {
    for (uint16_t i = 0; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) return true;
    }
    return false;
}

static void spawn_food(void) {
    for (int tries = 0; tries < 500; tries++) {
        int16_t rx = (int16_t)(random_val() % BOARD_COLS);
        int16_t ry = (int16_t)(random_val() % BOARD_ROWS);
        if (!is_cell_occupied(rx, ry)) {
            food_pos.x = rx;
            food_pos.y = ry;
            return;
        }
    }
    food_pos.x = 0;
    food_pos.y = 0;
}

static void reset_snake_game(void) {
    snake_len = 3;
    dir_x = 1;
    dir_y = 0;
    next_dx = 1;
    next_dy = 0;
    score = 0;
    step_timer_ms = 0;

    int16_t sx = BOARD_COLS / 2;
    int16_t sy = BOARD_ROWS / 2;
    for (uint16_t i = 0; i < snake_len; i++) {
        snake[i].x = sx - (int16_t)i;
        snake[i].y = sy;
    }
    spawn_food();
}

static void compute_window_layout(void) {
    struct pc_display_info disp;
    uint32_t dw = 640, dh = 480;
    if (pc_display_get_info(&disp) && disp.available && disp.width >= 640) {
        dw = disp.width;
        dh = disp.height;
    }

    uint32_t budget_w = dw > 60 ? dw - 60 : dw;
    uint32_t budget_h = dh > 80 ? dh - 80 : dh;

    g_cell = 20;
    uint32_t b_w = BOARD_COLS * g_cell;
    uint32_t b_h = BOARD_ROWS * g_cell;

    if (b_w + 30 > budget_w || b_h + 120 > budget_h) {
        g_cell = 16;
        b_w = BOARD_COLS * g_cell;
        b_h = BOARD_ROWS * g_cell;
    }

    g_win_w = b_w + 24 + PG_WINDOW_BORDER * 2;
    g_win_h = b_h + 110 + PG_TITLEBAR_HEIGHT + PG_WINDOW_BORDER * 2;

    uint32_t client_w = g_win_w - PG_WINDOW_BORDER * 2;
    g_board_x = (client_w > b_w) ? (client_w - b_w) / 2 : 12;
    g_board_y = 66;
}

static void draw_hud(struct pg_window *window) {
    uint32_t b_w = BOARD_COLS * g_cell;

    pg_window_rect(window, (struct pg_rect){g_board_x, 8, b_w, 48}, window->theme.titlebar);
    pg_window_rect(window, (struct pg_rect){g_board_x - 1, 7, b_w + 2, 50}, COLOR_BOARD_EDGE);

    char buf[32];
    char *p = buf;
    p = append_text(p, "SCORE: "); p = append_u32(p, score);
    pg_window_text(window, g_board_x + 12, 16, buf, COLOR_TEXT_MAIN);

    p = buf;
    p = append_text(p, "BEST: "); p = append_u32(p, high_score);
    pg_window_text(window, g_board_x + 12, 34, buf, COLOR_TEXT_MUTED);

    p = buf;
    p = append_text(p, "SPD: "); p = append_text(p, DIFF_LABELS[diff_level]);
    pg_window_text(window, g_board_x + b_w - 110, 16, buf, COLOR_TEXT_MAIN);

    p = buf;
    p = append_text(p, wrap_walls ? "WALLS: PASS" : "WALLS: DIE");
    pg_window_text(window, g_board_x + b_w - 110, 34, buf, wrap_walls ? COLOR_SNAKE_BODY : COLOR_FOOD);
}

static void draw_board(struct pg_window *window) {
    uint32_t b_w = BOARD_COLS * g_cell;
    uint32_t b_h = BOARD_ROWS * g_cell;

    pg_window_rect(window, (struct pg_rect){g_board_x - 2, g_board_y - 2, b_w + 4, b_h + 4}, COLOR_BOARD_EDGE);
    pg_window_rect(window, (struct pg_rect){g_board_x, g_board_y, b_w, b_h}, COLOR_BOARD_BG);

    uint32_t fx = g_board_x + (uint32_t)food_pos.x * g_cell;
    uint32_t fy = g_board_y + (uint32_t)food_pos.y * g_cell;
    pg_window_rect(window, (struct pg_rect){fx + 1, fy + 1, g_cell - 2, g_cell - 2}, COLOR_FOOD);
    if (g_cell >= 10) {
        pg_window_rect(window, (struct pg_rect){fx + 4, fy + 4, g_cell - 8, g_cell - 8}, COLOR_FOOD_IN);
    }

    for (uint16_t i = 1; i < snake_len; i++) {
        uint32_t bx = g_board_x + (uint32_t)snake[i].x * g_cell;
        uint32_t by = g_board_y + (uint32_t)snake[i].y * g_cell;
        pg_window_rect(window, (struct pg_rect){bx + 1, by + 1, g_cell - 2, g_cell - 2}, COLOR_SNAKE_BODY);
    }

    if (snake_len > 0) {
        uint32_t hx = g_board_x + (uint32_t)snake[0].x * g_cell;
        uint32_t hy = g_board_y + (uint32_t)snake[0].y * g_cell;
        pg_window_rect(window, (struct pg_rect){hx + 1, hy + 1, g_cell - 2, g_cell - 2}, COLOR_SNAKE_HEAD);

        if (g_cell >= 12) {
            uint32_t e1x, e1y, e2x, e2y;
            if (dir_x == 1) {
                e1x = hx + g_cell - 5; e1y = hy + 3;
                e2x = hx + g_cell - 5; e2y = hy + g_cell - 6;
            } else if (dir_x == -1) {
                e1x = hx + 2; e1y = hy + 3;
                e2x = hx + 2; e2y = hy + g_cell - 6;
            } else if (dir_y == -1) {
                e1x = hx + 3;          e1y = hy + 2;
                e2x = hx + g_cell - 6; e2y = hy + 2;
            } else {
                e1x = hx + 3;          e1y = hy + g_cell - 5;
                e2x = hx + g_cell - 6; e2y = hy + g_cell - 5;
            }
            pg_window_rect(window, (struct pg_rect){e1x, e1y, 3, 3}, COLOR_SNAKE_EYE);
            pg_window_rect(window, (struct pg_rect){e2x, e2y, 3, 3}, COLOR_SNAKE_EYE);
        }
    }

    uint32_t foot_y = g_board_y + b_h + 10;
    pg_window_text(window, g_board_x, foot_y,
                   "WASD/Arrows: Move | P: Pause | M: Menu | Q: Exit",
                   COLOR_TEXT_MUTED);
}

static void draw_menu(struct pg_window *window) {
    uint32_t b_w = BOARD_COLS * g_cell;
    uint32_t b_h = BOARD_ROWS * g_cell;

    pg_window_rect(window, (struct pg_rect){g_board_x - 2, g_board_y - 2, b_w + 4, b_h + 4}, COLOR_BOARD_EDGE);
    pg_window_rect(window, (struct pg_rect){g_board_x, g_board_y, b_w, b_h}, COLOR_BOARD_BG);

    uint32_t mw = b_w > 60 ? b_w - 60 : b_w - 20;
    uint32_t mx = g_board_x + (b_w - mw) / 2;
    uint32_t my = g_board_y + 16;

    pg_window_text_sized(window, mx + 20, my, "S N A K E", COLOR_SNAKE_HEAD, 24);
    pg_window_text(window, mx + 20, my + 30, "Classic Arcade PureC", COLOR_TEXT_MUTED);

    uint32_t item_y = my + 54;
    for (int i = 0; i < ACT_COUNT; i++) {
        bool sel = (i == (int)menu_sel);
        uint32_t bg = sel ? COLOR_BTN_SEL : COLOR_BTN_BG;
        uint32_t fg = sel ? COLOR_BTN_TEXT : COLOR_TEXT_MAIN;

        pg_window_rect(window, (struct pg_rect){mx, item_y + (uint32_t)i * 38, mw, 30}, bg);

        if (i == ACT_PLAY) {
            pg_window_text(window, mx + 16, item_y + (uint32_t)i * 38 + 8,
                           sel ? "> Start Game" : "  Start Game", fg);
        } else if (i == ACT_DIFF) {
            char buf[48]; char *p = buf;
            p = append_text(p, sel ? "> Speed: < " : "  Speed: < ");
            p = append_text(p, DIFF_LABELS[diff_level]);
            p = append_text(p, " >");
            pg_window_text(window, mx + 16, item_y + (uint32_t)i * 38 + 8, buf, fg);
        } else if (i == ACT_WALLS) {
            char buf[48]; char *p = buf;
            p = append_text(p, sel ? "> Walls: < " : "  Walls: < ");
            p = append_text(p, wrap_walls ? "Pass" : "Death");
            p = append_text(p, " >");
            pg_window_text(window, mx + 16, item_y + (uint32_t)i * 38 + 8, buf, fg);
        } else if (i == ACT_EXIT) {
            pg_window_text(window, mx + 16, item_y + (uint32_t)i * 38 + 8,
                           sel ? "> Exit" : "  Exit", fg);
        }
    }

    uint32_t foot_y = g_board_y + b_h + 10;
    pg_window_text(window, g_board_x, foot_y,
                   "Up/Down: Select | Left/Right: Change | Enter: OK",
                   COLOR_TEXT_MUTED);
}

static void draw_overlay(struct pg_window *window) {
    if (mode != MODE_PAUSED && mode != MODE_GAMEOVER) return;

    uint32_t b_w = BOARD_COLS * g_cell;
    uint32_t b_h = BOARD_ROWS * g_cell;
    uint32_t ow = 220;
    uint32_t oh = 68;
    uint32_t ox = g_board_x + (b_w - ow) / 2;
    uint32_t oy = g_board_y + (b_h - oh) / 2;

    pg_window_rect(window, (struct pg_rect){ox - 2, oy - 2, ow + 4, oh + 4}, COLOR_BOARD_EDGE);

    if (mode == MODE_PAUSED) {
        pg_window_rect(window, (struct pg_rect){ox, oy, ow, oh}, 0xF9E2AF);
        pg_window_text(window, ox + 80, oy + 12, "PAUSED", COLOR_BTN_TEXT);
        pg_window_text(window, ox + 22, oy + 38, "Press P to continue | M menu", COLOR_BTN_TEXT);
    } else if (mode == MODE_GAMEOVER) {
        pg_window_rect(window, (struct pg_rect){ox, oy, ow, oh}, 0xF38BA8);
        pg_window_text(window, ox + 68, oy + 12, "GAME OVER", COLOR_BTN_TEXT);
        pg_window_text(window, ox + 18, oy + 38, "Press R to restart | M menu", COLOR_BTN_TEXT);
    }
}

static void redraw(struct pg_window *window) {
    pg_window_begin(window);
    draw_hud(window);
    if (mode == MODE_MENU) {
        draw_menu(window);
    } else {
        draw_board(window);
        draw_overlay(window);
    }
    pg_window_end(window);
}

static void set_dir(int16_t dx, int16_t dy) {
    if (dir_x + dx == 0 && dir_y + dy == 0) return;
    if (next_dx + dx == 0 && next_dy + dy == 0) return;
    next_dx = dx;
    next_dy = dy;
    sfx_turn_blip();
}

static bool advance_snake(void) {
    dir_x = next_dx;
    dir_y = next_dy;

    struct point next = {
        (int16_t)(snake[0].x + dir_x),
        (int16_t)(snake[0].y + dir_y)
    };

    if (wrap_walls) {
        if (next.x < 0) next.x = BOARD_COLS - 1;
        else if (next.x >= BOARD_COLS) next.x = 0;
        if (next.y < 0) next.y = BOARD_ROWS - 1;
        else if (next.y >= BOARD_ROWS) next.y = 0;
    } else {
        if (next.x < 0 || next.x >= BOARD_COLS || next.y < 0 || next.y >= BOARD_ROWS) {
            return false;
        }
    }

    bool ate = (next.x == food_pos.x && next.y == food_pos.y);
    uint16_t check_len = ate ? snake_len : snake_len - 1;
    for (uint16_t i = 0; i < check_len; i++) {
        if (snake[i].x == next.x && snake[i].y == next.y) return false;
    }

    if (ate && snake_len < MAX_LENGTH) {
        snake_len++;
        score++;
        if (score > high_score) high_score = score;
        sfx_eat_sound();
        spawn_food();
    }

    for (uint16_t i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = next;
    return true;
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

        if (mode == MODE_MENU) {
            if (k == 'w' || k == 'W') {
                if (menu_sel > 0) menu_sel--;
                else menu_sel = ACT_COUNT - 1;
                sfx_turn_blip();
                redraw(window);
            } else if (k == 's' || k == 'S') {
                if (menu_sel + 1 < ACT_COUNT) menu_sel++;
                else menu_sel = 0;
                sfx_turn_blip();
                redraw(window);
            } else if (k == 'a' || k == 'A') {
                if (menu_sel == ACT_DIFF) {
                    if (diff_level > 0) diff_level--;
                    else diff_level = 2;
                } else if (menu_sel == ACT_WALLS) {
                    wrap_walls = !wrap_walls;
                }
                sfx_turn_blip();
                redraw(window);
            } else if (k == 'd' || k == 'D') {
                if (menu_sel == ACT_DIFF) {
                    diff_level = (diff_level + 1) % 3;
                } else if (menu_sel == ACT_WALLS) {
                    wrap_walls = !wrap_walls;
                }
                sfx_turn_blip();
                redraw(window);
            } else if (k == '\n' || k == '\r' || k == ' ') {
                if (menu_sel == ACT_PLAY) {
                    reset_snake_game();
                    mode = MODE_PLAYING;
                    redraw(window);
                } else if (menu_sel == ACT_DIFF) {
                    diff_level = (diff_level + 1) % 3;
                    redraw(window);
                } else if (menu_sel == ACT_WALLS) {
                    wrap_walls = !wrap_walls;
                    redraw(window);
                } else if (menu_sel == ACT_EXIT) {
                    pg_window_close(window);
                }
            }
            return;
        }

        if (mode == MODE_PAUSED) {
            if (k == 'p' || k == 'P' || k == ' ') {
                mode = MODE_PLAYING;
                redraw(window);
            } else if (k == 'm' || k == 'M') {
                mode = MODE_MENU;
                redraw(window);
            }
            return;
        }

        if (mode == MODE_GAMEOVER) {
            if (k == 'r' || k == 'R' || k == ' ' || k == '\n') {
                reset_snake_game();
                mode = MODE_PLAYING;
                redraw(window);
            } else if (k == 'm' || k == 'M') {
                mode = MODE_MENU;
                redraw(window);
            }
            return;
        }

        if (mode == MODE_PLAYING) {
            if (k == 'p' || k == 'P') {
                mode = MODE_PAUSED;
                redraw(window);
            } else if (k == 'm' || k == 'M') {
                mode = MODE_MENU;
                redraw(window);
            } else if (k == 'w' || k == 'W') set_dir(0, -1);
            else if (k == 's' || k == 'S') set_dir(0, 1);
            else if (k == 'a' || k == 'A') set_dir(-1, 0);
            else if (k == 'd' || k == 'D') set_dir(1, 0);
        }
    } else if (event->type == PG_EVENT_SPECIAL_KEY) {
        if (mode == MODE_MENU) {
            if (event->key == KEYBOARD_SPECIAL_UP) {
                if (menu_sel > 0) menu_sel--;
                else menu_sel = ACT_COUNT - 1;
                sfx_turn_blip();
                redraw(window);
            } else if (event->key == KEYBOARD_SPECIAL_DOWN) {
                if (menu_sel + 1 < ACT_COUNT) menu_sel++;
                else menu_sel = 0;
                sfx_turn_blip();
                redraw(window);
            } else if (event->key == KEYBOARD_SPECIAL_LEFT) {
                if (menu_sel == ACT_DIFF) {
                    if (diff_level > 0) diff_level--;
                    else diff_level = 2;
                } else if (menu_sel == ACT_WALLS) {
                    wrap_walls = !wrap_walls;
                }
                sfx_turn_blip();
                redraw(window);
            } else if (event->key == KEYBOARD_SPECIAL_RIGHT) {
                if (menu_sel == ACT_DIFF) {
                    diff_level = (diff_level + 1) % 3;
                } else if (menu_sel == ACT_WALLS) {
                    wrap_walls = !wrap_walls;
                }
                sfx_turn_blip();
                redraw(window);
            }
        } else if (mode == MODE_PLAYING) {
            if (event->key == KEYBOARD_SPECIAL_UP) set_dir(0, -1);
            else if (event->key == KEYBOARD_SPECIAL_DOWN) set_dir(0, 1);
            else if (event->key == KEYBOARD_SPECIAL_LEFT) set_dir(-1, 0);
            else if (event->key == KEYBOARD_SPECIAL_RIGHT) set_dir(1, 0);
        }
    }
}

static int snake_app_main(void) {
    compute_window_layout();

    struct pg_window window;
    if (!pg_window_center(&window, "Snake", g_win_w, g_win_h)) {
        return 1;
    }

    sfx_init_all();
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
        if (sfx_cool > 0) sfx_cool--;

        if (pg_window_is_minimized(&window)) continue;
        if (mode != MODE_PLAYING) continue;

        step_timer_ms += 16;
        if (step_timer_ms >= DIFF_SPEEDS[diff_level]) {
            step_timer_ms = 0;
            if (!advance_snake()) {
                sfx_die_sound();
                mode = MODE_GAMEOVER;
            }
            redraw(&window);
        }
    }

    pa_sfx_stop();
    pa_stop_tone();
    pg_window_close(&window);
    return 0;
}

void _start(void) {
    pc_exit(snake_app_main());
}
