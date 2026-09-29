// ============================================================
//  SpaceMantis — flight surface over the spreadsheet soul
//  "Elite Dangerous on a flippin ESP32" — career, not cartography.
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sheet.h"
#include "universe.h"
#include "resolve.h"
#include "contracts.h"
#include "sim.h"
#include "content.h"
#include "lore.h"

static constexpr int W = 320, H = 240;
static constexpr int MAX_LAYER = sm::DEPTH_BAND_COUNT;   // layers 1..5 map to bands 0..4

struct Gate {
  float x, y, z;
  int depth;                 // layer the gate opens toward (1 = real space)
  bool known, station, persistent, resurface, jobDest;
  char label[22];
};

static M5Canvas cv(&M5.Display);

// ---- flight state ----
static float tNow = 0, dt = 0.016f;
static float yaw = 0, roll = 0, steerX = 0, steerY = 0;
static float neutralAx = 0, neutralAy = 0, neutralAz = 1;
static bool neutralValid = false;
static float speed = 0.58f;
static int travelDepth = 1, depthBand = 0;
static Gate gates[8];
static int gateCount = 0, gateChain = 0;
static bool gateNear = false;
static int nearGateIdx = -1;
static bool diving = false;
static float diveT = 0;
static int diveTargetDepth = 1;
static uint32_t lastGateGen = 0, lastSave = 0;
static float fuelDrainAcc = 0;
static bool dragActive = false;
static int dragX = 160, dragY = 120;
static uint32_t rngState = 0xA341316Cu;
static uint32_t livesSeen = 0;

// ---- encounter state ----
static bool encounter = false;
static int encounterKind = 0;
static float encounterZ = 9.f, encounterX = 0, encounterY = 0;
static uint8_t encounterThreat = 2;
static const char *encounterName = "CONTACT";
static bool ambushChecked = false;
static uint32_t encounterCooldown = 0, lastSpawnRoll = 0;
static int lastContactSx = W / 2, lastContactSy = H / 2, lastContactR = 12;

// ---- station state ----
static constexpr int STATION_ROWS = 6;
static bool stationOpen = false;
static int stationChoice = 0;
static const char *stationName = "STATION";
static char stationMoodText[112] = "";
static sm::Opportunity stationOpportunity{};
static bool opportunityTaken = false;
static int gearCap = 0;

// ---- messages ----
static uint32_t bannerUntil = 0;
static char banner[112] = "LOST IN SPACE";
static char queued[2][112];
static uint16_t queuedMs[2];
static uint8_t queuedN = 0;

// ---- ending ----
static bool endingOpen = false;

// ---- contact presentation + short combat/mining theater ----
enum FxKind : uint8_t { FX_NONE = 0, FX_LASER, FX_MINE, FX_SCOOP, FX_SHIELD, FX_BOOM };
struct FxBolt { float x0, y0, x1, y1; uint8_t life; uint16_t col; };
static const int MAX_BOLTS = 8;
static FxBolt bolts[MAX_BOLTS];
static uint8_t boltN = 0;
static uint8_t fxKind = FX_NONE;
static float fxT = 0;           // 0..1 progress of current beat
static uint8_t combatVolleys = 0;
static uint8_t combatMaxVolley = 0;
static bool combatActive = false;
static bool mineActive = false;
static float theaterAcc = 0;
static float shipPhase = 0;     // bob/roll of contact silhouette
static int contactHullVis = 10; // visual-only for multi-volley feel

// ============================================================
//  small helpers
// ============================================================
static uint32_t rnd() {
  rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
  return rngState;
}
static int ri(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static float rf(float a, float b) { return a + (b - a) * ((rnd() & 0xFFFF) / 65535.f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static uint16_t hsv(float h, float s, float v) {
  h = fmodf(h, 360.f); if (h < 0) h += 360.f;
  v = clampf(v, 0.f, 1.f); s = clampf(s, 0.f, 1.f);
  float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
  float R = 0, G = 0, B = 0;
  if (h < 60) { R = c; G = x; } else if (h < 120) { R = x; G = c; }
  else if (h < 180) { G = c; B = x; } else if (h < 240) { G = x; B = c; }
  else if (h < 300) { R = x; B = c; } else { R = c; B = x; }
  return rgb((uint8_t)((R + m) * 255), (uint8_t)((G + m) * 255), (uint8_t)((B + m) * 255));
}
static void haptic(uint8_t l, uint16_t ms) { M5.Power.setVibration(l); delay(ms); M5.Power.setVibration(0); }

// The built-in 6x8 font is ASCII only. Content uses em dashes and arrows, so
// every string headed for the screen goes through this.
static void asciiCopy(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  if (!src) src = "";
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < cap; ) {
    if (*p < 0x80) { dst[o++] = (char)*p++; continue; }
    // UTF-8 lead byte: collapse the whole sequence into one '-'
    int len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    for (int k = 0; k < len && *p; k++) p++;
    dst[o++] = '-';
  }
  dst[o] = 0;
}

static void setBanner(const char *s, uint32_t ms = 2200) {
  asciiCopy(banner, sizeof(banner), s);
  bannerUntil = millis() + ms;
}
// Queue a message to show after the current one expires (job done, milestones).
static void noteBanner(const char *s, uint16_t ms = 2400) {
  if (bannerUntil <= millis()) { setBanner(s, ms); return; }
  if (queuedN >= 2) return;
  asciiCopy(queued[queuedN], sizeof(queued[0]), s);
  queuedMs[queuedN++] = ms;
}
static void serviceBanner() {
  if (bannerUntil > millis() || queuedN == 0) return;
  setBanner(queued[0], queuedMs[0]);
  if (queuedN == 2) { memcpy(queued[0], queued[1], sizeof(queued[0])); queuedMs[0] = queuedMs[1]; }
  queuedN--;
}

// Perspective projection shared by the sky, gates, contacts and combat bolts.
// Must be declared before anything that aims at a contact (combatTick).
static void project(float x, float y, float z, int &sx, int &sy, float &sc) {
  float zz = z < 0.2f ? 0.2f : z;
  sc = 140.f / zz;
  sx = (int)(W * 0.5f + (x + sinf(yaw) * 0.2f) * sc);
  sy = (int)(H * 0.5f + (y + sinf(roll) * 0.2f) * sc);
}

static void classifyDepth() {
  travelDepth = (int)clampf((float)travelDepth, 1.f, (float)MAX_LAYER);
  depthBand = travelDepth - 1;   // one layer per band: REAL, SHALLOW, DEEP, DEEPER, DEEPEST
}
static const char *depthName() {
  static const char *n[] = {"REAL SPACE", "SHALLOW SUBSPACE", "DEEP SUBSPACE", "DEEPER", "DEEPEST"};
  return n[depthBand < 5 ? depthBand : 4];
}

static void saveAll() {
  sm::sheetSave();
  sm::contractsSave();
  lastSave = millis();
}

// ---- encounter classification ----
static bool isMineable(int k) {
  return k == sm::ENC_LANDMARK_ROCK || k == sm::ENC_LANDMARK_GIANT ||
         k == sm::ENC_WRECK || k == sm::ENC_ARTIFACT;
}
static bool isShipLike(int k) {
  return !isMineable(k) && k != sm::ENC_STATION && k != sm::ENC_ESCAPE_POD;
}
static bool isAggressive(int k) {
  if (k == sm::ENC_PIRATE || k == sm::ENC_SUBPIRATE || k == sm::ENC_HOSTILE) return true;
  if (k == sm::ENC_SECURITY) return sm::sheet().heat[sm::HEAT_SECURITY] > 50;
  return false;
}
static const char *leftVerb(int k) {
  switch (k) {
    case sm::ENC_LANDMARK_GIANT: return "SCOOP";
    case sm::ENC_LANDMARK_ROCK: return "MINE";
    case sm::ENC_WRECK: return "SALVAGE";
    case sm::ENC_ARTIFACT: return "READ";
    case sm::ENC_ESCAPE_POD: return "RESCUE";
    case sm::ENC_STATION: return "DOCK";
    default: return "HAIL";
  }
}

// ============================================================
//  FX
// ============================================================
static void fxClear() {
  boltN = 0; fxKind = FX_NONE; fxT = 0; combatActive = false; mineActive = false;
}
static void fxAddBolt(float x0, float y0, float x1, float y1, uint16_t col) {
  if (boltN >= MAX_BOLTS) return;
  bolts[boltN++] = {x0, y0, x1, y1, 8, col};
}
static void fxService(float step) {
  shipPhase += step * 2.2f;
  for (int i = 0; i < boltN; ) {
    if (bolts[i].life > 0) bolts[i].life--;
    if (bolts[i].life == 0) bolts[i] = bolts[--boltN];
    else i++;
  }
  if (fxKind != FX_NONE) {
    fxT += step * (fxKind == FX_BOOM ? 1.8f : 2.5f);
    if (fxT >= 1.f) { fxKind = FX_NONE; fxT = 0; }
  }
}

// ============================================================
//  gates
// ============================================================
static void placeGate(Gate &g, float zlo, float zhi) {
  g.x = rf(-1.45f, 1.45f); g.y = rf(-0.82f, 0.82f); g.z = rf(zlo, zhi);
}

static void generateGates() {
  sm::GateOffer hand[sm::MAX_GATE_HAND];
  int n = sm::buildGateHand((uint8_t)depthBand, hand, sm::MAX_GATE_HAND);
  // shuffle so position in the hand never implies anything
  for (int i = n - 1; i > 0; --i) { int j = (int)(rnd() % (uint32_t)(i + 1)); sm::GateOffer t = hand[i]; hand[i] = hand[j]; hand[j] = t; }

  sm::Contract &c = sm::contract();
  bool wantDest = c.live && c.dest[0];
  bool wantUp = depthBand > 0;
  int room = 6 - (wantDest ? 1 : 0) - (wantUp ? 1 : 0);
  if (n > room) n = room;

  gateCount = 0;
  auto add = [&](const char *name, int depth, bool known, bool persistent) -> Gate & {
    Gate &g = gates[gateCount++];
    memset(&g, 0, sizeof(g));
    placeGate(g, 4.2f, 7.8f);
    g.depth = depth < 1 ? 1 : (depth > MAX_LAYER ? MAX_LAYER : depth);
    g.known = known;
    g.persistent = persistent;
    asciiCopy(g.label, sizeof(g.label), name);
    return g;
  };

  for (int i = 0; i < n; i++)
    add(hand[i].name, hand[i].depthRating + 1, !hand[i].unknown, hand[i].persistent != 0);
  // buildGateHand guarantees at least three, but never draw fewer than two
  while (gateCount < 2) add(sm::placeName(sm::urand(), (uint8_t)depthBand, false), travelDepth, false, false);

  if (wantDest) {
    Gate *d = nullptr;
    for (int i = 0; i < gateCount; i++) if (strncmp(gates[i].label, c.dest, sizeof(gates[i].label) - 1) == 0) d = &gates[i];
    if (!d) d = &add(c.dest, c.destDepth + 1, true, false);
    d->known = true;
    d->jobDest = true;
  }

  // In real space one ordinary gate may be a station approach.
  if (depthBand == 0 && sm::urand() % 100 < 48) {
    for (int i = 0; i < gateCount; i++) {
      if (!gates[i].jobDest && !gates[i].persistent) { gates[i].station = true; break; }
    }
  }

  // Once under the sky, one local gate always represents the way back up.
  // It is a gate, not a return button: the pilot still has to thread it.
  if (wantUp) {
    Gate &up = add("RESURFACE", 1, true, false);
    up.resurface = true;
  }
  nearGateIdx = -1; gateNear = false;
  lastGateGen = millis();
}

// ============================================================
//  destruction / damage
// ============================================================
static void onDestroyedFlow() {
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed() ^ 0x9E3779B9u;
  sm::contractAbandon();
  queuedN = 0;
  setBanner(sm::lossLine(sm::sheet().lives, sm::urand()), 4200);
  // Diegetic: pilot thinks they are lost — not that a cosmos was replaced.
  encounter = false; fxClear(); gateChain = 0; diving = false; stationOpen = false;
  travelDepth = 1; classifyDepth(); generateGates();
  encounterCooldown = millis() + 3000;
  haptic(200, 80);
  saveAll();
}

// Returns true if the ship was destroyed (and the universe replaced).
static bool checkDestroy() {
  bool died = sm::sheet().lives > livesSeen;
  if (died) onDestroyedFlow();
  livesSeen = sm::sheet().lives;
  return died;
}

static bool damage(int amount) {
  if (amount > 0) sm::damageHull((uint16_t)amount);
  return checkDestroy();
}

// ============================================================
//  landmarks and the long arc
// ============================================================
static int landmarksKnown() {
  int k = 0;
  for (int i = 0; i < sm::landmarkCount(); i++) {
    const sm::Landmark *lm = sm::landmarkAt(i);
    if (lm && sm::landmarkDiscovered(lm->id)) k++;
  }
  return k;
}

static void afterLandmarkDiscovery() {
  int k = landmarksKnown(), total = sm::landmarkCount();
  if (k == 3 && !sm::flagHas("arc_three")) {
    sm::flagSet("arc_three", 1, true);
    noteBanner("Three names have survived. The deep is keeping count.", 3200);
  } else if (k == total / 2 && !sm::flagHas("arc_half")) {
    sm::flagSet("arc_half", 1, true);
    noteBanner("Half the fixed points are yours. The deep feels like a neighborhood.", 3200);
  } else if (k >= total && total > 0 && !sm::flagHas("deep_small")) {
    sm::flagSet("deep_small", 1, true);
    endingOpen = true;
  }
}

// ============================================================
//  dives
// ============================================================
static void startDive(int targetDepth, bool shortcut) {
  // travelDepth is a layer count: 1 is real space, 2 shallow, 3 deep...
  targetDepth = (int)clampf((float)targetDepth, 1.f, (float)MAX_LAYER);
  if (targetDepth == travelDepth) {
    setBanner("THE GATE OPENS INTO THIS LAYER", 1200);
    return;
  }
  uint8_t targetBand = (uint8_t)(targetDepth - 1);
  auto da = sm::depthQuery(targetBand);
  float risk = targetBand > da.maxBand ? da.glitchRisk : 0.f;
  // Resurfacing straight to real space from far down is a shortcut with teeth.
  if (shortcut && depthBand >= 2) risk += 0.08f * (depthBand - 1);
  if (risk > 0.f) {
    char msg[64];
    snprintf(msg, sizeof(msg), "PUSHING PAST RATING - GLITCH %.0f%%", risk * 100.f);
    setBanner(msg, 1400);
    if (sm::urandf() < risk) {
      if (damage(ri(10, 28))) return;          // destroyed: new universe already set up
      setBanner("GLITCH - REALITY SLIPS", 2200);
      haptic(180, 60);
      if (sm::sheet().hull < 15) { setBanner("THE HULL REFUSES THE DIVE", 1800); gateChain = 0; return; }
    } else {
      setBanner("HULL COMPLAINS - PUSHING ANYWAY", 1600);
    }
  }
  diving = true; diveT = 0; diveTargetDepth = targetDepth;
  gateChain = 0; stationOpen = false; encounter = false; fxClear();
  setBanner(diveTargetDepth > travelDepth ? "DIVING UNDER" : "CLIMBING TOWARD REAL", 1500);
  haptic(110, 45);
}

static void finishDive() {
  diving = false;
  int prevBand = depthBand;
  int delta = abs(diveTargetDepth - travelDepth);
  bool climbing = diveTargetDepth < travelDepth;
  uint16_t diveFuel = (uint16_t)clampf(4.f + delta * 3.f, 4.f, 24.f);

  if (!sm::burnFuel(diveFuel)) {
    if (!climbing) {
      setBanner("LOW FUEL - THE DIVE STALLED", 1800);
      generateGates();
      return;
    }
    // Never strand the pilot below: a dry climb is paid for in hull.
    int shortfall = diveFuel - sm::sheet().fuel;
    sm::setFuel(0);
    if (damage(shortfall * 2)) return;
    setBanner("DRY CLIMB - THE HULL PAYS FOR IT", 2200);
  }

  travelDepth = diveTargetDepth;
  classifyDepth();
  sm::contractOnDepth((uint8_t)depthBand);

  if (depthBand == 0 && prevBand > 0) {
    sm::onResurface();
    setBanner(prevBand >= 2 ? "RESURFACE - FAR. SUB-PIRATES MAY HAVE NOTICED."
                            : "BACK UNDER REAL STARS", 2800);
  } else if (depthBand >= 3) {
    setBanner("DEEP WATER - NAMES GET STICKIER HERE", 2400);
    sm::grantXp(sm::CR_DEPTHRUNNER, (uint16_t)(6 + depthBand * 2));
    if ((rnd() & 1u) == 0u) noteBanner(sm::deepWhisper((uint8_t)depthBand, sm::urand()), 3000);
  } else {
    setBanner(depthBand > 0 ? "UNDER THE SKY" : "REAL SPACE", 1600);
    if (depthBand > 0) sm::grantXp(sm::CR_DEPTHRUNNER, 3);
  }
  encounter = false; encounterCooldown = millis() + 2500;
  generateGates();
  haptic(150, 40);
  saveAll();
}

// ============================================================
//  station board
// ============================================================
static int refuelCost() {
  const sm::Pilot &p = sm::sheet();
  return (p.fuelCap - p.fuel) * sm::fuelPrice() + (p.hullMax - p.hull) * sm::repairPrice();
}
static int rumorPrice() { return 12 + depthBand * 9; }
static int gearPrice() { return 80 + sm::capTier((sm::CapId)gearCap) * 55 + depthBand * 30; }

// Hold lines that are not owned by the active job.
static int sellableValue(bool doSell) {
  sm::Pilot &p = sm::sheet();
  const char *owned = sm::contract().live ? sm::contractCargo(sm::contract().kind) : nullptr;
  int pay = 0;
  char names[sm::MAX_HAUL_LINES][sm::NAME_LEN];
  int nn = 0;
  for (uint8_t i = 0; i < p.haulN; ++i) {
    if (owned && strncmp(p.haul[i].what, owned, sm::NAME_LEN) == 0) continue;
    pay += p.haul[i].amount * sm::marketPrice(p.haul[i].what, p.haul[i].legal != 0);
    strncpy(names[nn], p.haul[i].what, sm::NAME_LEN); nn++;
  }
  if (doSell) for (int i = 0; i < nn; i++) sm::haulRemove(names[i]);
  return pay;
}

static void dock(bool deep) {
  stationOpen = true; stationChoice = 0;
  encounter = false; fxClear();
  gateChain = 0;
  sm::contractOffer();
  opportunityTaken = !sm::makeOpportunity(stationOpportunity, (uint8_t)depthBand);
  gearCap = (int)(sm::urand() % sm::CAP_COUNT);
  // Station gear favors careers the pilot is actually pursuing.
  if (sm::sheet().rank[sm::CR_DEPTHRUNNER] >= 4 && (sm::urand() % 100) < 35) gearCap = sm::CAP_STABILIZER;
  stationName = deep ? sm::encounterFlavor(sm::ENC_STATION, (uint8_t)depthBand).name : "STATION";
  asciiCopy(stationMoodText, sizeof(stationMoodText),
            sm::stationMood((uint8_t)(sm::urand() % 5), sm::worldPressure(), sm::urand()));
  sm::grantXp(sm::CR_TRADER, 1);
  setBanner("DOCKED", 900);
  haptic(90, 35);
}

static void undock() {
  stationOpen = false;
  neutralValid = false;                      // re-capture the hand pose after the board
  encounterCooldown = millis() + 2500;
  generateGates();
  saveAll();
  setBanner("UNDOCKED", 900);
}

static void stationCommit() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Pilot &p = sm::sheet();
  char buf[112];
  switch (stationChoice) {
    case 0: {
      int fp = sm::fuelPrice(); if (fp < 1) fp = 1;
      int cost = refuelCost();
      if (p.fuel >= p.fuelCap && p.hull >= p.hullMax) { setBanner("ALREADY TOPPED UP", 1200); break; }
      if (sm::spendCredits(cost)) {
        sm::setFuel(p.fuelCap); sm::repairHull(p.hullMax);
        setBanner("REFUELED / REPAIRED", 1600);
      } else {
        // Stations will still sell a partial refill; the player never gets trapped by UI.
        int partial = p.credits / fp;
        int need = p.fuelCap - p.fuel;
        if (partial > need) partial = need;
        if (partial > 0) {
          sm::spendCredits(partial * fp);
          sm::setFuel((uint16_t)(p.fuel + partial));
          setBanner("PARTIAL REFUEL - CREDIT THIN", 1700);
        } else if (p.fuel < 12) {
          // A broke pilot is not stranded: the dock fronts enough to reach a gas giant.
          sm::setFuel(12);
          sm::addHeat(sm::HEAT_HOUSE, 6);
          setBanner("DOCK FRONTS YOU 12 FUEL. THEY WILL REMEMBER.", 2200);
        } else setBanner("TOO BROKE FOR THE DOCK", 1400);
      }
      break;
    }
    case 1:
      if (sm::contract().live) {
        sm::contractAbandon();
        setBanner("LEAD DROPPED", 1400);
      } else if (sm::contractAccept(off)) {
        snprintf(buf, sizeof(buf), "ACCEPTED: %s", off.title);
        setBanner(buf, 2000);
      } else {
        setBanner(off.kind == sm::CK_MARKET ? "CANNOT COVER THE CARGO - JOB STAYS" : "NO ROOM - JOB LEFT ON THE BOARD", 1800);
      }
      break;
    case 2: {
      // A rumor is a lead, not a quest object. Buying one simply makes a label
      // eligible to appear among the local gates.
      sm::GateOffer tmp[sm::MAX_GATE_HAND];
      int n = sm::buildGateHand((uint8_t)depthBand, tmp, sm::MAX_GATE_HAND);
      int pick = -1;
      for (int i = 0; i < n; i++) if (tmp[i].unknown) { pick = i; break; }
      if (pick < 0) { setBanner("NOBODY HERE KNOWS ANYTHING NEW", 1500); break; }
      if (sm::spendCredits(rumorPrice())) {
        sm::rumorAdd(tmp[pick].name, tmp[pick].depthRating, (uint8_t)(10 + depthBand * 3));
        sm::grantXp(sm::CR_TRADER, 4);
        snprintf(buf, sizeof(buf), "RUMOR BOUGHT: %s (d%d)", tmp[pick].name, tmp[pick].depthRating + 1);
        setBanner(buf, 2400);
        generateGates();
      } else setBanner("THE RUMOR SELLER WANTS MONEY", 1500);
      break;
    }
    case 3: {
      // Opportunistic market/gear surface. No loadout screen exists.
      int pay = sellableValue(false);
      if (pay > 0) {
        sellableValue(true);
        sm::addCredits(pay);
        sm::grantXp(sm::CR_TRADER, (uint16_t)(6 + pay / 25));
        snprintf(buf, sizeof(buf), "HAUL SOLD +%dcr", pay);
        setBanner(buf, 1900);
      } else {
        uint8_t t = sm::capTier((sm::CapId)gearCap);
        if (t >= 10) { setBanner("NOTHING HERE BEATS WHAT YOU FLY", 1500); break; }
        if (sm::spendCredits(gearPrice())) {
          sm::earnCap((sm::CapId)gearCap, (uint8_t)(t + 1));
          snprintf(buf, sizeof(buf), "%s %u - EQUIPPED", sm::capName((sm::CapId)gearCap), t + 1);
          setBanner(buf, 1900);
          gearCap = (int)(sm::urand() % sm::CAP_COUNT);
        } else setBanner("GEAR IS TOO EXPENSIVE HERE", 1500);
      }
      break;
    }
    case 4:
      if (opportunityTaken) { setBanner("THE BOARD IS EMPTY", 1200); break; }
      if (sm::contract().live) { setBanner("FINISH OR DROP YOUR LEAD FIRST", 1600); break; }
      if (sm::contractFromOpportunity(stationOpportunity)) {
        opportunityTaken = true;
        snprintf(buf, sizeof(buf), "LEAD: %s", stationOpportunity.title);
        setBanner(buf, 2000);
      } else setBanner("NO ROOM FOR THAT WORK", 1500);
      break;
    case 5:
      undock();
      return;
  }
  saveAll();
}

// ============================================================
//  encounters
// ============================================================
static void spawnEncounter() {
  if (encounter || stationOpen || diving || combatActive || mineActive) return;
  uint32_t now = millis();
  if (now < encounterCooldown || now - lastSpawnRoll < 1000) return;
  lastSpawnRoll = now;
  if ((int)(rnd() % 100) >= 22) return;

  uint8_t w[sm::ENC_COUNT];
  sm::encounterWeights((uint8_t)depthBand, w);
  int sum = 0; for (int i = 0; i < sm::ENC_COUNT; i++) sum += w[i];
  if (sum <= 0) return;
  int pick = (int)(rnd() % (uint32_t)sum), acc = 0, kind = 0;
  for (int i = 0; i < sm::ENC_COUNT; i++) { acc += w[i]; if (pick < acc) { kind = i; break; } }

  encounterKind = kind;
  encounter = true;
  ambushChecked = false;
  encounterZ = 8.f; encounterX = rf(-1.0f, 1.0f); encounterY = rf(-0.6f, 0.6f);
  const sm::EncounterFlavor &fl = sm::encounterFlavor((sm::EncounterClass)kind, (uint8_t)depthBand);
  encounterThreat = fl.threat ? fl.threat : 1;
  encounterName = fl.name;
  char buf[112];
  snprintf(buf, sizeof(buf), "%s: %s", fl.name, fl.hail);
  setBanner(buf, 2600);
  if ((rnd() % 6u) == 0u) noteBanner(sm::storyBeat((uint8_t)depthBand, sm::urand()).text, 3000);
}

static void finishEncounterResolve(bool attack) {
  sm::EncounterClass who = (sm::EncounterClass)encounterKind;
  if (who >= sm::ENC_COUNT) who = sm::ENC_TRAVELER;
  sm::Pilot &p = sm::sheet();
  int32_t credits0 = p.credits;
  int hull0 = p.hull, fuel0 = p.fuel;
  uint16_t hold0 = p.holdUsed;

  sm::ResolveIn in{attack ? sm::VERB_ATTACK : sm::VERB_HAIL, who, (uint8_t)depthBand, encounterThreat};
  sm::ResolveOut out = sm::resolve(in);

  encounter = false;
  combatActive = false;
  mineActive = false;
  encounterCooldown = millis() + 2500;

  if (checkDestroy()) return;   // loss line already on screen

  // Consequences, not the table: one line of what just changed.
  char tail[48] = "";
  int dc = (int)(p.credits - credits0), dh = (int)p.hull - hull0, df = (int)p.fuel - fuel0;
  int dhold = (int)p.holdUsed - (int)hold0;
  size_t o = 0;
  if (dc) o += snprintf(tail + o, sizeof(tail) - o, " %+dcr", dc);
  if (dh && o < sizeof(tail)) o += snprintf(tail + o, sizeof(tail) - o, " %+dhull", dh);
  if (df && o < sizeof(tail)) o += snprintf(tail + o, sizeof(tail) - o, " %+dfuel", df);
  if (dhold > 0 && o < sizeof(tail)) snprintf(tail + o, sizeof(tail) - o, " +%dhold", dhold);
  char buf[112];
  snprintf(buf, sizeof(buf), "%s%s%s", out.blurb ? out.blurb : "...", tail[0] ? " |" : "", tail);
  setBanner(buf, 2800);

  if (attack && out.destroyedOther) {
    fxKind = FX_BOOM; fxT = 0;
    haptic(160, 40);
  }
  if (out.flagKey[0] && strcmp(out.flagKey, "deep_seen") == 0) afterLandmarkDiscovery();
  saveAll();
}

static void beginCombat() {
  combatActive = true;
  combatVolleys = 0;
  theaterAcc = 0;
  combatMaxVolley = (uint8_t)(3 + sm::capTier(sm::CAP_WEAPONS) / 2);
  if (combatMaxVolley > 6) combatMaxVolley = 6;
  contactHullVis = 10;
  fxKind = FX_LASER;
  fxT = 0;
}

static void encounterAction(bool attack) {
  if (!encounter || combatActive || mineActive) return;
  int k = encounterKind;

  if (!attack && k == sm::ENC_STATION) {
    dock(depthBand > 0);
    return;
  }

  // mining / scoop / salvage / read theater: aim, commit, resolve, keep flying
  if (!attack && isMineable(k)) {
    mineActive = true;
    theaterAcc = 0;
    fxKind = (k == sm::ENC_LANDMARK_GIANT) ? FX_SCOOP : FX_MINE;
    fxT = 0;
    combatVolleys = 0;
    combatMaxVolley = (uint8_t)(3 + sm::capTier(sm::CAP_MINING));
    if (combatMaxVolley > 8) combatMaxVolley = 8;
    contactHullVis = 10;
    haptic(70, 25);
    setBanner(k == sm::ENC_LANDMARK_GIANT ? "SCOOPING VOLATILES..."
            : k == sm::ENC_WRECK ? "CUTTING INTO THE WRECK..."
            : k == sm::ENC_ARTIFACT ? "SCANNERS ON THE OBJECT..." : "CUTTERS ENGAGED...", 1200);
    return;
  }

  // ship battle theater
  if (attack && isShipLike(k)) {
    beginCombat();
    setBanner("WEAPONS FREE", 900);
    haptic(100, 30);
    return;
  }

  // hail / rescue / attacks on things that don't shoot back: resolve now
  finishEncounterResolve(attack);
}

static void combatTick(float step) {
  fxService(step);
  if (!combatActive && !mineActive) return;

  // project contact for bolt aims
  int sx, sy; float sc;
  project(encounterX, encounterY, encounterZ < 0.4f ? 0.4f : encounterZ, sx, sy, sc);

  theaterAcc += step;
  float beat = mineActive ? 0.22f : 0.30f;
  if (theaterAcc < beat) return;
  theaterAcc = 0;

  if (mineActive) {
    combatVolleys++;
    uint16_t col = encounterKind == sm::ENC_ARTIFACT ? rgb(200, 140, 255)
                 : encounterKind == sm::ENC_WRECK ? rgb(255, 200, 120) : rgb(140, 255, 100);
    fxAddBolt((float)(W / 2), (float)(H - 58), (float)sx + rf(-4, 4), (float)sy + rf(-4, 4), col);
    if (contactHullVis > 0) contactHullVis--;
    haptic(40, 12);
    if (combatVolleys >= combatMaxVolley) finishEncounterResolve(false);
    return;
  }

  combatVolleys++;
  // player bolt
  fxAddBolt((float)(W / 2), (float)(H - 60), (float)sx + rf(-6, 6), (float)sy + rf(-6, 6), rgb(120, 255, 180));
  // return fire
  if ((rnd() % 100) < (uint32_t)(55 + depthBand * 5)) {
    fxAddBolt((float)sx, (float)sy, (float)(W / 2 + ri(-20, 20)), (float)(H - 58), rgb(255, 100, 80));
    fxKind = FX_SHIELD;
    fxT = 0;
    // light visual damage only; real hull resolved at end
    if (contactHullVis > 0) contactHullVis--;
    haptic(50, 20);
  } else {
    if (contactHullVis > 1) contactHullVis -= 2;
    haptic(80, 15);
  }
  if (combatVolleys >= combatMaxVolley) finishEncounterResolve(true);
}

// Contacts approach while the pilot keeps flying. Ignoring one is a choice;
// aggressive ones may not let you.
static void updateEncounter() {
  if (!encounter || combatActive || mineActive) return;
  encounterX += (steerX * 0.32f + sinf(yaw) * 0.12f) * dt;
  encounterY += (steerY * 0.32f + sinf(roll) * 0.1f) * dt;
  encounterZ -= speed * dt * 0.85f;

  if (!ambushChecked && encounterZ < 1.8f && isAggressive(encounterKind)) {
    ambushChecked = true;
    float slip = sm::capTier(sm::CAP_CLOAK) * 0.12f + sm::rankOf(sm::CR_GHOST) * 0.02f;
    if (sm::urandf() < slip) {
      setBanner("THEY LOSE YOU IN THE DARK", 1600);
      sm::grantXp(sm::CR_GHOST, 5);
      encounter = false;
      encounterCooldown = millis() + 2000;
      return;
    }
    char buf[112];
    snprintf(buf, sizeof(buf), "%s: %s", encounterName,
             sm::encounterFlavor((sm::EncounterClass)encounterKind, (uint8_t)depthBand).attack);
    setBanner(buf, 1600);
    beginCombat();
    haptic(140, 40);
    return;
  }

  if (encounterZ < 0.55f) {
    encounter = false;
    encounterCooldown = millis() + 1500;
    if (encounterKind == sm::ENC_ESCAPE_POD) setBanner("THE POD'S BEACON FADES BEHIND YOU", 1400);
    else if (encounterKind == sm::ENC_STATION) setBanner("THE DOCK LIGHT SLIDES PAST", 1200);
  }
}

// ============================================================
//  input
// ============================================================
static void captureNeutral() {
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    neutralAx = d.accel.x; neutralAy = d.accel.y; neutralAz = d.accel.z;
    neutralValid = true;
  }
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static constexpr int BTN_Y = 184, BTN_H = 28, BTN_W = 130, BTN_L = 16, BTN_R = 174;
static constexpr int ROW_Y0 = 46, ROW_PITCH = 23, ROW_H = 20;

static void updateInput() {
  M5.update();
  auto td = M5.Touch.getDetail();

  if (endingOpen) {
    if (td.wasPressed() || M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
      endingOpen = false;
      setBanner("KEEP FLYING. THE NAMES WILL BE THERE.", 3000);
    }
    return;
  }

  if (td.wasPressed()) {
    dragActive = true; dragX = td.x; dragY = td.y;
    if (stationOpen) {
      dragActive = false;
      if (td.y >= ROW_Y0 && td.y < ROW_Y0 + STATION_ROWS * ROW_PITCH) {
        int row = (td.y - ROW_Y0) / ROW_PITCH;
        if (row == stationChoice) stationCommit();   // tap again to commit
        else stationChoice = row;
      }
    } else if (encounter && !combatActive && !mineActive) {
      if (inRect(td.x, td.y, BTN_L, BTN_Y, BTN_W, BTN_H)) { dragActive = false; encounterAction(false); }
      else if (inRect(td.x, td.y, BTN_R, BTN_Y, BTN_W, BTN_H)) { dragActive = false; encounterAction(true); }
    }
  }
  if (td.isPressed() && dragActive && !stationOpen && !diving) {
    yaw = clampf(yaw + (td.x - dragX) * 0.008f, -1.2f, 1.2f);
    roll = clampf(roll + (td.y - dragY) * 0.008f, -1.0f, 1.0f);
    dragX = td.x; dragY = td.y;
  }
  if (td.wasReleased()) dragActive = false;

  if (stationOpen) {
    if (M5.BtnA.wasPressed()) undock();
    else if (M5.BtnB.wasPressed()) stationCommit();
    else if (M5.BtnC.wasPressed()) stationChoice = (stationChoice + 1) % STATION_ROWS;
  } else {
    if (M5.BtnA.wasPressed() && encounter) encounterAction(false);
    if (M5.BtnB.wasPressed() && encounter) encounterAction(true);
    if (M5.BtnC.wasPressed()) {
      captureNeutral();
      yaw = 0; roll = 0; steerX = 0; steerY = 0;
      setBanner("ATTITUDE CENTERED", 900);
    }
  }

  if (!neutralValid) captureNeutral();
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    float ax = d.accel.x - neutralAx, ay = d.accel.y - neutralAy;
    steerX = clampf(steerX * 0.85f + (-ay) * 0.15f, -1.5f, 1.5f);
    steerY = clampf(steerY * 0.85f + (ax) * 0.15f, -1.5f, 1.5f);
  }
}

// ============================================================
//  world
// ============================================================
static void threadGate(Gate &g) {
  if (g.station) {
    gateChain = 0;
    dock(false);
    return;
  }
  gateChain++;
  if (g.resurface) {
    // counts toward the chain like any gate
  } else if (g.known) {
    sm::knownGateAdd(g.label, (uint8_t)(g.depth - 1));
  } else {
    sm::grantXp(sm::CR_WANDERER, 8);
    sm::knownGateAdd(g.label, (uint8_t)(g.depth - 1));
  }
  sm::contractOnGate(g.label, (uint8_t)depthBand, !g.known && !g.resurface);
  if (g.persistent) {
    const sm::Landmark *lm = sm::landmarkFind(g.label);
    if (lm) {
      bool first = !sm::landmarkDiscovered(lm->id);
      sm::discoverLandmark(lm->id);
      sm::grantXp(sm::CR_DEPTHRUNNER, first ? 18 : 4);
      if (first) {
        setBanner(sm::discoveryLine(lm->band, sm::urand()), 2800);
        noteBanner(sm::landmarkLine(*lm, sm::urand()), 2800);
        afterLandmarkDiscovery();
      }
    }
  }
  g.known = true;

  if (gateChain >= 3) {
    // The third gate is the commitment. Its label decides direction:
    // RESURFACE climbs straight to real space; a deeper gate goes to its layer;
    // a same-layer gate pushes one layer down; a shallower one climbs to it.
    int target;
    if (g.resurface) target = 1;
    else if (g.depth != travelDepth) target = g.depth;
    else target = travelDepth + 1;
    if (target > MAX_LAYER) {
      setBanner("THE DEEPEST HAS NO FLOOR. THE CHAIN UNRAVELS.", 2000);
      gateChain = 0;
      return;
    }
    startDive(target, g.resurface);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "GATE %d/3 - %s", gateChain, g.label);
    setBanner(buf, 1200);
  }
}

static void updateWorld() {
  sm::simTick(millis());
  sm::contractTick();
  serviceBanner();

  sm::Contract done;
  if (sm::contractTakeCompleted(done)) {
    char buf[112];
    snprintf(buf, sizeof(buf), "JOB DONE: %s +%dcr", done.title, done.pay);
    noteBanner(buf, 2600);
    haptic(120, 30);
  }

  if (stationOpen || endingOpen) return;
  if (diving) {
    diveT += dt * (0.7f + depthBand * 0.12f);
    if (diveT >= 1.f) finishDive();
    return;
  }
  if (combatActive || mineActive) return;   // the theater holds the frame

  speed = 0.52f + travelDepth * 0.03f + sm::sheet().cap[sm::CAP_SCANNERS] * 0.01f;
  if (sm::sheet().fuel == 0) speed *= 0.6f;

  gateNear = false;
  nearGateIdx = -1;
  for (int i = 0; i < gateCount; i++) {
    Gate &g = gates[i];
    g.x += (steerX * 0.32f + sinf(yaw) * 0.12f) * dt;
    g.y += (steerY * 0.32f + sinf(roll) * 0.1f) * dt;
    g.z -= speed * dt;

    float lateral = sqrtf(g.x * g.x + g.y * g.y);
    // Light assistance only when nearly threading: a gentle pull to center.
    if (g.z < 2.2f && lateral < 0.34f) {
      float pull = clampf(dt * 0.9f, 0.f, 0.2f);
      g.x -= g.x * pull; g.y -= g.y * pull;
      lateral = sqrtf(g.x * g.x + g.y * g.y);
    }
    bool aligned = g.z < 1.6f && lateral < (0.22f + g.z * 0.035f);
    if (aligned && (nearGateIdx < 0 || g.z < gates[nearGateIdx].z)) { nearGateIdx = i; gateNear = true; }

    if (g.z < 0.55f) {
      if (aligned) {
        threadGate(g);
        if (diving || stationOpen || gateCount == 0) return;   // world state changed under us
      }
      // Missed gates keep moving away and become a visual memory rather than
      // silently changing the player's destination.
      placeGate(g, 5.0f, 8.2f);
    }
  }

  spawnEncounter();
  updateEncounter();

  // slow idle burn, framerate independent: ~1 fuel every 9 seconds
  fuelDrainAcc += dt;
  if (fuelDrainAcc > 9.f) { fuelDrainAcc = 0; if (sm::sheet().fuel > 0) sm::burnFuel(1); }
  if (millis() > lastGateGen + 18000) generateGates();
  if (millis() > lastSave + 20000) saveAll();
}

// ============================================================
//  drawing
// ============================================================
static void drawSky() {
  // Real space is intentionally restrained: the visual reward for going down
  // comes from depth, not from turning every frame into a psychedelic effect.
  float alien = depthBand == 0 ? 0.f : clampf(depthBand / 4.f, 0.f, 1.f);
  for (int y = 0; y < H; y += 4) {
    for (int x = 0; x < W; x += 4) {
      float nx = (x - 160.f) / 160.f;
      float ny = (y - 120.f) / 120.f;
      float cloudA = sinf(nx * 4.2f + tNow * 0.018f) * 0.5f + 0.5f;
      float cloudB = cosf(ny * 3.1f - nx * 2.0f + tNow * 0.012f) * 0.5f + 0.5f;
      float cloud = cloudA * cloudB;
      float v = 0.008f + cloud * 0.012f + alien * alien * (0.012f + cloud * 0.028f);
      float h = 205.f + alien * 105.f + cloud * 20.f;
      cv.fillRect(x, y, 4, 4, hsv(h, 0.20f + alien * 0.38f, v));
    }
  }

  // Distant nebula dust. It is deterministic within a universe but never a map.
  uint32_t seed = sm::universeSeed() ^ ((uint32_t)depthBand * 0x9E3779B9u);
  if (!seed) seed = 1;
  for (int i = 0; i < 24; ++i) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    float x = (float)(seed % W);
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    float y = 20.f + (float)(seed % 170);
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    float r = 10.f + (float)(seed % 34);
    uint16_t c = hsv(205.f + alien * 100.f + (seed & 31), 0.25f + alien * 0.3f, 0.025f + alien * 0.045f);
    cv.fillCircle((int)x, (int)y, (int)r, c);
  }

  // Perspective starfield: stars move through the view rather than the player
  // dragging a flat texture.
  const float travel = speed * (0.7f + depthBand * 0.12f);
  for (int i = 0; i < 150; ++i) {
    uint32_t h = sm::universeSeed() + (uint32_t)i * 0x45D9F3Bu;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    float bx = ((int)(h & 0x3FF) - 512) / 190.f;
    float by = ((int)((h >> 10) & 0x3FF) - 512) / 250.f;
    float base = 0.45f + ((h >> 20) & 0xFF) / 255.f * 7.5f;
    float z = fmodf(base - tNow * travel * (0.32f + ((h >> 28) & 7) * 0.035f), 8.0f);
    if (z < 0.35f) z += 8.0f;
    int sx, sy; float sc;
    project(bx, by, z, sx, sy, sc);
    if (sx < 0 || sx >= W || sy < 20 || sy >= H) continue;
    float tw = 0.35f + 0.65f * sinf(tNow * (1.4f + (h & 7) * 0.2f) + i);
    uint16_t c = hsv(205.f + alien * 75.f + (h & 15), 0.08f + alien * 0.22f, 0.30f + tw * 0.55f);
    int r = z < 1.2f ? 2 : 1;
    cv.fillCircle(sx, sy, r, c);
  }
}

static void drawReticle() {
  uint16_t c = gateNear ? rgb(240, 240, 200) : rgb(70, 100, 100);
  int cx = W / 2, cy = H / 2;
  cv.drawLine(cx - 10, cy, cx - 4, cy, c);
  cv.drawLine(cx + 4, cy, cx + 10, cy, c);
  cv.drawLine(cx, cy - 10, cx, cy - 4, c);
  cv.drawLine(cx, cy + 4, cx, cy + 10, c);
}

static void drawGate(const Gate &g, bool aligned) {
  int sx, sy; float sc;
  project(g.x, g.y, g.z, sx, sy, sc);
  int r = (int)clampf(sc * 0.22f, 6.f, 50.f);
  uint16_t col = g.station ? rgb(70, 150, 255)
               : g.resurface ? rgb(230, 230, 240)
               : g.persistent ? rgb(220, 190, 90)
               : g.known ? rgb(70, 230, 170)
               : rgb(130, 130, 150);
  int rings = aligned ? 6 : 3;
  for (int k = 0; k < rings; k++) cv.drawCircle(sx, sy, r + k * (aligned ? 2 : 1), col);
  if (aligned) cv.drawCircle(sx, sy, r + 14, rgb(240, 240, 200));
  if (g.station) {
    cv.setTextColor(rgb(120, 180, 255));
    cv.setCursor(sx - 12, sy - 4);
    cv.print("DOCK");
  }
  cv.setTextColor(col);
  int tw = (int)strlen(g.label) * 6;
  cv.setCursor(sx - tw / 2, sy - r - 12);
  cv.print(g.label);
  cv.setCursor(sx - 12, sy + r + 3);
  if (g.jobDest) { cv.setTextColor(rgb(230, 200, 100)); cv.printf("d%d JOB", g.depth); }
  else cv.printf("d%d%s", g.depth, g.known ? "" : "?");
}

static void drawShipSilhouette(int sx, int sy, int r, uint8_t kind, uint16_t hullCol) {
  // simple readable silhouettes at Core2 scale
  float wob = sinf(shipPhase + kind) * 2.f;
  sy += (int)wob;
  if (kind == sm::ENC_MERCHANT) {
    cv.fillTriangle(sx - r, sy, sx + r, sy - r / 2, sx + r, sy + r / 2, hullCol);
    cv.fillRect(sx - r / 3, sy - r / 4, r, r / 2, rgb(40, 60, 80));
  } else if (kind == sm::ENC_SECURITY) {
    cv.fillTriangle(sx + r, sy, sx - r, sy - r / 2, sx - r, sy + r / 2, hullCol);
    cv.drawCircle(sx, sy, r / 3, rgb(80, 160, 255));
  } else if (kind == sm::ENC_PIRATE || kind == sm::ENC_SUBPIRATE) {
    cv.fillTriangle(sx - r, sy - r / 2, sx + r, sy, sx - r, sy + r / 2, hullCol);
    cv.fillTriangle(sx - r / 2, sy - r, sx, sy - r / 3, sx - r, sy - r / 4, rgb(120, 40, 40));
  } else if (kind == sm::ENC_HOSTILE) {
    for (int k = 0; k < 5; k++) {
      float a = shipPhase + k * 1.256f;
      cv.drawLine(sx, sy, sx + (int)(cosf(a) * r), sy + (int)(sinf(a) * r), hullCol);
    }
    cv.fillCircle(sx, sy, r / 3, rgb(180, 40, 200));
  } else if (kind == sm::ENC_ANOMALY) {
    cv.drawCircle(sx, sy, r, hullCol);
    cv.drawCircle(sx, sy, r / 2, rgb(160, 80, 200));
    cv.drawPixel(sx + (int)(cosf(shipPhase) * r / 2), sy + (int)(sinf(shipPhase) * r / 2), rgb(255, 200, 255));
  } else {
    // traveler / default
    cv.fillTriangle(sx + r, sy, sx - r / 2, sy - r / 2, sx - r / 2, sy + r / 2, hullCol);
    cv.fillRect(sx - r / 2, sy - 2, r / 2, 4, rgb(200, 200, 210));
  }
  cv.drawPixel(sx, sy, rgb(220, 240, 255));   // cockpit glint
}

static void drawRock(int sx, int sy, int r) {
  cv.fillCircle(sx, sy, r, rgb(110, 90, 60));
  cv.fillCircle(sx - r / 3, sy - r / 4, r / 3, rgb(90, 70, 45));
  cv.drawCircle(sx, sy, r, rgb(160, 130, 80));
  if (sm::capTier(sm::CAP_MINING) > 0) {   // mineral sparkle if mining gear
    cv.drawPixel(sx + r / 2, sy - r / 3, rgb(180, 220, 120));
    cv.drawPixel(sx - r / 4, sy + r / 3, rgb(200, 240, 140));
  }
}

static void drawGiant(int sx, int sy, int r) {
  cv.fillCircle(sx, sy, r, rgb(40, 90, 140));
  cv.fillCircle(sx - r / 4, sy - r / 5, r / 2, rgb(60, 120, 160));
  cv.drawLine(sx - r, sy - r / 4, sx + r, sy - r / 4, rgb(100, 160, 190));
  cv.drawLine(sx - r, sy + r / 5, sx + r, sy + r / 5, rgb(30, 70, 110));
  if (mineActive || fxKind == FX_SCOOP) {
    for (int k = 0; k < 6; k++) {
      float a = shipPhase * 2 + k;
      cv.drawPixel(sx + (int)(cosf(a) * (r + 6)), sy + (int)(sinf(a) * (r + 4)), rgb(160, 220, 255));
    }
  }
}

static void drawPod(int sx, int sy, int r) {
  cv.fillRoundRect(sx - r / 2, sy - r / 3, r, r * 2 / 3, 3, rgb(200, 200, 120));
  cv.drawRect(sx - r / 2, sy - r / 3, r, r * 2 / 3, rgb(255, 255, 180));
  if (((int)(tNow * 3)) & 1) cv.fillCircle(sx, sy, 2, rgb(80, 255, 120));   // blinking beacon
}

static void drawWreck(int sx, int sy, int r) {
  cv.drawLine(sx - r, sy - r / 2, sx + r, sy + r / 3, rgb(140, 140, 150));
  cv.drawLine(sx - r / 2, sy + r / 2, sx + r / 2, sy - r / 2, rgb(100, 100, 110));
  cv.fillCircle(sx, sy, r / 4, rgb(60, 60, 70));
}

static void drawArtifact(int sx, int sy, int r) {
  float a = shipPhase * 0.7f;
  int x0 = sx + (int)(cosf(a) * r * 0.6f), y0 = sy + (int)(sinf(a) * r * 0.6f);
  int x1 = sx + (int)(cosf(a + 2.09f) * r * 0.6f), y1 = sy + (int)(sinf(a + 2.09f) * r * 0.6f);
  int x2 = sx + (int)(cosf(a + 4.19f) * r * 0.6f), y2 = sy + (int)(sinf(a + 4.19f) * r * 0.6f);
  cv.drawLine(x0, y0, x1, y1, rgb(200, 160, 255));
  cv.drawLine(x1, y1, x2, y2, rgb(200, 160, 255));
  cv.drawLine(x2, y2, x0, y0, rgb(200, 160, 255));
  cv.drawCircle(sx, sy, r / 4, rgb(180, 120, 255));
}

static void drawStationContact(int sx, int sy, int r) {
  cv.drawCircle(sx, sy, r, rgb(70, 150, 255));
  cv.drawCircle(sx, sy, r - 3, rgb(40, 90, 170));
  cv.fillRect(sx - r / 2, sy - r / 4, r, r / 2, rgb(30, 60, 100));
  if (((int)(tNow * 2)) & 1) cv.fillCircle(sx, sy - r, 2, rgb(120, 200, 255));
}

static void drawBolts() {
  for (int i = 0; i < boltN; i++) {
    auto &b = bolts[i];
    cv.drawLine((int)b.x0, (int)b.y0, (int)b.x1, (int)b.y1, b.col);
    if (b.life > 4) cv.drawLine((int)b.x0 + 1, (int)b.y0, (int)b.x1 + 1, (int)b.y1, rgb(255, 255, 200));
  }
  if (combatActive || mineActive)   // ship's emitter
    cv.fillTriangle(W / 2 - 6, H - 56, W / 2 + 6, H - 56, W / 2, H - 68, rgb(80, 120, 100));
}

static void drawVerbButtons() {
  int k = encounterKind;
  bool dockBtn = (k == sm::ENC_STATION);
  cv.fillRoundRect(BTN_L, BTN_Y, BTN_W, BTN_H, 5, dockBtn ? rgb(18, 44, 96) : rgb(18, 70, 40));
  cv.drawRoundRect(BTN_L, BTN_Y, BTN_W, BTN_H, 5, dockBtn ? rgb(90, 150, 255) : rgb(80, 200, 120));
  cv.fillRoundRect(BTN_R, BTN_Y, BTN_W, BTN_H, 5, rgb(90, 22, 32));
  cv.drawRoundRect(BTN_R, BTN_Y, BTN_W, BTN_H, 5, rgb(220, 90, 100));
  const char *left = leftVerb(k);
  cv.setTextSize(2);
  cv.setTextColor(dockBtn ? rgb(150, 200, 255) : rgb(140, 255, 180));
  cv.setCursor(BTN_L + (BTN_W - (int)strlen(left) * 12) / 2, BTN_Y + 7);
  cv.print(left);
  cv.setTextColor(rgb(255, 140, 140));
  cv.setCursor(BTN_R + (BTN_W - 6 * 12) / 2, BTN_Y + 7);
  cv.print("ATTACK");
  cv.setTextSize(1);
}

static void drawEncounter() {
  if (!encounter && !combatActive && !mineActive) {
    // aftermath: let an explosion finish where the contact was
    if (fxKind == FX_BOOM) {
      int br = (int)(lastContactR * (0.5f + fxT * 2.f));
      cv.drawCircle(lastContactSx, lastContactSy, br, rgb(255, 180, 60));
      cv.drawCircle(lastContactSx, lastContactSy, br / 2, rgb(255, 80, 40));
      cv.drawCircle(lastContactSx, lastContactSy, br / 3, rgb(255, 230, 160));
    }
    drawBolts();
    return;
  }
  int sx, sy; float sc;
  project(encounterX, encounterY, encounterZ < 0.4f ? 0.4f : encounterZ, sx, sy, sc);
  int r = (int)clampf(sc * 0.17f, 6.f, 40.f);
  uint8_t kind = (uint8_t)encounterKind;
  lastContactSx = sx; lastContactSy = sy; lastContactR = r;

  switch (kind) {
    case sm::ENC_LANDMARK_GIANT: drawGiant(sx, sy, r + 8); break;
    case sm::ENC_LANDMARK_ROCK: drawRock(sx, sy, r); break;
    case sm::ENC_WRECK: drawWreck(sx, sy, r); break;
    case sm::ENC_ARTIFACT: drawArtifact(sx, sy, r); break;
    case sm::ENC_ESCAPE_POD: drawPod(sx, sy, r); break;
    case sm::ENC_STATION: drawStationContact(sx, sy, r); break;
    default: {
      uint16_t hull = rgb(200, 200, 210);
      if (kind == sm::ENC_PIRATE || kind == sm::ENC_SUBPIRATE) hull = rgb(180, 70, 70);
      if (kind == sm::ENC_SECURITY) hull = rgb(80, 140, 220);
      if (kind == sm::ENC_HOSTILE) hull = rgb(160, 60, 180);
      if (kind == sm::ENC_MERCHANT) hull = rgb(120, 160, 140);
      if (kind == sm::ENC_ANOMALY) hull = rgb(200, 150, 255);
      drawShipSilhouette(sx, sy, r, kind, hull);
    }
  }

  // integrity pips during combat / extraction
  if (combatActive || mineActive) {
    for (int i = 0; i < 5; i++) {
      uint16_t c = i < (contactHullVis + 1) / 2 ? (mineActive ? rgb(230, 210, 90) : rgb(80, 220, 100)) : rgb(50, 50, 50);
      cv.fillRect(sx - 12 + i * 5, sy - r - 8, 4, 3, c);
    }
  }

  // active FX beams
  if (mineActive && fxKind != FX_SCOOP) {
    uint16_t c = kind == sm::ENC_ARTIFACT ? rgb(190, 130, 255) : kind == sm::ENC_WRECK ? rgb(255, 190, 110) : rgb(120, 255, 80);
    cv.drawLine(W / 2, H - 58, sx, sy, c);
    cv.drawLine(W / 2 - 1, H - 58, sx - 1, sy, rgb(230, 255, 200));
    for (int k = 0; k < 4; k++)
      cv.drawPixel(sx + (int)(cosf(shipPhase * 3 + k) * (r / 2)), sy + (int)(sinf(shipPhase * 3 + k) * (r / 2)), rgb(255, 255, 100));
  }
  if (mineActive && kind == sm::ENC_LANDMARK_GIANT) {
    for (int k = 0; k < 6; k++) {
      float u = fmodf(tNow * 1.3f + k * 0.17f, 1.f);
      int px = (int)(sx + (W / 2 - sx) * u);
      int py = (int)(sy + (H - 58 - sy) * u);
      cv.fillCircle(px, py, 1, rgb(140, 210, 255));
    }
  }
  if (fxKind == FX_SHIELD) {
    cv.drawCircle(W / 2, H - 62, (int)(14 + fxT * 10), rgb(90, 160, 255));
  }

  drawBolts();

  cv.setTextColor(rgb(240, 240, 245));
  int tw = (int)strlen(encounterName) * 6;
  cv.setCursor(sx - tw / 2, sy + r + 6);
  cv.print(encounterName);

  if (encounter && !combatActive && !mineActive) {
    drawVerbButtons();
  } else if (combatActive) {
    cv.setTextColor(rgb(255, 200, 120));
    cv.setCursor(112, BTN_Y + 10);
    cv.printf("ENGAGED %u/%u", combatVolleys, combatMaxVolley);
  } else if (mineActive) {
    const char *v = kind == sm::ENC_LANDMARK_GIANT ? "SCOOPING..." : kind == sm::ENC_WRECK ? "SALVAGING..."
                  : kind == sm::ENC_ARTIFACT ? "READING..." : "MINING...";
    cv.setTextColor(rgb(160, 255, 140));
    cv.setCursor((W - (int)strlen(v) * 6) / 2, BTN_Y + 10);
    cv.print(v);
  }
}

static void drawHudTop() {
  sm::Pilot &p = sm::sheet();
  cv.setTextSize(1);
  cv.setTextColor(depthBand == 0 ? rgb(80, 210, 200) : rgb(180, 140, 255));
  cv.setCursor(4, 4);
  cv.print(depthName());

  bool lowHull = p.hull * 4 < p.hullMax, lowFuel = p.fuel < 15;
  cv.setCursor(166, 4);
  cv.setTextColor(lowHull ? rgb(255, 90, 80) : rgb(200, 210, 220)); cv.printf("H%d ", p.hull);
  cv.setTextColor(lowFuel ? rgb(255, 170, 60) : rgb(200, 210, 220)); cv.printf("F%d ", p.fuel);
  cv.setTextColor(rgb(200, 210, 220)); cv.printf("$%ld", (long)p.credits);

  cv.setTextColor(rgb(100, 120, 130));
  cv.setCursor(4, 16);
  cv.printf("life %lu  rate %u  chain %d/3", (unsigned long)p.lives, sm::depthRating(), gateChain);
  cv.setCursor(214, 16);
  cv.printf("hold %u/%u", p.holdUsed, p.holdCap);

  sm::Contract &c = sm::contract();
  if (c.live) {
    cv.setTextColor(rgb(230, 200, 100));
    cv.setCursor(4, 28);
    cv.printf("JOB %s %u/%u", c.title, c.progress, c.need);
    cv.setTextColor(rgb(150, 130, 80));
    cv.setCursor(4, 38);
    if (c.dest[0]) cv.printf("-> %s", c.dest);
    else cv.print(sm::contractHint(c));
  } else if (p.haulN) {
    cv.setTextColor(rgb(180, 160, 120));
    cv.setCursor(4, 28);
    cv.printf("HOLD %s x%u%s%s", p.haul[0].what, p.haul[0].amount, p.haul[0].legal ? "" : " HOT",
              p.haulN > 1 ? " +more" : "");
  }
}

// Word-wrapped text in the 6x8 font; the caller sets the color.
static void printWrapped(int x, int y, int cols, int maxLines, int lineH, const char *s) {
  char line[64];
  if (cols > 63) cols = 63;
  for (int ln = 0; ln < maxLines && s && *s; ln++) {
    int len = (int)strlen(s);
    int cut = len;
    if (len > cols) {
      cut = cols;
      while (cut > cols / 3 && s[cut] != ' ') cut--;
      if (s[cut] != ' ') cut = cols;
    }
    memcpy(line, s, (size_t)cut); line[cut] = 0;
    cv.setCursor(x, y + ln * lineH);
    cv.print(line);
    s += cut;
    while (*s == ' ') s++;
  }
}

// Two-line word-wrapped banner at the bottom of the screen.
static void drawBanner() {
  const int y0 = 216;
  if (bannerUntil > millis()) {
    cv.setTextColor(rgb(240, 245, 250));
    printWrapped(6, y0, 52, 2, 11, banner);
  } else if (!stationOpen) {
    cv.setTextColor(rgb(70, 100, 110));
    cv.setCursor(6, y0 + 11);
    if (encounter) cv.print("A/left verb  B/ATTACK  or fly past");
    else cv.printf("known %u  rumor %u  drag=yaw/roll  C=center", sm::knownGateCount(), sm::rumorCount());
  } else {
    cv.setTextColor(rgb(70, 100, 110));
    cv.setCursor(6, y0 + 11);
    cv.print("tap row, tap again to commit   A undock  C next");
  }
}

static void drawDive() {
  float u = clampf(diveT, 0, 1);
  float alien = clampf((diveTargetDepth - 1) / 4.f, 0, 1);
  for (int y = 0; y < H; y += 4)
    for (int x = 0; x < W; x += 4) {
      float dx = x - 160.f, dy = y - 120.f;
      float r = sqrtf(dx * dx + dy * dy) / 180.f;
      float wave = sinf(r * 22.f - u * 28.f + tNow * 4.f);
      cv.fillRect(x, y, 4, 4, hsv(195 + alien * 160 + wave * 18, 0.4f + alien * 0.4f, 0.02f + r * 0.1f + u * 0.14f));
    }
  cv.setTextSize(2);
  cv.setTextColor(rgb(230, 245, 240));
  const char *t = diveTargetDepth > travelDepth ? "DIVING" : "SURFACING";
  cv.setCursor((W - (int)strlen(t) * 12) / 2, 100);
  cv.print(t);
  cv.setTextSize(1);
  cv.setCursor(118, 124);
  cv.printf("LAYER %d -> %d", travelDepth, diveTargetDepth);
}

static void drawStation() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Contract &c = sm::contract();
  sm::Pilot &p = sm::sheet();
  cv.fillRect(8, 4, 304, 206, rgb(5, 10, 16));
  cv.drawRoundRect(8, 4, 304, 206, 8, rgb(70, 150, 255));
  cv.setTextSize(2);
  cv.setTextColor(rgb(100, 180, 255)); cv.setCursor(18, 10); cv.print(stationName);
  cv.setTextSize(1);
  cv.setTextColor(rgb(140, 150, 165)); cv.setCursor(176, 10);
  cv.printf("$%ld  H%d/%d", (long)p.credits, p.hull, p.hullMax);
  cv.setCursor(176, 20);
  cv.printf("F%d/%d  hold %u/%u", p.fuel, p.fuelCap, p.holdUsed, p.holdCap);
  char mood[48]; strncpy(mood, stationMoodText, 46); mood[46] = 0;
  cv.setTextColor(rgb(110, 125, 140)); cv.setCursor(18, 32); cv.print(mood);

  char rows[STATION_ROWS][56];
  int rc = refuelCost();
  if (rc > 0) snprintf(rows[0], 56, "REFUEL / REPAIR   %dcr", rc);
  else snprintf(rows[0], 56, "REFUEL / REPAIR   topped up");
  if (c.live) snprintf(rows[1], 56, "DROP LEAD: %s", c.title);
  else snprintf(rows[1], 56, "WORK: %s  +%dcr", off.title, off.pay);
  snprintf(rows[2], 56, "BUY RUMOR   %dcr", rumorPrice());
  int sell = sellableValue(false);
  if (sell > 0) snprintf(rows[3], 56, "SELL HAUL   +%dcr", sell);
  else snprintf(rows[3], 56, "BUY %s %u   %dcr", sm::capName((sm::CapId)gearCap),
                sm::capTier((sm::CapId)gearCap) + 1, gearPrice());
  if (opportunityTaken) snprintf(rows[4], 56, "BOARD: (taken)");
  else snprintf(rows[4], 56, "BOARD: %s +%d", stationOpportunity.title, stationOpportunity.reward);
  snprintf(rows[5], 56, "UNDOCK");

  for (int i = 0; i < STATION_ROWS; ++i) {
    int y = ROW_Y0 + i * ROW_PITCH;
    bool sel = i == stationChoice;
    cv.fillRoundRect(18, y, 284, ROW_H, 4, sel ? rgb(25, 90, 90) : rgb(16, 24, 32));
    cv.setTextColor(sel ? rgb(120, 255, 210) : rgb(180, 190, 200));
    cv.setCursor(28, y + 6);
    cv.print(rows[i]);
  }

  // detail line for the selected row
  const char *detail = "";
  char dbuf[112] = "";
  switch (stationChoice) {
    case 0: snprintf(dbuf, sizeof(dbuf), "fuel %dcr/u  hull %dcr/u", sm::fuelPrice(), sm::repairPrice()); detail = dbuf; break;
    case 1: {
      const sm::Contract &j = c.live ? c : off;
      if (j.dest[0]) snprintf(dbuf, sizeof(dbuf), "gate: %s (d%u)", j.dest, j.destDepth + 1);
      else snprintf(dbuf, sizeof(dbuf), "%s", sm::contractHint(j));
      detail = dbuf; break;
    }
    case 2: asciiCopy(dbuf, sizeof(dbuf), sm::marketRumor(sm::worldPressure(), sm::worldTick())); detail = dbuf; break;
    case 3: detail = sell > 0 ? "hold lines not owned by your lead" : "earned capability is equipped at once"; break;
    case 4: asciiCopy(dbuf, sizeof(dbuf), opportunityTaken ? "" : stationOpportunity.detail); detail = dbuf; break;
    case 5: detail = "back to the gates"; break;
  }
  cv.setTextColor(rgb(200, 180, 110));
  printWrapped(18, ROW_Y0 + STATION_ROWS * ROW_PITCH + 2, 46, 2, 10, detail);
}

static void drawEnding() {
  cv.fillRect(20, 40, 280, 150, rgb(4, 4, 10));
  cv.drawRoundRect(20, 40, 280, 150, 8, rgb(220, 190, 90));
  cv.setTextSize(2);
  cv.setTextColor(rgb(240, 215, 120));
  cv.setCursor(52, 56);
  cv.print("THE DEEP IS SMALL");
  cv.setTextSize(1);
  cv.setTextColor(rgb(200, 205, 215));
  static const char *lines[] = {
    "Every fixed point has a name now.",
    "The skies above keep changing. These do not.",
    "Whatever you lost up there, down here",
    "the roads remember you.",
  };
  for (int i = 0; i < 4; i++) { cv.setCursor(34, 88 + i * 14); cv.print(lines[i]); }
  cv.setTextColor(rgb(120, 130, 150));
  cv.setCursor(34, 164);
  cv.printf("lives %lu   tap to keep flying", (unsigned long)sm::sheet().lives);
}

static void draw() {
  cv.fillSprite(rgb(2, 4, 8));
  if (diving) { drawDive(); drawBanner(); cv.pushSprite(0, 0); return; }
  drawSky();
  // painter's algorithm: far gates first
  int order[8];
  for (int i = 0; i < gateCount; i++) order[i] = i;
  for (int i = 0; i < gateCount; i++)
    for (int j = i + 1; j < gateCount; j++)
      if (gates[order[j]].z > gates[order[i]].z) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int i = 0; i < gateCount; i++) drawGate(gates[order[i]], order[i] == nearGateIdx && gateNear);
  if (!stationOpen) drawReticle();
  drawEncounter();
  drawHudTop();
  if (stationOpen) drawStation();
  drawBanner();
  if (endingOpen) drawEnding();
  cv.pushSprite(0, 0);
}

// ============================================================
//  Arduino entry points
// ============================================================
void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;
  cfg.internal_imu = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(90);
  cv.setColorDepth(16);
  cv.createSprite(W, H);

  sm::sheetInit();
  sm::contractsInit();
  sm::simInit();
  if (sm::sheetLoad()) {
    sm::universeRestoreSeed(sm::sheet().universeSeed);
    sm::contractsLoad();
    setBanner("SHEET RESTORED - STILL LOST", 2500);
  } else setBanner("LOST IN SPACE - FIND A GATE", 3000);
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed() ? sm::universeSeed() : 0xA341316Cu;
  travelDepth = 1;
  classifyDepth();
  generateGates();
  encounterCooldown = millis() + 4000;
}

void loop() {
  uint32_t now = millis();
  static uint32_t prev = now;
  dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f);
  prev = now;
  tNow += dt;
  updateInput();
  combatTick(dt);
  updateWorld();
  draw();
  delay(8);
}
