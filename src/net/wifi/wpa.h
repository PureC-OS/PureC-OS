#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WPA_PMK_LEN 32
#define WPA_NONCE_LEN 32
#define WPA_TK_LEN 16
#define WPA_GTK_MAX 32
#define WPA_RSN_IE_MAX 64
#define WPA_FRAME_MAX 256

#define WPA_RX_REPLY 0x01
#define WPA_RX_INSTALL_PTK 0x02
#define WPA_RX_INSTALL_GTK 0x04
#define WPA_RX_COMPLETE 0x08

enum wpa_state
{
  WPA_IDLE,
  WPA_WAIT_MSG1,
  WPA_WAIT_MSG3,
  WPA_DONE,
  WPA_FAILED
};

struct wpa_keys
{
  uint8_t tk[WPA_TK_LEN];
  uint8_t gtk[WPA_GTK_MAX];
  uint8_t gtk_len;
  uint8_t gtk_index;
  uint8_t gtk_rsc[8];
};

struct wpa_sm
{
  enum wpa_state state;
  uint8_t pmk[WPA_PMK_LEN];
  uint8_t aa[6];
  uint8_t spa[6];
  uint8_t anonce[WPA_NONCE_LEN];
  uint8_t snonce[WPA_NONCE_LEN];
  uint8_t ptk[48];
  uint8_t replay[8];
  bool replay_valid;
  uint8_t rsn_ie[WPA_RSN_IE_MAX];
  uint8_t rsn_ie_len;
  const char *error;
  void (*random)(void *buffer, uint32_t count);
};

bool wpa_sm_start(struct wpa_sm *sm, const char *ssid, const char *passphrase, const uint8_t aa[6],
                  const uint8_t spa[6], const uint8_t *rsn_ie, uint8_t rsn_ie_len,
                  void (*random)(void *buffer, uint32_t count));
int wpa_sm_rx(struct wpa_sm *sm, const uint8_t *eapol, uint32_t len, uint8_t *reply, uint32_t *reply_len,
              struct wpa_keys *keys);
void wpa_sm_clear(struct wpa_sm *sm);
