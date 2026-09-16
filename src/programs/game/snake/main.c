#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"

#define BOARD_WIDTH  30
#define BOARD_HEIGHT 18
#define CELL_SIZE    18
#define MAX_LENGTH   (BOARD_WIDTH * BOARD_HEIGHT)

#define COLOR_BG          0x1E1E2E
#define COLOR_PANEL_BG    0x181825
#define COLOR_PANEL_EDGE  0x45475A
#define COLOR_SNAKE_BODY  0xA6E3A1
#define COLOR_SNAKE_HEAD  0x89B4FA
#define COLOR_SNAKE_EYE   0x11111B
#define COLOR_FOOD        0xF38BA8
#define COLOR_FOOD_INNER  0xF9E2AF
#define COLOR_TEXT        0xCDD6F4
#define COLOR_TEXT_MUTED  0x6C7086
#define COLOR_ACCENT      0xCBA6F7
#define COLOR_WARN        0xFAB387
#define COLOR_DANGER      0xF38BA8
#define COLOR_OVERLAY_BG  0x11111B
#define COLOR_BTN_SEL     0x89B4FA
#define COLOR_BTN_BG      0x313244

enum game_state {
    STATE_MENU,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_GAMEOVER
};

enum menu_item {
    MENU_START = 0,
    MENU_DIFFICULTY,
    MENU_WALLS,
    MENU_EXIT,
    MENU_COUNT
};

enum difficulty_level {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD,
    DIFF_COUNT
};

static const char *DIFF_NAMES[DIFF_COUNT] = {
    "Easy  (Slow)",
    "Normal (Mid)",
    "Hard  (Fast)"
};

static const uint32_t DIFF_DELAYS[DIFF_COUNT] = {
    140,
    95,
    60
};

struct point {
    int16_t x;
    int16_t y;
};

static struct point body[MAX_LENGTH];
static struct point food;
static uint16_t length;
static int16_t direction_x;
static int16_t direction_y;
static int16_t next_dir_x;
static int16_t next_dir_y;
static uint32_t random_state = 0x51A7E123;
static uint32_t score = 0;
static uint32_t high_score = 0;

static enum game_state current_state = STATE_MENU;
static enum menu_item selected_menu_item = MENU_START;
static enum difficulty_level selected_difficulty = DIFF_NORMAL;
static bool walls_enabled = true;

static int16_t sfx_turn[1100];
static uint32_t sfx_turn_frames;
static int16_t sfx_eat[2700];
static uint32_t sfx_eat_frames;
static int16_t sfx_die[11100];
static uint32_t sfx_die_frames;
static uint32_t sfx_cooldown_ticks;

static uint32_t sfx_preload(const char *name, int16_t *buffer, uint32_t capacity){
    static const char *dirs[] = {"/game/sound/", "/bin/sound/"};
    for(uint32_t d = 0; d < sizeof(dirs)/sizeof(dirs[0]); d++){
        char path[64];
        uint32_t pos = 0;
        const char *dir = dirs[d];
        while(dir[pos] && pos + 1 < sizeof(path)){
            path[pos] = dir[pos];
            pos++;
        }
        for(uint32_t i = 0; name[i] && pos + 1 < sizeof(path); i++){
            path[pos++] = name[i];
        }
        path[pos] = '\0';
        uint32_t frames = 0;
        if(pa_wav_load(path, buffer, capacity, &frames) == 0 && frames > 0){
            return frames;
        }
    }
    return 0;
}

static void sfx_play_buffer(const int16_t *buffer, uint32_t frames,
                            uint16_t fallback_hz, uint32_t fallback_ms){
    if(frames > 0 && pa_sfx_play(buffer, frames) == 0) return;
    pa_play_tone(fallback_hz, fallback_ms);
}

static void sfx_turn_blip(void){
    if(sfx_cooldown_ticks > 0) return;
    sfx_play_buffer(sfx_turn, sfx_turn_frames, 1200, 40);
}

static void sfx_eat_chime(void){
    sfx_cooldown_ticks = 1;
    sfx_play_buffer(sfx_eat, sfx_eat_frames, 950, 75);
}

static void sfx_die_jingle(void){
    sfx_cooldown_ticks = 4;
    sfx_play_buffer(sfx_die, sfx_die_frames, 280, 260);
}

static void sfx_menu_move(void){
    sfx_play_buffer(sfx_turn, sfx_turn_frames, 1400, 25);
}

static void sfx_menu_select(void){
    sfx_play_buffer(sfx_eat, sfx_eat_frames, 1100, 50);
}

static void sfx_init(void){
    sfx_turn_frames = sfx_preload("turn.wav", sfx_turn, sizeof(sfx_turn)/sizeof(sfx_turn[0]));
    sfx_eat_frames  = sfx_preload("eat.wav",  sfx_eat,  sizeof(sfx_eat)/sizeof(sfx_eat[0]));
    sfx_die_frames  = sfx_preload("die.wav",  sfx_die,  sizeof(sfx_die)/sizeof(sfx_die[0]));
}

static void sfx_shutdown(void){
    pa_sfx_stop();
    pa_stop_tone();
}

static uint32_t random_value(void){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void format_u32(uint32_t val, char *buf, uint32_t size){
    if(size == 0) return;
    if(val == 0){
        if(size >= 2){ buf[0] = '0'; buf[1] = '\0'; }
        else { buf[0] = '\0'; }
        return;
    }
    char tmp[16];
    int pos = 0;
    while(val > 0 && pos < (int)sizeof(tmp)){
        tmp[pos++] = '0' + (val % 10);
        val /= 10;
    }
    uint32_t out = 0;
    while(pos > 0 && out + 1 < size){
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

static bool occupied(int16_t x, int16_t y){
    for(uint16_t i = 0; i < length; i++){
        if(body[i].x == x && body[i].y == y) return true;
    }
    return false;
}

static void place_food(void){
    uint32_t tries = 0;
    do {
        food.x = (int16_t)(random_value() % BOARD_WIDTH);
        food.y = (int16_t)(random_value() % BOARD_HEIGHT);
        tries++;
        if(tries > 1000) break;
    } while(occupied(food.x, food.y));
}

static void reset_game(void){
    length = 4;
    direction_x = 1;
    direction_y = 0;
    next_dir_x = 1;
    next_dir_y = 0;
    score = 0;
    for(uint16_t i = 0; i < length; i++){
        body[i].x = BOARD_WIDTH / 2 - (int16_t)i;
        body[i].y = BOARD_HEIGHT / 2;
    }
    place_food();
}

static void draw_cell_body(uint32_t origin_x, uint32_t origin_y, struct point pt){
    uint32_t px = origin_x + pt.x * CELL_SIZE;
    uint32_t py = origin_y + pt.y * CELL_SIZE;
    pc_draw_rect(px, py, CELL_SIZE - 2, CELL_SIZE - 2, COLOR_SNAKE_BODY);
}

static void draw_cell_head(uint32_t origin_x, uint32_t origin_y, struct point pt, int16_t dx, int16_t dy){
    uint32_t px = origin_x + pt.x * CELL_SIZE;
    uint32_t py = origin_y + pt.y * CELL_SIZE;
    pc_draw_rect(px, py, CELL_SIZE - 2, CELL_SIZE - 2, COLOR_SNAKE_HEAD);

    if(CELL_SIZE >= 14){
        uint32_t e1x, e1y, e2x, e2y;
        if(dx == 1){
            e1x = px + CELL_SIZE - 6; e1y = py + 3;
            e2x = px + CELL_SIZE - 6; e2y = py + CELL_SIZE - 7;
        } else if(dx == -1){
            e1x = px + 2; e1y = py + 3;
            e2x = px + 2; e2y = py + CELL_SIZE - 7;
        } else if(dy == -1){
            e1x = px + 3;             e1y = py + 2;
            e2x = px + CELL_SIZE - 7; e2y = py + 2;
        } else {
            e1x = px + 3;             e1y = py + CELL_SIZE - 6;
            e2x = px + CELL_SIZE - 7; e2y = py + CELL_SIZE - 6;
        }
        pc_draw_rect(e1x, e1y, 3, 3, COLOR_SNAKE_EYE);
        pc_draw_rect(e2x, e2y, 3, 3, COLOR_SNAKE_EYE);
    }
}

static void draw_cell_food(uint32_t origin_x, uint32_t origin_y, struct point pt){
    uint32_t px = origin_x + pt.x * CELL_SIZE;
    uint32_t py = origin_y + pt.y * CELL_SIZE;
    pc_draw_rect(px, py, CELL_SIZE - 2, CELL_SIZE - 2, COLOR_FOOD);
    if(CELL_SIZE >= 8){
        pc_draw_rect(px + 4, py + 4, CELL_SIZE - 10, CELL_SIZE - 10, COLOR_FOOD_INNER);
    }
}

static void draw_cell_empty(uint32_t origin_x, uint32_t origin_y, struct point pt){
    uint32_t px = origin_x + pt.x * CELL_SIZE;
    uint32_t py = origin_y + pt.y * CELL_SIZE;
    pc_draw_rect(px, py, CELL_SIZE - 2, CELL_SIZE - 2, COLOR_PANEL_BG);
}

static void draw_board_full(uint32_t origin_x, uint32_t origin_y){
    pc_draw_rect(origin_x - 4, origin_y - 4,
                 BOARD_WIDTH * CELL_SIZE + 8, BOARD_HEIGHT * CELL_SIZE + 8,
                 COLOR_PANEL_EDGE);
    pc_draw_rect(origin_x, origin_y,
                 BOARD_WIDTH * CELL_SIZE, BOARD_HEIGHT * CELL_SIZE,
                 COLOR_PANEL_BG);

    draw_cell_food(origin_x, origin_y, food);

    for(uint16_t i = 1; i < length; i++){
        draw_cell_body(origin_x, origin_y, body[i]);
    }
    if(length > 0){
        draw_cell_head(origin_x, origin_y, body[0], direction_x, direction_y);
    }
}

static void draw_hud(uint32_t origin_x, uint32_t origin_y){
    uint32_t board_w = BOARD_WIDTH * CELL_SIZE;
    pc_draw_rect(origin_x - 4, origin_y - 36, board_w + 8, 30, COLOR_BG);

    char s_score[16];
    char s_high[16];
    format_u32(score, s_score, sizeof(s_score));
    format_u32(high_score, s_high, sizeof(s_high));

    char line1[64];
    uint32_t p = 0;
    const char *p1 = "Score: ";
    while(*p1) line1[p++] = *p1++;
    const char *p2 = s_score;
    while(*p2) line1[p++] = *p2++;
    const char *p3 = "   Best: ";
    while(*p3) line1[p++] = *p3++;
    const char *p4 = s_high;
    while(*p4) line1[p++] = *p4++;
    const char *p5 = walls_enabled ? "   Walls: ON" : "   Walls: OFF";
    while(*p5) line1[p++] = *p5++;
    line1[p] = '\0';

    pc_draw_text(origin_x, origin_y - 30, line1, COLOR_TEXT, COLOR_BG);

    uint32_t bot_y = origin_y + BOARD_HEIGHT * CELL_SIZE + 10;
    pc_draw_rect(origin_x - 4, bot_y - 2, board_w + 8, 22, COLOR_BG);
    pc_draw_text(origin_x, bot_y, "WASD/Arrows: Move | P/Space: Pause | M: Menu | Q: Exit",
                 COLOR_TEXT_MUTED, COLOR_BG);
}

static void draw_overlay_box(uint32_t origin_x, uint32_t origin_y,
                             const char *title, uint32_t title_color,
                             const char *line1, const char *line2, const char *line3){
    uint32_t board_w = BOARD_WIDTH * CELL_SIZE;
    uint32_t board_h = BOARD_HEIGHT * CELL_SIZE;
    uint32_t box_w = 340;
    uint32_t box_h = 130;
    uint32_t bx = origin_x + (board_w > box_w ? (board_w - box_w)/2 : 0);
    uint32_t by = origin_y + (board_h > box_h ? (board_h - box_h)/2 : 0);

    pc_draw_rect(bx - 3, by - 3, box_w + 6, box_h + 6, COLOR_PANEL_EDGE);
    pc_draw_rect(bx, by, box_w, box_h, COLOR_OVERLAY_BG);

    pc_draw_rect(bx, by, box_w, 32, title_color);
    pc_draw_text(bx + 18, by + 8, title, 0x11111B, title_color);

    if(line1) pc_draw_text(bx + 20, by + 46, line1, COLOR_TEXT, COLOR_OVERLAY_BG);
    if(line2) pc_draw_text(bx + 20, by + 72, line2, COLOR_TEXT_MUTED, COLOR_OVERLAY_BG);
    if(line3) pc_draw_text(bx + 20, by + 98, line3, COLOR_ACCENT, COLOR_OVERLAY_BG);
}

static void draw_menu_screen(uint32_t disp_w, uint32_t disp_h){
    pc_display_clear(COLOR_BG);

    uint32_t center_x = disp_w / 2;
    uint32_t start_y = disp_h > 360 ? (disp_h - 360) / 2 : 20;

    pc_draw_text_sized(center_x - 140, start_y, "S N A K E", COLOR_SNAKE_BODY, COLOR_BG, 24);
    pc_draw_text(center_x - 136, start_y + 36, "PureC Classic Edition", COLOR_TEXT_MUTED, COLOR_BG);

    uint32_t mw = 360;
    uint32_t mh = 220;
    uint32_t mx = center_x > mw/2 ? center_x - mw/2 : 10;
    uint32_t my = start_y + 70;

    pc_draw_rect(mx - 2, my - 2, mw + 4, mh + 4, COLOR_PANEL_EDGE);
    pc_draw_rect(mx, my, mw, mh, COLOR_PANEL_BG);

    for(int i = 0; i < MENU_COUNT; i++){
        uint32_t iy = my + 20 + i * 44;
        bool sel = (i == (int)selected_menu_item);

        if(sel){
            pc_draw_rect(mx + 16, iy - 4, mw - 32, 34, COLOR_BTN_SEL);
        } else {
            pc_draw_rect(mx + 16, iy - 4, mw - 32, 34, COLOR_BTN_BG);
        }

        uint32_t text_col = sel ? 0x11111B : COLOR_TEXT;
        uint32_t bg_col   = sel ? COLOR_BTN_SEL : COLOR_BTN_BG;

        if(i == MENU_START){
            pc_draw_text(mx + 30, iy + 4, sel ? "> Play Game" : "  Play Game", text_col, bg_col);
        } else if(i == MENU_DIFFICULTY){
            char buf[64];
            uint32_t p = 0;
            const char *hdr = sel ? "> Speed: < " : "  Speed: < ";
            while(*hdr) buf[p++] = *hdr++;
            const char *dn = DIFF_NAMES[selected_difficulty];
            while(*dn) buf[p++] = *dn++;
            buf[p++] = ' ';
            buf[p++] = '>';
            buf[p] = '\0';
            pc_draw_text(mx + 30, iy + 4, buf, text_col, bg_col);
        } else if(i == MENU_WALLS){
            char buf[64];
            uint32_t p = 0;
            const char *hdr = sel ? "> Walls: < " : "  Walls: < ";
            while(*hdr) buf[p++] = *hdr++;
            const char *wn = walls_enabled ? "Classic (Death) >" : "Wrap (Pass)    >";
            while(*wn) buf[p++] = *wn++;
            buf[p] = '\0';
            pc_draw_text(mx + 30, iy + 4, buf, text_col, bg_col);
        } else if(i == MENU_EXIT){
            pc_draw_text(mx + 30, iy + 4, sel ? "> Exit" : "  Exit", text_col, bg_col);
        }
    }

    pc_draw_text(center_x - 170, my + mh + 20,
                 "Up/Down: Select | Left/Right: Change | Enter: OK",
                 COLOR_TEXT, COLOR_BG);
    pc_draw_text(center_x - 120, my + mh + 42,
                 "Press ESC / Q to Exit", COLOR_TEXT_MUTED, COLOR_BG);
}

static bool advance_game(bool *ate_food, struct point *old_tail){
    direction_x = next_dir_x;
    direction_y = next_dir_y;

    struct point next = {
        (int16_t)(body[0].x + direction_x),
        (int16_t)(body[0].y + direction_y)
    };

    if(walls_enabled){
        if(next.x < 0 || next.x >= BOARD_WIDTH || next.y < 0 || next.y >= BOARD_HEIGHT){
            return false;
        }
    } else {
        if(next.x < 0) next.x = BOARD_WIDTH - 1;
        else if(next.x >= BOARD_WIDTH) next.x = 0;
        if(next.y < 0) next.y = BOARD_HEIGHT - 1;
        else if(next.y >= BOARD_HEIGHT) next.y = 0;
    }

    bool ate = (next.x == food.x && next.y == food.y);
    uint16_t collision_len = ate ? length : length - 1;
    for(uint16_t i = 0; i < collision_len; i++){
        if(body[i].x == next.x && body[i].y == next.y){
            return false;
        }
    }

    *old_tail = body[length - 1];
    if(ate && length < MAX_LENGTH){
        length++;
        score++;
        if(score > high_score) high_score = score;
    }
    for(uint16_t i = length - 1; i > 0; i--){
        body[i] = body[i - 1];
    }
    body[0] = next;

    if(ate) place_food();
    *ate_food = ate;
    return true;
}

static void draw_step(uint32_t origin_x, uint32_t origin_y, bool ate_food, struct point old_tail){
    if(!ate_food){
        draw_cell_empty(origin_x, origin_y, old_tail);
    }
    if(length > 1){
        draw_cell_body(origin_x, origin_y, body[1]);
    }
    draw_cell_head(origin_x, origin_y, body[0], direction_x, direction_y);
    if(ate_food){
        draw_cell_food(origin_x, origin_y, food);
        draw_hud(origin_x, origin_y);
    }
}

static void change_direction(int16_t nx, int16_t ny){
    if(direction_x + nx == 0 && direction_y + ny == 0) return;
    if(next_dir_x + nx == 0 && next_dir_y + ny == 0) return;
    next_dir_x = nx;
    next_dir_y = ny;
    sfx_turn_blip();
}

static int snake_main(void){
    struct pc_display_info display;
    if(!pc_display_get_info(&display)) return 1;

    sfx_init();

    uint32_t disp_w = display.width > 0 ? display.width : 640;
    uint32_t disp_h = display.height > 0 ? display.height : 480;

    uint32_t board_pixel_w = BOARD_WIDTH * CELL_SIZE;
    uint32_t board_pixel_h = BOARD_HEIGHT * CELL_SIZE;
    uint32_t origin_x = disp_w > board_pixel_w ? (disp_w - board_pixel_w) / 2 : 10;
    uint32_t origin_y = disp_h > board_pixel_h + 60 ? (disp_h - board_pixel_h) / 2 + 20 : 45;

    bool app_running = true;

    while(app_running){
        if(current_state == STATE_MENU){
            draw_menu_screen(disp_w, disp_h);

            bool in_menu = true;
            while(in_menu && app_running){
                int32_t key;
                int32_t sp = pc_try_get_special();

                if(sp == KEYBOARD_SPECIAL_UP){
                    if(selected_menu_item > 0) selected_menu_item--;
                    else selected_menu_item = MENU_COUNT - 1;
                    sfx_menu_move();
                    draw_menu_screen(disp_w, disp_h);
                } else if(sp == KEYBOARD_SPECIAL_DOWN){
                    if(selected_menu_item + 1 < MENU_COUNT) selected_menu_item++;
                    else selected_menu_item = 0;
                    sfx_menu_move();
                    draw_menu_screen(disp_w, disp_h);
                } else if(sp == KEYBOARD_SPECIAL_LEFT){
                    if(selected_menu_item == MENU_DIFFICULTY){
                        if(selected_difficulty > 0) selected_difficulty--;
                        else selected_difficulty = DIFF_COUNT - 1;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    } else if(selected_menu_item == MENU_WALLS){
                        walls_enabled = !walls_enabled;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    }
                } else if(sp == KEYBOARD_SPECIAL_RIGHT){
                    if(selected_menu_item == MENU_DIFFICULTY){
                        if(selected_difficulty + 1 < DIFF_COUNT) selected_difficulty++;
                        else selected_difficulty = 0;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    } else if(selected_menu_item == MENU_WALLS){
                        walls_enabled = !walls_enabled;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    }
                }

                while((key = pc_try_getchar()) >= 0){
                    if(key == 'q' || key == 'Q' || key == 27){
                        app_running = false;
                        in_menu = false;
                        break;
                    } else if(key == 'w' || key == 'W' || key == 'k' || key == 'K'){
                        if(selected_menu_item > 0) selected_menu_item--;
                        else selected_menu_item = MENU_COUNT - 1;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    } else if(key == 's' || key == 'S' || key == 'j' || key == 'J'){
                        if(selected_menu_item + 1 < MENU_COUNT) selected_menu_item++;
                        else selected_menu_item = 0;
                        sfx_menu_move();
                        draw_menu_screen(disp_w, disp_h);
                    } else if(key == 'a' || key == 'A' || key == 'h' || key == 'H'){
                        if(selected_menu_item == MENU_DIFFICULTY){
                            if(selected_difficulty > 0) selected_difficulty--;
                            else selected_difficulty = DIFF_COUNT - 1;
                            sfx_menu_move();
                            draw_menu_screen(disp_w, disp_h);
                        } else if(selected_menu_item == MENU_WALLS){
                            walls_enabled = !walls_enabled;
                            sfx_menu_move();
                            draw_menu_screen(disp_w, disp_h);
                        }
                    } else if(key == 'd' || key == 'D' || key == 'l' || key == 'L'){
                        if(selected_menu_item == MENU_DIFFICULTY){
                            if(selected_difficulty + 1 < DIFF_COUNT) selected_difficulty++;
                            else selected_difficulty = 0;
                            sfx_menu_move();
                            draw_menu_screen(disp_w, disp_h);
                        } else if(selected_menu_item == MENU_WALLS){
                            walls_enabled = !walls_enabled;
                            sfx_menu_move();
                            draw_menu_screen(disp_w, disp_h);
                        }
                    } else if(key == '\n' || key == '\r' || key == ' '){
                        sfx_menu_select();
                        if(selected_menu_item == MENU_START){
                            reset_game();
                            current_state = STATE_PLAYING;
                            in_menu = false;
                        } else if(selected_menu_item == MENU_DIFFICULTY){
                            selected_difficulty = (selected_difficulty + 1) % DIFF_COUNT;
                            draw_menu_screen(disp_w, disp_h);
                        } else if(selected_menu_item == MENU_WALLS){
                            walls_enabled = !walls_enabled;
                            draw_menu_screen(disp_w, disp_h);
                        } else if(selected_menu_item == MENU_EXIT){
                            app_running = false;
                            in_menu = false;
                        }
                    }
                }

                pa_update();
                pc_sleep(25);
            }
        }

        if(current_state == STATE_PLAYING && app_running){
            pc_display_clear(COLOR_BG);
            draw_board_full(origin_x, origin_y);
            draw_hud(origin_x, origin_y);

            uint32_t step_ms = DIFF_DELAYS[selected_difficulty];
            uint32_t sub_step_ms = 20;
            uint32_t sub_steps = step_ms / sub_step_ms;
            if(sub_steps < 1) sub_steps = 1;

            bool in_game = true;

            while(in_game && app_running){
                for(uint32_t s = 0; s < sub_steps && in_game && app_running; s++){
                    int32_t sp = pc_try_get_special();
                    if(sp == KEYBOARD_SPECIAL_UP)         change_direction(0, -1);
                    else if(sp == KEYBOARD_SPECIAL_DOWN)  change_direction(0, 1);
                    else if(sp == KEYBOARD_SPECIAL_LEFT)  change_direction(-1, 0);
                    else if(sp == KEYBOARD_SPECIAL_RIGHT) change_direction(1, 0);

                    int32_t key;
                    while((key = pc_try_getchar()) >= 0){
                        if(key == 'q' || key == 'Q' || key == 27){
                            current_state = STATE_MENU;
                            in_game = false;
                            break;
                        } else if(key == 'm' || key == 'M'){
                            current_state = STATE_MENU;
                            in_game = false;
                            break;
                        } else if(key == 'p' || key == 'P' || key == ' '){
                            current_state = STATE_PAUSED;
                            in_game = false;
                            break;
                        } else if(key == 'w' || key == 'W' || key == 'k' || key == 'K'){
                            change_direction(0, -1);
                        } else if(key == 's' || key == 'S' || key == 'j' || key == 'J'){
                            change_direction(0, 1);
                        } else if(key == 'a' || key == 'A' || key == 'h' || key == 'H'){
                            change_direction(-1, 0);
                        } else if(key == 'd' || key == 'D' || key == 'l' || key == 'L'){
                            change_direction(1, 0);
                        }
                    }

                    pa_update();
                    pc_sleep(sub_step_ms);
                }

                if(!in_game || !app_running) break;

                if(sfx_cooldown_ticks > 0) sfx_cooldown_ticks--;

                bool ate_food = false;
                struct point old_tail;
                if(!advance_game(&ate_food, &old_tail)){
                    sfx_die_jingle();
                    current_state = STATE_GAMEOVER;
                    in_game = false;
                    break;
                }

                if(ate_food){
                    sfx_eat_chime();
                }

                draw_step(origin_x, origin_y, ate_food, old_tail);
            }
        }

        if(current_state == STATE_PAUSED && app_running){
            draw_overlay_box(origin_x, origin_y,
                             "P A U S E D", COLOR_WARN,
                             "Game is currently paused",
                             "Press P or Space to Resume",
                             "Press M for Menu | Q to Exit");

            bool in_pause = true;
            while(in_pause && app_running){
                int32_t key = pc_try_getchar();
                if(key >= 0){
                    if(key == 'p' || key == 'P' || key == ' ' || key == '\n' || key == '\r'){
                        current_state = STATE_PLAYING;
                        in_pause = false;
                    } else if(key == 'm' || key == 'M' || key == 27){
                        current_state = STATE_MENU;
                        in_pause = false;
                    } else if(key == 'q' || key == 'Q'){
                        app_running = false;
                        in_pause = false;
                    }
                }
                pa_update();
                pc_sleep(30);
            }
        }

        if(current_state == STATE_GAMEOVER && app_running){
            char final_msg[64];
            char s_sc[16], s_hi[16];
            format_u32(score, s_sc, sizeof(s_sc));
            format_u32(high_score, s_hi, sizeof(s_hi));

            uint32_t p = 0;
            const char *m1 = "Final Score: ";
            while(*m1) final_msg[p++] = *m1++;
            const char *m2 = s_sc;
            while(*m2) final_msg[p++] = *m2++;
            const char *m3 = "  (Best: ";
            while(*m3) final_msg[p++] = *m3++;
            const char *m4 = s_hi;
            while(*m4) final_msg[p++] = *m4++;
            final_msg[p++] = ')';
            final_msg[p] = '\0';

            draw_overlay_box(origin_x, origin_y,
                             "G A M E   O V E R", COLOR_DANGER,
                             final_msg,
                             "Press R or Enter to Play Again",
                             "Press M for Menu | Q to Exit");

            bool in_gameover = true;
            while(in_gameover && app_running){
                int32_t key = pc_try_getchar();
                if(key >= 0){
                    if(key == 'r' || key == 'R' || key == '\n' || key == '\r' || key == ' '){
                        reset_game();
                        current_state = STATE_PLAYING;
                        in_gameover = false;
                    } else if(key == 'm' || key == 'M' || key == 27){
                        current_state = STATE_MENU;
                        in_gameover = false;
                    } else if(key == 'q' || key == 'Q'){
                        app_running = false;
                        in_gameover = false;
                    }
                }
                pa_update();
                pc_sleep(30);
            }
        }
    }

    sfx_shutdown();
    pc_display_clear(COLOR_BG);
    return 0;
}

void _start(void){
    pc_exit(snake_main());
}
