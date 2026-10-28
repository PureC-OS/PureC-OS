#include "include/802.h"

#ifndef PURECOS_KERNEL
static void data_memcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}
static int data_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
#else
#include "../../lib/string.h"
#define data_memcpy memcpy
#define data_memcmp memcmp
#endif

#define ETH_HDR_LEN 14
#define DOT11_DATA_HDR_LEN 24
#define DOT11_QOS_CTRL_LEN 2
#define DOT11_HT_CTRL_LEN 4

static const uint8_t llc_snap_prefix[6] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};

uint16_t dot11_data_header_len(uint16_t frame_control) {
    uint16_t hdr = DOT11_DATA_HDR_LEN;
    uint8_t stype = (uint8_t)DOT11_FC_STYPE(frame_control);

    if (stype & DOT11_STYPE_DATA_QOS_BIT) {
        hdr = (uint16_t)(hdr + DOT11_QOS_CTRL_LEN);
        if (frame_control & DOT11_FC_ORDER) hdr = (uint16_t)(hdr + DOT11_HT_CTRL_LEN);
    }
    return hdr;
}

uint16_t dot11_data_to_ethernet(const uint8_t *frame, uint16_t len,
                                const uint8_t bssid[DOT11_ADDR_LEN],
                                uint8_t *eth_out, uint16_t eth_cap) {
    uint16_t fc, hdr, payload;

    if (!frame || !bssid || !eth_out || len < DOT11_DATA_HDR_LEN) return 0;
    fc = (uint16_t)((uint16_t)frame[0] | ((uint16_t)frame[1] << 8));
    if ((fc & DOT11_FC_VERSION_MASK) != 0 || DOT11_FC_TYPE(fc) != DOT11_FTYPE_DATA) return 0;
    if (!(fc & DOT11_FC_FROMDS) || (fc & DOT11_FC_TODS)) return 0;
    if (fc & DOT11_FC_PROTECTED) return 0;
    if (DOT11_FC_STYPE(fc) & DOT11_STYPE_DATA_NULL_BIT) return 0;
    if (data_memcmp(frame + 10, bssid, DOT11_ADDR_LEN) != 0) return 0;

    hdr = dot11_data_header_len(fc);
    if (len < (uint16_t)(hdr + DOT11_LLC_SNAP_LEN)) return 0;
    if (data_memcmp(frame + hdr, llc_snap_prefix, 6) != 0) return 0;

    payload = (uint16_t)(len - hdr - DOT11_LLC_SNAP_LEN);
    if ((uint32_t)payload + ETH_HDR_LEN > eth_cap) return 0;

    data_memcpy(eth_out, frame + 4, DOT11_ADDR_LEN);
    data_memcpy(eth_out + 6, frame + 16, DOT11_ADDR_LEN);
    data_memcpy(eth_out + 12, frame + hdr + 6, 2);
    data_memcpy(eth_out + ETH_HDR_LEN, frame + hdr + DOT11_LLC_SNAP_LEN, payload);
    return (uint16_t)(payload + ETH_HDR_LEN);
}

uint16_t dot11_ethernet_to_data(const uint8_t *eth, uint16_t eth_len,
                                const uint8_t bssid[DOT11_ADDR_LEN],
                                uint16_t seq_ctrl, uint8_t *out, uint16_t out_cap) {
    uint16_t total;

    if (!eth || !bssid || !out || eth_len < ETH_HDR_LEN) return 0;
    total = (uint16_t)(DOT11_DATA_HDR_LEN + DOT11_LLC_SNAP_LEN + eth_len - ETH_HDR_LEN);
    if (total > out_cap) return 0;

    out[0] = (uint8_t)(DOT11_FC_MAKE(DOT11_FTYPE_DATA, 0) & 0xFF);
    out[1] = (uint8_t)(DOT11_FC_TODS >> 8);
    out[2] = 0;
    out[3] = 0;
    data_memcpy(out + 4, bssid, DOT11_ADDR_LEN);
    data_memcpy(out + 10, eth + 6, DOT11_ADDR_LEN);
    data_memcpy(out + 16, eth, DOT11_ADDR_LEN);
    out[22] = (uint8_t)seq_ctrl;
    out[23] = (uint8_t)(seq_ctrl >> 8);
    data_memcpy(out + DOT11_DATA_HDR_LEN, llc_snap_prefix, 6);
    data_memcpy(out + DOT11_DATA_HDR_LEN + 6, eth + 12, 2);
    data_memcpy(out + DOT11_DATA_HDR_LEN + DOT11_LLC_SNAP_LEN, eth + ETH_HDR_LEN,
                (uint32_t)(eth_len - ETH_HDR_LEN));
    return total;
}
