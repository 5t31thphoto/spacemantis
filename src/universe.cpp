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

int buildGateHand(uint8_t localBand, GateOffer *out, int maxOut) {
  if (!out || maxOut <= 0) return 0;
  int n = 0;
  Pilot &p = sheet();

  auto push = [&](const char *name, uint8_t depth, uint8_t unknown, uint8_t persistent) {
    if (n >= maxOut || !name) return;
    // dedupe
    for (int i = 0; i < n; i++)
      if (strncmp(out[i].name, name, NAME_LEN) == 0) return;
    strncpy(out[n].name, name, NAME_LEN - 1);
    out[n].name[NAME_LEN - 1] = 0;
    out[n].depthRating = depth;
    out[n].unknown = unknown;
    out[n].persistent = persistent;
    n++;
  };

  // The hand is a mix, not a list. Earlier code pushed every known gate first,
  // so once a pilot knew eight names the hand never showed an unknown gate or
  // a deep landmark again. Each source now gets a small quota.

  // 1) persistent landmarks — from shallow down; at most two per hand
  if (localBand >= DEPTH_SHALLOW && s_lmN > 0) {
    int taken = 0;
    int start = (int)(urand() % (uint32_t)s_lmN);
    for (int k = 0; k < s_lmN && taken < 2 && n < maxOut; k++) {
      const Landmark &L = s_lm[(start + k) % s_lmN];
      if (L.band < localBand) continue;
      // Before first discovery the landmark is still unknown to the pilot.
      // Once threaded, its identity becomes a cross-universe place.
      bool seen = landmarkDiscovered(L.id);
      // Unfound places only show up within one layer of where they live, so
      // the deepest names have to be earned by actually going down.
      if (!seen && L.band > localBand + 1) continue;
      if (urandf() >= (seen ? 0.5f : 0.4f)) continue;
      push(L.name, L.band, seen ? 0 : 1, 1);
      taken++;
    }
  }

  // 2) unknown wander gates
  int unknowns = 1 + (int)(urand() % 2);
  if (p.rank[CR_WANDERER] > 2) unknowns++;
  for (int k = 0; k < unknowns && n < maxOut; k++) {
    const char *nm = procName(urand());
    uint8_t d = localBand;
    if (urandf() < 0.35f) d = (uint8_t)urand(localBand, DEPTH_BAND_COUNT - 1);
    push(nm, d, 1, 0);
  }

  // 3) rumors, freshest first
  for (int i = (int)p.rumorN - 1, taken = 0; i >= 0 && taken < 2 && n < maxOut; i--, taken++)
    push(p.rumors[i].name, p.rumors[i].depthHint, 0, 0);

  // 4) a couple of gates the pilot already knows
  if (p.knownN) {
    int start = (int)(urand() % p.knownN);
    for (int k = 0; k < p.knownN && k < 2 && n < maxOut; k++) {
      const NameTag &t = p.known[(start + k) % p.knownN];
      push(t.name, t.depthHint, 0, 0);
    }
  }

  // 5) never a thin hand
  for (int guard = 0; n < 3 && n < maxOut && guard < 8; guard++)
    push(procName(urand()), localBand, 1, 0);
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
    out[ENC_LANDMARK_GIANT] = 6;
    out[ENC_STATION] = 14;
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
    out[ENC_LANDMARK_GIANT] = 3;   // a second horizon: still scoopable
  } else {
    out[ENC_ANOMALY] = 18 + band * 2;
    out[ENC_HOSTILE] = 14 + band * 3;
    out[ENC_SUBPIRATE] = 10;
    out[ENC_TRAVELER] = 4;
    out[ENC_WRECK] = 8;
    out[ENC_ARTIFACT] = 6 + band * 2;
    out[ENC_ESCAPE_POD] = 3;
    out[ENC_STATION] = 3;          // rare deep docks keep the deep from being a fuel trap
    out[ENC_LANDMARK_GIANT] = 2;
    // scanners make anomalies more "readable" later in resolve; weight stays
    if (p.cap[CAP_SCANNERS] > 2) out[ENC_ANOMALY] = (uint8_t)(out[ENC_ANOMALY] + 4);
  }

  // career biases — ranks change the sky you fly through
  out[ENC_MERCHANT] = (uint8_t)(out[ENC_MERCHANT] + p.rank[CR_TRADER] * 2);
  out[ENC_PIRATE] = (uint8_t)(out[ENC_PIRATE] + p.rank[CR_GUNHAND]);
  out[ENC_LANDMARK_ROCK] = (uint8_t)(out[ENC_LANDMARK_ROCK] + p.rank[CR_PROSPECTOR] * 2);
  out[ENC_LANDMARK_GIANT] = (uint8_t)(out[ENC_LANDMARK_GIANT] + p.rank[CR_PROSPECTOR]);
  out[ENC_TRAVELER] = (uint8_t)(out[ENC_TRAVELER] + p.rank[CR_WANDERER] + p.rank[CR_RESCUER]);
  if (out[ENC_ESCAPE_POD]) out[ENC_ESCAPE_POD] = (uint8_t)(out[ENC_ESCAPE_POD] + p.rank[CR_RESCUER]);
  if (out[ENC_WRECK]) out[ENC_WRECK] = (uint8_t)(out[ENC_WRECK] + p.rank[CR_PROSPECTOR]);
  if (band >= DEPTH_DEEP)
    out[ENC_HOSTILE] = (uint8_t)(out[ENC_HOSTILE] + p.rank[CR_DEPTHRUNNER]);
  if (p.rank[CR_GHOST] > 2 && out[ENC_SECURITY] > p.rank[CR_GHOST])
    out[ENC_SECURITY] = (uint8_t)(out[ENC_SECURITY] - p.rank[CR_GHOST] / 2);
  // soft clamp so one career cannot zero the sky
  for (int i = 0; i < ENC_COUNT; i++) if (out[i] > 80) out[i] = 80;
}

}  // namespace sm
