#include <purec.h>
#define COLOR_BG        0x1E1E2E
#define COLOR_BOARD_BG  0x181825
#define COLOR_TILE      0x89B4FA
#define COLOR_TILE_HL   0xB4BEFE
#define COLOR_TILE_TXT  0x1E1E2E
#define COLOR_EMPTY     0x313244
#define COLOR_WIN_BG    0xA6E3A1
#define COLOR_WIN_TXT   0x1E1E2E
#define COLOR_HEADER    0xCDD6F4
#define COLOR_SUBTEXT   0x6C7086
#define COLOR_BORDER    0x45475A
#define WIN_X      100
#define WIN_Y       60
#define WIN_W      500
#define WIN_H      560
#define BOARD_X    (WIN_X + 40)
#define BOARD_Y    (WIN_Y + 110)
#define BOARD_SIZE 420
#define TILE_SIZE  (BOARD_SIZE / 4)
#define GAP          4
static int board[4][4];
static int empty_r, empty_c;
static int moves;
static int won;

static void int_to_str(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0; while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static int str_len(const char *s) {int n = 0; while (s[n]) n++; return n;}

static int check_win(void) {
    int expected = 1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (r == 3 && c == 3) { if (board[r][c] != 0) return 0; }
            else { if (board[r][c] != expected++) return 0; }
        }
    return 1;
}

static unsigned int rng_state = 0;
static unsigned int rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void shuffle(void) {
    int val = 1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            board[r][c] = (r == 3 && c == 3) ? 0 : val++;
        }
    empty_r = 3; empty_c = 3;
    struct cpu_monitor_info cpu;
    if (pc_cpu_info(&cpu)) rng_state = (unsigned int)cpu.uptime_ms;
    else rng_state = 0xDEADBEEF;
    int dr[] = {-1, 1,  0, 0};
    int dc[] = { 0, 0, -1, 1};
    for (int i = 0; i < 500; i++) {
        int dir = (int)(rng_next() % 4);
        int nr = empty_r + dr[dir];
        int nc = empty_c + dc[dir];
        if (nr < 0 || nr > 3 || nc < 0 || nc > 3) continue;
        board[empty_r][empty_c] = board[nr][nc];
        board[nr][nc] = 0;
        empty_r = nr; empty_c = nc;
    }
    moves = 0;
    won = 0;
}

static int try_move(int tr, int tc) {
    if (tr < 0 || tr > 3 || tc < 0 || tc > 3) return 0;
    if (board[tr][tc] == 0) return 0;
    int dr = tr - empty_r, dc = tc - empty_c;
    if ((dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1))) {
        board[empty_r][empty_c] = board[tr][tc];
        board[tr][tc] = 0;
        empty_r = tr; empty_c = tc;
        moves++;
        if (check_win()) won = 1;
        return 1;
    }
    return 0;
}

static void draw_centered_number(int n, int rx, int ry, int rw, int rh, uint32_t fg, uint32_t bg) {
    char buf[4];
    int_to_str(n, buf);
    int len = str_len(buf);
    int scale = 3;
    int glyph_w = 8 * scale;
    int text_w  = len * glyph_w;
    int tx = rx + (rw - text_w) / 2;
    int ty = ry + (rh - 24) / 2;
    pc_draw_rect(rx, ry, rw, rh, bg);
    pc_draw_text_sized(tx, ty, buf, fg, bg, 24);
}

static void render(int hover_r, int hover_c) {
    pc_display_begin_update();
    pc_draw_rect(WIN_X, WIN_Y, WIN_W, WIN_H, COLOR_BG);
    pc_draw_text_sized(WIN_X + 30, WIN_Y + 18, "Пятнашки  15-Puzzle", COLOR_HEADER, COLOR_BG, 24);
    char mv_buf[32];
    const char *mv_label = "Ходов: ";
    char mv_num[12]; int_to_str(moves, mv_num);
    int i = 0, j = 0;
    while (mv_label[j]) mv_buf[i++] = mv_label[j++];
    j = 0; while (mv_num[j]) mv_buf[i++] = mv_num[j++];
    mv_buf[i] = '\0';
    pc_draw_rect(WIN_X + 30, WIN_Y + 58, 200, 30, COLOR_BG);
    pc_draw_text_sized(WIN_X + 30, WIN_Y + 62, mv_buf, COLOR_HEADER, COLOR_BG, 16);
    pc_draw_rect(WIN_X + 270, WIN_Y + 58, 210, 30, COLOR_BG);
    pc_draw_text_sized(WIN_X + 270, WIN_Y + 62, "R=новая  ESC=выход", COLOR_SUBTEXT, COLOR_BG, 12);
    pc_draw_rect(BOARD_X - 4, BOARD_Y - 4, BOARD_SIZE + 8, BOARD_SIZE + 8, COLOR_BORDER);
    pc_draw_rect(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE, COLOR_BOARD_BG);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int tx = BOARD_X + c * TILE_SIZE + GAP;
            int ty = BOARD_Y + r * TILE_SIZE + GAP;
            int tw = TILE_SIZE - GAP * 2;
            int th = TILE_SIZE - GAP * 2;
            int val = board[r][c];
            if (val == 0) {
                pc_draw_rect(tx, ty, tw, th, COLOR_EMPTY);
            } else {
                uint32_t tile_col = (r == hover_r && c == hover_c)
                                    ? COLOR_TILE_HL : COLOR_TILE;
                if (won) tile_col = COLOR_WIN_BG;
                draw_centered_number(val, tx, ty, tw, th, COLOR_TILE_TXT, tile_col);
            }
        }
    }
    if (won) {
        pc_draw_rect(WIN_X + 60, WIN_Y + 490, 380, 50, COLOR_WIN_BG);
        pc_draw_text_sized(WIN_X + 90, WIN_Y + 505, "Победа! Нажми R для новой игры", COLOR_WIN_TXT, COLOR_WIN_BG, 16);
    } else {
        pc_draw_rect(WIN_X + 60, WIN_Y + 490, 380, 50, COLOR_BG);
        pc_draw_text_sized(WIN_X + 80, WIN_Y + 505, "Кликни плитку рядом с пустой клеткой", COLOR_SUBTEXT, COLOR_BG, 12);
    }
    pc_display_end_update();
}

static int px_to_cell(int px, int py, int *out_r, int *out_c) {
    int rel_x = px - BOARD_X;
    int rel_y = py - BOARD_Y;
    if (rel_x < 0 || rel_x >= BOARD_SIZE || rel_y < 0 || rel_y >= BOARD_SIZE)
        return 0;
    *out_c = rel_x / TILE_SIZE;
    *out_r = rel_y / TILE_SIZE;
    return 1;
}

int main(void) {
    struct gui_window_request win_req = { .x = WIN_X, .y = WIN_Y, .width = WIN_W, .height = WIN_H };
    pc_gui_window_register(&win_req);
    shuffle();
    int hover_r = -1, hover_c = -1;
    int last_mouse_x = -1, last_mouse_y = -1;
    int left_was_down = 0;
    render(hover_r, hover_c);

    while (1) {
        int need_redraw = 0;
        struct mouse_state ms;
        if (pc_mouse_get(&ms) && ms.has_data) {
            int cr = -1, cc = -1;
            int on_board = px_to_cell(ms.x, ms.y, &cr, &cc);
            if (ms.x != last_mouse_x || ms.y != last_mouse_y) {
                last_mouse_x = ms.x; last_mouse_y = ms.y;
                int new_hr = on_board ? cr : -1;
                int new_hc = on_board ? cc : -1;
                if (new_hr != hover_r || new_hc != hover_c) {
                    hover_r = new_hr; hover_c = new_hc;
                    need_redraw = 1;
                }
            }
            int left_now = (ms.buttons & 1) ? 1 : 0;
            if (left_now && !left_was_down) {
                if (!won && on_board) {
                    if (try_move(cr, cc)) need_redraw = 1;
                }
            }
            left_was_down = left_now;
        }
        int key = pc_try_getchar();
        if (key > 0) {
            if (key == 27) {
                break;
            } else if (key == 'r' || key == 'R') {
                shuffle();
                hover_r = -1; hover_c = -1;
                need_redraw = 1;
            } else if (!won) {
                if (key == 'w' || key == 'W')
                    { if (try_move(empty_r - 1, empty_c)) need_redraw = 1; }
                else if (key == 's' || key == 'S')
                    { if (try_move(empty_r + 1, empty_c)) need_redraw = 1; }
                else if (key == 'a' || key == 'A')
                    { if (try_move(empty_r, empty_c - 1)) need_redraw = 1; }
                else if (key == 'd' || key == 'D')
                    { if (try_move(empty_r, empty_c + 1)) need_redraw = 1; }
            }
        }
        int sp = pc_try_get_special();
        if (sp > 0 && !won) {
            if (sp == 1) { if (try_move(empty_r - 1, empty_c)) need_redraw = 1; }
            if (sp == 2) { if (try_move(empty_r + 1, empty_c)) need_redraw = 1; }
            if (sp == 3) { if (try_move(empty_r, empty_c - 1)) need_redraw = 1; }
            if (sp == 4) { if (try_move(empty_r, empty_c + 1)) need_redraw = 1; }
        }
        uint32_t wstate = pc_gui_window_state();
        if (wstate & GUI_WINDOW_STATE_REPAINT) {
            need_redraw = 1;
            pc_gui_window_repaint_done();
        }
        if (need_redraw) render(hover_r, hover_c);
        pc_syscall(SYS_SCHED_YIELD, 0, 0, 0);
    }
    pc_gui_window_unregister();
    pc_desktop_redraw();
    return 0;
}