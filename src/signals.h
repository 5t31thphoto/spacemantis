#pragma once
// ============================================================
//  SpaceMantis signals (experimental): two pilots meet in one sky.
//  Pairing has no caller/answerer: both commission, both beacon, each pairs with
//  the first other commissioning device and names it in its own beacon. Ready when
//  each sees itself named back. Roles come from MAC order (lower = anchor, which
//  owns shared spawns and seeds the sky). Every packet carries a session tag.
// ============================================================
#include <stdint.h>

namespace net {

enum Phase : uint8_t { PH_OFF = 0, PH_SEEKING, PH_READY, PH_TRAVEL, PH_IN_SKY, PH_LOST };
enum Role : uint8_t { ROLE_NONE = 0, ROLE_ANCHOR, ROLE_GUEST };

struct RemoteState {   // the other pilot, as last heard
  float p[3], v[3], f[3], u[3];
  uint8_t ship, flags;            // flags: 1 firing, 2 cloaked, 4 it (tag)
  uint32_t atMs;
  bool fresh;
};
struct SpawnMsg { uint16_t id; uint8_t enc, mesh, hostile, ghost; uint16_t col; float radius; float p[3], v[3]; char name[16]; };
struct NpcMsg { uint16_t id; float p[3], v[3]; };

void commission(const char *callsign, uint8_t ship);   // start beaconing
void cancel();                                          // stop, tear down
void poll();                                            // call every frame
Phase phase();
Role role();
uint32_t seed();
const char *peerName();
uint8_t peerShip();
bool peerArrived(float out[3]);

void setTravelling();            // through the magenta gate
void setArrived(const float p[3]);
void sendState(const float p[3], const float v[3], const float f[3], const float u[3], uint8_t ship, uint8_t flags);
void sendChat(const char *text);
void sendTag(uint8_t hits);
void sendSpawn(const SpawnMsg &m);
void sendNpcs(const NpcMsg *m, int n);
void sendRemove(uint16_t id);
void sendKill(uint16_t id);      // a shared hostile destroyed (counts for both)
void sendBye();

const RemoteState &remote();
bool takeChat(char *out, int max);
int takeTag();                   // hits received (0 = none)
bool takeSpawn(SpawnMsg &m);
bool takeNpc(NpcMsg &m);
bool takeRemove(uint16_t &id);
bool takeKill(uint16_t &id);
bool takeBye();
uint32_t msSinceHeard();

}  // namespace net
