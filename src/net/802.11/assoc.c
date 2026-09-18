#include "include/802.h"

#ifndef PURECOS_KERNEL
static void mlme_memcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mlme_memset(void *d, int c, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) dd[i] = (uint8_t)c;
}
#else
#include "../../lib/string.h"
#define mlme_memcpy memcpy
#define mlme_memset memset
#endif

#define DOT11_HDR_LEN ((uint16_t)sizeof(struct dot11_mgmt_hdr))

static const uint8_t default_rates[] = {
    0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24,
    0x30, 0x48, 0x60, 0x6C,
};

static uint32_t mlme_strlen(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

bool dot11_addr_is_zero(const uint8_t a[DOT11_ADDR_LEN]) {
    if (!a) return true;
    for (int i = 0; i < DOT11_ADDR_LEN; i++)
        if (a[i]) return false;
    return true;
}

bool dot11_addr_eq(const uint8_t a[DOT11_ADDR_LEN], const uint8_t b[DOT11_ADDR_LEN]) {
    if (!a || !b) return false;
    for (int i = 0; i < DOT11_ADDR_LEN; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint16_t next_seq(struct dot11_mlme *m) {
    uint16_t s = m->seq;
    m->seq = (uint16_t)((m->seq + 1) & 0x0FFF);
    return (uint16_t)(s << 4);
}

static uint16_t build_hdr(uint8_t *out, uint16_t out_cap, uint8_t stype,
                          const uint8_t da[DOT11_ADDR_LEN],
                          const uint8_t sa[DOT11_ADDR_LEN],
                          const uint8_t bssid[DOT11_ADDR_LEN],
                          uint16_t seq_ctrl) {
    if (!out || !da || !sa || !bssid || out_cap < DOT11_HDR_LEN) return 0;
    put_le16(out + 0, DOT11_FC_MAKE(DOT11_FTYPE_MGMT, stype));
    put_le16(out + 2, 0);
    mlme_memcpy(out + 4, da, DOT11_ADDR_LEN);
    mlme_memcpy(out + 10, sa, DOT11_ADDR_LEN);
    mlme_memcpy(out + 16, bssid, DOT11_ADDR_LEN);
    put_le16(out + 22, seq_ctrl);
    return DOT11_HDR_LEN;
}

static bool mgmt_hdr_parse(const uint8_t *frame, uint16_t len,
                           uint8_t *stype_out, struct dot11_mgmt_hdr *hdr_out) {
    if (!frame || len < DOT11_HDR_LEN) return false;
    uint16_t fc = get_le16(frame);
    if ((fc & DOT11_FC_VERSION_MASK) != 0 ||
        DOT11_FC_TYPE(fc) != DOT11_FTYPE_MGMT)
        return false;
    if (stype_out) *stype_out = DOT11_FC_STYPE(fc);
    if (hdr_out) {
        hdr_out->frame_control = fc;
        hdr_out->duration = get_le16(frame + 2);
        mlme_memcpy(hdr_out->da, frame + 4, DOT11_ADDR_LEN);
        mlme_memcpy(hdr_out->sa, frame + 10, DOT11_ADDR_LEN);
        mlme_memcpy(hdr_out->bssid, frame + 16, DOT11_ADDR_LEN);
        hdr_out->seq_ctrl = get_le16(frame + 22);
    }
    return true;
}

uint16_t dot11_build_auth_req(const uint8_t sa[DOT11_ADDR_LEN],
                              const uint8_t bssid[DOT11_ADDR_LEN],
                              uint16_t seq_num, uint8_t *out, uint16_t out_cap) {
    if (!sa || !bssid || !out || out_cap < DOT11_HDR_LEN + 6) return 0;
    uint16_t off = build_hdr(out, out_cap, DOT11_STYPE_AUTH, bssid, sa, bssid, seq_num);
    if (!off) return 0;
    put_le16(out + off, DOT11_AUTH_ALG_OPEN);
    put_le16(out + off + 2, DOT11_AUTH_SEQ_REQ);
    put_le16(out + off + 4, DOT11_STATUS_SUCCESS);
    return (uint16_t)(off + 6);
}

uint16_t dot11_build_assoc_req(struct dot11_mlme *m, uint8_t *out, uint16_t out_cap) {
    if (!m || !out || !m->ssid_len || m->ssid_len > DOT11_SSID_MAX ||
        m->rate_count > DOT11_RATES_MAX)
        return 0;

    const uint8_t *rates = m->rate_count ? m->rates : default_rates;
    uint8_t rate_count = m->rate_count ? m->rate_count : (uint8_t)sizeof(default_rates);
    uint8_t first_rates = rate_count > 8 ? 8 : rate_count;
    uint8_t ext_rates = (uint8_t)(rate_count - first_rates);
    uint16_t need = (uint16_t)(DOT11_HDR_LEN + 4 + 2 + m->ssid_len + 2 + first_rates);
    if (ext_rates) need = (uint16_t)(need + 2 + ext_rates);
    if (out_cap < need) return 0;

    uint16_t off = build_hdr(out, out_cap, DOT11_STYPE_ASSOC_REQ,
                             m->ap, m->self, m->ap, next_seq(m));
    if (!off) return 0;
    put_le16(out + off, m->capability ? m->capability : DOT11_CAP_STA_OPEN);
    put_le16(out + off + 2, m->listen_interval ? m->listen_interval : 10);
    off += 4;
    out[off++] = DOT11_IE_SSID;
    out[off++] = m->ssid_len;
    mlme_memcpy(out + off, m->ssid, m->ssid_len);
    off = (uint16_t)(off + m->ssid_len);
    out[off++] = DOT11_IE_RATES;
    out[off++] = first_rates;
    mlme_memcpy(out + off, rates, first_rates);
    off = (uint16_t)(off + first_rates);
    if (ext_rates) {
        out[off++] = DOT11_IE_EXT_RATES;
        out[off++] = ext_rates;
        mlme_memcpy(out + off, rates + first_rates, ext_rates);
        off = (uint16_t)(off + ext_rates);
    }
    return off;
}

uint16_t dot11_build_disassoc(struct dot11_mlme *m, uint16_t reason,
                              uint8_t *out, uint16_t out_cap) {
    if (!m || !out || out_cap < DOT11_HDR_LEN + 2 || dot11_addr_is_zero(m->ap)) return 0;
    uint16_t off = build_hdr(out, out_cap, DOT11_STYPE_DISASSOC,
                             m->ap, m->self, m->ap, next_seq(m));
    if (!off) return 0;
    put_le16(out + off, reason);
    return (uint16_t)(off + 2);
}

bool dot11_parse_auth_resp(const uint8_t *frame, uint16_t len,
                           const uint8_t self[DOT11_ADDR_LEN],
                           uint16_t *status_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h) || stype != DOT11_STYPE_AUTH ||
        (self && !dot11_addr_eq(h.da, self)) || len < DOT11_HDR_LEN + 6)
        return false;
    const uint8_t *b = frame + DOT11_HDR_LEN;
    if (get_le16(b) != DOT11_AUTH_ALG_OPEN || get_le16(b + 2) != DOT11_AUTH_SEQ_RESP)
        return false;
    if (status_out) *status_out = get_le16(b + 4);
    return true;
}

bool dot11_parse_assoc_resp(const uint8_t *frame, uint16_t len,
                            const uint8_t self[DOT11_ADDR_LEN],
                            uint16_t *status_out, uint16_t *aid_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h) || stype != DOT11_STYPE_ASSOC_RESP ||
        (self && !dot11_addr_eq(h.da, self)) || len < DOT11_HDR_LEN + 6)
        return false;
    const uint8_t *b = frame + DOT11_HDR_LEN;
    if (status_out) *status_out = get_le16(b + 2);
    if (aid_out) *aid_out = (uint16_t)(get_le16(b + 4) & 0x3FFF);
    return true;
}

static bool bss_add_rate(struct dot11_bss *bss, uint8_t rate) {
    uint8_t value = (uint8_t)(rate & 0x7F);
    if (!value) return false;
    for (uint8_t i = 0; i < bss->rate_count; i++) {
        if ((bss->rates[i] & 0x7F) == value) {
            bss->rates[i] |= (uint8_t)(rate & 0x80);
            return true;
        }
    }
    if (bss->rate_count >= DOT11_RATES_MAX) return false;
    bss->rates[bss->rate_count++] = rate;
    return true;
}

bool dot11_parse_bss(const uint8_t *frame, uint16_t len, struct dot11_bss *bss_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!bss_out || !mgmt_hdr_parse(frame, len, &stype, &h) ||
        (stype != DOT11_STYPE_BEACON && stype != DOT11_STYPE_PROBE_RESP) ||
        len < DOT11_HDR_LEN + 12 || dot11_addr_is_zero(h.bssid) ||
        (h.bssid[0] & 1) || !dot11_addr_eq(h.sa, h.bssid))
        return false;

    struct dot11_bss bss;
    mlme_memset(&bss, 0, sizeof(bss));
    mlme_memcpy(bss.bssid, h.bssid, DOT11_ADDR_LEN);
    bss.beacon_interval = get_le16(frame + DOT11_HDR_LEN + 8);
    bss.capability = get_le16(frame + DOT11_HDR_LEN + 10);
    if (!(bss.capability & DOT11_CAP_ESS) || (bss.capability & DOT11_CAP_IBSS)) return false;

    bool have_ssid = false;
    bool have_supported_rates = false;
    bool have_extended_rates = false;
    uint16_t off = DOT11_HDR_LEN + 12;
    while (off < len) {
        if ((uint16_t)(len - off) < 2) return false;
        uint8_t id = frame[off];
        uint8_t elen = frame[off + 1];
        off += 2;
        if ((uint16_t)(len - off) < elen) return false;

        if (id == DOT11_IE_SSID) {
            if (have_ssid || elen > DOT11_SSID_MAX) return false;
            if (elen) mlme_memcpy(bss.ssid, frame + off, elen);
            bss.ssid[elen] = '\0';
            bss.ssid_len = elen;
            have_ssid = true;
        } else if (id == DOT11_IE_RATES || id == DOT11_IE_EXT_RATES) {
            if (!elen) return false;
            if (id == DOT11_IE_RATES) {
                if (have_supported_rates || elen > 8) return false;
                have_supported_rates = true;
            } else {
                if (have_extended_rates) return false;
                have_extended_rates = true;
            }
            for (uint8_t i = 0; i < elen; i++)
                if (!bss_add_rate(&bss, frame[off + i])) return false;
        } else if (id == DOT11_IE_DS_PARAM) {
            if (elen != 1) return false;
            bss.channel = frame[off];
        } else if (id == DOT11_IE_RSN) {
            bss.has_rsn = true;
        }
        off = (uint16_t)(off + elen);
    }
    if (!have_ssid || !have_supported_rates) return false;
    *bss_out = bss;
    return true;
}

bool dot11_parse_beacon(const uint8_t *frame, uint16_t len,
                        uint8_t bssid_out[DOT11_ADDR_LEN],
                        char ssid_out[DOT11_SSID_MAX + 1],
                        uint8_t *channel_out) {
    struct dot11_bss bss;
    if (!dot11_parse_bss(frame, len, &bss)) return false;
    if (bssid_out) mlme_memcpy(bssid_out, bss.bssid, DOT11_ADDR_LEN);
    if (ssid_out) mlme_memcpy(ssid_out, bss.ssid, sizeof(bss.ssid));
    if (channel_out) *channel_out = bss.channel;
    return true;
}

static void mlme_set_defaults(struct dot11_mlme *m) {
    m->state = DOT11_MLME_IDLE;
    m->capability = DOT11_CAP_STA_OPEN;
    m->listen_interval = 10;
    mlme_memcpy(m->rates, default_rates, sizeof(default_rates));
    m->rate_count = (uint8_t)sizeof(default_rates);
}

void dot11_mlme_init(struct dot11_mlme *m, const uint8_t self[DOT11_ADDR_LEN],
                     dot11_tx_fn tx, void *tx_ctx,
                     dot11_event_fn on_associated, dot11_event_fn on_failed,
                     void *event_ctx) {
    if (!m) return;
    mlme_memset(m, 0, sizeof(*m));
    if (self) mlme_memcpy(m->self, self, DOT11_ADDR_LEN);
    m->tx = tx;
    m->tx_ctx = tx_ctx;
    m->on_associated = on_associated;
    m->on_failed = on_failed;
    m->event_ctx = event_ctx;
    mlme_set_defaults(m);
}

static void *mlme_event_ctx(struct dot11_mlme *m) {
    return m->event_ctx ? m->event_ctx : m->tx_ctx;
}

static void mlme_fail(struct dot11_mlme *m, uint8_t reason, uint16_t status) {
    if (!m || m->state == DOT11_MLME_FAILED) return;
    m->state = DOT11_MLME_FAILED;
    m->fail_reason = reason;
    m->last_status = status;
    if (m->on_failed) m->on_failed(mlme_event_ctx(m));
}

static bool mlme_send_auth(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m->tx) return false;
    uint16_t len = dot11_build_auth_req(m->self, m->ap, next_seq(m),
                                        m->tx_buf, sizeof(m->tx_buf));
    if (!len) return false;
    m->last_tx_ms = now_ms;
    m->state = DOT11_MLME_AUTH_SENT;
    m->last_tx_failed = !m->tx(m->tx_ctx, m->tx_buf, len);
    return !m->last_tx_failed;
}

static bool mlme_send_assoc(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m->tx) return false;
    uint16_t len = dot11_build_assoc_req(m, m->tx_buf, sizeof(m->tx_buf));
    if (!len) return false;
    m->last_tx_ms = now_ms;
    m->state = DOT11_MLME_ASSOC_SENT;
    m->last_tx_failed = !m->tx(m->tx_ctx, m->tx_buf, len);
    return !m->last_tx_failed;
}

bool dot11_mlme_start_open(struct dot11_mlme *m, const char *ssid,
                           const uint8_t bssid[DOT11_ADDR_LEN],
                           uint8_t channel, uint64_t now_ms) {
    if (!m || !ssid || !bssid) return false;
    if (dot11_addr_is_zero(bssid) || (bssid[0] & 1)) {
        mlme_fail(m, DOT11_FAIL_NO_BSSID, 0);
        return false;
    }
    uint32_t slen = mlme_strlen(ssid);
    if (!slen || slen > DOT11_SSID_MAX || !m->tx) return false;
    mlme_memset(m->ssid, 0, sizeof(m->ssid));
    mlme_memcpy(m->ssid, ssid, slen);
    m->ssid_len = (uint8_t)slen;
    mlme_memcpy(m->ap, bssid, DOT11_ADDR_LEN);
    m->channel = channel;
    m->retries = 0;
    m->aid = 0;
    m->fail_reason = DOT11_FAIL_NONE;
    m->last_status = 0;
    m->ap_capability = 0;
    m->bss_profile_valid = false;
    m->capability = DOT11_CAP_STA_OPEN;
    mlme_memcpy(m->rates, default_rates, sizeof(default_rates));
    m->rate_count = (uint8_t)sizeof(default_rates);
    if (!mlme_send_auth(m, now_ms)) {
        mlme_fail(m, DOT11_FAIL_TX, 0);
        return false;
    }
    return true;
}

void dot11_mlme_stop(struct dot11_mlme *m) {
    if (!m) return;
    if (m->state == DOT11_MLME_ASSOCIATED && m->tx) {
        uint16_t len = dot11_build_disassoc(m, DOT11_REASON_LEAVING,
                                            m->tx_buf, sizeof(m->tx_buf));
        if (len) (void)m->tx(m->tx_ctx, m->tx_buf, len);
    }

    uint8_t keep_self[DOT11_ADDR_LEN];
    dot11_tx_fn tx = m->tx;
    void *tx_ctx = m->tx_ctx;
    dot11_event_fn oa = m->on_associated;
    dot11_event_fn of = m->on_failed;
    void *ectx = m->event_ctx;
    mlme_memcpy(keep_self, m->self, DOT11_ADDR_LEN);
    mlme_memset(m, 0, sizeof(*m));
    mlme_memcpy(m->self, keep_self, DOT11_ADDR_LEN);
    m->tx = tx;
    m->tx_ctx = tx_ctx;
    m->on_associated = oa;
    m->on_failed = of;
    m->event_ctx = ectx;
    mlme_set_defaults(m);
}

uint8_t dot11_mlme_state(const struct dot11_mlme *m) {
    return m ? m->state : DOT11_MLME_IDLE;
}

void dot11_mlme_poll(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m || (m->state != DOT11_MLME_AUTH_SENT && m->state != DOT11_MLME_ASSOC_SENT))
        return;
    if ((now_ms - m->last_tx_ms) < DOT11_RETRY_TIMEOUT_MS) return;
    if (m->retries + 1 >= DOT11_RETRY_MAX) {
        mlme_fail(m, m->last_tx_failed ? DOT11_FAIL_TX : DOT11_FAIL_TIMEOUT, 0);
        return;
    }
    m->retries++;
    if (m->state == DOT11_MLME_AUTH_SENT)
        (void)mlme_send_auth(m, now_ms);
    else
        (void)mlme_send_assoc(m, now_ms);
}

static bool frame_from_ap(const struct dot11_mlme *m, const struct dot11_mgmt_hdr *h) {
    return dot11_addr_eq(h->bssid, m->ap) && dot11_addr_eq(h->sa, m->ap) &&
           dot11_addr_eq(h->da, m->self);
}

static bool ssid_matches(const struct dot11_mlme *m, const struct dot11_bss *bss) {
    if (!bss->ssid_len) return true;
    if (bss->ssid_len != m->ssid_len) return false;
    for (uint8_t i = 0; i < m->ssid_len; i++)
        if ((uint8_t)m->ssid[i] != (uint8_t)bss->ssid[i]) return false;
    return true;
}

static bool station_supports_rate(uint8_t rate) {
    for (uint8_t i = 0; i < sizeof(default_rates); i++)
        if ((default_rates[i] & 0x7F) == (rate & 0x7F)) return true;
    return false;
}

static bool mlme_apply_bss(struct dot11_mlme *m, const struct dot11_bss *bss) {
    uint8_t compatible[DOT11_RATES_MAX];
    uint8_t compatible_count = 0;
    for (uint8_t i = 0; i < bss->rate_count; i++) {
        if (station_supports_rate(bss->rates[i])) {
            compatible[compatible_count++] = bss->rates[i];
        } else if (bss->rates[i] & 0x80) {
            return false;
        }
    }
    if (!compatible_count) return false;
    m->ap_capability = bss->capability;
    m->capability = (uint16_t)(DOT11_CAP_ESS |
        (bss->capability & (DOT11_CAP_SHORT_PREAMBLE | DOT11_CAP_SHORT_SLOT)));
    if (bss->channel) m->channel = bss->channel;
    mlme_memcpy(m->rates, compatible, compatible_count);
    m->rate_count = compatible_count;
    m->bss_profile_valid = true;
    return true;
}

bool dot11_mlme_input(struct dot11_mlme *m, const uint8_t *frame, uint16_t len,
                      uint64_t now_ms) {
    if (!m || !frame || !len) return false;
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h)) return false;

    if (stype == DOT11_STYPE_BEACON || stype == DOT11_STYPE_PROBE_RESP) {
        struct dot11_bss bss;
        if (!dot11_parse_bss(frame, len, &bss) || !dot11_addr_eq(bss.bssid, m->ap) ||
            !ssid_matches(m, &bss))
            return false;
        if ((bss.capability & DOT11_CAP_PRIVACY) || bss.has_rsn) {
            mlme_fail(m, DOT11_FAIL_UNSUPPORTED_SECURITY, 0);
            return true;
        }
        if ((m->state == DOT11_MLME_AUTH_SENT || m->state == DOT11_MLME_AUTH_OK) &&
            !mlme_apply_bss(m, &bss))
            mlme_fail(m, DOT11_FAIL_UNSUPPORTED_RATES, 0);
        return true;
    }
    if (!frame_from_ap(m, &h)) return false;

    if (stype == DOT11_STYPE_AUTH) {
        if (m->state != DOT11_MLME_AUTH_SENT) return true;
        uint16_t status = 0;
        if (!dot11_parse_auth_resp(frame, len, m->self, &status)) return true;
        if (status != DOT11_STATUS_SUCCESS) {
            mlme_fail(m, DOT11_FAIL_REJECTED, status);
            return true;
        }
        m->retries = 0;
        m->state = DOT11_MLME_AUTH_OK;
        if (!mlme_send_assoc(m, now_ms)) mlme_fail(m, DOT11_FAIL_TX, 0);
        return true;
    }
    if (stype == DOT11_STYPE_ASSOC_RESP) {
        if (m->state != DOT11_MLME_ASSOC_SENT) return true;
        uint16_t status = 0, aid = 0;
        if (!dot11_parse_assoc_resp(frame, len, m->self, &status, &aid)) return true;
        if (status != DOT11_STATUS_SUCCESS) {
            mlme_fail(m, DOT11_FAIL_REJECTED, status);
            return true;
        }
        if (!aid || aid > 2007) {
            mlme_fail(m, DOT11_FAIL_INVALID_RESPONSE, aid);
            return true;
        }
        m->aid = aid;
        m->retries = 0;
        m->last_tx_failed = false;
        m->state = DOT11_MLME_ASSOCIATED;
        if (m->on_associated) m->on_associated(mlme_event_ctx(m));
        return true;
    }
    if (stype == DOT11_STYPE_DEAUTH || stype == DOT11_STYPE_DISASSOC) {
        if (m->state == DOT11_MLME_IDLE || m->state == DOT11_MLME_FAILED) return true;
        if (len < DOT11_HDR_LEN + 2) return true;
        mlme_fail(m, DOT11_FAIL_DEAUTH, get_le16(frame + DOT11_HDR_LEN));
        return true;
    }
    return false;
}
