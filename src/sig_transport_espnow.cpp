// ESP-NOW transport (device only). Received frames are copied into a small ring
// from the radio callback and drained by net::poll() on the main loop.
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "sig_transport.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace net {
namespace {
struct Frame { uint8_t mac[6]; uint8_t len; uint8_t data[250]; };
const int RING = 12;
volatile int s_head = 0, s_tail = 0;
Frame s_ring[RING];
bool s_up = false;
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  int next = (s_head + 1) % RING;
  if (next == s_tail || len <= 0 || len > 250) return;   // full: drop (state packets repeat anyway)
  memcpy(s_ring[s_head].mac, mac, 6); s_ring[s_head].len = (uint8_t)len; memcpy(s_ring[s_head].data, data, len);
  s_head = next;
}
bool ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p{}; memcpy(p.peer_addr, mac, 6); p.channel = 0; p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}
}  // namespace

bool tpBegin(uint8_t selfMac[6]) {
  if (s_up) { WiFi.macAddress(selfMac); return true; }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  ensurePeer(BCAST);
  WiFi.macAddress(selfMac);
  s_head = s_tail = 0; s_up = true;
  return true;
}
void tpEnd() {
  if (!s_up) return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  s_up = false;
}
bool tpSend(const uint8_t *dst, const uint8_t *data, int len) {
  if (!s_up || len > 250) return false;
  const uint8_t *to = dst ? dst : BCAST;
  if (dst && !ensurePeer(dst)) return false;
  return esp_now_send(to, data, (size_t)len) == ESP_OK;
}
int tpRecv(uint8_t src[6], uint8_t *buf, int maxLen) {
  if (s_tail == s_head) return 0;
  Frame &f = s_ring[s_tail];
  int n = f.len < maxLen ? f.len : maxLen;
  memcpy(src, f.mac, 6); memcpy(buf, f.data, n);
  s_tail = (s_tail + 1) % RING;
  return n;
}
}  // namespace net
#endif
