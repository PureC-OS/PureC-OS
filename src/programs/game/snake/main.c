#include "../../../libc/include/purec.h"
#include "../../../libaudio/include/pureaudio.h"

#define BOARD_WIDTH 30
#define BOARD_HEIGHT 18
#define CELL_SIZE 18
#define MAX_LENGTH (BOARD_WIDTH*BOARD_HEIGHT)
#define COLOR_BACKGROUND 0x181825
#define COLOR_PANEL 0x313244
#define COLOR_SNAKE 0xA6E3A1
#define COLOR_HEAD 0x89B4FA
#define COLOR_FOOD 0xF38BA8
#define COLOR_TEXT 0xCDD6F4

struct point { int16_t x; int16_t y; };

static struct point body[MAX_LENGTH];
static struct point food;
static uint16_t length;
static int16_t direction_x;
static int16_t direction_y;
static uint32_t random_state=0x51A7E123;
static uint32_t score;

// Sampled SFX preloaded from /game/sound or /bin/sound (22050 Hz mono).
// Falls back to tone blips when files or the PCM backend are missing.
static int16_t sfx_turn[1100];
static uint32_t sfx_turn_frames;
static int16_t sfx_eat[2700];
static uint32_t sfx_eat_frames;
static int16_t sfx_die[11100];
static uint32_t sfx_die_frames;
static uint32_t sfx_cooldown_ticks;

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

static void sfx_play_buffer(const int16_t *buffer,uint32_t frames,
                            uint16_t fallback_hz,uint32_t fallback_ms){
    if(frames>0 && pa_sfx_play(buffer,frames)==0) return;
    pa_play_tone(fallback_hz,fallback_ms);
}

static void sfx_turn_blip(void){
    if(sfx_cooldown_ticks>0) return;
    sfx_play_buffer(sfx_turn,sfx_turn_frames,1200,45);
}

static void sfx_eat_chime(void){
    sfx_cooldown_ticks=1;
    sfx_play_buffer(sfx_eat,sfx_eat_frames,900,80);
}

static void sfx_die_jingle(void){
    sfx_cooldown_ticks=4;
    sfx_play_buffer(sfx_die,sfx_die_frames,300,300);
}

static void sfx_init(void){
    sfx_turn_frames=sfx_preload("turn.wav",sfx_turn,
                                sizeof(sfx_turn)/sizeof(sfx_turn[0]));
    sfx_eat_frames=sfx_preload("eat.wav",sfx_eat,
                               sizeof(sfx_eat)/sizeof(sfx_eat[0]));
    sfx_die_frames=sfx_preload("die.wav",sfx_die,
                               sizeof(sfx_die)/sizeof(sfx_die[0]));
}

static void sfx_shutdown(void){
    pa_sfx_stop();
    pa_stop_tone();
}

static uint32_t random_value(void){
    random_state^=random_state<<13;
    random_state^=random_state>>17;
    random_state^=random_state<<5;
    return random_state;
}

static bool occupied(int16_t x, int16_t y){
    for(uint16_t index=0;index<length;index++){
        if(body[index].x==x && body[index].y==y) return true;
    }
    return false;
}

static void place_food(void){
    do {
        food.x=(int16_t)(random_value()%BOARD_WIDTH);
        food.y=(int16_t)(random_value()%BOARD_HEIGHT);
    } while(occupied(food.x,food.y));
}

static void draw_header(uint32_t origin_x, uint32_t origin_y){
    pc_display_clear(COLOR_BACKGROUND);
    pc_draw_text(origin_x,origin_y-28,"Snake - WASD to move, Q to exit",
                 COLOR_TEXT,COLOR_BACKGROUND);
}

static void draw_board(uint32_t origin_x, uint32_t origin_y){
    pc_draw_rect(origin_x-4,origin_y-4,BOARD_WIDTH*CELL_SIZE+8,
                 BOARD_HEIGHT*CELL_SIZE+8,COLOR_PANEL);
    pc_draw_rect(origin_x+food.x*CELL_SIZE,origin_y+food.y*CELL_SIZE,
                 CELL_SIZE-2,CELL_SIZE-2,COLOR_FOOD);
    for(uint16_t index=0;index<length;index++){
        pc_draw_rect(origin_x+body[index].x*CELL_SIZE,
                     origin_y+body[index].y*CELL_SIZE,
                     CELL_SIZE-2,CELL_SIZE-2,index ? COLOR_SNAKE : COLOR_HEAD);
    }
}

static void draw_cell(uint32_t origin_x, uint32_t origin_y,
                      struct point point, uint32_t color){
    pc_draw_rect(origin_x+point.x*CELL_SIZE,origin_y+point.y*CELL_SIZE,
                 CELL_SIZE-2,CELL_SIZE-2,color);
}

static bool advance(bool *ate_food, struct point *old_tail){
    struct point next={
        (int16_t)(body[0].x+direction_x),
        (int16_t)(body[0].y+direction_y)
    };
    if(next.x<0 || next.x>=BOARD_WIDTH || next.y<0 || next.y>=BOARD_HEIGHT)
        return false;
    bool ate=next.x==food.x && next.y==food.y;
    uint16_t collision_length=ate ? length : length-1;
    for(uint16_t index=0;index<collision_length;index++){
        if(body[index].x==next.x && body[index].y==next.y) return false;
    }
    *old_tail=body[length-1];
    if(ate && length<MAX_LENGTH){ length++; score++; }
    for(uint16_t index=length-1;index>0;index--) body[index]=body[index-1];
    body[0]=next;
    if(ate) place_food();
    *ate_food=ate;
    return true;
}

static void draw_advance(uint32_t origin_x, uint32_t origin_y,
                         bool ate_food, struct point old_tail){
    if(!ate_food) draw_cell(origin_x,origin_y,old_tail,COLOR_PANEL);
    if(length>1) draw_cell(origin_x,origin_y,body[1],COLOR_SNAKE);
    draw_cell(origin_x,origin_y,body[0],COLOR_HEAD);
    if(ate_food) draw_cell(origin_x,origin_y,food,COLOR_FOOD);
}

static void handle_key(int32_t key, bool *running){
    if(key=='q' || key=='Q' || key==27){ *running=false; return; }
    if((key=='w' || key=='W') && direction_y!=1){
        direction_x=0; direction_y=-1;
        sfx_turn_blip();
    } else if((key=='s' || key=='S') && direction_y!=-1){
        direction_x=0; direction_y=1;
        sfx_turn_blip();
    } else if((key=='a' || key=='A') && direction_x!=1){
        direction_x=-1; direction_y=0;
        sfx_turn_blip();
    } else if((key=='d' || key=='D') && direction_x!=-1){
        direction_x=1; direction_y=0;
        sfx_turn_blip();
    }
}

static int snake_main(void){
    struct pc_display_info display;
    if(!pc_display_get_info(&display)) return 1;
    length=4;
    direction_x=1;
    direction_y=0;
    score=0;
    for(uint16_t index=0;index<length;index++){
        body[index].x=BOARD_WIDTH/2-(int16_t)index;
        body[index].y=BOARD_HEIGHT/2;
    }
    place_food();
    sfx_init();
    uint32_t board_width=BOARD_WIDTH*CELL_SIZE;
    uint32_t board_height=BOARD_HEIGHT*CELL_SIZE;
    uint32_t origin_x=display.width>board_width
        ? (display.width-board_width)/2 : 0;
    uint32_t origin_y=display.height>board_height+40
        ? (display.height-board_height)/2+20 : 40;
    draw_header(origin_x,origin_y);
    draw_board(origin_x,origin_y);
    bool running=true;
    bool died=false;
    while(running){
        bool ate_food=false;
        struct point old_tail;
        for(uint32_t sub=0;sub<4 && running;sub++){
            int32_t key;
            while((key=pc_try_getchar())>=0) handle_key(key,&running);
            pa_update();
            if(!running) break;
            pc_sleep(35);
        }
        if(sfx_cooldown_ticks>0) sfx_cooldown_ticks--;
        if(!running) break;
        if(!advance(&ate_food,&old_tail)){
            died=true;
            break;
        }
        if(ate_food) sfx_eat_chime();
        draw_advance(origin_x,origin_y,ate_food,old_tail);
    }
    if(died) sfx_die_jingle();
    pc_display_clear(COLOR_BACKGROUND);
    pc_draw_text(40,60,"Snake finished",COLOR_TEXT,COLOR_BACKGROUND);
    for(uint32_t i=0;i<30;i++){
        pa_update();
        pc_sleep(30);
    }
    sfx_shutdown();
    return 0;
}

void _start(void){
    pc_exit(snake_main());
}
