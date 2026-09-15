// PureC-OS 802.11 MLME — open-system association.
// Логика написана с нуля под PureC-OS, без копипасты GPL-кода.
// Только числовые константы subtype/status совпадают с IEEE 802.11.

#include "include/802.h"

// Локальный string-минимум чтобы не тянуть lib/string в хост-тесты.
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
static uint32_t mlme_strlen(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}
#else
#include "../../lib/string.h"
#define mlme_memcpy memcpy
#define mlme_memset memset
static uint32_t mlme_strlen(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}
#endif

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
    return (uint16_t)(s << 4); // frag = 0
}

static uint16_t build_hdr(uint8_t *out, uint16_t out_cap, uint8_t stype,
                          const uint8_t da[DOT11_ADDR_LEN],
                          const uint8_t sa[DOT11_ADDR_LEN],
                          const uint8_t bssid[DOT11_ADDR_LEN],
                          uint16_t seq_ctrl) {
    if (!out || out_cap < sizeof(struct dot11_mgmt_hdr)) return 0;
    struct dot11_mgmt_hdr *h = (struct dot11_mgmt_hdr *)out;
    h->frame_control = DOT11_FC_MAKE(DOT11_FTYPE_MGMT, stype);
    h->duration = 0;
    mlme_memcpy(h->da, da, DOT11_ADDR_LEN);
    mlme_memcpy(h->sa, sa, DOT11_ADDR_LEN);
    mlme_memcpy(h->bssid, bssid, DOT11_ADDR_LEN);
    h->seq_ctrl = seq_ctrl;
    return (uint16_t)sizeof(struct dot11_mgmt_hdr);
}

uint16_t dot11_build_auth_req(const uint8_t sa[DOT11_ADDR_LEN],
                              const uint8_t bssid[DOT11_ADDR_LEN],
                              uint16_t seq_num, uint8_t *out, uint16_t out_cap) {
    if (!sa || !bssid || !out) return 0;
    // auth body: alg(2) seq(2) status(2) = 6
    if (out_cap < sizeof(struct dot11_mgmt_hdr) + 6) return 0;
    uint16_t off = build_hdr(out, out_cap, DOT11_STYPE_AUTH, bssid, sa, bssid, seq_num);
    if (!off) return 0;
    put_le16(out + off + 0, DOT11_AUTH_ALG_OPEN);
    put_le16(out + off + 2, DOT11_AUTH_SEQ_REQ);
    put_le16(out + off + 4, DOT11_STATUS_SUCCESS);
    return (uint16_t)(off + 6);
}

// Классический набор rates для assoc req (CCK 1/2/5.5/11 + OFDM 6/9/12/18).
static const uint8_t assoc_rates[] = {0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
static const uint8_t assoc_ext_rates[] = {0x30, 0x48, 0x60, 0x6C};

uint16_t dot11_build_assoc_req(struct dot11_mlme *m, uint8_t *out, uint16_t out_cap) {
    if (!m || !out) return 0;
    if (!m->ssid_len || m->ssid_len > DOT11_SSID_MAX) return 0;
    // hdr(24) + cap(2) + listen(2) + ssid IE(2+32) + rates IE(2+8) + ext(2+4)
    uint16_t need = 24 + 2 + 2 + 2 + m->ssid_len + 2 + (uint16_t)sizeof(assoc_rates) +
                    2 + (uint16_t)sizeof(assoc_ext_rates);
    if (out_cap < need) return 0;
    uint16_t off = build_hdr(out, out_cap, DOT11_STYPE_ASSOC_REQ, m->ap, m->self, m->ap,
                             next_seq(m));
    if (!off) return 0;
    put_le16(out + off + 0, m->capability ? m->capability : DOT11_CAP_STA_OPEN);
    put_le16(out + off + 2, m->listen_interval ? m->listen_interval : 10);
    off += 4;
    out[off++] = DOT11_IE_SSID;
    out[off++] = m->ssid_len;
    mlme_memcpy(out + off, m->ssid, m->ssid_len);
    off += m->ssid_len;
    out[off++] = DOT11_IE_RATES;
    out[off++] = (uint8_t)sizeof(assoc_rates);
    mlme_memcpy(out + off, assoc_rates, sizeof(assoc_rates));
    off += (uint16_t)sizeof(assoc_rates);
    out[off++] = DOT11_IE_EXT_RATES;
    out[off++] = (uint8_t)sizeof(assoc_ext_rates);
    mlme_memcpy(out + off, assoc_ext_rates, sizeof(assoc_ext_rates));
    off += (uint16_t)sizeof(assoc_ext_rates);
    return off;
}

static bool mgmt_hdr_parse(const uint8_t *frame, uint16_t len,
                           uint8_t *stype_out, struct dot11_mgmt_hdr *hdr_out) {
    if (!frame || len < sizeof(struct dot11_mgmt_hdr)) return false;
    const struct dot11_mgmt_hdr *h = (const struct dot11_mgmt_hdr *)frame;
    if (DOT11_FC_TYPE(h->frame_control) != DOT11_FTYPE_MGMT) return false;
    if (stype_out) *stype_out = DOT11_FC_STYPE(h->frame_control);
    if (hdr_out) mlme_memcpy(hdr_out, h, sizeof(*hdr_out));
    return true;
}

bool dot11_parse_auth_resp(const uint8_t *frame, uint16_t len,
                           const uint8_t self[DOT11_ADDR_LEN],
                           uint16_t *status_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h)) return false;
    if (stype != DOT11_STYPE_AUTH) return false;
    if (self && !dot11_addr_eq(h.da, self)) return false;
    if (len < sizeof(h) + 6) return false;
    const uint8_t *b = frame + sizeof(h);
    uint16_t alg = get_le16(b + 0);
    uint16_t seq = get_le16(b + 2);
    uint16_t status = get_le16(b + 4);
    if (alg != DOT11_AUTH_ALG_OPEN) return false;
    if (seq != DOT11_AUTH_SEQ_RESP) return false;
    if (status_out) *status_out = status;
    return true;
}

bool dot11_parse_assoc_resp(const uint8_t *frame, uint16_t len,
                            const uint8_t self[DOT11_ADDR_LEN],
                            uint16_t *status_out, uint16_t *aid_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h)) return false;
    if (stype != DOT11_STYPE_ASSOC_RESP) return false;
    if (self && !dot11_addr_eq(h.da, self)) return false;
    if (len < sizeof(h) + 6) return false;
    const uint8_t *b = frame + sizeof(h);
    // cap(2) status(2) aid(2)
    uint16_t status = get_le16(b + 2);
    uint16_t aid = (uint16_t)(get_le16(b + 4) & 0x3FFF);
    if (status_out) *status_out = status;
    if (aid_out) *aid_out = aid;
    return true;
}

bool dot11_parse_beacon(const uint8_t *frame, uint16_t len,
                        uint8_t bssid_out[DOT11_ADDR_LEN],
                        char ssid_out[DOT11_SSID_MAX + 1],
                        uint8_t *channel_out) {
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h)) return false;
    if (stype != DOT11_STYPE_BEACON && stype != DOT11_STYPE_PROBE_RESP) return false;
    if (bssid_out) mlme_memcpy(bssid_out, h.bssid, DOT11_ADDR_LEN);
    // body: timestamp(8) + interval(2) + cap(2) + IEs
    if (len < sizeof(h) + 12) return false;
    uint16_t off = (uint16_t)(sizeof(h) + 12);
    bool have_ssid = false;
    uint8_t ch = 0;
    char ssid[DOT11_SSID_MAX + 1];
    mlme_memset(ssid, 0, sizeof(ssid));
    uint8_t ssid_len = 0;
    while (off + 2 <= len) {
        uint8_t id = frame[off];
        uint8_t elen = frame[off + 1];
        if (off + 2 + elen > len) break;
        if (id == DOT11_IE_SSID && !have_ssid) {
            ssid_len = elen > DOT11_SSID_MAX ? DOT11_SSID_MAX : elen;
            for (uint8_t i = 0; i < ssid_len; i++) ssid[i] = (char)frame[off + 2 + i];
            ssid[ssid_len] = '\0';
            have_ssid = true;
        } else if (id == DOT11_IE_DS_PARAM && elen >= 1) {
            ch = frame[off + 2];
        }
        off += (uint16_t)(2 + elen);
    }
    if (ssid_out) {
        for (int i = 0; i < DOT11_SSID_MAX + 1; i++) ssid_out[i] = ssid[i];
    }
    if (channel_out) *channel_out = ch;
    return true;
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
    m->state = DOT11_MLME_IDLE;
    m->capability = DOT11_CAP_STA_OPEN;
    m->listen_interval = 10;
}

static void mlme_fail(struct dot11_mlme *m, uint8_t reason, uint16_t status) {
    m->state = DOT11_MLME_FAILED;
    m->fail_reason = reason;
    m->last_status = status;
    if (m->on_failed) m->on_failed(m->event_ctx ? m->event_ctx : m->tx_ctx);
}

static bool mlme_send_auth(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m->tx) return false;
    uint16_t sc = next_seq(m);
    uint16_t len = dot11_build_auth_req(m->self, m->ap, sc, m->tx_buf, sizeof(m->tx_buf));
    if (!len) return false;
    if (!m->tx(m->tx_ctx, m->tx_buf, len)) return false;
    m->last_tx_ms = now_ms;
    m->state = DOT11_MLME_AUTH_SENT;
    return true;
}

static bool mlme_send_assoc(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m->tx) return false;
    uint16_t len = dot11_build_assoc_req(m, m->tx_buf, sizeof(m->tx_buf));
    if (!len) return false;
    if (!m->tx(m->tx_ctx, m->tx_buf, len)) return false;
    m->last_tx_ms = now_ms;
    m->state = DOT11_MLME_ASSOC_SENT;
    return true;
}

bool dot11_mlme_start_open(struct dot11_mlme *m, const char *ssid,
                           const uint8_t bssid[DOT11_ADDR_LEN],
                           uint8_t channel, uint64_t now_ms) {
    if (!m || !ssid || !bssid) return false;
    if (dot11_addr_is_zero(bssid)) {
        mlme_fail(m, DOT11_FAIL_NO_BSSID, 0);
        return false;
    }
    uint32_t slen = mlme_strlen(ssid);
    if (!slen || slen > DOT11_SSID_MAX) return false;
    if (!m->tx) return false;
    mlme_memset(m->ssid, 0, sizeof(m->ssid));
    for (uint32_t i = 0; i < slen; i++) m->ssid[i] = ssid[i];
    m->ssid_len = (uint8_t)slen;
    mlme_memcpy(m->ap, bssid, DOT11_ADDR_LEN);
    m->channel = channel;
    m->retries = 0;
    m->aid = 0;
    m->fail_reason = DOT11_FAIL_NONE;
    m->last_status = 0;
    return mlme_send_auth(m, now_ms);
}

void dot11_mlme_stop(struct dot11_mlme *m) {
    if (!m) return;
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
    m->state = DOT11_MLME_IDLE;
    m->capability = DOT11_CAP_STA_OPEN;
    m->listen_interval = 10;
}

uint8_t dot11_mlme_state(const struct dot11_mlme *m) {
    return m ? m->state : DOT11_MLME_IDLE;
}

void dot11_mlme_poll(struct dot11_mlme *m, uint64_t now_ms) {
    if (!m) return;
    if (m->state != DOT11_MLME_AUTH_SENT && m->state != DOT11_MLME_ASSOC_SENT) return;
    if ((now_ms - m->last_tx_ms) < DOT11_RETRY_TIMEOUT_MS) return;
    if (m->retries + 1 >= DOT11_RETRY_MAX) {
        mlme_fail(m, DOT11_FAIL_TIMEOUT, 0);
        return;
    }
    m->retries++;
    if (m->state == DOT11_MLME_AUTH_SENT) {
        (void)mlme_send_auth(m, now_ms);
    } else {
        (void)mlme_send_assoc(m, now_ms);
    }
}

bool dot11_mlme_input(struct dot11_mlme *m, const uint8_t *frame, uint16_t len,
                      uint64_t now_ms) {
    (void)now_ms;
    if (!m || !frame || !len) return false;
    struct dot11_mgmt_hdr h;
    uint8_t stype = 0;
    if (!mgmt_hdr_parse(frame, len, &stype, &h)) return false;
    // Фильтр: кадр нам (DA == self или broadcast для beacon) и BSSID наш,
    // кроме beacon/probe_resp где BSSID просто копируется из эфира.
    if (stype == DOT11_STYPE_BEACON || stype == DOT11_STYPE_PROBE_RESP) {
        return true; // scan-путь разберёт сам через dot11_parse_beacon
    }
    if (!dot11_addr_eq(h.bssid, m->ap)) return false;

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
        // Сразу шлём assoc — открытый auth без задержек.
        if (!mlme_send_assoc(m, now_ms)) {
            mlme_fail(m, DOT11_FAIL_TIMEOUT, 0);
        }
        return true;
    }
    if (stype == DOT11_STYPE_ASSOC_RESP) {
        if (m->state != DOT11_MLME_ASSOC_SENT && m->state != DOT11_MLME_AUTH_OK) return true;
        uint16_t status = 0, aid = 0;
        if (!dot11_parse_assoc_resp(frame, len, m->self, &status, &aid)) return true;
        if (status != DOT11_STATUS_SUCCESS) {
            mlme_fail(m, DOT11_FAIL_REJECTED, status);
            return true;
        }
        m->aid = aid;
        m->retries = 0;
        m->state = DOT11_MLME_ASSOCIATED;
        if (m->on_associated) m->on_associated(m->event_ctx ? m->event_ctx : m->tx_ctx);
        return true;
    }
    if (stype == DOT11_STYPE_DEAUTH || stype == DOT11_STYPE_DISASSOC) {
        // AP рвёт связь в любой момент после начала диалога.
        if (m->state == DOT11_MLME_IDLE) return true;
        uint16_t reason = 0;
        if (len >= sizeof(h) + 2) reason = get_le16(frame + sizeof(h));
        mlme_fail(m, DOT11_FAIL_DEAUTH, reason);
        return true;
    }
    return false;
}
