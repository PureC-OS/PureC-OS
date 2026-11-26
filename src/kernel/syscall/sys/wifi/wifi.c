#include <lib/string.h>
#include <net/wifi/wifi.h>
#include "../common.h"

long sys_wifi_scan(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!wifi_allowed())
    return -KERR_PERM;
  if (!wifi_has_device())
    return -1;
  return wifi_trigger_scan() ? 0 : -1;
}

long sys_wifi_list(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct wifi_network_info *buffer =
      (struct wifi_network_info *)(uintptr_t)a1;
  uint32_t capacity = (uint32_t)a2;
  if (capacity > WIFI_SCAN_MAX)
    return -1;
  if (capacity && !writable(buffer, capacity * sizeof(*buffer)))
    return -1;
  if (!capacity)
    return (int64_t)wifi_get_scan_results(0, 0);
  struct wifi_network temp[WIFI_SCAN_MAX];
  uint32_t count =
      wifi_get_scan_results((struct wifi_network *)temp, WIFI_SCAN_MAX);
  if (count > capacity)
    count = capacity;
  for (uint32_t i = 0; i < count; i++)
  {
    buffer[i] = *(struct wifi_network_info *)&temp[i];
  }
  return (int64_t)count;
}

long sys_wifi_connect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!wifi_allowed())
    return -KERR_PERM;
  const struct wifi_connect_request *user_req =
      (const struct wifi_connect_request *)(uintptr_t)a1;
  if (!readable(user_req, sizeof(*user_req)))
    return -1;
  struct wifi_connect_request req = *user_req;
  req.ssid[sizeof(req.ssid) - 1] = '\0';
  req.password[sizeof(req.password) - 1] = '\0';
  bool ssid_term = false;
  for (uint32_t i = 0; i < sizeof(req.ssid); i++)
    if (!req.ssid[i])
    {
      ssid_term = true;
      break;
    }
  if (!ssid_term || !req.ssid[0])
    return -1;
  bool pass_term = false;
  for (uint32_t i = 0; i < sizeof(req.password); i++)
    if (!req.password[i])
    {
      pass_term = true;
      break;
    }
  if (!pass_term)
    return -1;
  return wifi_connect(req.ssid, req.password) ? 0 : -1;
}

long sys_wifi_disconnect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!wifi_allowed())
    return -KERR_PERM;
  return wifi_disconnect() ? 0 : -1;
}

long sys_wifi_status(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
         uint64_t a5, uint64_t a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  struct wifi_status_info *out = (struct wifi_status_info *)(uintptr_t)a1;
  if (!writable(out, sizeof(*out)))
    return -1;
  struct wifi_status st;
  if (!wifi_get_status(&st))
    return -1;
  memset(out, 0, sizeof(*out));
  out->state = st.state;
  out->connected = st.connected ? 1 : 0;
  memcpy(out->ssid, st.ssid, sizeof(out->ssid));
  memcpy(out->bssid, st.bssid, 6);
  out->rssi = st.rssi;
  out->channel = st.channel;
  out->security = st.security;
  out->last_error = st.last_error;
  out->ip_address = st.ip_address;
  memcpy(out->mac, st.mac, 6);
  memcpy(out->interface_name, st.interface_name, sizeof(out->interface_name));
  out->scan_count = st.scan_count;
  out->last_scan_ms = st.last_scan_ms;
  out->has_device = st.has_device ? 1 : 0;
  return 0;
}
