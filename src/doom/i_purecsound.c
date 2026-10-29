#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "libaudio/include/pureaudio.h"

#define NUM_CHANNELS 16
#define MIX_RATE 22050

typedef struct {
    int16_t *samples;
    uint32_t length;
} sfx_data_t;

typedef struct {
    const int16_t *samples;
    uint32_t length;
    uint32_t position;
    int volume;
    sfxinfo_t *sfx;
} channel_t;

static channel_t channels[NUM_CHANNELS];
static boolean sound_initialized;
static boolean use_sfx_prefix;

static void get_sfx_lump_name(sfxinfo_t *sfx, char *buffer, size_t capacity)
{
    if (sfx->link != NULL) sfx = sfx->link;
    if (use_sfx_prefix)
        M_snprintf(buffer, capacity, "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(buffer, DEH_String(sfx->name), capacity);
}

static sfx_data_t *load_sfx(sfxinfo_t *sfx)
{
    sfx_data_t *data;
    byte *lump;
    int lump_length;
    unsigned int rate, length, count, out_count, i;
    const byte *samples;

    if (sfx->driver_data != NULL) return sfx->driver_data;

    lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    lump_length = W_LumpLength(sfx->lumpnum);
    if (lump_length < 8 || lump[0] != 0x03 || lump[1] != 0x00) {
        W_ReleaseLumpNum(sfx->lumpnum);
        return NULL;
    }
    rate = (unsigned int)lump[2] | ((unsigned int)lump[3] << 8);
    length = (unsigned int)lump[4] | ((unsigned int)lump[5] << 8)
           | ((unsigned int)lump[6] << 16) | ((unsigned int)lump[7] << 24);
    if (rate == 0 || length > (unsigned int)lump_length - 8 || length <= 48) {
        W_ReleaseLumpNum(sfx->lumpnum);
        return NULL;
    }
    samples = lump + 8 + 16;
    count = length - 32;

    out_count = (unsigned int)(((unsigned long long)count * MIX_RATE) / rate);
    if (out_count == 0) out_count = 1;
    data = malloc(sizeof(*data));
    if (!data) {
        W_ReleaseLumpNum(sfx->lumpnum);
        return NULL;
    }
    data->samples = malloc(out_count * sizeof(int16_t));
    if (!data->samples) {
        free(data);
        W_ReleaseLumpNum(sfx->lumpnum);
        return NULL;
    }
    data->length = out_count;
    for (i = 0; i < out_count; i++) {
        unsigned long long position = ((unsigned long long)i * rate << 8) / MIX_RATE;
        unsigned int index = (unsigned int)(position >> 8);
        unsigned int fraction = (unsigned int)(position & 0xFF);
        int a = (int)samples[index < count ? index : count - 1] - 128;
        int b = (int)samples[index + 1 < count ? index + 1 : count - 1] - 128;
        data->samples[i] = (int16_t)(((a * (256 - (int)fraction) + b * (int)fraction) * 256) >> 8);
    }
    W_ReleaseLumpNum(sfx->lumpnum);
    sfx->driver_data = data;
    return data;
}

static void mix_fill(int16_t *frames, uint32_t count, void *user)
{
    uint32_t i;
    int c;
    (void)user;

    for (i = 0; i < count; i++) {
        int mixed = 0;
        for (c = 0; c < NUM_CHANNELS; c++) {
            channel_t *channel = &channels[c];
            if (channel->position < channel->length) {
                mixed += (channel->samples[channel->position++] * channel->volume) / 127;
            }
        }
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        frames[i] = (int16_t)mixed;
    }
}

static boolean purec_init_sound(boolean prefix)
{
    use_sfx_prefix = prefix;
    memset(channels, 0, sizeof(channels));
    if (pa_stream_open(mix_fill, NULL) != 0) return false;
    sound_initialized = true;
    return true;
}

static void purec_shutdown_sound(void)
{
    if (!sound_initialized) return;
    pa_stream_close();
    sound_initialized = false;
}

static int purec_get_sfx_lump_num(sfxinfo_t *sfx)
{
    char name[9];
    get_sfx_lump_name(sfx, name, sizeof(name));
    return W_GetNumForName(name);
}

static void purec_update_sound(void)
{
    if (sound_initialized) pa_stream_pump();
}

static void purec_update_sound_params(int channel, int vol, int sep)
{
    (void)sep;
    if (channel < 0 || channel >= NUM_CHANNELS) return;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    channels[channel].volume = vol;
}

static int purec_start_sound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    sfx_data_t *data;
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS) return -1;
    data = load_sfx(sfx);
    if (!data) return -1;
    channels[channel].samples = data->samples;
    channels[channel].length = data->length;
    channels[channel].position = 0;
    channels[channel].sfx = sfx;
    purec_update_sound_params(channel, vol, sep);
    return channel;
}

static void purec_stop_sound(int channel)
{
    if (channel < 0 || channel >= NUM_CHANNELS) return;
    channels[channel].length = 0;
    channels[channel].position = 0;
    channels[channel].sfx = NULL;
}

static boolean purec_sound_is_playing(int channel)
{
    if (channel < 0 || channel >= NUM_CHANNELS) return false;
    return channels[channel].position < channels[channel].length;
}

static void purec_cache_sounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

static snddevice_t sound_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    sound_devices,
    sizeof(sound_devices) / sizeof(sound_devices[0]),
    purec_init_sound,
    purec_shutdown_sound,
    purec_get_sfx_lump_num,
    purec_update_sound,
    purec_update_sound_params,
    purec_start_sound,
    purec_stop_sound,
    purec_sound_is_playing,
    purec_cache_sounds,
};

static boolean music_init(void) { return true; }
static void music_shutdown(void) { }
static void music_set_volume(int volume) { (void)volume; }
static void music_pause(void) { }
static void music_resume(void) { }
static void *music_register(void *data, int len) { (void)data; (void)len; return (void *)1; }
static void music_unregister(void *handle) { (void)handle; }
static void music_play(void *handle, boolean looping) { (void)handle; (void)looping; }
static void music_stop(void) { }
static boolean music_is_playing(void) { return false; }
static void music_poll(void) { }

static snddevice_t music_devices[] = {
    SNDDEVICE_ADLIB,
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_GENMIDI,
    SNDDEVICE_AWE32,
};

music_module_t DG_music_module = {
    music_devices,
    sizeof(music_devices) / sizeof(music_devices[0]),
    music_init,
    music_shutdown,
    music_set_volume,
    music_pause,
    music_resume,
    music_register,
    music_unregister,
    music_play,
    music_stop,
    music_is_playing,
    music_poll,
};
