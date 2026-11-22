#include <string.h>
#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include "libc/include/purec.h"
#include "video-backend/include/vb.h"
#include "libaudio/include/pureaudio.h"

#define KEYQUEUE_SIZE 64
#define DEFAULT_IWAD "/usr/share/doom/doom1.wad"

static struct vb_window *window;
static bool window_ready;
static unsigned short key_queue[KEYQUEUE_SIZE];
static unsigned int key_write;
static unsigned int key_read;

static unsigned char to_doom_key(unsigned int scancode)
{
    if (scancode >= 4 && scancode <= 29) return (unsigned char)('a' + scancode - 4);
    if (scancode >= 30 && scancode <= 38) return (unsigned char)('1' + scancode - 30);
    if (scancode == 39) return '0';
    if (scancode >= 58 && scancode <= 67) return (unsigned char)(KEY_F1 + scancode - 58);
    switch (scancode) {
    case 40: case 88: return KEY_ENTER;
    case 41: return KEY_ESCAPE;
    case 42: return KEY_BACKSPACE;
    case 43: return KEY_TAB;
    case 44: return KEY_USE;
    case 45: return KEY_MINUS;
    case 46: return KEY_EQUALS;
    case 54: return ',';
    case 55: return '.';
    case 56: return '/';
    case 68: return KEY_F11;
    case 69: return KEY_F12;
    case 72: return KEY_PAUSE;
    case 79: return KEY_RIGHTARROW;
    case 80: return KEY_LEFTARROW;
    case 81: return KEY_DOWNARROW;
    case 82: return KEY_UPARROW;
    case 224: case 228: return KEY_FIRE;
    case 225: case 229: return KEY_RSHIFT;
    case 226: case 230: return KEY_LALT;
    default: return 0;
    }
}

static void queue_key(int pressed, unsigned char key)
{
    unsigned int next = (key_write + 1) % KEYQUEUE_SIZE;
    if (next == key_read) return;
    key_queue[key_write] = (unsigned short)((pressed << 8) | key);
    key_write = next;
}

static void pump_input(void)
{
    struct vb_event event;
    int budget;

    for (budget = 64; budget > 0; --budget) {
        int r = vb_next_event(&event, 0);
        if (r < 0) pc_exit(0);
        if (r == 0) break;
        if (event.type == VB_EVENT_KEY) {
            unsigned char doom_key = to_doom_key((unsigned int)event.key);
            if (doom_key && !event.repeat) queue_key(event.pressed, doom_key);
        } else if (event.type == VB_EVENT_CLOSE) {
            vb_window_destroy(window);
            pc_exit(0);
        }
    }
}

void DG_Init(void)
{
    if (vb_connect() != 0) {
        pc_write("doom: cannot connect to the display server\n");
        pc_exit(1);
    }
    window = vb_window_create(DOOMGENERIC_RESX, DOOMGENERIC_RESY, "DOOM");
    if (!window) {
        pc_write("doom: cannot create a window\n");
        pc_exit(1);
    }
    window_ready = true;
}

void DG_DrawFrame(void)
{
    uint32_t *dst;
    int w, h, y;

    if (!window_ready) return;
    pump_input();
    dst = vb_window_pixels(window);
    if (!dst) return;
    w = vb_window_width(window);
    h = vb_window_height(window);
    for (y = 0; y < h && y < DOOMGENERIC_RESY; ++y) {
        int n = w < DOOMGENERIC_RESX ? w : DOOMGENERIC_RESX;
        memcpy(dst + (size_t)y * w, (const uint32_t *)DG_ScreenBuffer + (size_t)y * DOOMGENERIC_RESX, (size_t)n * 4);
    }
    vb_window_damage_all(window);
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t slice;
    while (ms) {
        slice = ms > 8 ? 8 : ms;
        if (pa_stream_is_open()) pa_stream_pump();
        pc_sleep(slice);
        ms -= slice;
    }
}

uint32_t DG_GetTicksMs(void)
{
    struct time_info info;
    if (pc_time_get(&info) != 0) return 0;
    return (uint32_t)info.uptime_ms;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    unsigned short data;
    if (key_read == key_write) return 0;
    data = key_queue[key_read];
    key_read = (key_read + 1) % KEYQUEUE_SIZE;
    *pressed = data >> 8;
    *key = (unsigned char)(data & 0xFF);
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    if (!window_ready) return;
    vb_window_title(window, title);
}

int main(int argc, char **argv)
{
    char *args[64];
    int count = 0;
    int i;
    bool has_iwad = false;

    for (i = 0; i < argc && count < 60; i++) {
        args[count++] = argv[i];
        if (!pc_strcmp(argv[i], "-iwad")) has_iwad = true;
    }
    if (!has_iwad) {
        args[count++] = "-iwad";
        args[count++] = DEFAULT_IWAD;
    }
    args[count] = 0;

    doomgeneric_Create(count, args);
    for (;;) doomgeneric_Tick();
    return 0;
}
