#include "../../../libgui/include/puregui.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"
#include "../../../drivers/input/keyboard.h"

#define BOARD_W 10
#define BOARD_H 20

static int8_t board[BOARD_H][BOARD_W];
static int32_t piece_type;
static int32_t piece_rotation;
static int32_t piece_x;
static int32_t piece_y;
static int32_t next_type;
static uint32_t score;
static uint32_t lines;
static uint32_t level;
static bool game_over;
static bool paused;
static uint32_t random_state=0x1234ABCD;
static uint32_t drop_interval_ms=600;
static uint32_t drop_timer;

#define SFX_LEN(a) (sizeof(a)/sizeof((a)[0]))

struct sfx_note {
    uint16_t freq_hz;
    uint16_t tone_ms;
    uint16_t gap_ms;
};

// Melodies measured from src/audio/tetris/*.mp3 via spectral analysis:
// move ~= 1050 Hz / 40 ms blip; clear = rising arpeggio with gaps;
// game over = low descending buzz.
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

// Sampled SFX preloaded from /game/sound (22050 Hz mono). When samples
// or the PCM backend are unavailable the tone sequencer below is used.
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

static uint32_t g_cell_w = 28;
static uint32_t g_cell_h = 28;
static uint32_t g_preview_cell = 16;
static uint32_t g_board_x = 12;
static uint32_t g_board_y = 12;
static uint32_t g_canvas_x = 12;
static uint32_t g_canvas_y = 12;
static uint32_t g_canvas_w = 280;
static uint32_t g_canvas_h = 560;
static uint32_t g_hud_x = 12;
static uint32_t g_hud_y = 0;
static uint32_t g_hud_h = 100;
static uint32_t g_win_w = 360;
static uint32_t g_win_h = 700;
static uint32_t g_hud_w = 280;

static uint32_t piece_colors[7]={
    0x89B4FA,
    0xF9E2AF,
    0xCBA6F7,
    0xA6E3A1,
    0xF38BA8,
    0x89DCEB,
    0xFAB387
};

static const int8_t SHAPES[7][4][4][2]={
    {
        {{-1,0},{0,0},{1,0},{2,0}},
        {{1,-1},{1,0},{1,1},{1,2}},
        {{-1,1},{0,1},{1,1},{2,1}},
        {{0,-1},{0,0},{0,1},{0,2}}
    },
    {
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,0},{1,0},{0,1},{1,1}}
    },
    {
        {{-1,0},{0,0},{1,0},{0,1}},
        {{0,-1},{0,0},{0,1},{1,0}},
        {{-1,1},{0,1},{1,1},{0,0}},
        {{0,-1},{0,0},{0,1},{-1,0}}
    },
    {
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}},
        {{0,0},{1,0},{-1,1},{0,1}},
        {{0,-1},{0,0},{1,0},{1,1}}
    },
    {
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}},
        {{-1,0},{0,0},{0,1},{1,1}},
        {{1,-1},{0,0},{1,0},{0,1}}
    },
    {
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{1,-1},{0,0},{0,1}},
        {{-1,1},{0,1},{1,1},{1,0}},
        {{0,-1},{0,0},{0,1},{-1,1}}
    },
    {
        {{1,0},{-1,1},{0,1},{1,1}},
        {{0,-1},{0,0},{0,1},{1,1}},
        {{-1,0},{-1,1},{0,1},{1,1}},
        {{-1,-1},{0,-1},{0,0},{0,1}}
    }
};

static void compute_layout(void){
    struct pc_display_info info;
    uint32_t disp_w=1024, disp_h=768;
    if(pc_display_get_info(&info) && info.available && info.width>=640 && info.height>=480){
        disp_w=info.width;
        disp_h=info.height;
    }
    const uint32_t margin=12;
    const uint32_t gap=12;
    uint32_t outer_margin_w = disp_w>40 ? 40 : 0;
    uint32_t outer_margin_h = disp_h>60 ? 60 : 0;
    g_win_w = disp_w - outer_margin_w;
    g_win_h = disp_h - outer_margin_h;
    if(g_win_w<PG_WINDOW_BORDER*2+220) g_win_w=PG_WINDOW_BORDER*2+220;
    if(g_win_h<PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2+480)
        g_win_h=PG_TITLEBAR_HEIGHT+PG_WINDOW_BORDER*2+480;
    if(g_win_w>disp_w) g_win_w=disp_w;
    if(g_win_h>disp_h) g_win_h=disp_h;

    uint32_t client_w = g_win_w - PG_WINDOW_BORDER*2;
    uint32_t client_h = g_win_h - PG_TITLEBAR_HEIGHT - PG_WINDOW_BORDER*2;
    uint32_t preview_cell = client_w>=520 ? 16 : 12;
    uint32_t preview_box = 4*preview_cell + 10;
    uint32_t hud_h = preview_box + 34;
    if(hud_h<90) hud_h=90;
    uint32_t canvas_w = client_w>margin*2 ? client_w-margin*2 : 200;
    uint32_t canvas_h = (client_h>hud_h+gap+margin*2)
        ? client_h-hud_h-gap-margin*2 : 400;
    uint32_t by_width = canvas_w/BOARD_W;
    uint32_t by_height = canvas_h/BOARD_H;
    uint32_t cell = by_width<by_height ? by_width : by_height;
    if(cell<14) cell=14;
    uint32_t board_w=BOARD_W*cell;
    uint32_t board_h=BOARD_H*cell;

    g_cell_w=cell;
    g_cell_h=cell;
    g_preview_cell=preview_cell;
    g_hud_h=hud_h;
    g_hud_w=canvas_w;
    g_canvas_x=margin;
    g_canvas_y=margin;
    g_canvas_w=canvas_w;
    g_canvas_h=canvas_h;
    g_board_x=g_canvas_x + (canvas_w>board_w ? (canvas_w-board_w)/2 : 0);
    g_board_y=g_canvas_y + (canvas_h>board_h ? (canvas_h-board_h)/2 : 0);
    g_hud_x=g_canvas_x;
    g_hud_y=g_canvas_y+canvas_h+gap;
}

static void recompute_layout_for_current_window(void){
    const uint32_t margin=12;
    const uint32_t gap=12;
    uint32_t client_w = g_win_w - PG_WINDOW_BORDER*2;
    uint32_t client_h = g_win_h - PG_TITLEBAR_HEIGHT - PG_WINDOW_BORDER*2;
    uint32_t preview_box = 4*g_preview_cell + 10;
    g_hud_h=preview_box+34;
    if(g_hud_h<90) g_hud_h=90;
    uint32_t canvas_w=(client_w>margin*2)?client_w-margin*2:200;
    uint32_t canvas_h=(client_h>g_hud_h+gap+margin*2)
        ? client_h-g_hud_h-gap-margin*2 : 400;
    uint32_t by_width=canvas_w/BOARD_W;
    uint32_t by_height=canvas_h/BOARD_H;
    uint32_t cell=by_width<by_height ? by_width : by_height;
    if(cell<14) cell=14;
    uint32_t board_w=BOARD_W*cell;
    uint32_t board_h=BOARD_H*cell;
    g_cell_w=cell;
    g_cell_h=cell;
    g_hud_w=canvas_w;
    g_canvas_x=margin;
    g_canvas_y=margin;
    g_canvas_w=canvas_w;
    g_canvas_h=canvas_h;
    g_board_x=g_canvas_x + (canvas_w>board_w ? (canvas_w-board_w)/2 : 0);
    g_board_y=g_canvas_y + (canvas_h>board_h ? (canvas_h-board_h)/2 : 0);
    g_hud_x=g_canvas_x;
    g_hud_y=g_canvas_y+canvas_h+gap;
}

static uint32_t random_value(void){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void update_level_speed(void){
    level = lines / 10 + 1;
    if(level>20) level=20;
    if(level<=10) drop_interval_ms = 600 - (level-1)*45;
    else drop_interval_ms = 150 - (level-10)*5;
    if(drop_interval_ms<80) drop_interval_ms=80;
}

static bool check_collision(int32_t type,int32_t rotation,int32_t x,int32_t y){
    for(int32_t i=0;i<4;i++){
        int32_t bx = x + SHAPES[type][rotation][i][0];
        int32_t by = y + SHAPES[type][rotation][i][1];
        if(bx<0 || bx>=BOARD_W) return true;
        if(by>=BOARD_H) return true;
        if(by<0) continue;
        if(board[by][bx]!=-1) return true;
    }
    return false;
}

static void lock_piece(void){
    for(int32_t i=0;i<4;i++){
        int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
        int32_t by = piece_y + SHAPES[piece_type][piece_rotation][i][1];
        if(by>=0 && by<BOARD_H && bx>=0 && bx<BOARD_W){
            board[by][bx]=(int8_t)piece_type;
        }
    }
}

static int32_t clear_lines(void){
    int32_t cleared=0;
    for(int32_t y=BOARD_H-1;y>=0;y--){
        bool full=true;
        for(int32_t x=0;x<BOARD_W;x++) if(board[y][x]==-1){ full=false; break; }
        if(full){
            cleared++;
            for(int32_t ty=y;ty>0;ty--){
                for(int32_t x=0;x<BOARD_W;x++) board[ty][x]=board[ty-1][x];
            }
            for(int32_t x=0;x<BOARD_W;x++) board[0][x]=-1;
            y++;
        }
    }
    return cleared;
}

static void spawn_piece(void){
    piece_type = next_type;
    next_type = (int32_t)(random_value()%7);
    piece_rotation=0;
    piece_x=4;
    piece_y=1;
    if(check_collision(piece_type,piece_rotation,piece_x,piece_y)){
        game_over=true;
    }
}

static void reset_game(void){
    sfx_stop();
    for(int32_t y=0;y<BOARD_H;y++) for(int32_t x=0;x<BOARD_W;x++) board[y][x]=-1;
    score=0;
    lines=0;
    level=1;
    drop_interval_ms=600;
    game_over=false;
    paused=false;
    drop_timer=0;
    next_type=(int32_t)(random_value()%7);
    spawn_piece();
    update_level_speed();
}

static void add_score(int32_t lines_cleared){
    if(lines_cleared<=0) return;
    static const uint32_t table[5]={0,100,300,500,800};
    if(lines_cleared>4) lines_cleared=4;
    score += table[lines_cleared]*level;
    lines += (uint32_t)lines_cleared;
    update_level_speed();
}

static void lock_current_piece(void){
    lock_piece();
    int32_t cleared=clear_lines();
    add_score(cleared);
    spawn_piece();
    drop_timer=0;
    if(game_over){
        sfx_jingle(sfx_over_buf,sfx_over_n,32,
                   SFX_GAMEOVER,SFX_LEN(SFX_GAMEOVER));
    } else if(cleared>0){
        sfx_jingle(sfx_clear_buf,sfx_clear_n,34,
                   SFX_CLEAR,SFX_LEN(SFX_CLEAR));
    }
}

static bool try_move(int32_t dx,int32_t dy){
    if(game_over || paused) return false;
    if(!check_collision(piece_type,piece_rotation,piece_x+dx,piece_y+dy)){
        piece_x+=dx;
        piece_y+=dy;
        return true;
    }
    return false;
}

static bool try_rotate(void){
    if(game_over || paused) return false;
    int32_t new_rot=(piece_rotation+1)%4;
    if(!check_collision(piece_type,new_rot,piece_x,piece_y)){
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x-1,piece_y)){
        piece_x-=1;
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x+1,piece_y)){
        piece_x+=1;
        piece_rotation=new_rot;
        return true;
    }
    if(!check_collision(piece_type,new_rot,piece_x,piece_y-1)){
        piece_y-=1;
        piece_rotation=new_rot;
        return true;
    }
    return false;
}

static void hard_drop(void){
    if(game_over || paused) return;
    int32_t drop=0;
    while(!check_collision(piece_type,piece_rotation,piece_x,piece_y+1)){
        piece_y++;
        drop++;
    }
    score += (uint32_t)(drop*2);
}

static bool user_move(int32_t dx,int32_t dy){
    if(try_move(dx,dy)){
        sfx_move_blip();
        return true;
    }
    return false;
}

static bool user_rotate(void){
    if(try_rotate()){
        sfx_move_blip();
        return true;
    }
    return false;
}

static char *append_text(char *dst,const char *src){
    while(*src) *dst++=*src++;
    *dst='\0';
    return dst;
}
static char *append_u32(char *dst,uint32_t v){
    char rev[12];
    uint32_t len=0;
    if(v==0) rev[len++]='0';
    while(v){ rev[len++]=(char)('0'+v%10); v/=10; }
    while(len) *dst++=rev[--len];
    *dst='\0';
    return dst;
}

static void draw_board(struct pg_window *window){
    uint32_t board_w = BOARD_W*g_cell_w;
    uint32_t board_h = BOARD_H*g_cell_h;
    pg_window_rect(window,(struct pg_rect){g_canvas_x-2,g_canvas_y-2,g_canvas_w+4,g_canvas_h+4},window->theme.border);
    pg_window_rect(window,(struct pg_rect){g_canvas_x,g_canvas_y,g_canvas_w,g_canvas_h},0x11111B);
    pg_window_rect(window,(struct pg_rect){g_board_x-1,g_board_y-1,board_w+2,board_h+2},0x45475A);
    pg_window_rect(window,(struct pg_rect){g_board_x,g_board_y,board_w,board_h},0x11111B);
    if(g_cell_w>=20 && g_cell_h>=20){
        for(uint32_t x=1;x<BOARD_W;x++){
            pg_window_rect(window,(struct pg_rect){g_board_x + x*g_cell_w, g_board_y, 1, board_h},0x1E1E2E);
        }
        for(uint32_t y=1;y<BOARD_H;y++){
            pg_window_rect(window,(struct pg_rect){g_board_x, g_board_y + y*g_cell_h, board_w, 1},0x1E1E2E);
        }
    }
    for(int32_t y=0;y<BOARD_H;y++){
        for(int32_t x=0;x<BOARD_W;x++){
            int8_t cell=board[y][x];
            if(cell==-1) continue;
            uint32_t color=piece_colors[cell];
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)x*g_cell_w+1, g_board_y + (uint32_t)y*g_cell_h+1, g_cell_w-2, g_cell_h-2},color);
            uint32_t hl = g_cell_h>=28?4:3;
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)x*g_cell_w+1, g_board_y + (uint32_t)y*g_cell_h+1, g_cell_w-2, hl},color+0x222222);
        }
    }
    if(!game_over){
        for(int32_t i=0;i<4;i++){
            int32_t bx = piece_x + SHAPES[piece_type][piece_rotation][i][0];
            int32_t by = piece_y + SHAPES[piece_type][piece_rotation][i][1];
            if(by<0 || by>=BOARD_H || bx<0 || bx>=BOARD_W) continue;
            uint32_t color=piece_colors[piece_type];
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)bx*g_cell_w+1, g_board_y + (uint32_t)by*g_cell_h+1, g_cell_w-2, g_cell_h-2},color);
            uint32_t hl = g_cell_h>=28?4:3;
            pg_window_rect(window,(struct pg_rect){g_board_x + (uint32_t)bx*g_cell_w+1, g_board_y + (uint32_t)by*g_cell_h+1, g_cell_w-2, hl},color+0x222222);
        }
    }
}

static void draw_hud(struct pg_window *window){
    uint32_t board_w = BOARD_W*g_cell_w;
    uint32_t hud_w = g_hud_w ? g_hud_w : board_w;
    pg_window_rect(window,(struct pg_rect){g_hud_x-1,g_hud_y-1,hud_w+2,g_hud_h+2},window->theme.border);
    pg_window_rect(window,(struct pg_rect){g_hud_x,g_hud_y,hud_w,g_hud_h},window->theme.titlebar);

    uint32_t preview_box = 4*g_preview_cell + 10;
    uint32_t box_h = preview_box;
    uint32_t box_w = preview_box;
    uint32_t box_x = g_hud_x + 10;
    uint32_t box_y = g_hud_y + 20;
    pg_window_text(window, box_x + (box_w>32?(box_w-32)/2:0), g_hud_y + 6, "NEXT", window->theme.text);
    if(box_y + box_h > g_hud_y + g_hud_h - 6) box_y = g_hud_y + g_hud_h - box_h - 6;

    pg_window_rect(window,(struct pg_rect){box_x-2,box_y-2,box_w+4,box_h+4},window->theme.border);
    pg_window_rect(window,(struct pg_rect){box_x,box_y,box_w,box_h},0x11111B);

    int32_t minX=10, maxX=-10, minY=10, maxY=-10;
    for(int32_t i=0;i<4;i++){
        int32_t bx=SHAPES[next_type][0][i][0];
        int32_t by=SHAPES[next_type][0][i][1];
        if(bx<minX) minX=bx;
        if(bx>maxX) maxX=bx;
        if(by<minY) minY=by;
        if(by>maxY) maxY=by;
    }
    int32_t w = maxX - minX + 1;
    int32_t h = maxY - minY + 1;
    int32_t offX = (4 - w)/2 - minX;
    int32_t offY = (4 - h)/2 - minY;
    uint32_t inner_x = box_x + (box_w - 4*g_preview_cell)/2;
    uint32_t inner_y = box_y + (box_h - 4*g_preview_cell)/2;
    uint32_t col = piece_colors[next_type];
    for(int32_t i=0;i<4;i++){
        int32_t bx=SHAPES[next_type][0][i][0];
        int32_t by=SHAPES[next_type][0][i][1];
        int32_t px=bx+offX;
        int32_t py=by+offY;
        if(px<0||px>=4||py<0||py>=4) continue;
        pg_window_rect(window,(struct pg_rect){inner_x + (uint32_t)px*g_preview_cell+1, inner_y + (uint32_t)py*g_preview_cell+1, g_preview_cell-2, g_preview_cell-2},col);
        pg_window_rect(window,(struct pg_rect){inner_x + (uint32_t)px*g_preview_cell+1, inner_y + (uint32_t)py*g_preview_cell+1, g_preview_cell-2, 2},col+0x222222);
    }

    uint32_t stats_x = box_x + box_w + 24;
    uint32_t stats_y = g_hud_y + 18;
    if(stats_x + 96 > g_hud_x + hud_w){
        stats_x = g_hud_x + 10;
        stats_y = box_y + box_h + 10;
    }
    {
        char line[32]; char *p=line;
        p=append_text(p,"Score: "); p=append_u32(p,score);
        pg_window_text(window,stats_x,stats_y,line,window->theme.text);
    }
    {
        char line[32]; char *p=line;
        p=append_text(p,"Lines: "); p=append_u32(p,lines);
        pg_window_text(window,stats_x,stats_y+18,line,window->theme.text);
    }
    {
        char line[32]; char *p=line;
        p=append_text(p,"Level: "); p=append_u32(p,level);
        pg_window_text(window,stats_x,stats_y+36,line,window->theme.text);
    }
    {
        char line[32]; char *p=line;
        p=append_text(p,"Sound: ");
        p=append_text(p,pa_is_muted() ? "OFF (M)" : "ON (M)");
        pg_window_text(window,stats_x,stats_y+54,line,window->theme.text);
    }

}

static void draw_overlays(struct pg_window *window){
    if(!paused && !game_over) return;
    uint32_t board_w=BOARD_W*g_cell_w;
    uint32_t board_h=BOARD_H*g_cell_h;
    uint32_t ow = 160;
    uint32_t oh = 36;
    uint32_t ox = g_board_x + (board_w - ow)/2;
    uint32_t oy = g_board_y + (board_h - oh)/2;
    if(paused && !game_over){
        pg_window_rect(window,(struct pg_rect){ox-2,oy-2,ow+4,oh+4},window->theme.border);
        pg_window_rect(window,(struct pg_rect){ox,oy,ow,oh},0xF9E2AF);
        pg_window_text(window,ox+56,oy+14,"PAUSED",0x1E1E2E);
    }
    if(game_over){
        pg_window_rect(window,(struct pg_rect){ox-2,oy-2,ow+4,oh+4},window->theme.border);
        pg_window_rect(window,(struct pg_rect){ox,oy,ow,oh},0xF38BA8);
        pg_window_text(window,ox+44,oy+14,"GAME OVER",0x1E1E2E);
        uint32_t hx = g_board_x + (board_w - 144)/2;
        pg_window_text(window,hx,oy+42,"Press R to restart",window->theme.text);
    }
}

static void redraw(struct pg_window *window){
    pg_window_begin(window);
    draw_board(window);
    draw_hud(window);
    draw_overlays(window);
    pg_window_end(window);
}

static void handle_input(struct pg_window *window, struct pg_event *event){
    if(event->type==PG_EVENT_CLOSE){
        pg_window_close(window);
        return;
    }
    if(event->type==PG_EVENT_KEY){
        int32_t k=event->key;
        if(k=='q' || k=='Q'){
            pg_window_close(window);
            return;
        }
        if(game_over){
            if(k=='r' || k=='R'){
                reset_game();
                redraw(window);
            }
            return;
        }
        if(k=='p' || k=='P'){
            paused=!paused;
            redraw(window);
            return;
        }
        if(paused) return;
        if(k=='m' || k=='M'){
            pa_toggle_mute();
            redraw(window);
            return;
        }
        if(k=='a' || k=='A'){
            if(user_move(-1,0)) redraw(window);
        } else if(k=='d' || k=='D'){
            if(user_move(1,0)) redraw(window);
        } else if(k=='s' || k=='S'){
            if(user_move(0,1)){
                score+=1;
                redraw(window);
            } else {
                lock_current_piece();
                redraw(window);
            }
        } else if(k=='w' || k=='W'){
            if(user_rotate()) redraw(window);
        } else if(k==' '){
            hard_drop();
            sfx_move_blip();
            lock_current_piece();
            redraw(window);
        } else if(k=='r' || k=='R'){
            reset_game();
            redraw(window);
        }
    } else if(event->type==PG_EVENT_SPECIAL_KEY){
        if(game_over || paused) return;
        if(event->key==KEYBOARD_SPECIAL_LEFT){
            if(user_move(-1,0)) redraw(window);
        } else if(event->key==KEYBOARD_SPECIAL_RIGHT){
            if(user_move(1,0)) redraw(window);
        } else if(event->key==KEYBOARD_SPECIAL_DOWN){
            if(user_move(0,1)){
                score+=1;
                redraw(window);
            } else {
                lock_current_piece();
                redraw(window);
            }
        } else if(event->key==KEYBOARD_SPECIAL_UP){
            if(user_rotate()) redraw(window);
        }
    }
}

static int tetris_main(void){
    compute_layout();
    struct pg_window window;
    bool opened=false;
    for(int32_t attempt=0;attempt<6;attempt++){
        if(pg_window_center(&window,"Tetris",g_win_w,g_win_h)){
            opened=true;
            break;
        }
        if(g_win_w>260) g_win_w-=40;
        if(g_win_h>520) g_win_h-=40;
        recompute_layout_for_current_window();
    }
    if(!opened) return 1;
    sfx_preload_all();
    reset_game();
    redraw(&window);
    while(pg_window_is_open(&window)){
        struct pg_event event;
        bool has_event=pg_window_poll_event(&window,&event);
        if(has_event){
            if(event.type==PG_EVENT_REPAINT || event.type==PG_EVENT_MOVE || event.type==PG_EVENT_FOCUS){
                redraw(&window);
            } else {
                handle_input(&window,&event);
            }
            if(!pg_window_is_open(&window)) break;
        } else {
            pc_sleep(16);
        }
        pa_update();
        sfx_tick(16);
        if(pg_window_is_minimized(&window)) continue;
        if(game_over || paused) continue;
        drop_timer+=16;
        if(drop_timer>=drop_interval_ms){
            drop_timer=0;
            if(!try_move(0,1)){
                lock_current_piece();
                redraw(&window);
                if(game_over) redraw(&window);
            } else {
                redraw(&window);
            }
        }
    }
    sfx_stop();
    pg_window_close(&window);
    return 0;
}

void _start(void){
    pc_exit(tetris_main());
}
