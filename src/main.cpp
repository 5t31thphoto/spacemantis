// ============================================================
//  SpaceMantis — flight surface over the spreadsheet soul
//  "Elite Dangerous on a flippin ESP32" — career, not cartography.
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "sheet.h"
#include "universe.h"
#include "resolve.h"
#include "contracts.h"
#include "sim.h"
#include "content.h"
#include "lore.h"

static constexpr int W = 320, H = 240;

struct Gate {
  float x, y, z;
  int depth;
  bool known, station, persistent;
  char label[22];
};

static M5Canvas cv(&M5.Display);

static float tNow = 0, dt = 0.016f;
static float yaw = 0, roll = 0, steerX = 0, steerY = 0;
static float neutralAx = 0, neutralAy = 0, neutralAz = 1;
static bool neutralValid = false;
static float speed = 0.58f;
static int travelDepth = 1, depthBand = 0;
static int encounter = 0, encounterKind = 0;
static float encounterZ = 9.f, encounterX = 0, encounterY = 0;
static Gate gates[8];
static int gateCount = 0, gateChain = 0;
static bool diving = false;
static float diveT = 0;
static int diveTargetDepth = 1;
static bool stationOpen = false;
static int stationChoice = 0;
static uint32_t bannerUntil = 0;
static char banner[56] = "LOST IN SPACE";
static uint32_t lastGateGen = 0, encounterCooldown = 0, lastSave = 0;
static bool dragActive = false;
static int dragX = 160, dragY = 120;
static uint32_t rngState = 0xA341316Cu;
static uint32_t livesSeen = 0;
static uint32_t lastSim = 0;
static bool gateNear = false;
static int nearGateIdx = -1;
static int opportunityPulse = 0;
static sm::Opportunity stationOpportunity{};

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
static bool combatPendingAttack = false;
static bool combatActive = false;
static bool mineActive = false;
static float shipPhase = 0;     // bob/roll of contact silhouette
static int contactHullVis = 10; // visual-only for multi-volley feel

static void fxClear() {
  boltN = 0; fxKind = FX_NONE; fxT = 0; combatActive = false; mineActive = false;
}
static void fxAddBolt(float x0, float y0, float x1, float y1, uint16_t col) {
  if (boltN >= MAX_BOLTS) return;
  bolts[boltN++] = {x0, y0, x1, y1, 8, col};
}
static void fxService(float dt) {
  shipPhase += dt * 2.2f;
  for (int i = 0; i < boltN; ) {
    if (bolts[i].life > 0) bolts[i].life--;
    if (bolts[i].life == 0) {
      bolts[i] = bolts[--boltN];
    } else i++;
  }
  if (fxKind != FX_NONE) {
    fxT += dt * (fxKind == FX_BOOM ? 1.8f : 2.5f);
    if (fxT >= 1.f) { fxKind = FX_NONE; fxT = 0; }
  }
}


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
  float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
  float R = 0, G = 0, B = 0;
  if (h < 60) { R = c; G = x; } else if (h < 120) { R = x; G = c; }
  else if (h < 180) { G = c; B = x; } else if (h < 240) { G = x; B = c; }
  else if (h < 300) { R = x; B = c; } else { R = c; B = x; }
  return rgb((uint8_t)((R + m) * 255), (uint8_t)((G + m) * 255), (uint8_t)((B + m) * 255));
}
static void haptic(uint8_t l, uint16_t ms) { M5.Power.setVibration(l); delay(ms); M5.Power.setVibration(0); }
static void setBanner(const char *s, uint32_t ms = 2200) {
  strncpy(banner, s, sizeof(banner) - 1); banner[sizeof(banner) - 1] = 0; bannerUntil = millis() + ms;
}

static void classifyDepth() {
  if (travelDepth <= 1) depthBand = 0;
  else if (travelDepth == 2) depthBand = 1;
  else if (travelDepth == 3) depthBand = 2;
  else if (travelDepth <= 5) depthBand = 3;
  else depthBand = 4;
}
static const char *depthName() {
  static const char *n[] = {"REAL SPACE", "SHALLOW SUBSPACE", "DEEP SUBSPACE", "DEEPER", "DEEPEST"};
  return n[depthBand < 5 ? depthBand : 4];
}

static void generateGates() {
  sm::GateOffer hand[sm::MAX_GATE_HAND];
  int n = sm::buildGateHand((uint8_t)depthBand, hand, sm::MAX_GATE_HAND);
  if (n < 3) n = 3; if (n > 6) n = 6;
  gateCount = n;
  // if active contract has a dest, force one gate to match
  sm::Contract &c = sm::contract();
  for (int i = 0; i < gateCount; i++) {
    Gate &g = gates[i];
    g.x = rf(-1.45f, 1.45f); g.y = rf(-0.82f, 0.82f); g.z = rf(4.2f, 7.8f);
    g.depth = hand[i].depthRating + 1; if (g.depth < 1) g.depth = 1;
    g.known = !hand[i].unknown;
    g.persistent = hand[i].persistent;
    g.station = (i == 0 && g.known && depthBand == 0 && sm::urand() % 100 < 48);
    strncpy(g.label, hand[i].name, sizeof(g.label) - 1);
    g.label[sizeof(g.label) - 1] = 0;
  }
  if (c.live && c.dest[0] && strcmp(c.dest, "ANYWHERE") != 0 && gateCount > 1) {
    strncpy(gates[1].label, c.dest, sizeof(gates[1].label) - 1);
    gates[1].known = true;
    gates[1].depth = c.destDepth + 1;
    gates[1].station = false;
  }
  // Once under the sky, one local gate always represents the way back up.
  // It is a gate, not a return button: the pilot still has to thread it.
  if (depthBand > 0 && gateCount > 0) {
    Gate &up = gates[gateCount - 1];
    strncpy(up.label, "RESURFACE", sizeof(up.label) - 1);
    up.label[sizeof(up.label) - 1] = 0;
    up.known = true;
    up.station = false;
    up.persistent = false;
    up.depth = travelDepth - 1;
  }
  lastGateGen = millis();
}

static void spawnEncounter() {
  if (millis() < encounterCooldown || encounter || stationOpen || diving) return;
  uint8_t w[sm::ENC_COUNT];
  sm::encounterWeights((uint8_t)depthBand, w);
  int sum = 0; for (int i = 0; i < sm::ENC_COUNT; i++) sum += w[i];
  if (sum <= 0 || (int)(rnd() % 100) > 28) return;
  int pick = (int)(rnd() % sum), acc = 0, kind = 0;
  for (int i = 0; i < sm::ENC_COUNT; i++) { acc += w[i]; if (pick < acc) { kind = i; break; } }
  encounterKind = kind;
  encounter = 1;
  if (kind == sm::ENC_LANDMARK_ROCK || kind == sm::ENC_LANDMARK_GIANT || kind == sm::ENC_WRECK || kind == sm::ENC_ARTIFACT) encounter = 2;
  else if (kind == sm::ENC_STATION) encounter = 3;
  else if (kind == sm::ENC_ANOMALY || kind == sm::ENC_HOSTILE || kind == sm::ENC_ESCAPE_POD) encounter = 4;
  encounterZ = 8.f; encounterX = rf(-1.2f, 1.2f); encounterY = rf(-0.7f, 0.7f);
  const sm::StoryBeat &beat = sm::storyBeat((uint8_t)depthBand, sm::urand());
  if ((rnd() & 3u) == 0u) setBanner(beat.text, 2600);
}

static void onDestroyedFlow() {
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed() ^ 0x9E3779B9u;
  sm::contractAbandon();
  setBanner(sm::lossLine(sm::sheet().lives, sm::urand()), 4200);
  // Diegetic: pilot thinks they are lost — not that a cosmos was replaced.

  encounter = 0; gateChain = 0; diving = false; stationOpen = false;
  travelDepth = 1; classifyDepth(); generateGates();
  haptic(200, 80);
}

static void checkDestroy() {
  if (sm::sheet().lives > livesSeen) onDestroyedFlow();
  livesSeen = sm::sheet().lives;
}

static void damage(int amount) {
  if (amount > 0) sm::damageHull((uint16_t)amount);
  checkDestroy();
}

static void startDive(int targetDepth) {
  // travelDepth is a layer count: 1 is real space, 2 shallow, 3 deep...
  // Gates never demand a nonexistent negative layer.
  targetDepth = (int)clampf((float)targetDepth, 1.f, (float)sm::DEPTH_BAND_COUNT);
  if (targetDepth == travelDepth) {
    setBanner("THE GATE OPENS INTO THIS LAYER", 1200);
    return;
  }
  auto da = sm::depthQuery((uint8_t)clampf((float)targetDepth - 1, 0, sm::DEPTH_BAND_COUNT - 1));
  if (targetDepth - 1 > (int)da.maxBand) {
    char risk[48];
    snprintf(risk, sizeof(risk), "PUSHING PAST RATING — GLITCH %.0f%%", da.glitchRisk * 100.f);
    setBanner(risk, 1400);

    if (sm::urandf() < da.glitchRisk) {
      damage(ri(10, 28));
      setBanner("GLITCH — REALITY SLIPS", 2200);
      haptic(180, 60);
      if (sm::sheet().hull < 15) return;
    } else {
      setBanner("HULL COMPLAINS — PUSHING ANYWAY", 1600);
    }
  }
  diving = true; diveT = 0; diveTargetDepth = targetDepth;
  gateChain = 0; stationOpen = false;
  setBanner(diveTargetDepth > travelDepth ? "DIVING UNDER" : "CLIMBING TOWARD REAL", 1500);
  haptic(110, 45);
}

static void finishDive() {
  diving = false;
  int prevBand = depthBand;
  travelDepth = diveTargetDepth;
  classifyDepth();
  uint16_t diveFuel = (uint16_t)clampf(4.f + fabsf((float)diveTargetDepth - travelDepth) * 3.f, 4.f, 24.f);
  if (!sm::burnFuel(diveFuel)) {
    setBanner("LOW FUEL — THE DIVE STALLED", 1800);
    diving = false;
    generateGates();
    return;
  }
  if (depthBand == 0 && prevBand > 0) {
    sm::onResurface();
    setBanner("RESURFACE — FAR. SUB-PIRATES MAY HAVE NOTICED.", 2800);
  } else if (depthBand >= 3) {
    setBanner("DEEP WATER — NAMES GET STICKIER HERE", 2400);
    sm::grantXp(sm::CR_DEPTHRUNNER, 6);
  } else {
    setBanner(depthBand > 0 ? "UNDER THE SKY" : "REAL SPACE", 1600);
  }
  encounter = 0; encounterCooldown = millis() + 1200;
  generateGates();
  haptic(150, 40);
  sm::sheetSave();
}

static void dock() {
  stationOpen = true; stationChoice = 0;
  sm::contractOffer();
  sm::makeOpportunity(stationOpportunity, (uint8_t)depthBand);
  opportunityPulse = 0;
  setBanner(sm::stationMood((uint8_t)(sm::urand() % 5), sm::worldPressure(), sm::urand()), 2200);
  haptic(90, 35);
  setBanner("DOCKED", 900);
}

static void stationCommit() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Pilot &p = sm::sheet();
  switch (stationChoice) {
    case 0: {
      int fp = sm::fuelPrice();
      int rp = sm::repairPrice();
      int needFuel = p.fuelCap - p.fuel;
      int needHull = p.hullMax - p.hull;
      int cost = needFuel * fp + needHull * rp;
      if (cost <= 0) { setBanner("ALREADY TOPPED UP", 1200); break; }
      if (sm::spendCredits(cost)) {
        sm::setFuel(p.fuelCap); sm::repairHull(p.hullMax);
        setBanner("REFUELED / REPAIRED", 1600);
      } else {
        // Stations will still sell a partial refill; the player never gets trapped by UI.
        int partial = p.credits / (fp > 0 ? fp : 1);
        if (partial > 0) {
          uint16_t f = (uint16_t)(p.fuel + partial);
          sm::setFuel(f);
          sm::spendCredits(partial * fp);
          setBanner("PARTIAL REFUEL — CREDIT THIN", 1700);
        } else setBanner("TOO BROKE FOR THE DOCK", 1400);
      }
      break;
    }
    case 1:
      if (sm::contract().live) {
        sm::contractAbandon();
        setBanner("CONTRACT DROPPED", 1400);
      } else {
        int beforeHold = p.holdUsed;
        sm::contractAccept(off);
        if (sm::contract().live) {
          char buf[56]; snprintf(buf, sizeof(buf), "ACCEPTED: %s", off.title);
          setBanner(buf, 2000);
        } else if (p.holdUsed == beforeHold) {
          setBanner("NO ROOM — JOB LEFT ON THE BOARD", 1800);
        }
      }
      break;
    case 2: {
      // A rumor is a lead, not a quest object. Buying one simply makes a label
      // eligible to appear among the local gates.
      sm::GateOffer tmp[2];
      int n = sm::buildGateHand((uint8_t)depthBand, tmp, 2);
      if (n > 0) {
        int price = 12 + depthBand * 9;
        if (sm::spendCredits(price)) {
          sm::rumorAdd(tmp[(n > 1) ? 1 : 0].name, tmp[(n > 1) ? 1 : 0].depthRating, (uint8_t)(10 + depthBand * 3));
          sm::grantXp(sm::CR_TRADER, 4);
          char buf[56]; snprintf(buf, sizeof(buf), "RUMOR BOUGHT: %s", tmp[(n > 1) ? 1 : 0].name);
          setBanner(buf, 2100);
        } else setBanner("THE RUMOR SELLER WANTS MONEY", 1500);
      }
      break;
    }
    case 3: {
      // Opportunistic market/gear surface. No loadout screen exists.
      if (p.haulN) {
        int pay = 0;
        for (uint8_t i = 0; i < p.haulN; ++i)
          pay += p.haul[i].amount * sm::marketPrice(p.haul[i].what, p.haul[i].legal != 0);
        sm::addCredits(pay);
        sm::grantXp(sm::CR_TRADER, (uint16_t)(6 + p.haulN * 2));
        sm::haulClear();
        char buf[48]; snprintf(buf, sizeof(buf), "HAUL SOLD +%dcr", pay);
        setBanner(buf, 1900);
      } else {
        int id = (int)(sm::urand() % sm::CAP_COUNT);
        // Station gear favors careers the pilot is actually pursuing.
        if (p.rank[sm::CR_DEPTHRUNNER] >= 4 && (sm::urand() % 100) < 35) id = sm::CAP_STABILIZER;
        uint8_t t = sm::capTier((sm::CapId)id);
        int cost = 80 + t * 55 + depthBand * 30;
        if (sm::spendCredits(cost)) {
          sm::earnCap((sm::CapId)id, (uint8_t)(t + 1));
          char buf[48]; snprintf(buf, sizeof(buf), "%s +1 — EQUIPPED", sm::capName((sm::CapId)id));
          setBanner(buf, 1900);
        } else setBanner("GEAR IS TOO EXPENSIVE HERE", 1500);
      }
      break;
    }
    case 4:
      if (sm::makeOpportunity(stationOpportunity, (uint8_t)depthBand)) {
        sm::applyOpportunity(stationOpportunity);
        setBanner("OPPORTUNITY COMMITTED", 1800);
      }
      break;
  }
  generateGates();
  sm::sheetSave();
}


static void finishEncounterResolve(bool attack) {
  sm::EncounterClass who = (sm::EncounterClass)encounterKind;
  if (who >= sm::ENC_COUNT) who = sm::ENC_TRAVELER;
  sm::ResolveIn in{attack ? sm::VERB_ATTACK : sm::VERB_HAIL, who, (uint8_t)depthBand,
                   (uint8_t)(2 + depthBand + (encounterKind % 4))};
  sm::ResolveOut out = sm::resolve(in);
  setBanner(out.blurb ? out.blurb : "...", 2400);
  if (attack && out.destroyedOther) {
    fxKind = FX_BOOM; fxT = 0;
    haptic(160, 40);
  }
  checkDestroy();
  encounter = 0;
  combatActive = false;
  mineActive = false;
  encounterCooldown = millis() + 2000;
  sm::sheetSave();
}

static void encounterAction(bool attack) {
  if (combatActive || mineActive) return;
  sm::EncounterClass who = (sm::EncounterClass)encounterKind;
  bool isRock = (who == sm::ENC_LANDMARK_ROCK);
  bool isGiant = (who == sm::ENC_LANDMARK_GIANT);
  bool isShip = !isRock && !isGiant && who != sm::ENC_STATION && who != sm::ENC_ARTIFACT && who != sm::ENC_WRECK;

  // mining / scoop theater
  if (!attack && (isRock || isGiant)) {
    mineActive = true;
    fxKind = isGiant ? FX_SCOOP : FX_MINE;
    fxT = 0;
    combatVolleys = 0;
    combatMaxVolley = (uint8_t)(3 + sm::capTier(sm::CAP_MINING));
    contactHullVis = 10;
    haptic(70, 25);
    setBanner(isGiant ? "SCOOPING VOLATILES..." : "CUTTERS ENGAGED...", 1200);
    return;
  }

  // ship battle theater
  if (attack && isShip) {
    combatActive = true;
    combatPendingAttack = true;
    combatVolleys = 0;
    combatMaxVolley = (uint8_t)(3 + sm::capTier(sm::CAP_WEAPONS) / 2);
    if (combatMaxVolley > 6) combatMaxVolley = 6;
    contactHullVis = 10;
    fxKind = FX_LASER;
    fxT = 0;
    setBanner("WEAPONS FREE", 900);
    haptic(100, 30);
    return;
  }

  // hail / other: resolve after a short laser-less beat
  if (!attack) {
    setBanner("OPENING COMM...", 600);
  }
  finishEncounterResolve(attack);
}

static void combatTick(float dt) {
  fxService(dt);
  if (!combatActive && !mineActive) return;

  // project contact for bolt aims
  int sx, sy; float sc;
  project(encounterX, encounterY, encounterZ < 0.4f ? 0.4f : encounterZ, sx, sy, sc);

  static float acc = 0;
  acc += dt;
  float beat = mineActive ? 0.22f : 0.30f;
  if (acc < beat) return;
  acc = 0;

  if (mineActive) {
    combatVolleys++;
    fxAddBolt((float)(W / 2), (float)(H - 50), (float)sx + rf(-4, 4), (float)sy + rf(-4, 4), rgb(140, 255, 100));
    haptic(40, 12);
    if (combatVolleys >= combatMaxVolley) {
      mineActive = false;
      finishEncounterResolve(false);
    }
    return;
  }

  if (combatActive) {
    combatVolleys++;
    // player bolt
    fxAddBolt((float)(W / 2), (float)(H - 60), (float)sx + rf(-6, 6), (float)sy + rf(-6, 6),
              rgb(120, 255, 180));
    // return fire
    if ((rnd() % 100) < 55 + depthBand * 5) {
      fxAddBolt((float)sx, (float)sy, (float)(W / 2 + ri(-20, 20)), (float)(H - 50),
                rgb(255, 100, 80));
      fxKind = FX_SHIELD;
      fxT = 0;
      // light visual damage only; real hull resolved at end
      if (contactHullVis > 0) contactHullVis--;
      haptic(50, 20);
    } else {
      if (contactHullVis > 1) contactHullVis -= 2;
      haptic(80, 15);
    }
    if (combatVolleys >= combatMaxVolley) {
      combatActive = false;
      finishEncounterResolve(true);
    }
  }
}


static void updateInput() {
  M5.update();
  auto td = M5.Touch.getDetail();
  if (td.wasPressed()) {
    dragActive = true; dragX = td.x; dragY = td.y;
    if (stationOpen) {
      if (td.y > 70 && td.y < 212) {
        stationChoice = (td.y - 70) / 28;
        if (stationChoice < 0) stationChoice = 0;
        if (stationChoice > 4) stationChoice = 4;
      }
      if (td.y > 205) { stationCommit(); stationOpen = false; neutralValid = false; }
    } else if (encounter) {
      encounterAction(td.x > W / 2);
    }
  }
  if (td.isPressed() && dragActive && !stationOpen && !encounter && !diving) {
    yaw = clampf(yaw + (td.x - dragX) * 0.008f, -1.2f, 1.2f);
    roll = clampf(roll + (td.y - dragY) * 0.008f, -1.0f, 1.0f);
    dragX = td.x; dragY = td.y;
  }
  if (td.wasReleased()) dragActive = false;

  if (M5.BtnA.wasPressed()) {
    if (stationOpen) { stationCommit(); stationOpen = false; neutralValid = false; }
    else if (encounter) encounterAction(false);
  }
  if (M5.BtnB.wasPressed()) {
    if (!neutralValid) {
      if (M5.Imu.update()) {
        auto d = M5.Imu.getImuData();
        neutralAx = d.accel.x; neutralAy = d.accel.y; neutralAz = d.accel.z;
        neutralValid = true;
        setBanner("ATTITUDE CENTERED", 900);
      }
    } else if (stationOpen) { stationCommit(); stationOpen = false; neutralValid = false; }
    else if (encounter) encounterAction(true);
  }
  if (M5.BtnC.wasPressed() && stationOpen) stationChoice = (stationChoice + 1) % 5;

  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    if (!neutralValid) {
      neutralAx = d.accel.x; neutralAy = d.accel.y; neutralAz = d.accel.z;
      neutralValid = true;
    }
    float ax = d.accel.x - neutralAx, ay = d.accel.y - neutralAy;
    steerX = clampf(steerX * 0.85f + (-ay) * 0.15f, -1.5f, 1.5f);
    steerY = clampf(steerY * 0.85f + (ax) * 0.15f, -1.5f, 1.5f);
  }
}

static void updateWorld() {
  sm::simTick(millis());
  sm::contractTick();
  if (stationOpen || encounter) return;
  if (diving) {
    diveT += dt * (0.7f + depthBand * 0.12f);
    if (diveT >= 1.f) finishDive();
    return;
  }

  speed = 0.52f + travelDepth * 0.03f + sm::sheet().cap[sm::CAP_SCANNERS] * 0.01f;
  if (sm::sheet().fuel == 0) speed *= 0.6f;

  for (int i = 0; i < gateCount; i++) {
    Gate &g = gates[i];
    g.x += (steerX * 0.32f + sinf(yaw) * 0.12f) * dt;
    g.y += (steerY * 0.32f + sinf(roll) * 0.1f) * dt;
    g.z -= speed * dt;
    gateNear = false;
    nearGateIdx = -1;
    if (g.z < 0.95f) {
      float lateral = sqrtf(g.x * g.x + g.y * g.y);
      gateNear = lateral < (0.22f + (g.z * 0.035f));
      if (gateNear) nearGateIdx = i;
    }
    if (g.z < 0.55f) {
      if (!gateNear) {
        // Missed gates keep moving away and become a visual memory rather than
        // silently changing the player's destination.
        g.z = rf(4.8f, 7.5f); g.x = rf(-1.45f, 1.45f); g.y = rf(-0.82f, 0.82f);
        continue;
      }
      if (g.station) {
        dock();
        gateChain = 0;
      } else {
        gateChain++;
        if (g.known) sm::knownGateAdd(g.label, (uint8_t)depthBand);
        else sm::grantXp(sm::CR_WANDERER, 8);
        sm::contractOnGate(g.label, (uint8_t)depthBand, !g.known);
        if (g.persistent) {
          const sm::Landmark *lm = sm::landmarkFind(g.label);
          if (lm) {
            bool first = !sm::landmarkDiscovered(lm->id);
            sm::discoverLandmark(lm->id);
            sm::grantXp(sm::CR_DEPTHRUNNER, first ? 18 : 4);
            if (first) setBanner(sm::discoveryLine(lm->band, sm::urand()), 2800);
          }
        }
        if (gateChain >= 3) {
          int target = g.depth;
          // Context decides direction. An explicitly shallower gate resurfaces;
          // a same/deeper gate descends. The gate itself is the commitment.
          if (target >= travelDepth) target = target + 1;
          if (target > sm::DEPTH_BAND_COUNT) target = sm::DEPTH_BAND_COUNT;
          if (target < travelDepth) target = target;
          if (target == travelDepth) target = travelDepth > 1 ? travelDepth - 1 : 2;
          startDive(target);
        } else {
          char buf[48];
          snprintf(buf, sizeof(buf), "GATE %d/3 — %s", gateChain, g.label);
          setBanner(buf, 1200);
        }
      }
      g.z = rf(5.5f, 8.2f);
      g.x = rf(-1.45f, 1.45f);
      g.y = rf(-0.82f, 0.82f);
    }
  }

  spawnEncounter();
  if (encounter) {
    encounterZ -= speed * dt * 0.85f;
    if (encounterZ < 0.55f) {
      encounter = 0;
      encounterCooldown = millis() + 1500;
    }
  }

  if (sm::sheet().fuel > 0 && rnd() % 900 < 2) sm::burnFuel(1);
  if (millis() > lastGateGen + 18000) generateGates();
  if (millis() > lastSave + 20000) { sm::sheetSave(); lastSave = millis(); }
}

static void project(float x, float y, float z, int &sx, int &sy, float &sc) {
  float zz = z < 0.2f ? 0.2f : z;
  sc = 140.f / zz;
  sx = (int)(W * 0.5f + (x + sinf(yaw) * 0.2f) * sc);
  sy = (int)(H * 0.5f + (y + sinf(roll) * 0.2f) * sc);
}

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
  // dragging a flat texture. This is the visual foundation inherited from V23.
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

static void drawGate(const Gate &g, bool aligned) {
  int sx, sy; float sc;
  project(g.x, g.y, g.z, sx, sy, sc);
  int r = (int)clampf(sc * 0.22f, 6.f, 50.f);
  uint16_t col = g.station ? rgb(70, 150, 255)
               : g.persistent ? rgb(220, 190, 90)
               : g.known ? rgb(70, 230, 170)
               : rgb(130, 130, 150);
  int rings = aligned ? 6 : 3;
  for (int k = 0; k < rings; k++) cv.drawCircle(sx, sy, r + k * (aligned ? 2 : 1), col);
  if (aligned) {
    cv.drawCircle(sx, sy, r + 14, rgb(240, 240, 200));
  }
  if (g.station) {
    cv.setTextColor(rgb(120, 180, 255));
    cv.setCursor(sx - 16, sy - 4);
    cv.print("DOCK");
  }
  cv.setTextColor(col);
  int tw = (int)strlen(g.label) * 6;
  cv.setCursor(sx - tw / 2, sy - r - 12);
  cv.printf("%s", g.label);
  cv.setCursor(sx - 18, sy + r + 3);
  cv.printf("d%d%s", g.depth, g.known ? "" : "?");
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
  // cockpit glint
  cv.drawPixel(sx, sy, rgb(220, 240, 255));
}

static void drawRock(int sx, int sy, int r) {
  cv.fillCircle(sx, sy, r, rgb(110, 90, 60));
  cv.fillCircle(sx - r / 3, sy - r / 4, r / 3, rgb(90, 70, 45));
  cv.drawCircle(sx, sy, r, rgb(160, 130, 80));
  // mineral sparkle if mining gear
  if (sm::capTier(sm::CAP_MINING) > 0) {
    cv.drawPixel(sx + r / 2, sy - r / 3, rgb(180, 220, 120));
    cv.drawPixel(sx - r / 4, sy + r / 3, rgb(200, 240, 140));
  }
}

static void drawGiant(int sx, int sy, int r) {
  cv.fillCircle(sx, sy, r, rgb(40, 90, 140));
  cv.fillCircle(sx - r / 4, sy - r / 5, r / 2, rgb(60, 120, 160));
  // bands
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
  cv.fillCircle(sx, sy, 2, rgb(80, 255, 120)); // beacon
}

static void drawWreck(int sx, int sy, int r) {
  cv.drawLine(sx - r, sy - r / 2, sx + r, sy + r / 3, rgb(140, 140, 150));
  cv.drawLine(sx - r / 2, sy + r / 2, sx + r / 2, sy - r / 2, rgb(100, 100, 110));
  cv.fillCircle(sx, sy, r / 4, rgb(60, 60, 70));
}

static void drawBolts() {
  for (int i = 0; i < boltN; i++) {
    auto &b = bolts[i];
    cv.drawLine((int)b.x0, (int)b.y0, (int)b.x1, (int)b.y1, b.col);
    if (b.life > 4) cv.drawLine((int)b.x0 + 1, (int)b.y0, (int)b.x1 + 1, (int)b.y1, rgb(255, 255, 200));
  }
  // player "gun" origin hint when fighting
  if (combatActive) {
    cv.fillTriangle(W / 2 - 6, H - 56, W / 2 + 6, H - 56, W / 2, H - 68, rgb(80, 120, 100));
  }
}

static void drawEncounter() {
  if (!encounter && !combatActive && !mineActive) return;
  int sx, sy; float sc;
  project(encounterX, encounterY, encounterZ < 0.4f ? 0.4f : encounterZ, sx, sy, sc);
  int r = (int)clampf(sc * 0.17f, 6.f, 40.f);
  uint8_t kind = (uint8_t)encounterKind;

  if (encounter == 2 || kind == sm::ENC_LANDMARK_ROCK || kind == sm::ENC_LANDMARK_GIANT) {
    if (kind == sm::ENC_LANDMARK_GIANT) drawGiant(sx, sy, r + 8);
    else if (kind == sm::ENC_WRECK) drawWreck(sx, sy, r);
    else if (kind == sm::ENC_ARTIFACT) {
      cv.drawRect(sx - r / 2, sy - r / 2, r, r, rgb(200, 160, 255));
      cv.drawLine(sx - r, sy, sx + r, sy, rgb(180, 120, 255));
    } else drawRock(sx, sy, r);
  } else if (kind == sm::ENC_ESCAPE_POD) {
    drawPod(sx, sy, r);
  } else if (encounter == 3 || kind == sm::ENC_STATION) {
    cv.drawCircle(sx, sy, r, rgb(70, 150, 255));
    cv.fillRect(sx - r / 2, sy - r / 4, r, r / 2, rgb(30, 60, 100));
  } else {
    uint16_t hull = rgb(200, 200, 210);
    if (kind == sm::ENC_PIRATE || kind == sm::ENC_SUBPIRATE) hull = rgb(180, 70, 70);
    if (kind == sm::ENC_SECURITY) hull = rgb(80, 140, 220);
    if (kind == sm::ENC_HOSTILE) hull = rgb(160, 60, 180);
    if (kind == sm::ENC_MERCHANT) hull = rgb(120, 160, 140);
    drawShipSilhouette(sx, sy, r, kind, hull);
    // vis hull pips during combat
    if (combatActive) {
      for (int i = 0; i < 5; i++) {
        uint16_t c = i < contactHullVis / 2 ? rgb(80, 220, 100) : rgb(50, 50, 50);
        cv.fillRect(sx - 12 + i * 5, sy - r - 8, 4, 3, c);
      }
    }
  }

  // active FX beams
  if (fxKind == FX_MINE || mineActive) {
    cv.drawLine(W / 2, H - 50, sx, sy, rgb(120, 255, 80));
    cv.drawLine(W / 2 - 1, H - 50, sx - 1, sy, rgb(200, 255, 160));
    for (int k = 0; k < 4; k++)
      cv.drawPixel(sx + (int)(cosf(shipPhase * 3 + k) * (r / 2)), sy + (int)(sinf(shipPhase * 3 + k) * (r / 2)), rgb(255, 255, 100));
  }
  if (fxKind == FX_SCOOP) {
    for (int k = 0; k < 5; k++) {
      float u = (fxT + k * 0.15f);
      int px = (int)(sx + (W / 2 - sx) * u);
      int py = (int)(sy + (H - 40 - sy) * u);
      cv.drawPixel(px, py, rgb(140, 210, 255));
    }
  }
  if (fxKind == FX_BOOM) {
    int br = (int)(r * (0.5f + fxT * 2.f));
    cv.drawCircle(sx, sy, br, rgb(255, 180, 60));
    cv.drawCircle(sx, sy, br / 2, rgb(255, 80, 40));
  }

  drawBolts();

  const char *tag = "CONTACT";
  if (kind == sm::ENC_LANDMARK_GIANT) tag = "GAS GIANT";
  else if (kind == sm::ENC_LANDMARK_ROCK) tag = "ASTEROID";
  else if (kind == sm::ENC_WRECK) tag = "WRECK";
  else if (kind == sm::ENC_ARTIFACT) tag = "ARTIFACT";
  else if (kind == sm::ENC_STATION) tag = "STATION";
  else if (kind == sm::ENC_ESCAPE_POD) tag = "ESCAPE POD";
  else if (kind == sm::ENC_PIRATE) tag = "PIRATE";
  else if (kind == sm::ENC_SUBPIRATE) tag = "SUB-PIRATE";
  else if (kind == sm::ENC_SECURITY) tag = "SECURITY";
  else if (kind == sm::ENC_MERCHANT) tag = "MERCHANT";
  else if (kind == sm::ENC_HOSTILE) tag = "HOSTILE";
  else if (kind == sm::ENC_ANOMALY) tag = "ANOMALY";
  else if (kind == sm::ENC_TRAVELER) tag = "TRAVELER";

  cv.setTextColor(rgb(240, 240, 245));
  cv.setCursor(sx - 30, sy + r + 6);
  cv.print(tag);

  if (!combatActive && !mineActive && encounter) {
    const char *left = (encounter == 2 && kind == sm::ENC_LANDMARK_GIANT) ? "SCOOP"
                     : (encounter == 2 && kind == sm::ENC_LANDMARK_ROCK) ? "MINE"
                     : (kind == sm::ENC_ESCAPE_POD) ? "RESCUE" : "HAIL";
    cv.fillRoundRect(16, H - 48, 130, 30, 4, rgb(18, 70, 40));
    cv.fillRoundRect(174, H - 48, 130, 30, 4, rgb(90, 22, 32));
    cv.setTextColor(rgb(140, 255, 180)); cv.setCursor(40, H - 38); cv.print(left);
    cv.setTextColor(rgb(255, 140, 140)); cv.setCursor(208, H - 38); cv.print("ATTACK");
  } else if (combatActive) {
    cv.setTextColor(rgb(255, 200, 120));
    cv.setCursor(100, H - 40);
    cv.printf("ENGAGED %u/%u", combatVolleys, combatMaxVolley);
  } else if (mineActive) {
    cv.setTextColor(rgb(160, 255, 140));
    cv.setCursor(110, H - 40);
    cv.print(encounterKind == sm::ENC_LANDMARK_GIANT ? "SCOOPING..." : "MINING...");
  }
}


static void drawHud() {
  sm::Pilot &p = sm::sheet();
  cv.setTextSize(1);
  cv.setTextColor(rgb(80, 210, 200));
  cv.setCursor(4, 4);
  cv.printf("%s", depthName());
  cv.setTextColor(rgb(200, 210, 220));
  cv.setCursor(168, 4);
  cv.printf("H%d F%d $%ld", p.hull, p.fuel, (long)p.credits);

  cv.setTextColor(rgb(100, 120, 130));
  cv.setCursor(4, 16);
  cv.printf("life %lu  rate %u  chain %d/3", (unsigned long)p.lives, sm::depthRating(), gateChain);
  cv.setCursor(214, 16);
  cv.printf("P%u", sm::worldPressure());

  // haul / contract line
  sm::Contract &c = sm::contract();
  if (c.live) {
    cv.setTextColor(rgb(230, 200, 100));
    cv.setCursor(4, 28);
    cv.printf("JOB %s %u/%u", c.title, c.progress, c.need);
  } else if (p.haulN) {
    cv.setTextColor(rgb(180, 160, 120));
    cv.setCursor(4, 28);
    cv.printf("HOLD %s x%u%s", p.haul[0].what, p.haul[0].amount, p.haul[0].legal ? "" : " HOT");
  }

  if (bannerUntil > millis()) {
    cv.setTextColor(rgb(240, 245, 250));
    cv.setCursor(6, 224);
    cv.printf("%s", banner);
  } else {
    cv.setTextColor(rgb(70, 100, 110));
    cv.setCursor(6, 224);
    cv.printf("known %u  rumor %u  slide=yaw/roll", sm::knownGateCount(), sm::rumorCount());
  }
}

static void drawDive() {
  float u = clampf(diveT, 0, 1);
  float alien = clampf(diveTargetDepth / 6.f, 0, 1);
  for (int y = 0; y < H; y += 4)
    for (int x = 0; x < W; x += 4) {
      float dx = x - 160.f, dy = y - 120.f;
      float r = sqrtf(dx * dx + dy * dy) / 180.f;
      float wave = sinf(r * 22.f - u * 28.f + tNow * 4.f);
      cv.fillRect(x, y, 4, 4, hsv(195 + alien * 160 + wave * 18, 0.4f + alien * 0.4f, 0.02f + r * 0.1f + u * 0.14f));
    }
  cv.setTextSize(2);
  cv.setTextColor(rgb(230, 245, 240));
  cv.setCursor(96, 100);
  cv.print(diveTargetDepth > travelDepth ? "DIVING" : "SURFACING");
  cv.setTextSize(1);
  cv.setCursor(110, 124);
  cv.printf("DEPTH %d → %d", travelDepth, diveTargetDepth);
}

static void drawStation() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Contract &c = sm::contract();
  sm::Pilot &p = sm::sheet();
  cv.fillRect(10, 24, 300, 205, rgb(5, 10, 16));
  cv.drawRoundRect(10, 24, 300, 205, 8, rgb(70, 150, 255));
  cv.setTextSize(2);
  cv.setTextColor(rgb(100, 180, 255)); cv.setCursor(24, 32); cv.print("STATION");
  cv.setTextSize(1);
  cv.setTextColor(rgb(140, 150, 165)); cv.setCursor(170, 37);
  cv.printf("$%ld  H%d/%d F%d/%d", (long)p.credits, p.hull, p.hullMax, p.fuel, p.fuelCap);

  const char *opts[5] = {
    "REFUEL / REPAIR", c.live ? "DROP / ACTIVE WORK" : "ACCEPT WORK",
    "BUY RUMOR", p.haulN ? "SELL HAUL" : "BUY GEAR", "COMMIT OPPORTUNITY"
  };
  for (int i = 0; i < 5; ++i) {
    int y = 62 + i * 29;
    cv.fillRoundRect(24, y, 272, 25, 4, i == stationChoice ? rgb(25, 90, 90) : rgb(16, 24, 32));
    cv.setTextColor(i == stationChoice ? rgb(120, 255, 210) : rgb(180, 190, 200));
    cv.setCursor(36, y + 8); cv.print(opts[i]);
  }
  cv.setTextColor(rgb(130, 145, 160)); cv.setCursor(24, 210);
  cv.printf("offer %s +%dcr   C cycle  B commit", off.title, off.pay);
  if (stationChoice == 4 && stationOpportunity.title[0]) {
    cv.setTextColor(rgb(225, 200, 110));
    cv.setCursor(24, 224);
    cv.printf("%s", stationOpportunity.title);
  } else if (stationChoice == 2) {
    cv.setTextColor(rgb(145, 165, 175));
    cv.setCursor(24, 224);
    cv.printf("%s", sm::marketRumor(sm::worldPressure(), sm::worldTick()));
  }
}

static void draw() {
  cv.fillSprite(rgb(2, 4, 8));
  if (diving) { drawDive(); cv.pushSprite(0, 0); return; }
  drawSky();
  // painter's algorithm: far gates first
  int order[8];
  for (int i = 0; i < gateCount; i++) order[i] = i;
  for (int i = 0; i < gateCount; i++)
    for (int j = i + 1; j < gateCount; j++)
      if (gates[order[j]].z > gates[order[i]].z) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int i = 0; i < gateCount; i++) drawGate(gates[order[i]], order[i] == nearGateIdx && gateNear);
  drawEncounter();
  drawHud();
  if (stationOpen) drawStation();
  cv.pushSprite(0, 0);
}

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
    setBanner("SHEET RESTORED — STILL LOST", 2500);
  } else setBanner("LOST IN SPACE — FIND A GATE", 3000);
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed();
  travelDepth = 1;
  classifyDepth();
  generateGates();
}

void loop() {
  uint32_t now = millis();
  static uint32_t prev = now;
  dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f);
  prev = now;
  tNow += dt;
  updateInput();
  updateWorld();
  draw();
  delay(8);
}
