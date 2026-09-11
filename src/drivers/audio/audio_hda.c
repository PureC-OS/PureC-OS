#include "audio_hda.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_HDA 0x03
#define HDA_MMIO_SIZE 0x4000
#define HDA_TIMEOUT 1000000U
#define HDA_GCTL 0x08
#define HDA_STATESTS 0x0E
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP 0x48
#define HDA_CORBRP 0x4A
#define HDA_CORBCTL 0x4C
#define HDA_CORBSIZE 0x4E
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP 0x58
#define HDA_RINTCNT 0x5A
#define HDA_RIRBCTL 0x5C
#define HDA_RIRBSTS 0x5D
#define HDA_STREAM_BASE 0x80
#define HDA_STREAM_DESCRIPTOR_SIZE 0x20
#define HDA_STREAM_CTL 0x00
#define HDA_STREAM_STS 0x03
#define HDA_STREAM_LPIB 0x04
#define HDA_STREAM_CBL 0x08
#define HDA_STREAM_LVI 0x0C
#define HDA_STREAM_FMT 0x12
#define HDA_STREAM_BDPL 0x18
#define HDA_STREAM_BDPU 0x1C
#define HDA_GCTL_RESET 0x01
#define HDA_STREAM_RESET 0x01
#define HDA_STREAM_RUN 0x02
#define HDA_WIDGET_AUDIO_OUTPUT 0x00
#define HDA_WIDGET_PIN 0x04
#define HDA_PARAMETER_SUBNODES 0x04
#define HDA_PARAMETER_FUNCTION_GROUP_TYPE 0x05
#define HDA_PARAMETER_WIDGET_CAPS 0x09
#define HDA_PARAMETER_PIN_CAPS 0x0C
#define HDA_PARAMETER_INPUT_AMP_CAPS 0x0D
#define HDA_PARAMETER_CONNECTION_LIST_LENGTH 0x0E
#define HDA_PARAMETER_OUTPUT_AMP_CAPS 0x12
#define HDA_FUNCTION_GROUP_AUDIO 0x01
#define HDA_PIN_CAP_OUTPUT 0x10
#define HDA_VERB_GET_PARAMETER 0x0F00
#define HDA_VERB_GET_CONFIG_DEFAULT 0xF1C
#define HDA_VERB_GET_CONNECTION_LIST_ENTRY 0xF02
#define HDA_VERB_GET_POWER_STATE 0xF05
#define HDA_VERB_SET_CONNECTION_SELECT 0x701
#define HDA_VERB_SET_POWER_STATE 0x705
#define HDA_VERB_SET_STREAM_FORMAT 0x200
#define HDA_VERB_SET_CONVERTER_STREAM_CHANNEL 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_EAPD_BTL 0x70C
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300
#define HDA_PCM_FORMAT 0x4011
#define HDA_STREAM_TAG 1
#define HDA_PCM_RATE 44100U
#define HDA_PCM_SAMPLES 4096U
#define HDA_PCM_BYTES (HDA_PCM_SAMPLES * 4U)
#define HDA_MAX_OUTPUT_DEVICES 16U
#define HDA_NO_OUTPUT_DEVICE UINT32_MAX
#define HDA_MAX_CODEC_NODES 128U
#define HDA_WIDGET_CAP_INPUT_AMP 0x00000002U
#define HDA_WIDGET_CAP_OUTPUT_AMP 0x00000004U
#define HDA_WIDGET_CAP_AMP_OVERRIDE 0x00000008U
#define HDA_PIN_CAP_EAPD 0x00010000U

struct hda_bdl_entry {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

static struct hda_controller_info controller;
static volatile uint8_t *regs;
static bool present;
static bool pcm_ready;
static bool mapping_ready;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint8_t codec_address;
static uint8_t dac_node;
static uint8_t pin_node;
static uint8_t function_group_node;
static struct hda_output_device_info output_devices[HDA_MAX_OUTPUT_DEVICES];
static uint32_t output_device_count;
static uint32_t selected_output_index;
static uint8_t widget_types[HDA_MAX_CODEC_NODES];
static uint32_t widget_capabilities[HDA_MAX_CODEC_NODES];
static uint32_t widget_connections[HDA_MAX_CODEC_NODES];
static bool widget_present[HDA_MAX_CODEC_NODES];
static uint32_t corb[256] __attribute__((aligned(4096)));
static uint64_t rirb[256] __attribute__((aligned(4096)));
static struct hda_bdl_entry bdl[2] __attribute__((aligned(128)));
static uint8_t pcm_buffer[HDA_PCM_BYTES * 2U] __attribute__((aligned(4096)));

static uint16_t read16(uint32_t offset) {
    return *(volatile uint16_t *)(regs + offset);
}

static uint8_t read8(uint32_t offset) {
    return *(volatile uint8_t *)(regs + offset);
}

static uint32_t read32(uint32_t offset) {
    return *(volatile uint32_t *)(regs + offset);
}

static void write16(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write16 off=0x%03x value=0x%04x",
          offset, value);
}

static void write8(uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write8 off=0x%03x value=0x%02x",
          offset, value);
}

static void write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write32 off=0x%03x value=0x%08x",
          offset, value);
}

static void trace_ring(const char *stage) {
    klogf(KLOG_DEBUG,
          "audio: HDA %s CORBWP=0x%04x CORBRP=0x%04x CORBCTL=0x%02x CORBSIZE=0x%02x RIRBWP=0x%04x RIRBCTL=0x%02x RIRBSTS=0x%02x STATESTS=0x%04x",
          stage, read16(HDA_CORBWP), read16(HDA_CORBRP), read8(HDA_CORBCTL),
          read8(HDA_CORBSIZE), read16(HDA_RIRBWP), read8(HDA_RIRBCTL),
          read8(HDA_RIRBSTS), read16(HDA_STATESTS));
}

static uint64_t physical_address(const void *pointer) {
    return (uint64_t)(uintptr_t)pointer - kernel_virtual_base + kernel_physical_base;
}

static bool wait_reset_state(bool asserted) {
    klogf(KLOG_DEBUG, "audio: HDA reset wait target=%u", asserted ? 1 : 0);
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        if (((read32(HDA_GCTL) & HDA_GCTL_RESET) != 0) == asserted) {
            return true;
        }
        __asm__ volatile("pause");
    }
    klogf(KLOG_ERROR, "audio: HDA reset wait timeout target=%u", asserted ? 1 : 0);
    return false;
}

static uint32_t read_stream_control(volatile uint8_t *stream) {
    uint32_t low = *(volatile uint16_t *)(stream + HDA_STREAM_CTL);
    uint32_t high = *(volatile uint8_t *)(stream + HDA_STREAM_CTL + 2);
    return low | (high << 16);
}

static void write_stream_control(volatile uint8_t *stream, uint32_t value) {
    klogf(KLOG_DEBUG, "audio: HDA stream CTL write value=0x%06x", value & 0xFFFFFF);
    *(volatile uint16_t *)(stream + HDA_STREAM_CTL) = (uint16_t)value;
    *(volatile uint8_t *)(stream + HDA_STREAM_CTL + 2) = (uint8_t)(value >> 16);
    klogf(KLOG_DEBUG, "audio: HDA stream CTL readback=0x%06x",
          read_stream_control(stream));
}

static bool wait_stream_control(volatile uint8_t *stream, uint32_t mask,
                                bool asserted) {
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        if (((read_stream_control(stream) & mask) != 0) == asserted) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static void trace_stream_status(const char *stage, volatile uint8_t *stream) {
    uint8_t status = *(volatile uint8_t *)(stream + HDA_STREAM_STS);
    klogf(KLOG_DEBUG,
          "audio: HDA stream status stage=%s raw=0x%02x fifo_ready=%u descriptor_error=%u fifo_error=%u buffer_complete=%u lpib=0x%08x",
          stage, status, (status & 0x20) != 0 ? 1 : 0,
          (status & 0x10) != 0 ? 1 : 0, (status & 0x08) != 0 ? 1 : 0,
          (status & 0x04) != 0 ? 1 : 0,
          *(volatile uint32_t *)(stream + 0x04));
}

static bool reset_stream(volatile uint8_t *stream) {
    uint32_t control = read_stream_control(stream) & ~HDA_STREAM_RUN;
    klogf(KLOG_DEBUG, "audio: HDA stream reset begin ctl=0x%06x", control);
    write_stream_control(stream, control);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, false)) {
        klog(KLOG_ERROR, "audio: HDA stream RUN clear timeout");
        return false;
    }
    klog(KLOG_DEBUG, "audio: HDA stream RUN confirmed clear");
    write_stream_control(stream, control | HDA_STREAM_RESET);
    if (!wait_stream_control(stream, HDA_STREAM_RESET, true)) {
        klog(KLOG_ERROR, "audio: HDA stream reset assert timeout");
        return false;
    }
    klog(KLOG_DEBUG, "audio: HDA stream reset asserted");
    write_stream_control(stream, control & ~HDA_STREAM_RESET);
    if (!wait_stream_control(stream, HDA_STREAM_RESET, false)) {
        klog(KLOG_ERROR, "audio: HDA stream reset deassert timeout");
        return false;
    }
    klog(KLOG_DEBUG, "audio: HDA stream reset deasserted");
    return true;
}

static bool reset_controller(void) {
    klogf(KLOG_DEBUG, "audio: HDA reset begin GCTL=0x%08x", read32(HDA_GCTL));
    write32(HDA_GCTL, read32(HDA_GCTL) & ~HDA_GCTL_RESET);
    if (!wait_reset_state(false)) {
        klogf(KLOG_ERROR, "audio: HDA reset deassert timeout GCTL=0x%08x",
              read32(HDA_GCTL));
        return false;
    }
    write32(HDA_GCTL, read32(HDA_GCTL) | HDA_GCTL_RESET);
    if (!wait_reset_state(true)) {
        klogf(KLOG_ERROR, "audio: HDA reset assert timeout GCTL=0x%08x",
              read32(HDA_GCTL));
        return false;
    }
    klogf(KLOG_DEBUG, "audio: HDA reset complete GCTL=0x%08x STATESTS=0x%04x",
          read32(HDA_GCTL), read16(HDA_STATESTS));
    return true;
}

static bool setup_command_ring(void) {
    klog(KLOG_DEBUG, "audio: HDA command ring setup begin");
    uint64_t corb_address = physical_address(corb);
    uint64_t rirb_address = physical_address(rirb);
    write8(HDA_CORBCTL, 0);
    write8(HDA_RIRBCTL, 0);
    write16(HDA_CORBRP, 0x8000);
    write16(HDA_CORBRP, 0x0000);
    write32(HDA_CORBLBASE, (uint32_t)corb_address);
    write32(HDA_CORBUBASE, (uint32_t)(corb_address >> 32));
    write32(HDA_RIRBLBASE, (uint32_t)rirb_address);
    write32(HDA_RIRBUBASE, (uint32_t)(rirb_address >> 32));
    write16(HDA_CORBWP, 0);
    write16(HDA_RIRBWP, 0x8000);
    write16(HDA_RIRBWP, 0x0000);
    write16(HDA_RINTCNT, 1);
    write8(HDA_CORBSIZE, (uint8_t)((read8(HDA_CORBSIZE) & 0xF0) | 0x02));
    write8(HDA_CORBCTL, 0x02);
    write8(HDA_RIRBCTL, 0x02);
    bool ready = (read8(HDA_CORBCTL) & 0x02) != 0
        && (read8(HDA_RIRBCTL) & 0x02) != 0;
    klogf(KLOG_DEBUG, "audio: HDA ring pointers reset corbrp=0x%04x rirbwp=0x%04x",
          read16(HDA_CORBRP), read16(HDA_RIRBWP));
    klogf(ready ? KLOG_DEBUG : KLOG_ERROR,
          "audio: HDA CORB/RIRB corb=0x%llx rirb=0x%llx size=0x%04x ctl=0x%04x/0x%04x ready=%u",
          corb_address, rirb_address, read8(HDA_CORBSIZE),
          read8(HDA_CORBCTL), read8(HDA_RIRBCTL), ready ? 1 : 0);
    return ready;
}

static bool send_verb(uint32_t verb, uint32_t *response) {
    uint16_t old_write = read16(HDA_CORBWP) & 0xFF;
    uint16_t next_write = (uint16_t)((old_write + 1) & 0xFF);
    uint16_t old_response = read16(HDA_RIRBWP) & 0xFF;
    uint8_t codec = (uint8_t)(verb >> 28);
    uint8_t node = (uint8_t)((verb >> 20) & 0x7F);
    uint16_t command = (uint16_t)((verb >> 8) & 0x0FFF);
    uint8_t payload = (uint8_t)verb;
    klogf(KLOG_DEBUG,
          "audio: HDA VERB OUT raw=0x%08x codec=%u node=0x%02x verb=0x%03x payload=0x%02x slot=%u",
          verb, codec, node, command, payload, next_write);
    trace_ring("before-submit");
    corb[next_write] = verb;
    write16(HDA_CORBWP, next_write);
    klogf(KLOG_DEBUG, "audio: HDA CORB[%u] phys=0x%llx value=0x%08x",
          next_write, physical_address(&corb[next_write]), corb[next_write]);
    trace_ring("after-submit");
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        uint16_t current_response = read16(HDA_RIRBWP) & 0xFF;
        if (current_response != old_response) {
            uint16_t response_index = (uint16_t)((old_response + 1) & 0xFF);
            uint64_t response_entry = rirb[response_index];
            uint32_t response_value = (uint32_t)response_entry;
            uint32_t response_extended = (uint32_t)(response_entry >> 32);
            if (response) {
                *response = response_value;
                klogf(KLOG_DEBUG,
                      "audio: HDA VERB IN raw=0x%08x extended=0x%08x unsolicited=%u response_slot=%u phys=0x%llx",
                      *response, response_extended,
                      (response_extended & 0x10) != 0 ? 1 : 0, response_index,
                      physical_address(&rirb[response_index]));
            } else {
                klogf(KLOG_DEBUG,
                      "audio: HDA VERB ACK raw=0x%08x extended=0x%08x unsolicited=%u slot=%u",
                      response_value, response_extended,
                      (response_extended & 0x10) != 0 ? 1 : 0,
                      response_index);
            }
            trace_ring("response-received");
            return true;
        }
        __asm__ volatile("pause");
    }
    trace_ring("response-timeout");
    klogf(KLOG_ERROR,
          "audio: HDA VERB TIMEOUT raw=0x%08x codec=%u node=0x%02x verb=0x%03x payload=0x%02x expected_rirbwp=0x%04x",
          verb, codec, node, command, payload, old_response);
    return false;
}

static bool codec_parameter(uint8_t node, uint8_t parameter, uint32_t *value) {
    klogf(KLOG_DEBUG, "audio: codec%u GET_PARAMETER node=0x%02x parameter=0x%02x",
          codec_address, node, parameter);
    return send_verb(((uint32_t)codec_address << 28)
                     | ((uint32_t)node << 20)
                     | ((uint32_t)HDA_VERB_GET_PARAMETER << 8)
                     | parameter, value);
}

static bool codec_command(uint8_t node, uint16_t verb, uint16_t payload) {
    klogf(KLOG_DEBUG, "audio: codec%u command node=0x%02x verb=0x%03x payload=0x%04x",
          codec_address, node, verb, payload);
    return send_verb(((uint32_t)codec_address << 28)
                     | ((uint32_t)node << 20)
                     | ((uint32_t)verb << 8) | payload, 0);
}

static bool codec_read_command(uint8_t node, uint16_t verb, uint8_t payload,
                               uint32_t *response) {
    klogf(KLOG_DEBUG,
          "audio: codec%u read command node=0x%02x verb=0x%03x payload=0x%02x",
          codec_address, node, verb, payload);
    return send_verb(((uint32_t)codec_address << 28)
                     | ((uint32_t)node << 20)
                     | ((uint32_t)verb << 8) | payload, response);
}

static uint8_t subnode_start(uint32_t value) {
    return (uint8_t)(value >> 16);
}

static uint8_t subnode_count(uint32_t value) {
    return (uint8_t)value;
}

static bool find_audio_function_group(uint32_t root_subnodes,
                                      uint8_t *function_group) {
    uint8_t first_group = subnode_start(root_subnodes);
    uint8_t group_count = subnode_count(root_subnodes);
    klogf(KLOG_DEBUG,
          "audio: codec%u function groups start=%u count=%u raw=0x%08x",
          codec_address, first_group, group_count, root_subnodes);
    for (uint8_t index = 0; index < group_count; index++) {
        uint8_t node = (uint8_t)(first_group + index);
        uint32_t type = 0;
        if (!codec_parameter(node, HDA_PARAMETER_FUNCTION_GROUP_TYPE, &type)) {
            klogf(KLOG_WARN,
                  "audio: codec%u function group node=%u type query failed",
                  codec_address, node);
            continue;
        }
        klogf(KLOG_DEBUG,
              "audio: codec%u function group node=%u type=0x%08x audio=%u",
              codec_address, node, type,
              (type & 0xFF) == HDA_FUNCTION_GROUP_AUDIO ? 1 : 0);
        if ((type & 0xFF) == HDA_FUNCTION_GROUP_AUDIO) {
            *function_group = node;
            klogf(KLOG_INFO, "audio: codec%u selected audio function group node=%u",
                  codec_address, node);
            return true;
        }
    }
    klogf(KLOG_WARN, "audio: codec%u has no audio function group",
          codec_address);
    return false;
}

static bool register_output_device(uint8_t group, uint8_t dac, uint8_t pin,
                                   uint32_t pin_capabilities,
                                   uint32_t default_configuration,
                                   const uint8_t *route_nodes,
                                   const uint8_t *route_connections,
                                   uint8_t route_length) {
    if (output_device_count >= HDA_MAX_OUTPUT_DEVICES) {
        klogf(KLOG_WARN,
              "audio: HDA output ignored reason=DEVICE_TABLE_FULL codec=%u dac=%u pin=%u limit=%u",
              codec_address, dac, pin, HDA_MAX_OUTPUT_DEVICES);
        return false;
    }
    uint32_t index = output_device_count++;
    output_devices[index].codec_address = codec_address;
    output_devices[index].function_group_node = group;
    output_devices[index].dac_node = dac;
    output_devices[index].pin_node = pin;
    output_devices[index].pin_capabilities = pin_capabilities;
    output_devices[index].default_configuration = default_configuration;
    output_devices[index].route_length = route_length;
    for (uint8_t route_index = 0; route_index < route_length; route_index++) {
        output_devices[index].route_nodes[route_index] = route_nodes[route_index];
        output_devices[index].route_connections[route_index] =
            route_connections[route_index];
    }
    klogf(KLOG_INFO,
          "audio: HDA output registered index=%u codec=%u group=%u dac=%u pin=%u route_length=%u pin_caps=0x%08x config=0x%08x device_type=%u connectivity=%u association=%u sequence=%u",
          index, codec_address, group, dac, pin, route_length, pin_capabilities,
          default_configuration, (default_configuration >> 20) & 0x0F,
          default_configuration >> 30, (default_configuration >> 4) & 0x0F,
          default_configuration & 0x0F);
    for (uint8_t route_index = 0; route_index < route_length; route_index++) {
        klogf(KLOG_INFO,
              "audio: HDA output index=%u route[%u] node=%u connection=%u next=%u",
              index, route_index, route_nodes[route_index],
              route_connections[route_index],
              route_index + 1U < route_length ? route_nodes[route_index + 1U] : dac);
    }
    return true;
}

static uint8_t connection_count(uint32_t parameter) {
    return (uint8_t)(parameter & 0x7F);
}

static bool read_connection_nid(uint8_t node, uint8_t index, uint8_t *nid) {
    uint32_t parameter = widget_connections[node];
    bool long_form = (parameter & 0x80) != 0;
    uint8_t entries_per_response = long_form ? 2 : 4;
    uint8_t request_index = (uint8_t)(index
        - index % entries_per_response);
    uint32_t response = 0;
    if (!codec_read_command(node, HDA_VERB_GET_CONNECTION_LIST_ENTRY,
                            request_index, &response)) {
        klogf(KLOG_WARN,
              "audio: codec%u node=%u connection read failed index=%u request=%u long=%u",
              codec_address, node, index, request_index, long_form ? 1 : 0);
        return false;
    }
    uint8_t slot = (uint8_t)(index - request_index);
    uint32_t encoded;
    bool range;
    if (long_form) {
        encoded = (response >> (slot * 16U)) & 0xFFFF;
        range = (encoded & 0x8000) != 0;
        *nid = (uint8_t)(encoded & 0x7FFF);
    } else {
        encoded = (response >> (slot * 8U)) & 0xFF;
        range = (encoded & 0x80) != 0;
        *nid = (uint8_t)(encoded & 0x7F);
    }
    klogf(KLOG_DEBUG,
          "audio: codec%u node=%u connection index=%u request=%u response=0x%08x encoded=0x%x nid=%u range=%u long=%u",
          codec_address, node, index, request_index, response, encoded, *nid,
          range ? 1 : 0, long_form ? 1 : 0);
    if (range) {
        klogf(KLOG_WARN,
              "audio: codec%u node=%u connection index=%u uses range encoding; endpoint nid=%u",
              codec_address, node, index, *nid);
    }
    return true;
}

static uint32_t discover_routes_from_node(
    uint8_t group,
    uint8_t pin,
    uint32_t pin_capabilities,
    uint32_t default_configuration,
    uint8_t node,
    uint8_t depth,
    uint8_t *route_nodes,
    uint8_t *route_connections,
    bool *visited
) {
    if (depth >= HDA_MAX_ROUTE_NODES) {
        klogf(KLOG_WARN,
              "audio: codec%u route from pin=%u stopped reason=DEPTH_LIMIT node=%u depth=%u",
              codec_address, pin, node, depth);
        return 0;
    }
    uint8_t count = connection_count(widget_connections[node]);
    klogf(KLOG_DEBUG,
          "audio: codec%u route walk pin=%u node=%u type=%u depth=%u connections=%u",
          codec_address, pin, node, widget_types[node], depth, count);
    uint32_t found = 0;
    for (uint8_t index = 0; index < count; index++) {
        uint8_t next = 0;
        if (!read_connection_nid(node, index, &next)) {
            continue;
        }
        if (next >= HDA_MAX_CODEC_NODES || !widget_present[next]) {
            klogf(KLOG_WARN,
                  "audio: codec%u route pin=%u node=%u connection=%u rejected next=%u reason=UNKNOWN_WIDGET",
                  codec_address, pin, node, index, next);
            continue;
        }
        route_nodes[depth] = node;
        route_connections[depth] = index;
        if (widget_types[next] == HDA_WIDGET_AUDIO_OUTPUT) {
            klogf(KLOG_OK,
                  "audio: codec%u route found pin=%u dac=%u depth=%u via_node=%u connection=%u",
                  codec_address, pin, next, depth + 1U, node, index);
            if (register_output_device(group, next, pin, pin_capabilities,
                                       default_configuration, route_nodes,
                                       route_connections,
                                       (uint8_t)(depth + 1U))) {
                found++;
            }
            continue;
        }
        if (visited[next]) {
            klogf(KLOG_WARN,
                  "audio: codec%u route pin=%u cycle avoided current=%u next=%u",
                  codec_address, pin, node, next);
            continue;
        }
        visited[next] = true;
        found += discover_routes_from_node(
            group, pin, pin_capabilities, default_configuration, next,
            (uint8_t)(depth + 1U), route_nodes, route_connections, visited);
        visited[next] = false;
    }
    return found;
}

static bool discover_codec(void) {
    klogf(KLOG_INFO, "audio: codec%u discovery begin", codec_address);
    uint32_t subnodes;
    uint32_t function_group;
    if (!codec_parameter(0, HDA_PARAMETER_SUBNODES, &subnodes)) {
        klogf(KLOG_ERROR, "audio: codec%u root subnode query failed", codec_address);
        return false;
    }
    uint8_t first_group = 0;
    uint8_t group_count = subnode_count(subnodes);
    if (group_count == 0 || !find_audio_function_group(subnodes, &first_group)
        || !codec_parameter(first_group, HDA_PARAMETER_SUBNODES, &function_group)) {
        klogf(KLOG_ERROR, "audio: codec%u function group query failed first=%u count=%u",
              codec_address, first_group, group_count);
        return false;
    }
    function_group_node = first_group;
    uint8_t first_widget = subnode_start(function_group);
    uint8_t widget_count = subnode_count(function_group);
    uint32_t vendor_id = 0;
    uint32_t revision_id = 0;
    (void)codec_parameter(0, 0x00, &vendor_id);
    (void)codec_parameter(0, 0x02, &revision_id);
    klogf(KLOG_INFO, "audio: codec%u vendor=0x%08x revision=0x%08x root=0x%08x group=0x%08x",
          codec_address, vendor_id, revision_id, subnodes, function_group);
    klogf(KLOG_INFO,
          "audio: codec%u widget enumeration start=%u count=%u output_table_before=%u",
          codec_address, first_widget, widget_count, output_device_count);
    memset(widget_types, 0, sizeof(widget_types));
    memset(widget_capabilities, 0, sizeof(widget_capabilities));
    memset(widget_connections, 0, sizeof(widget_connections));
    memset(widget_present, 0, sizeof(widget_present));
    uint32_t output_dac_count = 0;
    uint8_t output_pins[HDA_MAX_OUTPUT_DEVICES];
    uint32_t output_pin_caps[HDA_MAX_OUTPUT_DEVICES];
    uint32_t output_pin_configs[HDA_MAX_OUTPUT_DEVICES];
    uint32_t output_pin_count = 0;
    for (uint8_t index = 0; index < widget_count; index++) {
        uint8_t node = (uint8_t)(first_widget + index);
        uint32_t capabilities;
        if (!codec_parameter(node, HDA_PARAMETER_WIDGET_CAPS, &capabilities)) {
            klogf(KLOG_WARN, "audio: codec%u node%u widget caps query failed",
                  codec_address, node);
            continue;
        }
        uint8_t type = (uint8_t)((capabilities >> 20) & 0x0F);
        uint32_t connection_length = 0;
        bool connection_query = codec_parameter(
            node, HDA_PARAMETER_CONNECTION_LIST_LENGTH, &connection_length);
        klogf(KLOG_DEBUG, "audio: codec%u node%u caps=0x%08x type=0x%x connections=0x%08x",
              codec_address, node, capabilities, type, connection_length);
        if (node < HDA_MAX_CODEC_NODES) {
            widget_present[node] = true;
            widget_types[node] = type;
            widget_capabilities[node] = capabilities;
            widget_connections[node] = connection_length;
        }
        if (!connection_query) {
            klogf(KLOG_WARN,
                  "audio: codec%u node%u connection list length query failed",
                  codec_address, node);
        }
        if (type == HDA_WIDGET_AUDIO_OUTPUT) {
            klogf(KLOG_INFO,
                  "audio: codec%u DAC candidate local_index=%u node=%u",
                  codec_address, output_dac_count, node);
            output_dac_count++;
        }
        if (type == HDA_WIDGET_PIN) {
            uint32_t pin_capabilities = 0;
            if (codec_parameter(node, HDA_PARAMETER_PIN_CAPS,
                                &pin_capabilities)) {
                klogf(KLOG_DEBUG, "audio: codec%u node%u pin caps=0x%08x output=%u",
                      codec_address, node, pin_capabilities,
                      (pin_capabilities & HDA_PIN_CAP_OUTPUT) != 0 ? 1 : 0);
                if ((pin_capabilities & HDA_PIN_CAP_OUTPUT) != 0) {
                    uint32_t default_configuration = 0;
                    bool config_ready = codec_read_command(
                        node, HDA_VERB_GET_CONFIG_DEFAULT, 0,
                        &default_configuration);
                    if (!config_ready) {
                        klogf(KLOG_WARN,
                              "audio: codec%u output pin=%u default config query failed",
                              codec_address, node);
                    }
                    if (output_pin_count < HDA_MAX_OUTPUT_DEVICES) {
                        output_pins[output_pin_count] = node;
                        output_pin_caps[output_pin_count] = pin_capabilities;
                        output_pin_configs[output_pin_count] = default_configuration;
                        klogf(KLOG_INFO,
                              "audio: codec%u output pin candidate local_index=%u node=%u config=0x%08x",
                              codec_address, output_pin_count, node,
                              default_configuration);
                        output_pin_count++;
                    } else {
                        klogf(KLOG_WARN,
                              "audio: codec%u output pin ignored node=%u reason=LOCAL_TABLE_FULL",
                              codec_address, node);
                    }
                } else {
                    klogf(KLOG_DEBUG,
                          "audio: codec%u pin node=%u rejected reason=NOT_OUTPUT_CAPABLE",
                          codec_address, node);
                }
            } else {
                klogf(KLOG_WARN,
                      "audio: codec%u pin node=%u capabilities query failed",
                      codec_address, node);
            }
        } else if (type != HDA_WIDGET_AUDIO_OUTPUT) {
            klogf(KLOG_DEBUG,
                  "audio: codec%u node=%u not an output endpoint widget_type=0x%x",
                  codec_address, node, type);
        }
    }
    if (output_dac_count == 0) {
        klogf(KLOG_WARN, "audio: codec%u rejected reason=NO_DAC widgets=%u",
              codec_address, widget_count);
        return false;
    }
    if (output_pin_count == 0) {
        klogf(KLOG_WARN, "audio: codec%u rejected reason=NO_OUTPUT_PINS dacs=%u",
              codec_address, output_dac_count);
        return false;
    }
    uint32_t registered_before = output_device_count;
    for (uint32_t pin_index = 0; pin_index < output_pin_count; pin_index++) {
        uint8_t route_nodes[HDA_MAX_ROUTE_NODES] = {0};
        uint8_t route_connections[HDA_MAX_ROUTE_NODES] = {0};
        bool visited[HDA_MAX_CODEC_NODES] = {false};
        uint8_t pin = output_pins[pin_index];
        visited[pin] = true;
        uint32_t routes = discover_routes_from_node(
            first_group, pin, output_pin_caps[pin_index],
            output_pin_configs[pin_index], pin, 0, route_nodes,
            route_connections, visited);
        if (routes == 0) {
            klogf(KLOG_WARN,
                  "audio: codec%u output pin=%u rejected reason=NO_PATH_TO_DAC",
                  codec_address, pin);
        } else {
            klogf(KLOG_OK,
                  "audio: codec%u output pin=%u topology routes=%u",
                  codec_address, pin, routes);
        }
    }
    klogf(KLOG_OK,
          "audio: codec%u discovery complete dacs=%u output_pins=%u registered_routes=%u",
          codec_address, output_dac_count, output_pin_count,
          output_device_count - registered_before);
    return output_device_count > registered_before;
}

static bool set_widget_power_d0(uint8_t node) {
    if (!codec_command(node, HDA_VERB_SET_POWER_STATE, 0)) {
        klogf(KLOG_ERROR,
              "audio: codec%u power D0 set failed node=%u",
              codec_address, node);
        return false;
    }
    uint32_t state = 0;
    if (!codec_read_command(node, HDA_VERB_GET_POWER_STATE, 0, &state)) {
        klogf(KLOG_WARN,
              "audio: codec%u power state readback failed node=%u",
              codec_address, node);
        return true;
    }
    klogf((state & 0x0F) == 0 ? KLOG_DEBUG : KLOG_WARN,
          "audio: codec%u power node=%u requested=D0 actual=%u raw=0x%08x",
          codec_address, node, state & 0x0F, state);
    return true;
}

static uint8_t amp_zero_db_gain(uint8_t node, uint8_t parameter,
                                uint32_t widget_caps) {
    uint8_t capability_node =
        (widget_caps & HDA_WIDGET_CAP_AMP_OVERRIDE) != 0
            ? node : function_group_node;
    uint32_t capabilities = 0;
    if (!codec_parameter(capability_node, parameter, &capabilities)) {
        klogf(KLOG_WARN,
              "audio: codec%u amp caps query failed widget=%u capability_node=%u parameter=0x%02x using_gain=0",
              codec_address, node, capability_node, parameter);
        return 0;
    }
    uint8_t gain = (uint8_t)(capabilities & 0x7F);
    klogf(KLOG_DEBUG,
          "audio: codec%u amp caps widget=%u capability_node=%u parameter=0x%02x raw=0x%08x zero_db_gain=%u steps=%u step_size=%u mute=%u",
          codec_address, node, capability_node, parameter, capabilities, gain,
          (capabilities >> 8) & 0x7F, (capabilities >> 16) & 0x7F,
          capabilities >> 31);
    return gain;
}

static bool unmute_route_widget(uint8_t node, uint8_t connection,
                                uint32_t capabilities) {
    bool ready = true;
    if ((capabilities & HDA_WIDGET_CAP_INPUT_AMP) != 0) {
        uint8_t gain = amp_zero_db_gain(
            node, HDA_PARAMETER_INPUT_AMP_CAPS, capabilities);
        uint16_t payload = (uint16_t)(0x7000
            | ((uint16_t)connection << 8) | gain);
        if (!codec_command(node, HDA_VERB_SET_AMP_GAIN_MUTE, payload)) {
            klogf(KLOG_ERROR,
                  "audio: codec%u input amp unmute failed node=%u connection=%u payload=0x%04x",
                  codec_address, node, connection, payload);
            ready = false;
        } else {
            klogf(KLOG_DEBUG,
                  "audio: codec%u input amp unmuted node=%u connection=%u payload=0x%04x",
                  codec_address, node, connection, payload);
        }
    } else {
        klogf(KLOG_DEBUG,
              "audio: codec%u node=%u has no input amp",
              codec_address, node);
    }
    if ((capabilities & HDA_WIDGET_CAP_OUTPUT_AMP) != 0) {
        uint8_t gain = amp_zero_db_gain(
            node, HDA_PARAMETER_OUTPUT_AMP_CAPS, capabilities);
        uint16_t payload = (uint16_t)(0xB000 | gain);
        if (!codec_command(node, HDA_VERB_SET_AMP_GAIN_MUTE, payload)) {
            klogf(KLOG_ERROR,
                  "audio: codec%u output amp unmute failed node=%u",
                  codec_address, node);
            ready = false;
        } else {
            klogf(KLOG_DEBUG,
                  "audio: codec%u output amp unmuted node=%u payload=0x%04x",
                  codec_address, node, payload);
        }
    } else {
        klogf(KLOG_DEBUG,
              "audio: codec%u node=%u has no output amp",
              codec_address, node);
    }
    return ready;
}

static bool configure_codec(const struct hda_output_device_info *device) {
    klogf(KLOG_INFO, "audio: configuring codec%u pin=%u dac=%u format=0x%04x",
          codec_address, pin_node, dac_node, HDA_PCM_FORMAT);
    if (!set_widget_power_d0(function_group_node)) {
        return false;
    }
    if (!set_widget_power_d0(dac_node)) {
        return false;
    }
    for (uint8_t index = device->route_length; index > 0; index--) {
        uint8_t route_index = (uint8_t)(index - 1U);
        uint8_t node = device->route_nodes[route_index];
        uint8_t connection = device->route_connections[route_index];
        uint32_t capabilities = 0;
        uint32_t connections = 0;
        if (!codec_parameter(node, HDA_PARAMETER_WIDGET_CAPS, &capabilities)
            || !codec_parameter(node, HDA_PARAMETER_CONNECTION_LIST_LENGTH,
                                &connections)) {
            klogf(KLOG_ERROR,
                  "audio: route metadata refresh failed node=%u route_index=%u",
                  node, route_index);
            return false;
        }
        klogf(KLOG_INFO,
              "audio: configuring route index=%u node=%u connection=%u type=%u caps=0x%08x",
              route_index, node, connection,
              (capabilities >> 20) & 0x0F, capabilities);
        if (!set_widget_power_d0(node)) {
            return false;
        }
        if (connection_count(connections) > 1) {
            if (!codec_command(node, HDA_VERB_SET_CONNECTION_SELECT,
                               connection)) {
                klogf(KLOG_ERROR,
                      "audio: route connection select failed node=%u index=%u",
                      node, connection);
                return false;
            }
            klogf(KLOG_DEBUG,
                  "audio: route connection selected node=%u index=%u",
                  node, connection);
        } else {
            klogf(KLOG_DEBUG,
                  "audio: route node=%u has a single connection index=%u",
                  node, connection);
        }
        if (!unmute_route_widget(node, connection, capabilities)) {
            return false;
        }
    }
    if (!codec_command(pin_node, HDA_VERB_SET_PIN_WIDGET_CONTROL, 0x40)) {
        klog(KLOG_ERROR, "audio: failed to enable output pin");
        return false;
    }
    klogf(KLOG_DEBUG, "audio: route stage=PIN_OUTPUT_ENABLE result=OK pin=%u",
          pin_node);
    if ((device->pin_capabilities & HDA_PIN_CAP_EAPD) != 0) {
        if (!codec_command(pin_node, HDA_VERB_SET_EAPD_BTL, 0x02)) {
            klog(KLOG_ERROR, "audio: failed to enable codec amplifier");
            return false;
        }
        klogf(KLOG_DEBUG, "audio: route stage=EAPD_ENABLE result=OK pin=%u",
              pin_node);
    } else {
        klogf(KLOG_DEBUG,
              "audio: route stage=EAPD_ENABLE skipped pin=%u reason=NOT_SUPPORTED",
              pin_node);
    }
    if (!codec_command(dac_node, HDA_VERB_SET_CONVERTER_STREAM_CHANNEL,
                       HDA_STREAM_TAG << 4)) {
        klog(KLOG_ERROR, "audio: failed to bind DAC to output stream");
        return false;
    }
    klogf(KLOG_DEBUG,
          "audio: route stage=STREAM_BIND result=OK dac=%u tag=%u channel=0",
          dac_node, HDA_STREAM_TAG);
    if (!codec_command(dac_node, HDA_VERB_SET_STREAM_FORMAT, HDA_PCM_FORMAT)) {
        klog(KLOG_ERROR, "audio: failed to set DAC stream format");
        return false;
    }
    klogf(KLOG_DEBUG,
          "audio: route stage=FORMAT result=OK dac=%u format=0x%04x",
          dac_node, HDA_PCM_FORMAT);
    uint32_t dac_capabilities = 0;
    if (!codec_parameter(dac_node, HDA_PARAMETER_WIDGET_CAPS,
                         &dac_capabilities)) {
        klog(KLOG_ERROR, "audio: failed to query DAC widget capabilities");
        return false;
    }
    if ((dac_capabilities & HDA_WIDGET_CAP_OUTPUT_AMP) != 0) {
        uint8_t gain = amp_zero_db_gain(
            dac_node, HDA_PARAMETER_OUTPUT_AMP_CAPS, dac_capabilities);
        if (!codec_command(dac_node, HDA_VERB_SET_AMP_GAIN_MUTE,
                           (uint16_t)(0xB000 | gain))) {
            klog(KLOG_ERROR, "audio: failed to set DAC amplifier gain");
            return false;
        }
    } else {
        klogf(KLOG_DEBUG, "audio: DAC node=%u has no output amplifier",
              dac_node);
    }
    klogf(KLOG_OK,
          "audio: codec route configured codec=%u group=%u dac=%u pin=%u",
          codec_address, function_group_node, dac_node, pin_node);
    return true;
}

static bool configure_stream(void) {
    klog(KLOG_INFO, "audio: HDA PCM stream configuration begin");
    uint64_t bdl_address = physical_address(bdl);
    uint64_t pcm_address = physical_address(pcm_buffer);
    uint32_t stream_offset = HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    volatile uint8_t *stream = regs + stream_offset;
    klogf(KLOG_INFO,
          "audio: HDA output stream selected descriptor=%u offset=0x%x inputs=%u outputs=%u",
          controller.input_streams, stream_offset, controller.input_streams,
          controller.output_streams);
    klogf(KLOG_DEBUG,
          "audio: HDA DMA layout pcm_virt=0x%llx pcm_phys=0x%llx bdl_virt=0x%llx bdl_phys=0x%llx bytes=%u",
          (uint64_t)(uintptr_t)pcm_buffer, pcm_address,
          (uint64_t)(uintptr_t)bdl, bdl_address, HDA_PCM_BYTES * 2U);
    uint32_t control = read_stream_control(stream);
    klogf(KLOG_DEBUG, "audio: HDA stream before ctl=0x%08x sts=0x%02x lpib=0x%08x",
          control, *(volatile uint8_t *)(stream + 0x03),
          *(volatile uint32_t *)(stream + 0x04));
    if (!reset_stream(stream)) {
        return false;
    }
    bdl[0].address = pcm_address;
    bdl[0].length = HDA_PCM_BYTES;
    bdl[0].flags = 0;
    bdl[1].address = pcm_address + HDA_PCM_BYTES;
    bdl[1].length = HDA_PCM_BYTES;
    bdl[1].flags = 0;
    klogf(KLOG_DEBUG,
          "audio: HDA BDL[0] addr=0x%llx length=%u flags=0x%x BDL[1] addr=0x%llx length=%u flags=0x%x",
          bdl[0].address, bdl[0].length, bdl[0].flags, bdl[1].address,
          bdl[1].length, bdl[1].flags);
    *(volatile uint32_t *)(stream + HDA_STREAM_CBL) = HDA_PCM_BYTES * 2U;
    klogf(KLOG_DEBUG, "audio: HDA stream CBL write=%u readback=%u",
          HDA_PCM_BYTES * 2U,
          *(volatile uint32_t *)(stream + HDA_STREAM_CBL));
    *(volatile uint16_t *)(stream + HDA_STREAM_LVI) = 1;
    klogf(KLOG_DEBUG, "audio: HDA stream LVI write=1 readback=%u",
          *(volatile uint16_t *)(stream + HDA_STREAM_LVI));
    *(volatile uint16_t *)(stream + HDA_STREAM_FMT) = HDA_PCM_FORMAT;
    klogf(KLOG_DEBUG, "audio: HDA stream FMT write=0x%04x readback=0x%04x",
          HDA_PCM_FORMAT, *(volatile uint16_t *)(stream + HDA_STREAM_FMT));
    *(volatile uint32_t *)(stream + HDA_STREAM_BDPL) = (uint32_t)bdl_address;
    klogf(KLOG_DEBUG, "audio: HDA stream BDPL write=0x%08x readback=0x%08x",
          (uint32_t)bdl_address,
          *(volatile uint32_t *)(stream + HDA_STREAM_BDPL));
    *(volatile uint32_t *)(stream + HDA_STREAM_BDPU) = (uint32_t)(bdl_address >> 32);
    klogf(KLOG_DEBUG, "audio: HDA stream BDPU write=0x%08x readback=0x%08x",
          (uint32_t)(bdl_address >> 32),
          *(volatile uint32_t *)(stream + HDA_STREAM_BDPU));
    *(volatile uint8_t *)(stream + HDA_STREAM_STS) = 0x1C;
    klogf(KLOG_DEBUG, "audio: HDA stream status cleared readback=0x%02x",
          *(volatile uint8_t *)(stream + HDA_STREAM_STS));
    write_stream_control(stream, (uint32_t)HDA_STREAM_TAG << 20);
    klogf(KLOG_DEBUG, "audio: HDA stream programmed base=0x%x ctl=0x%08x cbl=%u lvi=%u fmt=0x%04x bdl=0x%llx",
          stream_offset, read_stream_control(stream),
          HDA_PCM_BYTES * 2U, 1, HDA_PCM_FORMAT, bdl_address);
    return true;
}

static void deactivate_current_output(void) {
    if (selected_output_index >= output_device_count) {
        klog(KLOG_DEBUG,
             "audio: HDA deactivate skipped reason=NO_SELECTED_OUTPUT");
        return;
    }
    struct hda_output_device_info *device =
        &output_devices[selected_output_index];
    volatile uint8_t *stream = regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    uint32_t control = read_stream_control(stream);
    klogf(KLOG_INFO,
          "audio: HDA deactivate output index=%u codec=%u dac=%u pin=%u ctl=0x%06x",
          selected_output_index, device->codec_address, device->dac_node,
          device->pin_node, control);
    write_stream_control(stream, control & ~HDA_STREAM_RUN);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, false)) {
        klog(KLOG_ERROR,
             "audio: HDA deactivate stream stop timeout; continuing route switch");
    }
    codec_address = device->codec_address;
    if (!codec_command(device->pin_node, HDA_VERB_SET_PIN_WIDGET_CONTROL, 0)) {
        klogf(KLOG_WARN,
              "audio: HDA previous pin disable failed codec=%u pin=%u",
              device->codec_address, device->pin_node);
    } else {
        klogf(KLOG_DEBUG,
              "audio: HDA previous pin disabled codec=%u pin=%u",
              device->codec_address, device->pin_node);
    }
}

static bool configure_output_device(uint32_t index) {
    if (index >= output_device_count) {
        klogf(KLOG_ERROR,
              "audio: HDA configure rejected reason=INDEX_OUT_OF_RANGE index=%u count=%u",
              index, output_device_count);
        return false;
    }
    deactivate_current_output();
    selected_output_index = HDA_NO_OUTPUT_DEVICE;
    struct hda_output_device_info *device = &output_devices[index];
    codec_address = device->codec_address;
    function_group_node = device->function_group_node;
    dac_node = device->dac_node;
    pin_node = device->pin_node;
    pcm_ready = false;
    klogf(KLOG_INFO,
          "audio: HDA configure output begin index=%u codec=%u group=%u dac=%u pin=%u config=0x%08x",
          index, codec_address, function_group_node, dac_node, pin_node,
          device->default_configuration);
    if (!configure_codec(device)) {
        klogf(KLOG_ERROR,
              "audio: HDA configure output failed index=%u stage=CODEC_ROUTE",
              index);
        return false;
    }
    if (!configure_stream()) {
        klogf(KLOG_ERROR,
              "audio: HDA configure output failed index=%u stage=DMA_STREAM",
              index);
        return false;
    }
    selected_output_index = index;
    pcm_ready = true;
    klogf(KLOG_OK,
          "audio: HDA configure output complete index=%u codec=%u dac=%u pin=%u pcm_ready=1",
          index, codec_address, dac_node, pin_node);
    return true;
}

static bool setup_pcm(void) {
    klog(KLOG_INFO, "audio: HDA PCM setup begin");
    klogf(KLOG_INFO,
          "audio: HDA prerequisites mapping=%u mmio=%u outputs=%u inputs=%u regs=0x%llx",
          mapping_ready ? 1 : 0, controller.mmio_ready ? 1 : 0,
          controller.output_streams, controller.input_streams,
          (uint64_t)(uintptr_t)regs);
    if (!mapping_ready) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=ADDRESS_MAPPING_NOT_READY");
        return false;
    }
    if (!controller.mmio_ready || !regs) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=MMIO_NOT_READY");
        return false;
    }
    if (controller.output_streams == 0) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=NO_OUTPUT_STREAMS");
        return false;
    }
    if (!reset_controller()) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=CONTROLLER_RESET_FAILED");
        return false;
    }
    if (!setup_command_ring()) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=COMMAND_RING_FAILED");
        return false;
    }
    uint16_t state = read16(HDA_STATESTS);
    klogf(KLOG_INFO, "audio: HDA codec presence STATESTS=0x%04x", state);
    if ((state & 0x7FFF) == 0) {
        klog(KLOG_ERROR, "audio: HDA PCM unavailable reason=NO_CODEC_PRESENCE_BITS");
        return false;
    }
    for (uint8_t address = 0; address < 15; address++) {
        klogf(KLOG_DEBUG, "audio: HDA checking codec address=%u present=%u",
              address, (state & (1U << address)) != 0 ? 1 : 0);
        if ((state & (1U << address)) == 0) {
            klogf(KLOG_DEBUG,
                  "audio: codec address=%u skipped reason=STATESTS_NOT_PRESENT",
                  address);
            continue;
        }
        codec_address = address;
        if (!discover_codec()) {
            klogf(KLOG_WARN,
                  "audio: codec%u discovery produced no selectable output devices",
                  codec_address);
        }
    }
    klogf(KLOG_INFO, "audio: HDA discovery summary output_devices=%u",
          output_device_count);
    if (output_device_count == 0) {
        klog(KLOG_ERROR,
             "audio: HDA PCM unavailable reason=NO_SELECTABLE_OUTPUT_DEVICES");
        return false;
    }
    for (uint32_t index = 0; index < output_device_count; index++) {
        klogf(KLOG_INFO, "audio: automatic output trial index=%u count=%u",
              index, output_device_count);
        if (configure_output_device(index)) {
            klogf(KLOG_OK,
                  "audio: automatic output selected index=%u; manual switching remains available",
                  index);
            return true;
        }
        klogf(KLOG_WARN, "audio: automatic output trial failed index=%u",
              index);
    }
    klog(KLOG_ERROR,
         "audio: HDA PCM unavailable reason=ALL_OUTPUT_CONFIGURATIONS_FAILED");
    return false;
}

static void inspect_audio_device(const struct pci_device_info *device, void *context) {
    (void)context;
    klogf(KLOG_DEBUG,
          "audio: PCI inspect %u:%u.%u id=%04x:%04x class=%02x/%02x prog=%02x hda=%u already_selected=%u",
          device->bus, device->slot, device->function, device->vendor_id,
          device->device_id, device->class_code, device->subclass,
          device->programming_interface,
          device->class_code == PCI_CLASS_MULTIMEDIA
              && device->subclass == PCI_SUBCLASS_HDA ? 1 : 0,
          present ? 1 : 0);
    if (device->class_code != PCI_CLASS_MULTIMEDIA
        || device->subclass != PCI_SUBCLASS_HDA) {
        klogf(KLOG_DEBUG,
              "audio: PCI device %u:%u.%u rejected reason=NOT_HDA_CLASS",
              device->bus, device->slot, device->function);
        return;
    }
    if (present) {
        klogf(KLOG_WARN,
              "audio: HDA PCI candidate %u:%u.%u ignored reason=CONTROLLER_ALREADY_SELECTED",
              device->bus, device->slot, device->function);
        return;
    }
    klogf(KLOG_INFO, "audio: HDA PCI candidate %u:%u.%u id=%04x:%04x class=%02x/%02x prog=%02x",
          device->bus, device->slot, device->function, device->vendor_id,
          device->device_id, device->class_code, device->subclass,
          device->programming_interface);
    memset(&controller, 0, sizeof(controller));
    controller.vendor_id = device->vendor_id;
    controller.device_id = device->device_id;
    controller.bus = device->bus;
    controller.slot = device->slot;
    controller.function = device->function;
    uint32_t command = pci_read_config32(device->bus, device->slot, device->function, 0x04);
    klogf(KLOG_DEBUG, "audio: HDA PCI command before=0x%08x request=0x%08x",
          command, command | 0x00000006);
    pci_write_config32(device->bus, device->slot, device->function, 0x04, command | 0x00000006);
    uint32_t command_after = pci_read_config32(
        device->bus, device->slot, device->function, 0x04);
    klogf(KLOG_DEBUG, "audio: HDA PCI command readback=0x%08x mem=%u busmaster=%u",
          command_after, (command_after & 0x02) != 0 ? 1 : 0,
          (command_after & 0x04) != 0 ? 1 : 0);
    uint64_t bar0 = pci_read_bar(device->bus, device->slot, device->function, 0);
    if (bar0 == 0) {
        klog(KLOG_WARN, "audio: HDA BAR0 is empty");
        return;
    }
    regs = (volatile uint8_t *)mmio_map(bar0, HDA_MMIO_SIZE);
    if (!regs) {
        klog(KLOG_WARN, "audio: HDA BAR0 could not be mapped");
        return;
    }
    klogf(KLOG_INFO, "audio: HDA MMIO mapped phys=0x%llx virt=0x%llx size=0x%x",
          bar0, (uint64_t)(uintptr_t)regs, HDA_MMIO_SIZE);
    uint16_t capabilities = read16(0x00);
    controller.output_streams = (uint8_t)((capabilities >> 12) & 0x0F);
    controller.input_streams = (uint8_t)((capabilities >> 8) & 0x0F);
    controller.bidirectional_streams = (uint8_t)((capabilities >> 3) & 0x1F);
    controller.minor_version = *((volatile uint8_t *)(regs + 0x02));
    controller.major_version = *((volatile uint8_t *)(regs + 0x03));
    controller.mmio_ready = true;
    present = true;
    klogf(KLOG_INFO, "audio: HDA PCI %u:%u.%u command 0x%08x BAR0=0x%llx GCAP=0x%04x version=%u.%u",
          device->bus, device->slot, device->function, command_after,
          bar0, capabilities, controller.major_version, controller.minor_version);
    klogf(KLOG_DEBUG,
          "audio: HDA initial registers GCTL=0x%08x STATESTS=0x%04x CORBCTL=0x%02x RIRBCTL=0x%02x CORBSIZE=0x%02x",
          read32(HDA_GCTL), read16(HDA_STATESTS), read8(HDA_CORBCTL),
          read8(HDA_RIRBCTL), read8(HDA_CORBSIZE));
}

void hda_set_address_mapping(uint64_t physical_base, uint64_t virtual_base) {
    kernel_physical_base = physical_base;
    kernel_virtual_base = virtual_base;
    mapping_ready = true;
    klogf(KLOG_INFO,
          "audio: HDA address mapping ready kernel_phys=0x%llx kernel_virt=0x%llx",
          physical_base, virtual_base);
}

void hda_init(void) {
    klog(KLOG_INFO, "audio: HDA init begin");
    present = false;
    pcm_ready = false;
    regs = 0;
    codec_address = 0;
    function_group_node = 0;
    dac_node = 0;
    pin_node = 0;
    output_device_count = 0;
    selected_output_index = HDA_NO_OUTPUT_DEVICE;
    memset(&controller, 0, sizeof(controller));
    memset(output_devices, 0, sizeof(output_devices));
    klog(KLOG_DEBUG, "audio: HDA state cleared; starting full PCI enumeration");
    pci_enumerate(inspect_audio_device, 0);
    if (!present) {
        klog(KLOG_WARN, "audio: no Intel HDA controller found on PCI");
        return;
    }
    klogf(KLOG_INFO, "audio: HDA %04x:%04x at %u:%u.%u out=%u in=%u",
          controller.vendor_id, controller.device_id, controller.bus,
          controller.slot, controller.function, controller.output_streams,
          controller.input_streams);
    bool setup_ready = setup_pcm();
    klogf(setup_ready ? KLOG_OK : KLOG_ERROR,
          "audio: HDA setup_pcm returned=%u outputs=%u selected=%u",
          setup_ready ? 1 : 0, output_device_count, selected_output_index);
    klogf(KLOG_INFO, "audio: HDA init complete present=%u pcm_ready=%u",
          present ? 1 : 0, pcm_ready ? 1 : 0);
}

bool hda_is_present(void) {
    return present;
}

bool hda_pcm_output_ready(void) {
    return pcm_ready;
}

uint32_t hda_output_device_count(void) {
    klogf(KLOG_DEBUG, "audio: HDA output device count query result=%u",
          output_device_count);
    return output_device_count;
}

uint32_t hda_selected_output_device(void) {
    klogf(KLOG_DEBUG, "audio: HDA selected output query result=%u",
          selected_output_index);
    return selected_output_index;
}

bool hda_get_output_device(uint32_t index, struct hda_output_device_info *out) {
    if (!out) {
        klogf(KLOG_ERROR,
              "audio: HDA get output device failed index=%u reason=NULL_OUTPUT",
              index);
        return false;
    }
    if (index >= output_device_count) {
        klogf(KLOG_DEBUG,
              "audio: HDA get output device miss index=%u count=%u",
              index, output_device_count);
        return false;
    }
    *out = output_devices[index];
    klogf(KLOG_DEBUG,
          "audio: HDA get output device index=%u codec=%u group=%u dac=%u pin=%u config=0x%08x",
          index, out->codec_address, out->function_group_node, out->dac_node,
          out->pin_node, out->default_configuration);
    return true;
}

bool hda_select_output_device(uint32_t index) {
    klogf(KLOG_INFO,
          "audio: HDA manual selection request index=%u count=%u present=%u regs=%u",
          index, output_device_count, present ? 1 : 0, regs ? 1 : 0);
    if (!present || !regs) {
        klog(KLOG_ERROR,
             "audio: HDA manual selection failed reason=CONTROLLER_NOT_READY");
        return false;
    }
    return configure_output_device(index);
}

static uint8_t amp_gain_for_volume(uint8_t node, uint8_t parameter,
                                   uint32_t widget_caps, uint8_t volume) {
    uint8_t capability_node=
        (widget_caps&HDA_WIDGET_CAP_AMP_OVERRIDE)!=0
            ? node : function_group_node;
    uint32_t capabilities=0;
    if(!codec_parameter(capability_node,parameter,&capabilities)) return 0;
    uint32_t zero_db=capabilities&0x7F;
    return (uint8_t)((zero_db*volume)/100U);
}

static bool set_widget_master_gain(uint8_t node, uint8_t connection,
                                   uint8_t volume, bool muted) {
    uint32_t capabilities=0;
    if(!codec_parameter(node,HDA_PARAMETER_WIDGET_CAPS,&capabilities))
        return false;
    bool ready=true;
    uint16_t mute=muted ? 0x80U : 0;
    if((capabilities&HDA_WIDGET_CAP_INPUT_AMP)!=0){
        uint8_t gain=amp_gain_for_volume(
            node,HDA_PARAMETER_INPUT_AMP_CAPS,capabilities,volume);
        uint16_t payload=(uint16_t)(0x7000U
            |((uint16_t)connection<<8)|mute|gain);
        if(!codec_command(node,HDA_VERB_SET_AMP_GAIN_MUTE,payload))
            ready=false;
    }
    if((capabilities&HDA_WIDGET_CAP_OUTPUT_AMP)!=0){
        uint8_t gain=amp_gain_for_volume(
            node,HDA_PARAMETER_OUTPUT_AMP_CAPS,capabilities,volume);
        if(!codec_command(node,HDA_VERB_SET_AMP_GAIN_MUTE,
                          (uint16_t)(0xB000U|mute|gain))) ready=false;
    }
    return ready;
}

bool hda_set_master_volume(uint8_t volume, bool muted) {
    if(!pcm_ready || selected_output_index>=output_device_count) return false;
    if(volume>100) volume=100;
    const struct hda_output_device_info *device=
        &output_devices[selected_output_index];
    codec_address=device->codec_address;
    function_group_node=device->function_group_node;
    bool ready=true;
    for(uint8_t index=0;index<device->route_length;index++)
        if(!set_widget_master_gain(device->route_nodes[index],
                                   device->route_connections[index],
                                   volume,muted)) ready=false;
    if(!set_widget_master_gain(device->dac_node,0,volume,muted)) ready=false;
    klogf(ready ? KLOG_DEBUG : KLOG_WARN,
          "audio: HDA master gain volume=%u mute=%u route=%u result=%u",
          volume,muted ? 1 : 0,selected_output_index,ready ? 1 : 0);
    return ready;
}

bool hda_play_tone(uint16_t frequency_hz, uint8_t volume) {
    if (!pcm_ready || frequency_hz == 0 || volume == 0) {
        klogf(KLOG_WARN,
              "audio: HDA play rejected pcm_ready=%u frequency=%u volume=%u selected=%u",
              pcm_ready ? 1 : 0, frequency_hz, volume,
              selected_output_index);
        return false;
    }
    uint32_t amplitude = (32767U * volume) / 100U;
    klogf(KLOG_DEBUG, "audio: HDA PCM fill begin freq=%u volume=%u amplitude=%u bytes=%u",
          frequency_hz, volume, amplitude, HDA_PCM_BYTES * 2U);
    for (uint32_t frame = 0; frame < HDA_PCM_SAMPLES * 2U; frame++) {
        uint32_t phase = (uint32_t)(((uint64_t)frame * frequency_hz) % HDA_PCM_RATE);
        int16_t value = phase < HDA_PCM_RATE / 2U
            ? (int16_t)amplitude : (int16_t)-amplitude;
        uint32_t offset = frame * 4U;
        pcm_buffer[offset] = (uint8_t)value;
        pcm_buffer[offset + 1] = (uint8_t)(value >> 8);
        pcm_buffer[offset + 2] = (uint8_t)value;
        pcm_buffer[offset + 3] = (uint8_t)(value >> 8);
    }
    volatile uint8_t *stream = regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control | HDA_STREAM_RUN);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, true)) {
        klog(KLOG_ERROR, "audio: HDA PCM RUN assertion timeout");
        return false;
    }
    klogf(KLOG_INFO, "audio: HDA PCM stream RUN ctl=0x%08x sts=0x%02x lpib=0x%08x",
          read_stream_control(stream),
          *(volatile uint8_t *)(stream + 0x03),
          *(volatile uint32_t *)(stream + 0x04));
    trace_stream_status("after-run", stream);
    return true;
}

static volatile uint8_t *pcm_stream(void) {
    return regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
}

void hda_pcm_fill_half(uint32_t half, const int16_t *stereo_frames,
                       uint32_t frame_count) {
    if (!regs || half > 1U || !stereo_frames) {
        return;
    }
    if (frame_count > HDA_PCM_SAMPLES) {
        frame_count = HDA_PCM_SAMPLES;
    }
    uint8_t *dst = pcm_buffer + half * HDA_PCM_BYTES;
    const uint8_t *src = (const uint8_t *)stereo_frames;
    uint32_t bytes = frame_count * 4U;
    for (uint32_t i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
    for (uint32_t i = bytes; i < HDA_PCM_BYTES; i++) {
        dst[i] = 0;
    }
}

bool hda_pcm_start(void) {
    if (!pcm_ready || !regs) {
        klog(KLOG_WARN, "audio: HDA PCM start rejected reason=NOT_READY");
        return false;
    }
    volatile uint8_t *stream = pcm_stream();
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control | HDA_STREAM_RUN);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, true)) {
        klog(KLOG_ERROR, "audio: HDA PCM RUN assertion timeout");
        return false;
    }
    return true;
}

void hda_pcm_stop(void) {
    if (!pcm_ready || !regs) {
        return;
    }
    volatile uint8_t *stream = pcm_stream();
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control & ~HDA_STREAM_RUN);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, false)) {
        klog(KLOG_ERROR, "audio: HDA PCM RUN clear timeout during stop");
    }
}

uint32_t hda_pcm_position(void) {
    if (!pcm_ready || !regs) {
        return 0;
    }
    return *(volatile uint32_t *)(pcm_stream() + HDA_STREAM_LPIB);
}

void hda_stop_tone(void) {
    if (!pcm_ready || !regs) {
        klogf(KLOG_DEBUG, "audio: HDA stop ignored pcm_ready=%u regs=%u",
              pcm_ready ? 1 : 0, regs ? 1 : 0);
        return;
    }
    volatile uint8_t *stream = regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control & ~HDA_STREAM_RUN);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, false)) {
        klog(KLOG_ERROR, "audio: HDA PCM RUN clear timeout during stop");
    }
    klogf(KLOG_DEBUG, "audio: HDA PCM stream STOP ctl=0x%08x sts=0x%02x",
          read_stream_control(stream),
          *(volatile uint8_t *)(stream + 0x03));
    trace_stream_status("after-stop", stream);
}

bool hda_get_controller_info(struct hda_controller_info *out) {
    if (!out || !present) {
        return false;
    }
    *out = controller;
    return true;
}
