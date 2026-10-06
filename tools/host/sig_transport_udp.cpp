// Desktop stand-in for ESP-NOW: each simulated device is a UDP port on localhost.
// SM_DEVICE=1..4 picks the MAC 02:00:00:00:00:0N and port 41000+N; "broadcast" sends to the others.
#include "sig_transport.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
namespace net {
static int s_fd = -1, s_id = 1;
bool tpBegin(uint8_t mac[6]) {
  const char *e = getenv("SM_DEVICE"); s_id = e ? atoi(e) : 1;
  uint8_t m[6] = {0x02, 0, 0, 0, 0, (uint8_t)s_id}; memcpy(mac, m, 6);
  if (s_fd >= 0) return true;
  s_fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(41000 + s_id); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int one = 1; setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (bind(s_fd, (sockaddr *)&a, sizeof(a)) != 0) { close(s_fd); s_fd = -1; return false; }
  fcntl(s_fd, F_SETFL, O_NONBLOCK);
  return true;
}
void tpEnd() { if (s_fd >= 0) close(s_fd); s_fd = -1; }
bool tpSend(const uint8_t *dst, const uint8_t *data, int len) {
  if (s_fd < 0) return false;
  uint8_t pkt[260]; uint8_t self[6] = {0x02, 0, 0, 0, 0, (uint8_t)s_id}; memcpy(pkt, self, 6); memcpy(pkt + 6, data, len);
  for (int id = 1; id <= 4; id++) {
    if (id == s_id || (dst && dst[5] != id)) continue;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(41000 + id); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(s_fd, pkt, 6 + len, 0, (sockaddr *)&a, sizeof(a));
  }
  return true;
}
int tpRecv(uint8_t src[6], uint8_t *buf, int maxLen) {
  if (s_fd < 0) return 0;
  uint8_t pkt[260]; int n = (int)recv(s_fd, pkt, sizeof(pkt), 0);
  if (n <= 6) return 0;
  memcpy(src, pkt, 6); int k = n - 6 < maxLen ? n - 6 : maxLen; memcpy(buf, pkt + 6, k);
  return k;
}
}  // namespace net
