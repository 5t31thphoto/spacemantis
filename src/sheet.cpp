// ============================================================
//  SpaceMantis — pilot spreadsheet implementation
// ============================================================
#include "sheet.h"
#include "universe.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace sm {
namespace {

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

Pilot s_p;

void wipeKnowledge() {
  s_p.rumorN = 0;
  s_p.knownN = 0;
  memset(s_p.rumors, 0, sizeof(s_p.rumors));
  memset(s_p.known, 0, sizeof(s_p.known));
}

void defaults() {
  memset(&s_p, 0, sizeof(s_p));
  s_p.lives = 0;
  s_p.credits = 500;
  s_p.fuelCap = 100;
  s_p.fuel = 100;
  s_p.holdCap = 20;
  s_p.hullMax = 100;
  s_p.hull = 100;
  // baseline teeth so Attack isn't impossible naked
  s_p.cap[CAP_WEAPONS] = 1;
  s_p.cap[CAP_SHIELDS] = 1;
  s_p.cap[CAP_BULKHEADS] = 1;
}

int findFlag(const char *key) {
  for (uint8_t i = 0; i < s_p.flagN; i++)
    if (strncmp(s_p.flags[i].key, key, FLAG_LEN) == 0) return (int)i;
  return -1;
}

}  // namespace

void sheetInit() {
  defaults();
  universeInit();
  s_p.universeSeed = universeSeed();
}

Pilot &sheet() { return s_p; }

void onDestroy() {
  s_p.lives++;
  // keep: ranks, xp, caps, credits (soft), persist flags, haul optional strip
  // wipe: knowledge, heat (fresh cosmos), hull refill
  wipeKnowledge();
  memset(s_p.heat, 0, sizeof(s_p.heat));
  s_p.hull = s_p.hullMax;
  s_p.fuel = s_p.fuelCap;
  // haul does not auto-survive a hard loss — inconvenient
  s_p.haulN = 0;
  memset(s_p.haul, 0, sizeof(s_p.haul));
  // strip non-persist flags
  uint8_t w = 0;
  for (uint8_t i = 0; i < s_p.flagN; i++) {
    if (s_p.flags[i].persist) s_p.flags[w++] = s_p.flags[i];
  }
  s_p.flagN = w;
  universeReseed(universeSeed() ^ (millis() * 2654435761u) ^ (s_p.lives * 747796405u));
  s_p.universeSeed = universeSeed();
  sheetSave();
}

void onResurface() {
  // sub-pirates notice breach — heat in UNDER channel
  addHeat(HEAT_UNDER, 8 + (int16_t)s_p.cap[CAP_CLOAK]);  // cloak doesn't fully erase wake; still a tell
}

void addCredits(int32_t d) {
  int64_t v = (int64_t)s_p.credits + d;
  if (v < 0) v = 0;
  if (v > 2000000000L) v = 2000000000L;
  s_p.credits = (int32_t)v;
}

bool spendCredits(int32_t d) {
  if (d < 0) d = -d;
  if (s_p.credits < d) return false;
  s_p.credits -= d;
  return true;
}

void setFuel(uint16_t v) {
  if (v > s_p.fuelCap) v = s_p.fuelCap;
  s_p.fuel = v;
}

bool burnFuel(uint16_t v) {
  if (s_p.fuel < v) return false;
  s_p.fuel = (uint16_t)(s_p.fuel - v);
  return true;
}

void repairHull(uint16_t v) {
  uint32_t h = (uint32_t)s_p.hull + v;
  if (h > s_p.hullMax) h = s_p.hullMax;
  s_p.hull = (uint16_t)h;
}

void damageHull(uint16_t v) {
  if (v >= s_p.hull) {
    s_p.hull = 0;
    onDestroy();
    return;
  }
  s_p.hull = (uint16_t)(s_p.hull - v);
}

void grantXp(CareerId id, uint16_t amount) {
  if (id >= CR_COUNT || amount == 0) return;
  uint32_t x = (uint32_t)s_p.xp[id] + amount;
  // simple curve: rank up every 100 * (rank+1)
  while (s_p.rank[id] < 20) {
    uint16_t need = (uint16_t)(100 * (s_p.rank[id] + 1));
    if (x < need) break;
    x = (uint16_t)(x - need);
    s_p.rank[id]++;
    // Career ranks are the main long-term progression. They quietly alter the fit.
    uint8_t r = s_p.rank[id];
    if (id == CR_HAULER && (r % 4) == 0) s_p.holdCap = (uint16_t)(s_p.holdCap + 3);
    if (id == CR_GUNHAND && (r % 3) == 0) earnCap(CAP_WEAPONS, (uint8_t)(s_p.cap[CAP_WEAPONS] + 1));
    if (id == CR_PROSPECTOR && (r % 3) == 0) earnCap(CAP_MINING, (uint8_t)(s_p.cap[CAP_MINING] + 1));
    if (id == CR_RESCUER && (r % 4) == 0) earnCap(CAP_SHIELDS, (uint8_t)(s_p.cap[CAP_SHIELDS] + 1));
    if (id == CR_TRADER && (r % 5) == 0) s_p.fuelCap = (uint16_t)(s_p.fuelCap + 8);
    if (id == CR_WANDERER && (r % 3) == 0) earnCap(CAP_SCANNERS, (uint8_t)(s_p.cap[CAP_SCANNERS] + 1));
    if (id == CR_DEPTHRUNNER && (r % 2) == 0) earnCap(CAP_BULKHEADS, (uint8_t)(s_p.cap[CAP_BULKHEADS] + 1));
    if (id == CR_GHOST && (r % 3) == 0) earnCap(CAP_CLOAK, (uint8_t)(s_p.cap[CAP_CLOAK] + 1));
  }
  s_p.xp[id] = (uint16_t)x;
}

uint8_t rankOf(CareerId id) {
  return id < CR_COUNT ? s_p.rank[id] : 0;
}

void earnCap(CapId id, uint8_t tier) {
  if (id >= CAP_COUNT) return;
  if (tier > 10) tier = 10;
  if (tier > s_p.cap[id]) s_p.cap[id] = tier;
}

uint8_t capTier(CapId id) {
  return id < CAP_COUNT ? s_p.cap[id] : 0;
}

uint8_t depthRating() {
  // bulkheads are the spine; stabilizer is separate push in depthQuery
  uint8_t d = s_p.cap[CAP_BULKHEADS];
  if (d < 1) d = 1;
  // depthrunner ranks nudge tolerance
  uint8_t dr = s_p.rank[CR_DEPTHRUNNER] / 3;
  uint8_t v = (uint8_t)(d + dr);
  if (v > 10) v = 10;
  return v;
}

DepthAbility depthQuery(uint8_t attemptBand) {
  DepthAbility a{};
  uint8_t rating = depthRating();
  a.stabilizer = s_p.cap[CAP_STABILIZER] > 0 ? 1 : 0;
  a.maxBand = rating;
  if (a.stabilizer && a.maxBand < DEPTH_BAND_COUNT - 1)
    a.maxBand = (uint8_t)(a.maxBand + 1);
  if (a.maxBand >= DEPTH_BAND_COUNT) a.maxBand = DEPTH_BAND_COUNT - 1;

  if (attemptBand <= rating) {
    a.glitchRisk = 0.02f * (attemptBand);
  } else if (attemptBand == (uint8_t)(rating + 1) && a.stabilizer) {
    a.glitchRisk = 0.25f + 0.05f * attemptBand;
  } else {
    int over = (int)attemptBand - (int)rating;
    a.glitchRisk = clampf(0.35f + 0.15f * over, 0.f, 0.95f);
  }
  return a;
}

void addHeat(HeatId id, int16_t d) {
  if (id >= HEAT_COUNT) return;
  int v = (int)s_p.heat[id] + d;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  s_p.heat[id] = (uint8_t)v;
}

void coolHeat(uint8_t amount) {
  for (int i = 0; i < HEAT_COUNT; i++) {
    if (s_p.heat[i] > amount) s_p.heat[i] = (uint8_t)(s_p.heat[i] - amount);
    else s_p.heat[i] = 0;
  }
}

bool haulAdd(const char *what, uint16_t amount, bool legal) {
  if (!what || amount == 0) return false;
  if ((uint32_t)s_p.holdUsed + amount > s_p.holdCap) return false;
  // merge same name
  for (uint8_t i = 0; i < s_p.haulN; i++) {
    if (strncmp(s_p.haul[i].what, what, NAME_LEN) == 0) {
      s_p.haul[i].amount = (uint16_t)(s_p.haul[i].amount + amount);
      if (!legal) s_p.haul[i].legal = 0;
      s_p.holdUsed = (uint16_t)(s_p.holdUsed + amount);
      return true;
    }
  }
  if (s_p.haulN >= MAX_HAUL_LINES) return false;
  HaulLine &h = s_p.haul[s_p.haulN++];
  strncpy(h.what, what, NAME_LEN - 1);
  h.what[NAME_LEN - 1] = 0;
  h.amount = amount;
  h.legal = legal ? 1 : 0;
  s_p.holdUsed = (uint16_t)(s_p.holdUsed + amount);
  return true;
}

void haulClear() {
  s_p.haulN = 0;
  s_p.holdUsed = 0;
  memset(s_p.haul, 0, sizeof(s_p.haul));
}

uint16_t haulUsed() { return s_p.holdUsed; }

void flagSet(const char *key, int8_t value, bool persist) {
  if (!key || !key[0]) return;
  int i = findFlag(key);
  if (i >= 0) {
    s_p.flags[i].value = value;
    if (persist) s_p.flags[i].persist = 1;
    return;
  }
  if (s_p.flagN >= MAX_FLAGS) return;
  Flag &f = s_p.flags[s_p.flagN++];
  strncpy(f.key, key, FLAG_LEN - 1);
  f.key[FLAG_LEN - 1] = 0;
  f.value = value;
  f.persist = persist ? 1 : 0;
}

int8_t flagGet(const char *key) {
  int i = findFlag(key);
  return i >= 0 ? s_p.flags[i].value : 0;
}

bool flagHas(const char *key) { return findFlag(key) >= 0; }

static bool addNameTag(NameTag *arr, uint8_t *n, uint8_t maxN,
                       const char *name, uint8_t depthHint, uint8_t ttl, uint8_t kind) {
  if (!name || !name[0] || *n >= maxN) return false;
  for (uint8_t i = 0; i < *n; i++) {
    if (strncmp(arr[i].name, name, NAME_LEN) == 0) {
      arr[i].depthHint = depthHint;
      if (ttl > arr[i].ttl) arr[i].ttl = ttl;
      return true;
    }
  }
  NameTag &t = arr[(*n)++];
  strncpy(t.name, name, NAME_LEN - 1);
  t.name[NAME_LEN - 1] = 0;
  t.depthHint = depthHint;
  t.ttl = ttl;
  t.kind = kind;
  return true;
}

bool rumorAdd(const char *name, uint8_t depthHint, uint8_t ttl) {
  return addNameTag(s_p.rumors, &s_p.rumorN, MAX_RUMORS, name, depthHint, ttl, 0);
}

bool knownGateAdd(const char *name, uint8_t depthHint) {
  return addNameTag(s_p.known, &s_p.knownN, MAX_KNOWN_GATES, name, depthHint, 255, 1);
}

void knowledgeTick() {
  uint8_t w = 0;
  for (uint8_t i = 0; i < s_p.rumorN; i++) {
    if (s_p.rumors[i].ttl > 0) s_p.rumors[i].ttl--;
    if (s_p.rumors[i].ttl > 0) s_p.rumors[w++] = s_p.rumors[i];
  }
  s_p.rumorN = w;
}

uint8_t rumorCount() { return s_p.rumorN; }
uint8_t knownGateCount() { return s_p.knownN; }
const NameTag *rumorAt(uint8_t i) { return i < s_p.rumorN ? &s_p.rumors[i] : nullptr; }
const NameTag *knownAt(uint8_t i) { return i < s_p.knownN ? &s_p.known[i] : nullptr; }

bool sheetSave() {
  Preferences prefs;
  if (!prefs.begin("sm_sheet", false)) return false;
  prefs.putBytes("pilot", &s_p, sizeof(s_p));
  prefs.end();
  return true;
}

bool sheetLoad() {
  Preferences prefs;
  if (!prefs.begin("sm_sheet", true)) return false;
  size_t n = prefs.getBytesLength("pilot");
  bool ok = false;
  if (n == sizeof(s_p)) {
    prefs.getBytes("pilot", &s_p, sizeof(s_p));
    universeRestoreSeed(s_p.universeSeed);
    ok = true;
  }
  prefs.end();
  return ok;
}

}  // namespace sm
