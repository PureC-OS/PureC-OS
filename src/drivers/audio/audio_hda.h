#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HDA_MAX_ROUTE_NODES 16

struct hda_controller_info {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t output_streams;
    uint8_t input_streams;
    uint8_t bidirectional_streams;
    uint8_t major_version;
    uint8_t minor_version;
    bool mmio_ready;
};

struct hda_output_device_info {
    uint8_t codec_address;
    uint8_t function_group_node;
    uint8_t dac_node;
    uint8_t pin_node;
    uint32_t pin_capabilities;
    uint32_t default_configuration;
    uint8_t route_length;
    uint8_t route_nodes[HDA_MAX_ROUTE_NODES];
    uint8_t route_connections[HDA_MAX_ROUTE_NODES];
};

void hda_init(void);
void hda_set_address_mapping(uint64_t kernel_physical_base,
                             uint64_t kernel_virtual_base);
bool hda_is_present(void);
bool hda_pcm_output_ready(void);
uint32_t hda_output_device_count(void);
uint32_t hda_selected_output_device(void);
bool hda_get_output_device(uint32_t index, struct hda_output_device_info *out);
bool hda_select_output_device(uint32_t index);
bool hda_set_master_volume(uint8_t volume, bool muted);
bool hda_play_tone(uint16_t frequency_hz, uint8_t volume);
void hda_stop_tone(void);
// Sampled PCM streaming (44.1 kHz stereo S16, DMA double buffer).
// fill_half copies one half (up to HDA_PCM_SAMPLES stereo frames,
// interleaved LRLR...) into the DMA buffer and pads the rest with
// silence. start/stop control the stream RUN bit. position returns
// the LPIB byte offset within the cyclic buffer (0 on error).
void hda_pcm_fill_half(uint32_t half, const int16_t *stereo_frames,
                       uint32_t frame_count);
bool hda_pcm_start(void);
void hda_pcm_stop(void);
uint32_t hda_pcm_position(void);
bool hda_get_controller_info(struct hda_controller_info *out);
