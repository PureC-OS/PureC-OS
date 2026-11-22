#include "wpa.h"
#include "sha1.h"
#include "aes.h"
#ifdef WPA_HOST_TEST
#include <string.h>
#else
#include "../../lib/string.h"
#endif

#define KI_VER_MASK 0x0007
#define KI_VER_HMAC_SHA1_AES 2
#define KI_PAIRWISE 0x0008
#define KI_INSTALL 0x0040
#define KI_ACK 0x0080
#define KI_MIC 0x0100
#define KI_SECURE 0x0200
#define KI_ERROR 0x0400
#define KI_ENCRYPTED 0x1000

#define OFF_DESC 4
#define OFF_INFO 5
#define OFF_KEYLEN 7
#define OFF_REPLAY 9
#define OFF_NONCE 17
#define OFF_RSC 65
#define OFF_MIC 81
#define OFF_DATALEN 97
#define OFF_DATA 99

static uint16_t be16(const uint8_t *p)
{
  return (uint16_t)((p[0] << 8) | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static int cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++)
    if (a[i] != b[i])
      return a[i] < b[i] ? -1 : 1;
  return 0;
}

bool wpa_sm_start(struct wpa_sm *sm, const char *ssid, const char *passphrase, const uint8_t aa[6],
                  const uint8_t spa[6], const uint8_t *rsn_ie, uint8_t rsn_ie_len,
                  void (*random)(void *buffer, uint32_t count))
{
  wpa_sm_clear(sm);
  size_t ssid_len = strlen(ssid), pass_len = strlen(passphrase);
  if (!ssid_len || ssid_len > 32 || pass_len < 8 || pass_len > 64 || rsn_ie_len > WPA_RSN_IE_MAX || !random)
  {
    sm->error = "bad ssid, passphrase or RSN IE";
    sm->state = WPA_FAILED;
    return false;
  }
  if (pass_len == 64)
  {
    for (int i = 0; i < 32; i++)
    {
      uint8_t v = 0;
      for (int j = 0; j < 2; j++)
      {
        char c = passphrase[i * 2 + j];
        uint8_t d = (c >= '0' && c <= '9') ? (uint8_t)(c - '0')
                    : (c >= 'a' && c <= 'f') ? (uint8_t)(c - 'a' + 10)
                    : (c >= 'A' && c <= 'F') ? (uint8_t)(c - 'A' + 10) : 0xFF;
        if (d == 0xFF)
        {
          sm->error = "64-character key is not hex";
          sm->state = WPA_FAILED;
          return false;
        }
        v = (uint8_t)(v << 4 | d);
      }
      sm->pmk[i] = v;
    }
  }
  else
    pure_pbkdf2_sha1((const uint8_t *)passphrase, pass_len, (const uint8_t *)ssid, ssid_len, 4096, sm->pmk,
                     WPA_PMK_LEN);
  memcpy(sm->aa, aa, 6);
  memcpy(sm->spa, spa, 6);
  memcpy(sm->rsn_ie, rsn_ie, rsn_ie_len);
  sm->rsn_ie_len = rsn_ie_len;
  sm->random = random;
  sm->state = WPA_WAIT_MSG1;
  return true;
}

void wpa_sm_clear(struct wpa_sm *sm)
{
  volatile uint8_t *p = (volatile uint8_t *)sm;
  for (uint32_t i = 0; i < sizeof(*sm); i++)
    p[i] = 0;
}

static void derive_ptk(struct wpa_sm *sm)
{
  uint8_t data[6 + 6 + 32 + 32];
  bool aa_low = cmp(sm->aa, sm->spa, 6) < 0;
  memcpy(data, aa_low ? sm->aa : sm->spa, 6);
  memcpy(data + 6, aa_low ? sm->spa : sm->aa, 6);
  bool an_low = cmp(sm->anonce, sm->snonce, 32) < 0;
  memcpy(data + 12, an_low ? sm->anonce : sm->snonce, 32);
  memcpy(data + 44, an_low ? sm->snonce : sm->anonce, 32);
  pure_prf_sha1(sm->pmk, WPA_PMK_LEN, "Pairwise key expansion", data, sizeof(data), sm->ptk, 48);
  memset(data, 0, sizeof(data));
}

static bool mic_ok(const struct wpa_sm *sm, const uint8_t *frame, uint32_t len)
{
  uint8_t copy[512];
  if (len > sizeof(copy))
    return false;
  memcpy(copy, frame, len);
  memset(copy + OFF_MIC, 0, 16);
  uint8_t mic[20];
  pure_hmac_sha1(sm->ptk, 16, copy, len, mic);
  int diff = 0;
  for (int i = 0; i < 16; i++)
    diff |= mic[i] ^ frame[OFF_MIC + i];
  return diff == 0;
}

static uint32_t build(struct wpa_sm *sm, uint8_t *out, uint16_t info, const uint8_t *nonce, const uint8_t *data,
                      uint16_t data_len)
{
  uint32_t len = OFF_DATA + data_len;
  memset(out, 0, len);
  out[0] = 1;
  out[1] = 3;
  put16(out + 2, (uint16_t)(len - 4));
  out[OFF_DESC] = 2;
  put16(out + OFF_INFO, info);
  memcpy(out + OFF_REPLAY, sm->replay, 8);
  if (nonce)
    memcpy(out + OFF_NONCE, nonce, 32);
  put16(out + OFF_DATALEN, data_len);
  if (data_len)
    memcpy(out + OFF_DATA, data, data_len);
  uint8_t mic[20];
  pure_hmac_sha1(sm->ptk, 16, out, len, mic);
  memcpy(out + OFF_MIC, mic, 16);
  return len;
}

static bool parse_gtk(const uint8_t *kd, uint32_t len, struct wpa_keys *keys)
{
  uint32_t p = 0;
  while (p + 2 <= len)
  {
    uint8_t type = kd[p], l = kd[p + 1];
    if (type == 0xdd && l == 0)
      break;
    if (p + 2 + l > len)
      return false;
    if (type == 0xdd && l >= 6 && kd[p + 2] == 0x00 && kd[p + 3] == 0x0f && kd[p + 4] == 0xac &&
        kd[p + 5] == 0x01)
    {
      uint32_t gl = (uint32_t)l - 6;
      if (gl < 16 || gl > WPA_GTK_MAX)
        return false;
      keys->gtk_index = kd[p + 6] & 3;
      memcpy(keys->gtk, kd + p + 8, gl);
      keys->gtk_len = (uint8_t)gl;
      return true;
    }
    p += 2u + l;
  }
  return false;
}

static bool replay_newer(const struct wpa_sm *sm, const uint8_t *replay)
{
  return !sm->replay_valid || cmp(replay, sm->replay, 8) > 0;
}

static int fail(struct wpa_sm *sm, const char *why)
{
  sm->error = why;
  return -1;
}

int wpa_sm_rx(struct wpa_sm *sm, const uint8_t *f, uint32_t len, uint8_t *reply, uint32_t *reply_len,
              struct wpa_keys *keys)
{
  *reply_len = 0;
  if (sm->state == WPA_IDLE || sm->state == WPA_FAILED)
    return fail(sm, "handshake not started");
  if (len < OFF_DATA || f[1] != 3 || (f[OFF_DESC] != 2 && f[OFF_DESC] != 254))
    return fail(sm, "not an EAPOL-Key frame");
  uint32_t body = be16(f + 2) + 4u;
  if (body > len)
    return fail(sm, "truncated EAPOL frame");
  len = body;
  uint16_t info = be16(f + OFF_INFO);
  uint16_t data_len = be16(f + OFF_DATALEN);
  if (OFF_DATA + (uint32_t)data_len > len)
    return fail(sm, "key data longer than frame");
  if ((info & KI_VER_MASK) != KI_VER_HMAC_SHA1_AES)
    return fail(sm, "unsupported key descriptor version (need HMAC-SHA1/AES)");
  if (!(info & KI_ACK))
    return fail(sm, "frame without ACK bit");
  if (!replay_newer(sm, f + OFF_REPLAY))
    return fail(sm, "replay counter not increased");
  uint16_t ver = info & KI_VER_MASK;

  if ((info & KI_PAIRWISE) && !(info & KI_MIC))
  {
    memcpy(sm->anonce, f + OFF_NONCE, 32);
    sm->random(sm->snonce, 32);
    derive_ptk(sm);
    memcpy(sm->replay, f + OFF_REPLAY, 8);
    sm->replay_valid = true;
    *reply_len = build(sm, reply, (uint16_t)(ver | KI_PAIRWISE | KI_MIC), sm->snonce, sm->rsn_ie,
                       sm->rsn_ie_len);
    sm->state = WPA_WAIT_MSG3;
    return WPA_RX_REPLY;
  }

  if (sm->state == WPA_WAIT_MSG1)
    return fail(sm, "message 3 before message 1");
  if (!mic_ok(sm, f, len))
    return fail(sm, "MIC mismatch: wrong password?");
  if (!(info & KI_ENCRYPTED) || data_len < 24 || data_len % 8 || data_len > 256)
    return fail(sm, "key data not wrapped");
  uint8_t plain[256];
  if (pure_aes_unwrap(sm->ptk + 16, f + OFF_DATA, data_len, plain) != 0)
    return fail(sm, "key data unwrap failed");
  bool have_gtk = parse_gtk(plain, data_len - 8u, keys);
  memset(plain, 0, sizeof(plain));
  memcpy(keys->gtk_rsc, f + OFF_RSC, 8);
  memcpy(sm->replay, f + OFF_REPLAY, 8);
  sm->replay_valid = true;

  if (info & KI_PAIRWISE)
  {
    if (!(info & KI_INSTALL))
      return fail(sm, "message 3 without install bit");
    if (cmp(sm->anonce, f + OFF_NONCE, 32) != 0)
      return fail(sm, "ANonce changed between message 1 and 3");
    *reply_len = build(sm, reply, (uint16_t)(ver | KI_PAIRWISE | KI_MIC | KI_SECURE), 0, 0, 0);
    if (sm->state == WPA_DONE)
      return WPA_RX_REPLY;
    memcpy(keys->tk, sm->ptk + 32, WPA_TK_LEN);
    sm->state = WPA_DONE;
    return WPA_RX_REPLY | WPA_RX_INSTALL_PTK | (have_gtk ? WPA_RX_INSTALL_GTK : 0) | WPA_RX_COMPLETE;
  }

  if (sm->state != WPA_DONE)
    return fail(sm, "group key before pairwise handshake");
  if (!have_gtk)
    return fail(sm, "group message without GTK");
  *reply_len = build(sm, reply, (uint16_t)(ver | KI_MIC | KI_SECURE), 0, 0, 0);
  return WPA_RX_REPLY | WPA_RX_INSTALL_GTK;
}
