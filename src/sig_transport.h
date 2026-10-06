#pragma once
// ============================================================
//  SpaceMantis signals: the transport under the meeting protocol.
//  Device: ESP-NOW (sig_transport_espnow.cpp). Desktop tests: local UDP.
//  A future Wi-Fi backend implements these same four calls; the device only
//  ever makes outbound connections (no listening sockets, no open ports).
// ============================================================
#include <stdint.h>

namespace net {
bool tpBegin(uint8_t selfMac[6]);                                     // radio up; fills our MAC
void tpEnd();
bool tpSend(const uint8_t *dstMac, const uint8_t *data, int len);    // dstMac null = broadcast
int tpRecv(uint8_t srcMac[6], uint8_t *buf, int maxLen);             // 0 = nothing waiting
}
