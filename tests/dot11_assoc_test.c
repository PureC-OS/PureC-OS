#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net/802.11/include/802.h"

#define HDR_LEN 24
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #expr); \
        failures++; \
        return; \
    } \
} while (0)

struct fixture {
    struct dot11_mlme mlme;
    uint8_t tx_frames[8][DOT11_MGMT_MAX];
    uint16_t tx_lens[8];
    unsigned tx_count;
    unsigned associated_count;
    unsigned failed_count;
    bool tx_ok;
};

static const uint8_t station[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t ap[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
static const uint8_t rogue[6] = {0x12, 0x20, 0x30, 0x40, 0x50, 0x61};
static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void make_hdr(uint8_t *frame, uint8_t stype, const uint8_t da[6],
                     const uint8_t sa[6], const uint8_t bssid[6]) {
    memset(frame, 0, HDR_LEN);
    put_le16(frame, DOT11_FC_MAKE(DOT11_FTYPE_MGMT, stype));
    memcpy(frame + 4, da, 6);
    memcpy(frame + 10, sa, 6);
    memcpy(frame + 16, bssid, 6);
}

static uint16_t make_beacon(uint8_t *frame, const uint8_t source[6],
                            uint16_t capability, const uint8_t *rates,
                            uint8_t rate_count, bool add_rsn) {
    make_hdr(frame, DOT11_STYPE_BEACON, broadcast, source, source);
    memset(frame + HDR_LEN, 0, 12);
    put_le16(frame + HDR_LEN + 8, 100);
    put_le16(frame + HDR_LEN + 10, capability);
    uint16_t off = HDR_LEN + 12;
    frame[off++] = DOT11_IE_SSID;
    frame[off++] = 7;
    memcpy(frame + off, "TestNet", 7);
    off += 7;
    frame[off++] = DOT11_IE_RATES;
    frame[off++] = rate_count > 8 ? 8 : rate_count;
    memcpy(frame + off, rates, rate_count > 8 ? 8 : rate_count);
    off += rate_count > 8 ? 8 : rate_count;
    if (rate_count > 8) {
        frame[off++] = DOT11_IE_EXT_RATES;
        frame[off++] = (uint8_t)(rate_count - 8);
        memcpy(frame + off, rates + 8, rate_count - 8);
        off += (uint16_t)(rate_count - 8);
    }
    frame[off++] = DOT11_IE_DS_PARAM;
    frame[off++] = 1;
    frame[off++] = 6;
    if (add_rsn) {
        frame[off++] = DOT11_IE_RSN;
        frame[off++] = 2;
        frame[off++] = 1;
        frame[off++] = 0;
    }
    return off;
}

static uint16_t make_auth_resp(uint8_t *frame, const uint8_t source[6], uint16_t status) {
    make_hdr(frame, DOT11_STYPE_AUTH, station, source, ap);
    put_le16(frame + HDR_LEN, DOT11_AUTH_ALG_OPEN);
    put_le16(frame + HDR_LEN + 2, DOT11_AUTH_SEQ_RESP);
    put_le16(frame + HDR_LEN + 4, status);
    return HDR_LEN + 6;
}

static uint16_t make_assoc_resp(uint8_t *frame, uint16_t status, uint16_t aid) {
    make_hdr(frame, DOT11_STYPE_ASSOC_RESP, station, ap, ap);
    put_le16(frame + HDR_LEN, DOT11_CAP_ESS);
    put_le16(frame + HDR_LEN + 2, status);
    put_le16(frame + HDR_LEN + 4, (uint16_t)(aid | 0xC000));
    return HDR_LEN + 6;
}

static uint16_t make_reason_frame(uint8_t *frame, uint8_t stype,
                                  const uint8_t source[6], uint16_t reason) {
    make_hdr(frame, stype, station, source, ap);
    put_le16(frame + HDR_LEN, reason);
    return HDR_LEN + 2;
}

static bool capture_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct fixture *f = ctx;
    if (f->tx_count < ARRAY_SIZE(f->tx_frames)) {
        memcpy(f->tx_frames[f->tx_count], frame, len);
        f->tx_lens[f->tx_count] = len;
    }
    f->tx_count++;
    return f->tx_ok;
}

static void associated(void *ctx) {
    ((struct fixture *)ctx)->associated_count++;
}

static void failed(void *ctx) {
    ((struct fixture *)ctx)->failed_count++;
}

static void fixture_init(struct fixture *f) {
    memset(f, 0, sizeof(*f));
    f->tx_ok = true;
    dot11_mlme_init(&f->mlme, station, capture_tx, f, associated, failed, f);
}

static uint8_t frame_stype(const uint8_t *frame) {
    return (uint8_t)DOT11_FC_STYPE(get_le16(frame));
}

static const uint8_t *find_ie(const uint8_t *frame, uint16_t len, uint16_t start,
                              uint8_t id, uint8_t *ie_len) {
    uint16_t off = start;
    while (off + 2 <= len) {
        uint8_t element_len = frame[off + 1];
        if (off + 2 + element_len > len) return NULL;
        if (frame[off] == id) {
            if (ie_len) *ie_len = element_len;
            return frame + off + 2;
        }
        off = (uint16_t)(off + 2 + element_len);
    }
    return NULL;
}

static void test_bss_parser_rejects_malformed_elements(void) {
    uint8_t frame[128];
    const uint8_t rates[] = {0x82, 0x84};
    uint16_t len = make_beacon(frame, ap, DOT11_CAP_ESS, rates, sizeof(rates), false);
    struct dot11_bss bss;

    CHECK(dot11_parse_bss(frame, len, &bss));
    CHECK(strcmp(bss.ssid, "TestNet") == 0);
    CHECK(bss.channel == 6);
    CHECK(bss.rate_count == 2);
    CHECK(bss.beacon_interval == 100);

    frame[len - 2] = 10;
    CHECK(!dot11_parse_bss(frame, len, &bss));

    len = make_beacon(frame, ap, DOT11_CAP_ESS, rates, sizeof(rates), false);
    frame[HDR_LEN + 13] = DOT11_SSID_MAX + 1;
    CHECK(!dot11_parse_bss(frame, len, &bss));
}

static void test_association_uses_selected_bss_profile(void) {
    struct fixture f;
    uint8_t frame[256];
    const uint8_t rates[] = {0x82, 0x84, 0x8B, 0x0C, 0x12};
    fixture_init(&f);

    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 1, 100));
    CHECK(f.tx_count == 1);
    CHECK(frame_stype(f.tx_frames[0]) == DOT11_STYPE_AUTH);

    uint16_t len = make_beacon(frame, ap,
        DOT11_CAP_ESS | DOT11_CAP_SHORT_SLOT, rates, sizeof(rates), false);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 110));
    CHECK(f.mlme.bss_profile_valid);
    CHECK(f.mlme.channel == 6);

    len = make_auth_resp(frame, rogue, DOT11_STATUS_SUCCESS);
    CHECK(!dot11_mlme_input(&f.mlme, frame, len, 120));
    CHECK(f.mlme.state == DOT11_MLME_AUTH_SENT);
    CHECK(f.tx_count == 1);

    len = make_auth_resp(frame, ap, DOT11_STATUS_SUCCESS);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 130));
    CHECK(f.mlme.state == DOT11_MLME_ASSOC_SENT);
    CHECK(f.tx_count == 2);
    CHECK(frame_stype(f.tx_frames[1]) == DOT11_STYPE_ASSOC_REQ);
    CHECK(get_le16(f.tx_frames[1] + HDR_LEN) ==
          (DOT11_CAP_ESS | DOT11_CAP_SHORT_SLOT));

    uint8_t ie_len = 0;
    const uint8_t *ie = find_ie(f.tx_frames[1], f.tx_lens[1], HDR_LEN + 4,
                                DOT11_IE_RATES, &ie_len);
    CHECK(ie != NULL);
    CHECK(ie_len == sizeof(rates));
    CHECK(memcmp(ie, rates, sizeof(rates)) == 0);
    CHECK(find_ie(f.tx_frames[1], f.tx_lens[1], HDR_LEN + 4,
                  DOT11_IE_EXT_RATES, NULL) == NULL);

    len = make_assoc_resp(frame, DOT11_STATUS_SUCCESS, 42);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 140));
    CHECK(f.mlme.state == DOT11_MLME_ASSOCIATED);
    CHECK(f.mlme.aid == 42);
    CHECK(f.associated_count == 1);

    CHECK(dot11_mlme_input(&f.mlme, frame, len, 150));
    CHECK(f.associated_count == 1);
    dot11_mlme_stop(&f.mlme);
    CHECK(f.tx_count == 3);
    CHECK(frame_stype(f.tx_frames[2]) == DOT11_STYPE_DISASSOC);
    CHECK(get_le16(f.tx_frames[2] + HDR_LEN) == DOT11_REASON_LEAVING);
    CHECK(f.mlme.state == DOT11_MLME_IDLE);
}

static void test_invalid_aid_and_security_fail_once(void) {
    struct fixture f;
    uint8_t frame[256];
    fixture_init(&f);
    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 6, 0));
    uint16_t len = make_auth_resp(frame, ap, DOT11_STATUS_SUCCESS);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 10));
    len = make_assoc_resp(frame, DOT11_STATUS_SUCCESS, 0);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 20));
    CHECK(f.mlme.state == DOT11_MLME_FAILED);
    CHECK(f.mlme.fail_reason == DOT11_FAIL_INVALID_RESPONSE);
    CHECK(f.failed_count == 1);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 30));
    CHECK(f.failed_count == 1);

    fixture_init(&f);
    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 6, 0));
    const uint8_t rates[] = {0x82};
    len = make_beacon(frame, ap, DOT11_CAP_ESS | DOT11_CAP_PRIVACY,
                      rates, sizeof(rates), true);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 10));
    CHECK(f.mlme.fail_reason == DOT11_FAIL_UNSUPPORTED_SECURITY);
    CHECK(f.failed_count == 1);

    fixture_init(&f);
    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 6, 0));
    const uint8_t unsupported_basic_rate[] = {0xFE};
    len = make_beacon(frame, ap, DOT11_CAP_ESS, unsupported_basic_rate,
                      sizeof(unsupported_basic_rate), false);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 10));
    CHECK(f.mlme.fail_reason == DOT11_FAIL_UNSUPPORTED_RATES);
    CHECK(f.failed_count == 1);
}

static void test_timeout_retries_are_bounded(void) {
    struct fixture f;
    fixture_init(&f);
    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 1, 0));
    for (uint64_t tick = 1; tick <= DOT11_RETRY_MAX; tick++)
        dot11_mlme_poll(&f.mlme, tick * DOT11_RETRY_TIMEOUT_MS);
    CHECK(f.tx_count == DOT11_RETRY_MAX);
    CHECK(f.mlme.state == DOT11_MLME_FAILED);
    CHECK(f.mlme.fail_reason == DOT11_FAIL_TIMEOUT);
    CHECK(f.failed_count == 1);
    dot11_mlme_poll(&f.mlme, 10000);
    CHECK(f.failed_count == 1);
}

static void test_deauth_requires_selected_ap(void) {
    struct fixture f;
    uint8_t frame[64];
    fixture_init(&f);
    CHECK(dot11_mlme_start_open(&f.mlme, "TestNet", ap, 1, 0));

    uint16_t len = make_reason_frame(frame, DOT11_STYPE_DEAUTH, rogue, 7);
    CHECK(!dot11_mlme_input(&f.mlme, frame, len, 10));
    CHECK(f.mlme.state == DOT11_MLME_AUTH_SENT);
    CHECK(f.failed_count == 0);

    len = make_reason_frame(frame, DOT11_STYPE_DEAUTH, ap, 7);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 20));
    CHECK(f.mlme.state == DOT11_MLME_FAILED);
    CHECK(f.mlme.fail_reason == DOT11_FAIL_DEAUTH);
    CHECK(f.mlme.last_status == 7);
    CHECK(f.failed_count == 1);
    CHECK(dot11_mlme_input(&f.mlme, frame, len, 30));
    CHECK(f.failed_count == 1);
}

static void test_initial_tx_failure_is_reported(void) {
    struct fixture f;
    fixture_init(&f);
    f.tx_ok = false;
    CHECK(!dot11_mlme_start_open(&f.mlme, "TestNet", ap, 1, 0));
    CHECK(f.mlme.state == DOT11_MLME_FAILED);
    CHECK(f.mlme.fail_reason == DOT11_FAIL_TX);
    CHECK(f.failed_count == 1);
}

int main(void) {
    test_bss_parser_rejects_malformed_elements();
    test_association_uses_selected_bss_profile();
    test_invalid_aid_and_security_fail_once();
    test_timeout_retries_are_bounded();
    test_deauth_requires_selected_ap();
    test_initial_tx_failure_is_reported();
    if (failures) {
        fprintf(stderr, "dot11 association tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("dot11 association tests passed");
    return 0;
}