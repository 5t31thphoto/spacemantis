#include "signals.h"
#include "sig_transport.h"
#include <Arduino.h>
#include <string.h>

namespace net {
namespace {
const uint8_t MAGIC0 = 'S', MAGIC1 = 'M', VERSION = 1;
enum PType : uint8_t { P_HELLO = 1, P_STATE, P_CHAT, P_TAG, P_SPAWN, P_NPC, P_REMOVE, P_KILL, P_ARRIVED, P_BYE };

Phase s_phase = PH_OFF;
Role s_role = ROLE_NONE;
uint8_t s_mac[6], s_peer[6];
bool s_havePeer = false, s_peerNamedMe = false, s_peerArrived = false;
uint32_t s_nonce = 0, s_peerNonce = 0, s_seed = 0, s_tag = 0, s_lastBeacon = 0, s_lastHeard = 0;
char s_call[16] = "", s_peerCall[16] = "";
uint8_t s_ship = 0, s_peerShip = 0;
float s_peerArrivePos[3];
RemoteState s_remote{};

// small inboxes (overwrite oldest when full)
template <typename T, int N> struct Box { T q[N]; int h = 0, t = 0;
  void put(const T &v) { q[h] = v; h = (h + 1) % N; if (h == t) t = (t + 1) % N; }
  bool take(T &v) { if (h == t) return false; v = q[t]; t = (t + 1) % N; return true; } };
struct Chat { char s[52]; };
Box<Chat, 6> s_chat; Box<SpawnMsg, 8> s_spawn; Box<NpcMsg, 16> s_npc; Box<uint16_t, 16> s_remove, s_kill;
int s_tags = 0; bool s_bye = false;

uint32_t mix(uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2)); h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return h ? h : 1; }
bool macLess(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) < 0; }

void header(uint8_t *b, uint8_t type, uint32_t tag) { b[0] = MAGIC0; b[1] = MAGIC1; b[2] = VERSION; b[3] = type; memcpy(b + 4, &tag, 4); }
void sendTo(const uint8_t *dst, uint8_t *b, int len) { tpSend(dst, b, len); }
void sendPeer(uint8_t type, const void *payload, int n) {
  if (!s_havePeer) return;
  uint8_t b[250]; header(b, type, s_tag);
  if (n > 0) memcpy(b + 8, payload, n);
  sendTo(s_peer, b, 8 + n);
}

struct Hello { uint32_t nonce; char call[16]; uint8_t ship, commissioning; uint8_t pairedTo[6]; };

void beacon() {
  uint8_t b[64]; header(b, P_HELLO, 0);
  Hello h{}; h.nonce = s_nonce; memcpy(h.call, s_call, 16); h.ship = s_ship; h.commissioning = 1;
  if (s_havePeer) memcpy(h.pairedTo, s_peer, 6);
  memcpy(b + 8, &h, sizeof(h));
  sendTo(s_havePeer ? s_peer : nullptr, b, 8 + (int)sizeof(h));
  if (!s_havePeer) return;
  // once paired, keep a broadcast beacon too so a missed unicast never stalls the handshake
  sendTo(nullptr, b, 8 + (int)sizeof(h));
}

void settleRoles() {
  s_role = macLess(s_mac, s_peer) ? ROLE_ANCHOR : ROLE_GUEST;
  uint32_t a = s_role == ROLE_ANCHOR ? s_nonce : s_peerNonce, g = s_role == ROLE_ANCHOR ? s_peerNonce : s_nonce;
  s_seed = mix(a, g); s_tag = mix(s_seed, 0x51A7u);
}

void handle(const uint8_t *src, const uint8_t *b, int n) {
  if (n < 8 || b[0] != MAGIC0 || b[1] != MAGIC1 || b[2] != VERSION) return;   // not ours, or another firmware
  uint8_t type = b[3]; uint32_t tag; memcpy(&tag, b + 4, 4);
  const uint8_t *p = b + 8; int pn = n - 8;
  if (type == P_HELLO) {
    if (pn < (int)sizeof(Hello) || s_phase == PH_OFF) return;
    Hello h; memcpy(&h, p, sizeof(h));
    if (!h.commissioning) return;
    if (!s_havePeer) {
      if (s_phase != PH_SEEKING) return;
      memcpy(s_peer, src, 6); s_havePeer = true; s_peerNonce = h.nonce;
      memcpy(s_peerCall, h.call, 16); s_peerCall[15] = 0; s_peerShip = h.ship;
      settleRoles();
    }
    if (memcmp(src, s_peer, 6) != 0) return;   // someone else nearby: not our session
    s_peerNonce = h.nonce; s_peerShip = h.ship; settleRoles();
    s_peerNamedMe = memcmp(h.pairedTo, s_mac, 6) == 0;
    s_lastHeard = millis();
    if (s_phase == PH_SEEKING && s_peerNamedMe) s_phase = PH_READY;
    return;
  }
  if (!s_havePeer || memcmp(src, s_peer, 6) != 0 || tag != s_tag) return;       // wrong sender or session
  s_lastHeard = millis();
  if (s_phase == PH_LOST) s_phase = PH_IN_SKY;
  switch (type) {
    case P_STATE: if (pn >= 50) { memcpy(s_remote.p, p, 12); memcpy(s_remote.v, p + 12, 12); memcpy(s_remote.f, p + 24, 12); memcpy(s_remote.u, p + 36, 12);
                                  s_remote.ship = p[48]; s_remote.flags = p[49]; s_remote.atMs = millis(); s_remote.fresh = true; } break;
    case P_CHAT: { Chat c{}; int k = pn < 51 ? pn : 51; memcpy(c.s, p, k); c.s[k] = 0; s_chat.put(c); break; }
    case P_TAG: if (pn >= 1) s_tags += p[0]; break;
    case P_SPAWN: if (pn >= (int)sizeof(SpawnMsg)) { SpawnMsg m; memcpy(&m, p, sizeof(m)); m.name[15] = 0; s_spawn.put(m); } break;
    case P_NPC: { int cnt = pn > 0 ? p[0] : 0; for (int i = 0; i < cnt && 1 + (i + 1) * (int)sizeof(NpcMsg) <= pn; i++) { NpcMsg m; memcpy(&m, p + 1 + i * sizeof(NpcMsg), sizeof(m)); s_npc.put(m); } break; }
    case P_REMOVE: if (pn >= 2) { uint16_t id; memcpy(&id, p, 2); s_remove.put(id); } break;
    case P_KILL: if (pn >= 2) { uint16_t id; memcpy(&id, p, 2); s_kill.put(id); } break;
    case P_ARRIVED: if (pn >= 12) { memcpy(s_peerArrivePos, p, 12); s_peerArrived = true; } break;
    case P_BYE: s_bye = true; s_phase = PH_LOST; break;
  }
}
}  // namespace

void commission(const char *callsign, uint8_t ship) {
  if (s_phase != PH_OFF && s_phase != PH_LOST) return;
  if (!tpBegin(s_mac)) return;
  strncpy(s_call, callsign, 15); s_call[15] = 0; s_ship = ship;
  s_nonce = mix(micros(), (uint32_t)s_mac[5] << 8 | s_mac[4]) ^ millis();
  s_havePeer = s_peerNamedMe = s_peerArrived = false; s_role = ROLE_NONE; s_seed = s_tag = 0;
  s_remote = RemoteState{}; s_tags = 0; s_bye = false;
  s_phase = PH_SEEKING; s_lastBeacon = 0; s_lastHeard = millis();
}

void cancel() {
  if (s_phase == PH_OFF) return;
  if (s_havePeer && s_phase >= PH_READY && s_phase != PH_LOST) sendPeer(P_BYE, nullptr, 0);
  tpEnd(); s_phase = PH_OFF; s_havePeer = false; s_role = ROLE_NONE;
}

void poll() {
  if (s_phase == PH_OFF) return;
  uint8_t src[6], buf[250];
  for (int k = 0; k < 24; k++) { int n = tpRecv(src, buf, sizeof(buf)); if (n <= 0) break; handle(src, buf, n); }
  uint32_t now = millis();
  if ((s_phase == PH_SEEKING || s_phase == PH_READY || s_phase == PH_TRAVEL) && now - s_lastBeacon > 400) { beacon(); s_lastBeacon = now; }
  if (s_phase == PH_IN_SKY && s_havePeer && now - s_lastHeard > 8000) s_phase = PH_LOST;
}

Phase phase() { return s_phase; }
Role role() { return s_role; }
uint32_t seed() { return s_seed; }
const char *peerName() { return s_havePeer ? s_peerCall : ""; }
uint8_t peerShip() { return s_peerShip; }
bool peerArrived(float out[3]) { if (s_peerArrived) memcpy(out, s_peerArrivePos, 12); return s_peerArrived; }

void setTravelling() { if (s_phase == PH_READY) s_phase = PH_TRAVEL; }
void setArrived(const float p[3]) { s_phase = PH_IN_SKY; s_lastHeard = millis(); sendPeer(P_ARRIVED, p, 12); }
void sendState(const float p[3], const float v[3], const float f[3], const float u[3], uint8_t ship, uint8_t flags) {
  uint8_t b[50]; memcpy(b, p, 12); memcpy(b + 12, v, 12); memcpy(b + 24, f, 12); memcpy(b + 36, u, 12); b[48] = ship; b[49] = flags;
  sendPeer(P_STATE, b, 50);
}
void sendChat(const char *t) { int n = (int)strlen(t); if (n > 51) n = 51; sendPeer(P_CHAT, t, n); }
void sendTag(uint8_t hits) { sendPeer(P_TAG, &hits, 1); }
void sendSpawn(const SpawnMsg &m) { sendPeer(P_SPAWN, &m, sizeof(m)); }
void sendNpcs(const NpcMsg *m, int n) {
  uint8_t b[1 + 8 * sizeof(NpcMsg)]; if (n > 8) n = 8; b[0] = (uint8_t)n; memcpy(b + 1, m, n * sizeof(NpcMsg));
  sendPeer(P_NPC, b, 1 + n * (int)sizeof(NpcMsg));
}
void sendRemove(uint16_t id) { sendPeer(P_REMOVE, &id, 2); }
void sendKill(uint16_t id) { sendPeer(P_KILL, &id, 2); }
void sendBye() { sendPeer(P_BYE, nullptr, 0); }

const RemoteState &remote() { return s_remote; }
bool takeChat(char *out, int max) { Chat c; if (!s_chat.take(c)) return false; strncpy(out, c.s, max - 1); out[max - 1] = 0; return true; }
int takeTag() { int t = s_tags; s_tags = 0; return t; }
bool takeSpawn(SpawnMsg &m) { return s_spawn.take(m); }
bool takeNpc(NpcMsg &m) { return s_npc.take(m); }
bool takeRemove(uint16_t &id) { return s_remove.take(id); }
bool takeKill(uint16_t &id) { return s_kill.take(id); }
bool takeBye() { bool b = s_bye; s_bye = false; return b; }
uint32_t msSinceHeard() { return millis() - s_lastHeard; }

}  // namespace net
