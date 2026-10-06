#include "contracts.h"
#include "sheet.h"
#include "universe.h"
#include "sim.h"
#include "content.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

namespace sm {
namespace {
Contract s_c{};
Contract s_offer{};
Contract s_done{};
bool s_doneFresh = false;
uint32_t s_lastHeatTick = 0;
char s_here[NAME_LEN] = "";   // where the pilot is now: never a destination
struct LaneCand { char name[NAME_LEN]; uint8_t depth, visited; };
LaneCand s_lanes[8];          // gates in this system a job may point at
uint8_t s_band = 0;           // the layer the pilot is in
int s_laneN = 0;

const char *kindTitle(ContractKind k) {
  switch (k) {
    case CK_HAUL: return "HAUL JOB";
    case CK_BOUNTY: return "BOUNTY";
    case CK_RESCUE: return "DISTRESS";
    case CK_SURVEY: return "SURVEY";
    case CK_SMUGGLE: return "QUIET RUN";
    case CK_TOUR: return "TOUR";
    case CK_SUPPLY: return "OUTPOST SUPPLY";
    case CK_ESCORT: return "ESCORT";
    case CK_MARKET: return "MARKET RUN";
    case CK_GHOST: return "GHOST THE PATROL";
    case CK_DEPTHRUN: return "DEPTH RUN";
    case CK_LANDMARK: return "LANDMARK WATCH";
    case CK_DEEPRESCUE: return "PODS LOST BELOW";
    case CK_DEEPSCAN: return "DEEP SCAN";
    default: return "-";
  }
}

bool isCargo(ContractKind k) {
  return k == CK_HAUL || k == CK_SMUGGLE || k == CK_SUPPLY || k == CK_MARKET;
}
bool hasDest(ContractKind k) { return isCargo(k) || k == CK_DEPTHRUN; }

bool holdIsHot() {
  const Pilot &p = sheet();
  for (uint8_t i = 0; i < p.haulN; i++) if (!p.haul[i].legal) return true;
  return false;
}

bool roll(Contract &o, ContractKind k) {
  memset(&o, 0, sizeof(o));
  if (k == CK_NONE) {
    static const ContractKind pool[] = {
      CK_HAUL, CK_BOUNTY, CK_RESCUE, CK_SURVEY, CK_SMUGGLE,
      CK_TOUR, CK_SUPPLY, CK_ESCORT, CK_MARKET
    };
    int weights[9] = {
      18 + rankOf(CR_HAULER) * 3,
      12 + rankOf(CR_GUNHAND) * 3,
      12 + rankOf(CR_RESCUER) * 4,
      10 + rankOf(CR_WANDERER) * 3 + rankOf(CR_DEPTHRUNNER) * 2,
      7 + rankOf(CR_GHOST) * 3,
      7 + rankOf(CR_TRADER) * 3,
      8 + rankOf(CR_HAULER) * 2,
      7 + rankOf(CR_RESCUER) * 2,
      9 + rankOf(CR_TRADER) * 4
    };
    int sum = 0; for (int i = 0; i < 9; ++i) sum += weights[i];
    int pick = (int)(urand() % (uint32_t)sum), acc = 0;
    k = CK_HAUL;
    for (int i = 0; i < 9; ++i) { acc += weights[i]; if (pick < acc) { k = pool[i]; break; } }
  }
  o.kind = k;
  strncpy(o.title, kindTitle(k), sizeof(o.title) - 1);

  if (hasDest(k)) {
    // A job never makes a gate: it points at one already in this system,
    // somewhere not yet been if there is one. A depth run needs a deep lane.
    int pick = -1, seen = 0;
    for (int pass = 0; pass < 2 && pick < 0; pass++)
      for (int i = 0; i < s_laneN; i++) {
        const LaneCand &c = s_lanes[i];
        if ((pass == 0) == (c.visited != 0) || sameName(c.name, s_here)) continue;
        if (k == CK_DEPTHRUN && c.depth < 2) continue;
        if (urand(0, ++seen - 1) == 0) pick = i;
      }
    if (pick < 0) return false;
    strncpy(o.dest, s_lanes[pick].name, NAME_LEN - 1);
    o.destDepth = s_lanes[pick].depth < 1 ? 1 : s_lanes[pick].depth;
  }

  switch (k) {
    case CK_HAUL:     o.pay = (int16_t)(60 + urand(0, 100));  o.xp = 18; o.track = CR_HAULER;      o.need = 1; break;
    case CK_BOUNTY:   o.pay = (int16_t)(90 + urand(0, 130));  o.xp = 22; o.track = CR_GUNHAND;     o.need = 1; break;
    case CK_RESCUE:   o.pay = (int16_t)(70 + urand(0, 80));   o.xp = 20; o.track = CR_RESCUER;     o.need = 1; break;
    case CK_SURVEY:   o.pay = (int16_t)(55 + urand(0, 90));   o.xp = 24; o.track = CR_WANDERER;    o.need = 2; break;
    case CK_SMUGGLE:  o.pay = (int16_t)(120 + urand(0, 140)); o.xp = 20; o.track = CR_GHOST;       o.need = 1; break;
    case CK_TOUR:     o.pay = (int16_t)(80 + urand(0, 100));  o.xp = 24; o.track = CR_TRADER;      o.need = 2; break;
    case CK_SUPPLY:   o.pay = (int16_t)(110 + urand(0, 120)); o.xp = 30; o.track = CR_HAULER;      o.need = 1; break;
    case CK_ESCORT:   o.pay = (int16_t)(95 + urand(0, 120));  o.xp = 28; o.track = CR_RESCUER;     o.need = 3; break;
    case CK_MARKET:   o.pay = (int16_t)(130 + urand(0, 150)); o.xp = 26; o.track = CR_TRADER;      o.need = 1; break;
    case CK_GHOST:    o.pay = (int16_t)(150 + urand(0, 80));  o.xp = 40; o.track = CR_GHOST;       o.need = 1; break;
    case CK_DEPTHRUN: o.pay = (int16_t)(200 + urand(0, 80));  o.xp = 55; o.track = CR_DEPTHRUNNER; o.need = 1; break;
    case CK_LANDMARK: o.pay = (int16_t)(250 + urand(0, 80));  o.xp = 65; o.track = CR_DEPTHRUNNER; o.need = 1; break;
    case CK_DEEPRESCUE: o.pay = (int16_t)(180 + urand(0, 90)); o.xp = 40; o.track = CR_RESCUER;   o.need = 1; break;
    case CK_DEEPSCAN: o.pay = (int16_t)(160 + urand(0, 90));   o.xp = 38; o.track = CR_WANDERER;  o.need = 2; break;
    default: return false;
  }
  // deeper destinations pay for the dive
  if (o.destDepth > 1) o.pay = (int16_t)(o.pay + (o.destDepth - 1) * 45);
  return true;
}

void complete() {
  if (!s_c.live) return;
  addCredits(s_c.pay);
  grantXp(s_c.track, s_c.xp);
  int8_t done = flagGet("job_done");
  flagSet("job_done", (int8_t)(done < 120 ? done + 1 : 120), false);
  if (const char *cargo = contractCargo(s_c.kind)) haulRemove(cargo);
  if (s_c.kind == CK_RESCUE) flagSet("saved_someone", 1, false);
  if (s_c.kind == CK_TOUR) flagSet("tourist", 1, false);
  if (s_c.kind == CK_SUPPLY) flagSet("outpost_help", 1, false);
  if (s_c.kind == CK_MARKET) flagSet("market_runner", 1, false);
  if (s_c.kind == CK_ESCORT) flagSet("escort_done", 1, false);
  if (s_c.kind == CK_SURVEY) flagSet("survey_done", 1, false);
  if (s_c.kind == CK_DEPTHRUN) flagSet("depth_run", 1, true);
  if (s_c.kind == CK_LANDMARK) flagSet("landmark_watch", 1, true);
  s_done = s_c;
  s_done.progress = s_done.need;
  s_done.live = 0;
  s_doneFresh = true;
  memset(&s_c, 0, sizeof(s_c));
}

void step() { if (++s_c.progress >= s_c.need) complete(); }

} // namespace

void contractsInit() {
  memset(&s_c, 0, sizeof(s_c));
  memset(&s_offer, 0, sizeof(s_offer));
  memset(&s_done, 0, sizeof(s_done));
  s_doneFresh = false;
}

Contract &contract() { return s_c; }

void contractSetHere(const char *place) {
  strncpy(s_here, place ? place : "", NAME_LEN - 1);
  s_here[NAME_LEN - 1] = 0;
}
bool contractOffer(ContractKind prefer) {
  // a delivery with nowhere to go here is not offered; something else is
  for (int tries = 0; tries < 12; tries++) if (roll(s_offer, prefer)) return true;
  return roll(s_offer, CK_BOUNTY);
}

void contractSetBand(uint8_t band) { s_band = band; }
bool contractOfferDeep() { return roll(s_offer, (urand() & 1u) ? CK_DEEPRESCUE : CK_DEEPSCAN); }

void contractSetLanes(const char *const *names, const uint8_t *depths, const uint8_t *visited, int n) {
  s_laneN = 0;
  for (int i = 0; i < n && s_laneN < 8; i++) {
    LaneCand &c = s_lanes[s_laneN++];
    strncpy(c.name, names[i], NAME_LEN - 1); c.name[NAME_LEN - 1] = 0;
    c.depth = depths[i]; c.visited = visited[i];
  }
}

bool contractDueHere(const char *place) { return s_c.live && hasDest(s_c.kind) && sameName(s_c.dest, place); }

bool contractDeliverHere(const char *place) {
  if (!contractDueHere(place)) return false;
  s_c.progress = s_c.need;
  complete();
  return true;
}

bool contractAccept(const Contract &offer) {
  if (offer.kind == CK_NONE || s_c.live) return false;
  Contract c = offer;
  c.live = 1;
  c.progress = 0;
  // Cargo is acquired when the pilot commits; the offer roll has no side effects.
  if (c.kind == CK_HAUL) {
    if (!haulAdd("crate", (uint16_t)(3 + rankOf(CR_HAULER) / 3), true)) return false;
  } else if (c.kind == CK_SUPPLY) {
    if (!haulAdd("machine parts", (uint16_t)(2 + rankOf(CR_HAULER) / 4), true)) return false;
  } else if (c.kind == CK_SMUGGLE) {
    if (!haulAdd("sealed tin", (uint16_t)(2 + rankOf(CR_GHOST) / 4), false)) return false;
    addHeat(HEAT_SECURITY, 6);
  } else if (c.kind == CK_MARKET) {
    int cost = marketPrice("spice", true) * 2;
    if (sheet().credits < cost) return false;
    if (!haulAdd("spice", (uint16_t)(2 + rankOf(CR_TRADER) / 4), true)) return false;
    spendCredits(cost);
  }
  s_c = c;
  return true;
}

bool contractFromOpportunity(const Opportunity &op) {
  ContractKind k = CK_NONE;
  switch (op.kind) {
    case 6: k = CK_TOUR; break;
    case 7: k = CK_SUPPLY; break;
    case 8: k = CK_ESCORT; break;
    case 9: k = CK_MARKET; break;
    case 10: k = CK_SURVEY; break;
    case 11: k = CK_BOUNTY; break;
    case 12: k = CK_RESCUE; break;
    case 13: k = CK_GHOST; break;
    case 14: k = CK_DEPTHRUN; break;
    case 15: k = CK_LANDMARK; break;
    default: return false;
  }
  Contract c;
  if (!roll(c, k)) return false;
  strncpy(c.title, op.title, sizeof(c.title) - 1);
  c.title[sizeof(c.title) - 1] = 0;
  c.pay = op.reward;
  c.xp = op.xp;
  c.track = op.track;
  return contractAccept(c);
}

void contractAbandon() {
  if (const char *cargo = contractCargo(s_c.kind)) haulRemove(cargo);
  memset(&s_c, 0, sizeof(s_c));
}

void contractOnGate(const char *label, uint8_t band, bool unknown) {
  if (!s_c.live) return;
  bool arrival = label && label[0];
  if ((s_c.kind == CK_SURVEY || s_c.kind == CK_TOUR) && arrival && unknown) { step(); return; }
  if (s_c.kind == CK_ESCORT && !arrival && band <= DEPTH_SHALLOW) { step(); return; }
  // deliveries are paid at the destination's dock (contractDeliverHere), not on arrival
}

void contractOnResolve(EncounterClass who, bool attacked, bool destroyedOther) {
  if (!s_c.live) return;
  if (s_c.kind == CK_BOUNTY && attacked && destroyedOther &&
      (who == ENC_PIRATE || who == ENC_SUBPIRATE || who == ENC_HOSTILE)) { complete(); return; }
  if (s_c.kind == CK_RESCUE && !attacked && (who == ENC_TRAVELER || who == ENC_ESCAPE_POD)) { complete(); return; }
  if (s_c.kind == CK_ESCORT && !attacked && (who == ENC_TRAVELER || who == ENC_MERCHANT)) { step(); return; }
  if (s_c.kind == CK_DEEPRESCUE && !attacked && who == ENC_ESCAPE_POD && s_band >= 1) { complete(); return; }
  if (s_c.kind == CK_DEEPSCAN && !attacked && who == ENC_ANOMALY && s_band >= 1) { step(); return; }
  if (s_c.kind == CK_GHOST && !attacked && who == ENC_SECURITY &&
      !holdIsHot() && sheet().heat[HEAT_SECURITY] <= 35) { complete(); return; }
}

void contractOnMine() {
  if (s_c.live && s_c.kind == CK_SURVEY) step();
}

void contractOnLandmark() {
  if (s_c.live && s_c.kind == CK_LANDMARK) complete();
}

void contractTick() {
  if (!s_c.live) return;
  // worldTick advances once a second; act on the edge, not every frame.
  uint32_t t = worldTick();
  if (t == s_lastHeatTick) return;
  s_lastHeatTick = t;
  if (s_c.kind == CK_SMUGGLE && (t % 31u) == 0u) addHeat(HEAT_SECURITY, 1);
}

Contract &contractOfferPeek() { return s_offer; }

const char *contractCargo(ContractKind k) {
  switch (k) {
    case CK_HAUL: return "crate";
    case CK_SUPPLY: return "machine parts";
    case CK_SMUGGLE: return "sealed tin";
    case CK_MARKET: return "spice";
    default: return nullptr;
  }
}

const char *contractHint(const Contract &c) {
  switch (c.kind) {
    case CK_HAUL: case CK_SUPPLY: case CK_SMUGGLE: case CK_MARKET: case CK_DEPTHRUN: return "deliver at its dock";
    case CK_BOUNTY: return "destroy a pirate or hostile";
    case CK_RESCUE: return "rescue a pod or lost traveler";
    case CK_SURVEY: return "arrive somewhere unheard-of / mine";
    case CK_TOUR: return "arrive somewhere unheard-of";
    case CK_ESCORT: return "shallow gates, friendly hails";
    case CK_GHOST: return "pass a security hail clean";
    case CK_LANDMARK: return "chart a deep landmark";
    case CK_DEEPRESCUE: return "rescue a pod in subspace";
    case CK_DEEPSCAN: return "scan anomalies in subspace";
    default: return "";
  }
}

bool contractTakeCompleted(Contract &out) {
  if (!s_doneFresh) return false;
  out = s_done;
  s_doneFresh = false;
  return true;
}

bool contractsSave() {
  Preferences prefs;
  if (!prefs.begin("sm_job", false)) return false;
  prefs.putBytes("lead", &s_c, sizeof(s_c));
  prefs.end();
  return true;
}

bool contractsLoad() {
  Preferences prefs;
  if (!prefs.begin("sm_job", true)) return false;
  bool ok = false;
  if (prefs.getBytesLength("lead") == sizeof(s_c)) {
    Contract c;
    prefs.getBytes("lead", &c, sizeof(c));
    if (c.kind < CK_COUNT) { s_c = c; ok = true; }
  }
  prefs.end();
  return ok;
}

} // namespace sm
