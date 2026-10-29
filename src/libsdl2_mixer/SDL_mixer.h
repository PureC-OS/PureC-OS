#ifndef SDL_MIXER_H_
#define SDL_MIXER_H_

/* Small SDL_mixer replacement for PureC OS: sample channels with panning,
   the post-mix effect and music hook used by Chocolate Doom's OPL player.
   Playing music files (MIDI, Ogg, MP3, FLAC) is not supported. */

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MIXER_MAJOR_VERSION 2
#define SDL_MIXER_MINOR_VERSION 0
#define SDL_MIXER_PATCHLEVEL    4
#define SDL_MIXER_VERSION_ATLEAST(X, Y, Z) 0

#define MIX_MAX_VOLUME  SDL_MIX_MAXVOLUME
#define MIX_CHANNEL_POST (-2)

typedef enum {
    MIX_INIT_FLAC = 0x00000001,
    MIX_INIT_MOD  = 0x00000002,
    MIX_INIT_MP3  = 0x00000008,
    MIX_INIT_OGG  = 0x00000010,
    MIX_INIT_MID  = 0x00000020,
    MIX_INIT_OPUS = 0x00000040
} MIX_InitFlags;

typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8 volume;
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;

typedef void (*Mix_EffectFunc_t)(int chan, void *stream, int len, void *udata);
typedef void (*Mix_EffectDone_t)(int chan, void *udata);

extern int Mix_Init(int flags);
extern void Mix_Quit(void);
extern int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
extern int Mix_OpenAudioDevice(int frequency, Uint16 format, int channels, int chunksize,
                               const char *device, int allowed_changes);
extern void Mix_CloseAudio(void);
extern int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
extern int Mix_AllocateChannels(int numchans);

extern int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
#define Mix_PlayChannel(channel, chunk, loops) Mix_PlayChannelTimed(channel, chunk, loops, -1)
extern int Mix_HaltChannel(int channel);
extern int Mix_Playing(int channel);
extern int Mix_SetPanning(int channel, Uint8 left, Uint8 right);
extern int Mix_Volume(int channel, int volume);
extern int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg);
extern int Mix_UnregisterEffect(int channel, Mix_EffectFunc_t f);

extern void Mix_HookMusic(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg);
extern int Mix_VolumeMusic(int volume);
extern Mix_Music *Mix_LoadMUS(const char *file);
extern Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
extern void Mix_FreeMusic(Mix_Music *music);
extern int Mix_PlayMusic(Mix_Music *music, int loops);
extern int Mix_PlayingMusic(void);
extern int Mix_HaltMusic(void);
extern void Mix_PauseMusic(void);
extern void Mix_ResumeMusic(void);
extern int Mix_SetMusicPosition(double position);
extern int Mix_SetMusicCMD(const char *command);

#define Mix_GetError SDL_GetError

#ifdef __cplusplus
}
#endif

#endif /* SDL_MIXER_H_ */
