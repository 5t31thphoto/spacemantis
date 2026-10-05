// ============================================================
//  SpaceMantis — universe seed + gate hand + encounter weights
// ============================================================
#include "universe.h"
#include "sheet.h"
#include "sim.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace sm {
namespace {

uint32_t s_seed = 0xC0FFEEu;
Landmark s_lm[MAX_LANDMARKS];
int s_lmN = 0;

uint32_t mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}

const char *procName(uint32_t s) {
  // small procedural name pool — semantic labels, not coordinates
  static const char *A[] = {
    "Keldran", "Voss", "Orryx", "Mire", "Cinder", "Nahl", "Pyre", "Sul",
    "Ashen", "Rook", "Vellum", "Jote", "Kestrel", "Dusk", "Brine", "Harrow"
  };
  static const char *B[] = {
    "Reach", "Well", "Gate", "Shelf", "Drift", "Hollow", "Span", "Mere",
    "Road", "Fall", "Verge", "Crown", "Pit", "Sound", "March", "Cross"
  };
  static char buf[NAME_LEN];
  snprintf(buf, sizeof(buf), "%s %s", A[s & 15], B[(s >> 4) & 15]);
  return buf;
}

}  // namespace

void universeInit() {
  s_seed = 0xA5A5A5A5u ^ (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);
  s_lmN = 0;
  // seed a few abyss/deep persistent landmarks (authored-flavored + procedural unique)
  landmarkRegister("The Still Road", DEPTH_ABYSS, true, 1);
  landmarkRegister("Mantis Glass", DEPTH_ABYSS, true, 2);
  landmarkRegister("Under-Customs", DEPTH_DEEPER, true, 3);
  landmarkRegister("Black Verge", DEPTH_DEEP, false, 4);
  landmarkRegister("Second Keel", DEPTH_DEEPER, false, 5);
  landmarkRegister("Quiet Quay", DEPTH_DEEP, true, 6);
  landmarkRegister("Nine-Lock Drift", DEPTH_DEEPER, true, 7);
  landmarkRegister("Orphan Beacon", DEPTH_DEEP, false, 8);
  landmarkRegister("Glass Toll", DEPTH_DEEPER, true, 9);
  landmarkRegister("Last Honest Star", DEPTH_ABYSS, true, 10);
  landmarkRegister("Sub-Pirate Bank", DEPTH_SHALLOW, false, 11);
  landmarkRegister("Folded Harbor", DEPTH_DEEPER, true, 12);
}

void universeReseed(uint32_t seed) {
  s_seed = mix(seed ? seed : millis());
}

uint32_t universeSeed() { return s_seed; }

void universeRestoreSeed(uint32_t seed) { s_seed = seed ? seed : 0xA5A5A5A5u; }

uint32_t urand() {
  s_seed = mix(s_seed + 0x9E3779B9u);
  return s_seed;
}

uint32_t urand(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (urand() % (hi - lo + 1));
}

float urandf() { return (urand() & 0xFFFFFF) / float(0xFFFFFF); }

void landmarkRegister(const char *name, uint8_t band, bool handmade, uint32_t id) {
  if (!name || s_lmN >= MAX_LANDMARKS) return;
  for (int i = 0; i < s_lmN; i++)
    if (s_lm[i].id == id || strncmp(s_lm[i].name, name, NAME_LEN) == 0) return;
  Landmark &L = s_lm[s_lmN++];
  strncpy(L.name, name, NAME_LEN - 1);
  L.name[NAME_LEN - 1] = 0;
  L.band = band;
  L.handmade = handmade ? 1 : 0;
  L.id = id;
}

int landmarkCount() { return s_lmN; }
const Landmark *landmarkAt(int i) { return (i >= 0 && i < s_lmN) ? &s_lm[i] : nullptr; }

const Landmark *landmarkFind(const char *name) {
  if (!name) return nullptr;
  for (int i = 0; i < s_lmN; i++)
    if (strncmp(s_lm[i].name, name, NAME_LEN) == 0) return &s_lm[i];
  return nullptr;
}

int landmarksForBand(uint8_t band, const Landmark **out, int maxOut) {
  int n = 0;
  for (int i = 0; i < s_lmN && n < maxOut; i++) {
    if (s_lm[i].band == band) out[n++] = &s_lm[i];
  }
  return n;
}

// How deep a dive a place needs. Stable per name within one universe, so a
// rumor, a job and a gate all agree. Most places are a shallow hop; a few
// are genuinely deep.
uint8_t placeDepth(const char *name) {
  uint32_t h = 2166136261u ^ s_seed;
  for (const char *c = name; c && *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
  uint32_t r = mix(h) % 100;
  if (r < 60) return 1;
  if (r < 87) return 2;
  if (r < 97) return 3;
  return 4;
}

int buildGateHand(uint8_t localBand, GateOffer *out, int maxOut) {
  (void)localBand;   // destinations are offered in real space
  if (!out || maxOut <= 0) return 0;
  int n = 0;
  Pilot &p = sheet();

  auto push = [&](const char *name, uint8_t depth, uint8_t unknown, uint8_t persistent) {
    if (n >= maxOut || !name || !name[0]) return;
    for (int i = 0; i < n; i++)
      if (sameName(out[i].name, name)) return;
    strncpy(out[n].name, name, NAME_LEN - 1);
    out[n].name[NAME_LEN - 1] = 0;
    out[n].depthRating = depth < 1 ? 1 : (depth > 4 ? 4 : depth);
    out[n].unknown = unknown;
    out[n].persistent = persistent;
    n++;
  };

  // A mix with quotas: once a pilot knew eight names the old hand never showed
  // anything new again. Places you heard of lately come first.

  // 1) rumors, freshest first
  for (int i = (int)p.rumorN - 1, taken = 0; i >= 0 && taken < 2; i--, taken++)
    push(p.rumors[i].name, p.rumors[i].depthHint, 0, 0);

  // 2) somewhere you have never heard of
  int unknowns = 1 + (int)(urand() % 2);
  if (p.rank[CR_WANDERER] > 2) unknowns++;
  for (int k = 0; k < unknowns; k++) {
    const char *nm = procName(urand());
    char tmp[NAME_LEN]; strncpy(tmp, nm, NAME_LEN - 1); tmp[NAME_LEN - 1] = 0;
    push(tmp, placeDepth(tmp), 1, 0);
  }

  // 3) a couple of places you have been
  if (p.knownN) {
    int start = (int)(urand() % p.knownN);
    for (int k = 0; k < p.knownN && k < 2; k++) {
      const NameTag &t = p.known[(start + k) % p.knownN];
      push(t.name, t.depthHint, 0, 0);
    }
  }

  // 4) a charted deep landmark: a fixed point that survives lost universes
  if (s_lmN > 0 && urandf() < 0.6f) {
    int start = (int)(urand() % (uint32_t)s_lmN);
    for (int k = 0; k < s_lmN; k++) {
      const Landmark &L = s_lm[(start + k) % s_lmN];
      if (!landmarkDiscovered(L.id)) continue;
      push(L.name, L.band, 0, 1);
      break;
    }
  }

  // 5) never a thin hand
  for (int guard = 0; n < 3 && n < maxOut && guard < 8; guard++) {
    const char *nm = procName(urand());
    char tmp[NAME_LEN]; strncpy(tmp, nm, NAME_LEN - 1); tmp[NAME_LEN - 1] = 0;
    push(tmp, placeDepth(tmp), 1, 0);
  }
  return n;
}

void encounterWeights(uint8_t band, uint8_t out[ENC_COUNT]) {
  for (int i = 0; i < ENC_COUNT; i++) out[i] = 0;
  Pilot &p = sheet();

  if (band == DEPTH_REAL) {
    out[ENC_TRAVELER] = 20;
    out[ENC_MERCHANT] = 18;
    out[ENC_SECURITY] = 12 + p.heat[HEAT_SECURITY] / 8;
    out[ENC_PIRATE] = 10 + p.heat[HEAT_PIRATE] / 5;
    // cloak reduces pirate pressure slightly
    if (p.cap[CAP_CLOAK] > 0 && out[ENC_PIRATE] > p.cap[CAP_CLOAK] * 2)
      out[ENC_PIRATE] = (uint8_t)(out[ENC_PIRATE] - p.cap[CAP_CLOAK] * 2);
    out[ENC_LANDMARK_ROCK] = 10 + p.cap[CAP_MINING] * 2;
    out[ENC_LANDMARK_GIANT] = 0;   // giants are bodies in the scene, not passers-by
    out[ENC_STATION] = 5;          // an outpost drifting into view
    out[ENC_ESCAPE_POD] = 5;
    out[ENC_WRECK] = 7;
  } else if (band == DEPTH_SHALLOW) {
    out[ENC_TRAVELER] = 12; // lost
    out[ENC_SUBPIRATE] = 22 + p.heat[HEAT_UNDER] / 4;
    out[ENC_ANOMALY] = 10;
    out[ENC_MERCHANT] = 6;
    out[ENC_ESCAPE_POD] = 6;
    out[ENC_WRECK] = 8;
    out[ENC_ARTIFACT] = 3;
  } else {
    out[ENC_ANOMALY] = 18 + band * 2;
    out[ENC_HOSTILE] = 14 + band * 3;
    out[ENC_SUBPIRATE] = 10;
    out[ENC_TRAVELER] = 4;
    out[ENC_WRECK] = 8;
    out[ENC_ARTIFACT] = 6 + band * 2;
    out[ENC_ESCAPE_POD] = 3;
    // scanners make anomalies more "readable" later in resolve; weight stays
    if (p.cap[CAP_SCANNERS] > 2) out[ENC_ANOMALY] = (uint8_t)(out[ENC_ANOMALY] + 4);
  }

  // career biases — ranks change the sky you fly through
  out[ENC_MERCHANT] = (uint8_t)(out[ENC_MERCHANT] + p.rank[CR_TRADER] * 2);
  out[ENC_PIRATE] = (uint8_t)(out[ENC_PIRATE] + p.rank[CR_GUNHAND]);
  out[ENC_LANDMARK_ROCK] = (uint8_t)(out[ENC_LANDMARK_ROCK] + p.rank[CR_PROSPECTOR] * 2);
  if (out[ENC_WRECK]) out[ENC_WRECK] = (uint8_t)(out[ENC_WRECK] + p.rank[CR_PROSPECTOR]);
  if (out[ENC_ESCAPE_POD]) out[ENC_ESCAPE_POD] = (uint8_t)(out[ENC_ESCAPE_POD] + p.rank[CR_RESCUER]);
  out[ENC_TRAVELER] = (uint8_t)(out[ENC_TRAVELER] + p.rank[CR_WANDERER] + p.rank[CR_RESCUER]);
  if (band >= DEPTH_DEEP)
    out[ENC_HOSTILE] = (uint8_t)(out[ENC_HOSTILE] + p.rank[CR_DEPTHRUNNER]);
  if (p.rank[CR_GHOST] > 2 && out[ENC_SECURITY] > p.rank[CR_GHOST])
    out[ENC_SECURITY] = (uint8_t)(out[ENC_SECURITY] - p.rank[CR_GHOST] / 2);
  // soft clamp so one career cannot zero the sky
  for (int i = 0; i < ENC_COUNT; i++) if (out[i] > 80) out[i] = 80;
}

}  // namespace sm
