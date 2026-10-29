#include "SDL_mixer.h"

#define MIXER_MAX_CHANNELS 64
#define MIXER_MAX_EFFECTS 4
#define MIXER_SCRATCH_BYTES 16384

struct channel {
    Mix_Chunk *chunk;
    Uint32 position;
    int loops;
    int playing;
    int volume;
    int left;
    int right;
};

struct effect {
    Mix_EffectFunc_t function;
    Mix_EffectDone_t done;
    void *argument;
};

static SDL_AudioDeviceID device;
static int open_count;
static int mixer_frequency;
static Uint16 mixer_format;
static int mixer_channels;
static struct channel slots[MIXER_MAX_CHANNELS];
static int channel_count = 8;
static struct effect post_effects[MIXER_MAX_EFFECTS];
static void (*music_hook)(void *, Uint8 *, int);
static void *music_hook_argument;
static int music_volume = MIX_MAX_VOLUME;
static Uint8 scratch[MIXER_SCRATCH_BYTES];

static Sint16 clip(int value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (Sint16)value;
}

static void mix_channel(struct channel *channel, Sint16 *out, int frames)
{
    Mix_Chunk *chunk = channel->chunk;
    const Sint16 *samples = (const Sint16 *)chunk->abuf;
    Uint32 total = chunk->alen / sizeof(Sint16);
    int volume = (channel->volume * chunk->volume) / MIX_MAX_VOLUME;
    int i, c;

    for (i = 0; i < frames && channel->playing; ++i) {
        for (c = 0; c < mixer_channels; ++c) {
            int pan = mixer_channels == 2 ? (c == 0 ? channel->left : channel->right) : 255;
            int sample = samples[channel->position + (Uint32)c];
            sample = (sample * volume) / MIX_MAX_VOLUME;
            sample = (sample * pan) / 255;
            out[i * mixer_channels + c] = clip(out[i * mixer_channels + c] + sample);
        }
        channel->position += (Uint32)mixer_channels;
        if (channel->position + (Uint32)mixer_channels > total) {
            if (channel->loops != 0) {
                if (channel->loops > 0) {
                    --channel->loops;
                }
                channel->position = 0;
            } else {
                channel->playing = 0;
            }
        }
    }
}

static void SDLCALL mixer_callback(void *userdata, Uint8 *stream, int len)
{
    int frames = len / (int)(sizeof(Sint16) * (size_t)mixer_channels);
    int i;

    (void)userdata;
    SDL_memset(stream, 0, (size_t)len);

    for (i = 0; i < channel_count; ++i) {
        if (slots[i].playing) {
            mix_channel(&slots[i], (Sint16 *)stream, frames);
        }
    }

    if (music_hook) {
        int done = 0;
        while (done < len) {
            int part = len - done;
            if (part > MIXER_SCRATCH_BYTES) {
                part = MIXER_SCRATCH_BYTES;
            }
            SDL_memset(scratch, 0, (size_t)part);
            music_hook(music_hook_argument, scratch, part);
            SDL_MixAudioFormat(stream + done, scratch, mixer_format, (Uint32)part, music_volume);
            done += part;
        }
    }

    for (i = 0; i < MIXER_MAX_EFFECTS; ++i) {
        if (post_effects[i].function) {
            post_effects[i].function(MIX_CHANNEL_POST, stream, len, post_effects[i].argument);
        }
    }
}

int Mix_Init(int flags)
{
    (void)flags;
    return 0;
}

void Mix_Quit(void)
{
}

int Mix_OpenAudioDevice(int frequency, Uint16 format, int channels, int chunksize,
                        const char *devname, int allowed_changes)
{
    SDL_AudioSpec want, have;

    (void)devname;
    (void)allowed_changes;
    if (open_count > 0) {
        ++open_count;
        return 0;
    }
    if (format != AUDIO_S16SYS || (channels != 1 && channels != 2)) {
        return SDL_SetError("Only signed 16-bit mono or stereo mixing is supported");
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return -1;
    }

    SDL_zero(want);
    want.freq = frequency;
    want.format = format;
    want.channels = (Uint8)channels;
    want.samples = (Uint16)chunksize;
    want.callback = mixer_callback;

    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!device) {
        return -1;
    }
    mixer_frequency = have.freq;
    mixer_format = have.format;
    mixer_channels = have.channels;
    SDL_memset(slots, 0, sizeof(slots));
    SDL_PauseAudioDevice(device, 0);
    open_count = 1;
    return 0;
}

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    return Mix_OpenAudioDevice(frequency, format, channels, chunksize, NULL, 0);
}

void Mix_CloseAudio(void)
{
    if (open_count == 0) {
        return;
    }
    if (--open_count > 0) {
        return;
    }
    SDL_CloseAudioDevice(device);
    device = 0;
    music_hook = NULL;
    SDL_memset(post_effects, 0, sizeof(post_effects));
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels_out)
{
    if (open_count == 0) {
        return 0;
    }
    if (frequency) {
        *frequency = mixer_frequency;
    }
    if (format) {
        *format = mixer_format;
    }
    if (channels_out) {
        *channels_out = mixer_channels;
    }
    return open_count;
}

int Mix_AllocateChannels(int numchans)
{
    if (numchans > 0) {
        channel_count = numchans > MIXER_MAX_CHANNELS ? MIXER_MAX_CHANNELS : numchans;
    }
    return channel_count;
}

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
    int i;

    (void)ticks;
    if (!chunk || open_count == 0) {
        return -1;
    }
    if (channel < 0) {
        for (i = 0; i < channel_count && slots[i].playing; ++i) {
        }
        if (i == channel_count) {
            return -1;
        }
        channel = i;
    } else if (channel >= channel_count) {
        return -1;
    }

    SDL_LockAudioDevice(device);
    slots[channel].chunk = chunk;
    slots[channel].position = 0;
    slots[channel].loops = loops;
    slots[channel].volume = MIX_MAX_VOLUME;
    slots[channel].left = 255;
    slots[channel].right = 255;
    slots[channel].playing = chunk->alen > 0;
    SDL_UnlockAudioDevice(device);
    return channel;
}

int Mix_HaltChannel(int channel)
{
    int i;

    for (i = 0; i < channel_count; ++i) {
        if (channel < 0 || channel == i) {
            slots[i].playing = 0;
        }
    }
    return 0;
}

int Mix_Playing(int channel)
{
    int i, count = 0;

    if (channel >= 0) {
        return channel < channel_count && slots[channel].playing;
    }
    for (i = 0; i < channel_count; ++i) {
        count += slots[i].playing;
    }
    return count;
}

int Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
    if (channel < 0 || channel >= channel_count) {
        return 0;
    }
    slots[channel].left = left;
    slots[channel].right = right;
    return 1;
}

int Mix_Volume(int channel, int volume)
{
    int old;

    if (channel < 0 || channel >= channel_count) {
        return 0;
    }
    old = slots[channel].volume;
    if (volume >= 0) {
        slots[channel].volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
    }
    return old;
}

int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg)
{
    int i;

    if (chan != MIX_CHANNEL_POST || !f) {
        return SDL_SetError("Only post-mix effects are supported");
    }
    for (i = 0; i < MIXER_MAX_EFFECTS; ++i) {
        if (!post_effects[i].function) {
            post_effects[i].function = f;
            post_effects[i].done = d;
            post_effects[i].argument = arg;
            return 1;
        }
    }
    return SDL_SetError("Too many post-mix effects");
}

int Mix_UnregisterEffect(int channel, Mix_EffectFunc_t f)
{
    int i;

    if (channel != MIX_CHANNEL_POST) {
        return 0;
    }
    for (i = 0; i < MIXER_MAX_EFFECTS; ++i) {
        if (post_effects[i].function == f) {
            post_effects[i].function = NULL;
            return 1;
        }
    }
    return 0;
}

void Mix_HookMusic(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg)
{
    music_hook = mix_func;
    music_hook_argument = arg;
}

int Mix_VolumeMusic(int volume)
{
    int old = music_volume;

    if (volume >= 0) {
        music_volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
    }
    return old;
}

Mix_Music *Mix_LoadMUS(const char *file)
{
    (void)file;
    SDL_SetError("Music files are not supported");
    return NULL;
}

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc)
{
    (void)src;
    (void)freesrc;
    SDL_SetError("Music files are not supported");
    return NULL;
}

void Mix_FreeMusic(Mix_Music *music)
{
    (void)music;
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
    (void)music;
    (void)loops;
    return SDL_SetError("Music files are not supported");
}

int Mix_PlayingMusic(void)
{
    return 0;
}

int Mix_HaltMusic(void)
{
    return 0;
}

void Mix_PauseMusic(void)
{
}

void Mix_ResumeMusic(void)
{
}

int Mix_SetMusicPosition(double position)
{
    (void)position;
    return -1;
}

int Mix_SetMusicCMD(const char *command)
{
    (void)command;
    return 0;
}
