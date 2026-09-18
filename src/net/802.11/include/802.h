#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOT11_SSID_MAX 32
#define DOT11_MGMT_MAX 512
#define DOT11_ADDR_LEN 6

#define DOT11_FTYPE_MGMT 0x00
#define DOT11_FTYPE_CTRL 0x01
#define DOT11_FTYPE_DATA 0x02

#define DOT11_STYPE_ASSOC_REQ 0x00
#define DOT11_STYPE_ASSOC_RESP 0x01
#define DOT11_STYPE_REASSOC_REQ 0x02
#define DOT11_STYPE_PROBE_REQ 0x04
#define DOT11_STYPE_PROBE_RESP 0x05
#define DOT11_STYPE_BEACON 0x08
#define DOT11_STYPE_DISASSOC 0x0A
#define DOT11_STYPE_AUTH 0x0B
#define DOT11_STYPE_DEAUTH 0x0C

#define DOT11_FC_MAKE(type, stype) \
    ((uint16_t)(((uint16_t)(type) << 2) | ((uint16_t)(stype) << 4)))
#define DOT11_FC_TYPE(fc) (((fc) >> 2) & 0x3)
#define DOT11_FC_STYPE(fc) (((fc) >> 4) & 0xF)

#define DOT11_AUTH_ALG_OPEN 0x0000
#define DOT11_AUTH_SEQ_REQ 0x0001
#define DOT11_AUTH_SEQ_RESP 0x0002

#define DOT11_STATUS_SUCCESS 0x0000
#define DOT11_STATUS_AUTH_REJECT 0x0001
#define DOT11_STATUS_ASSOC_REJECT 0x000A

#define DOT11_CAP_ESS 0x0001
#define DOT11_CAP_IBSS 0x0002
#define DOT11_CAP_SHORT_PREAMBLE 0x0020
#define DOT11_CAP_SHORT_SLOT 0x0400

#define DOT11_CAP_STA_OPEN (DOT11_CAP_ESS | DOT11_CAP_SHORT_PREAMBLE | DOT11_CAP_SHORT_SLOT)

#define DOT11_IE_SSID 0
#define DOT11_IE_RATES 1
#define DOT11_IE_DS_PARAM 3
#define DOT11_IE_EXT_RATES 50

#define DOT11_MLME_IDLE 0
#define DOT11_MLME_AUTH_SENT 1
#define DOT11_MLME_AUTH_OK 2
#define DOT11_MLME_ASSOC_SENT 3
#define DOT11_MLME_ASSOCIATED 4
#define DOT11_MLME_FAILED 5

#define DOT11_FAIL_NONE 0
#define DOT11_FAIL_TIMEOUT 1
#define DOT11_FAIL_REJECTED 2
#define DOT11_FAIL_DEAUTH 3
#define DOT11_FAIL_NO_BSSID 4

#ifndef DOT11_RETRY_MAX
#define DOT11_RETRY_MAX 5
#endif
#ifndef DOT11_RETRY_TIMEOUT_MS
#define DOT11_RETRY_TIMEOUT_MS 500
#endif

struct dot11_mgmt_hdr {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t da[DOT11_ADDR_LEN];
    uint8_t sa[DOT11_ADDR_LEN];
    uint8_t bssid[DOT11_ADDR_LEN];
    uint16_t seq_ctrl;
} __attribute__((packed));

struct dot11_mlme;

typedef bool (*dot11_tx_fn)(void *ctx, const uint8_t *frame, uint16_t len);
typedef void (*dot11_event_fn)(void *ctx);

struct dot11_mlme {
    uint8_t state;
    uint8_t fail_reason;
    uint16_t last_status;
    char ssid[DOT11_SSID_MAX + 1];
    uint8_t ssid_len;
    uint8_t ap[DOT11_ADDR_LEN];
    uint8_t self[DOT11_ADDR_LEN];
    uint8_t channel;
    uint16_t seq;
    uint8_t retries;
    uint64_t last_tx_ms;
    uint16_t aid;
    uint16_t capability;
    uint16_t listen_interval;
    dot11_tx_fn tx;
    void *tx_ctx;
    dot11_event_fn on_associated;
    dot11_event_fn on_failed;
    void *event_ctx;
    uint8_t tx_buf[DOT11_MGMT_MAX];
};

void dot11_mlme_init(struct dot11_mlme *m, const uint8_t self[DOT11_ADDR_LEN],
                     dot11_tx_fn tx, void *tx_ctx,
                     dot11_event_fn on_associated, dot11_event_fn on_failed,
                     void *event_ctx);

bool dot11_mlme_start_open(struct dot11_mlme *m, const char *ssid,
                           const uint8_t bssid[DOT11_ADDR_LEN],
                           uint8_t channel, uint64_t now_ms);
void dot11_mlme_stop(struct dot11_mlme *m);
uint8_t dot11_mlme_state(const struct dot11_mlme *m);

void dot11_mlme_poll(struct dot11_mlme *m, uint64_t now_ms);

bool dot11_mlme_input(struct dot11_mlme *m, const uint8_t *frame, uint16_t len,
                      uint64_t now_ms);

uint16_t dot11_build_auth_req(const uint8_t sa[DOT11_ADDR_LEN],
                              const uint8_t bssid[DOT11_ADDR_LEN],
                              uint16_t seq_num, uint8_t *out, uint16_t out_cap);
uint16_t dot11_build_assoc_req(struct dot11_mlme *m, uint8_t *out, uint16_t out_cap);
bool dot11_parse_auth_resp(const uint8_t *frame, uint16_t len,
                           const uint8_t self[DOT11_ADDR_LEN],
                           uint16_t *status_out);
bool dot11_parse_assoc_resp(const uint8_t *frame, uint16_t len,
                            const uint8_t self[DOT11_ADDR_LEN],
                            uint16_t *status_out, uint16_t *aid_out);

bool dot11_parse_beacon(const uint8_t *frame, uint16_t len,
                        uint8_t bssid_out[DOT11_ADDR_LEN],
                        char ssid_out[DOT11_SSID_MAX + 1],
                        uint8_t *channel_out);

bool dot11_addr_is_zero(const uint8_t a[DOT11_ADDR_LEN]);
bool dot11_addr_eq(const uint8_t a[DOT11_ADDR_LEN], const uint8_t b[DOT11_ADDR_LEN]);