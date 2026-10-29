#include "SDL.h"

#define WIDTH 480
#define HEIGHT 320

static volatile int beep_samples_left;
static int beep_phase;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)sizeof(Sint16);
    int i;

    (void)userdata;
    for (i = 0; i < frames; ++i) {
        if (beep_samples_left > 0) {
            out[i] = (beep_phase / 25) % 2 ? 6000 : -6000;
            beep_phase = (beep_phase + 1) % 50;
            --beep_samples_left;
        } else {
            out[i] = 0;
        }
    }
}

static void fill(SDL_Surface *surface, SDL_Rect *rect, Uint32 color)
{
    SDL_FillRect(surface, rect, color);
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Surface *surface;
    SDL_bool running = SDL_TRUE;
    int mouse_x = WIDTH / 2, mouse_y = HEIGHT / 2;
    int scroll = 0;
    Uint8 shade = 0;
    Uint8 keys_down = 0;
    Uint64 last_title = 0;

    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("SDL2 on PureC OS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIDTH, HEIGHT, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 22050;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = audio_callback;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 || SDL_OpenAudio(&want, &have) != 0) {
            SDL_Log("audio unavailable: %s", SDL_GetError());
        } else {
            SDL_PauseAudio(0);
        }
    }
    surface = SDL_GetWindowSurface(window);
    if (!surface) {
        SDL_Log("SDL_GetWindowSurface: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = SDL_FALSE;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = SDL_FALSE;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_SPACE && !event.key.repeat) {
                    beep_samples_left = 22050 / 5;
                }
                if (!event.key.repeat) {
                    keys_down++;
                }
                break;
            case SDL_KEYUP:
                if (keys_down) {
                    keys_down--;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse_x = event.motion.x;
                mouse_y = event.motion.y;
                break;
            case SDL_MOUSEWHEEL:
                scroll += event.wheel.y;
                break;
            default:
                break;
            }
        }

        shade = (Uint8)(SDL_GetTicks() / 8);
        fill(surface, NULL, SDL_MapRGB(surface->format, 24, 24, 40));

        SDL_Rect bar = { 16, 16, 16 + (scroll * 8 > 0 ? scroll * 8 : 0), 24 };
        if (bar.w > WIDTH - 32) {
            bar.w = WIDTH - 32;
        }
        fill(surface, &bar, SDL_MapRGB(surface->format, 137, 180, 250));

        SDL_Rect keys = { 16, 52, 24, 24 };
        for (int i = 0; i < keys_down && i < 12; ++i) {
            fill(surface, &keys, SDL_MapRGB(surface->format, 166, 227, 161));
            keys.x += 32;
        }

        SDL_Rect cursor = { mouse_x - 8, mouse_y - 8, 16, 16 };
        fill(surface, &cursor, SDL_MapRGB(surface->format, 243, 139, 168));

        SDL_Rect pulse = { WIDTH - 48, HEIGHT - 48, 32, 32 };
        fill(surface, &pulse, SDL_MapRGB(surface->format, shade, 128, (Uint8)(255 - shade)));

        SDL_UpdateWindowSurface(window);

        if (SDL_GetTicks64() - last_title > 500) {
            char title[96];
            SDL_snprintf(title, sizeof(title), "SDL2 PureC: %lu ms, keys=%d, wheel=%d",
                         (unsigned long)SDL_GetTicks64(), keys_down, scroll);
            SDL_SetWindowTitle(window, title);
            last_title = SDL_GetTicks64();
        }
        SDL_Delay(16);
    }

    SDL_CloseAudio();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
