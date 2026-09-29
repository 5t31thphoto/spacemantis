#include "contracts.h"
#include "sheet.h"
#include "universe.h"
#include "sim.h"
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
    default: return "-";
  }
}

bool isCargo(ContractKind k) {
  return k == CK_HAUL || k == CK_SMUGGLE || k == CK_SUPPLY || k == CK_MARKET;
}

bool holdIsHot() {
  const Pilot &p = sheet();
  for (uint8_t i = 0; i < p.haulN; i++) if (!p.haul[i].legal) return true;
  return false;
}

// Fills `o` with a fresh, not-yet-accepted job of kind k. No side effects.
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
    for (int i = 0; i < 9; ++i) {
      acc += weights[i];
      if (pick < acc) { k = pool[i]; break; }
    }
  }

  o.kind = k;
  o.live = 0;
  strncpy(o.title, kindTitle(k), sizeof(o.title) - 1);

  // Only cargo jobs point at a named gate; the rest are opportunistic.
  if (isCargo(k)) {
    GateOffer hand[6];
    int n = buildGateHand(DEPTH_REAL, hand, 6);
    if (n > 0) {
      int idx = (int)(urand() % (uint32_t)n);
      strncpy(o.dest, hand[idx].name, NAME_LEN - 1);
      o.destDepth = hand[idx].depthRating;
    }
  }

  switch (k) {
    case CK_HAUL:     o.pay = (int16_t)(60 + urand(0, 100));  o.xp = 18; o.track = CR_HAULER;      o.need = 1; break;
    case CK_BOUNTY:   o.pay = (int16_t)(90 + urand(0, 130));  o.xp = 22; o.track = CR_GUNHAND;     o.need = 1; break;
    case CK_RESCUE:   o.pay = (int16_t)(70 + urand(0, 80));   o.xp = 20; o.track = CR_RESCUER;     o.need = 1; break;
    case CK_SURVEY:   o.pay = (int16_t)(55 + urand(0, 90));   o.xp = 24; o.track = CR_WANDERER;    o.need = 3; break;
    case CK_SMUGGLE:  o.pay = (int16_t)(120 + urand(0, 140)); o.xp = 20; o.track = CR_GHOST;       o.need = 1; break;
    case CK_TOUR:     o.pay = (int16_t)(80 + urand(0, 100));  o.xp = 24; o.track = CR_TRADER;      o.need = 2; break;
    case CK_SUPPLY:   o.pay = (int16_t)(110 + urand(0, 120)); o.xp = 30; o.track = CR_HAULER;      o.need = 1; break;
    case CK_ESCORT:   o.pay = (int16_t)(95 + urand(0, 120));  o.xp = 28; o.track = CR_RESCUER;     o.need = 2; break;
    case CK_MARKET:   o.pay = (int16_t)(130 + urand(0, 150)); o.xp = 26; o.track = CR_TRADER;      o.need = 1; break;
    case CK_GHOST:    o.pay = (int16_t)(150 + urand(0, 80));  o.xp = 40; o.track = CR_GHOST;       o.need = 1; break;
    case CK_DEPTHRUN: o.pay = (int16_t)(200 + urand(0, 80));  o.xp = 55; o.track = CR_DEPTHRUNNER; o.need = 2; break;
    case CK_LANDMARK: o.pay = (int16_t)(250 + urand(0, 80));  o.xp = 65; o.track = CR_DEPTHRUNNER; o.need = 2; break;
    default: return false;
  }
  return true;
}

void complete() {
  if (!s_c.live) return;
  addCredits(s_c.pay);
  grantXp(s_c.track, s_c.xp);
  flagSet("job_done", (int8_t)(flagGet("job_done") < 120 ? flagGet("job_done") + 1 : 120), false);
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

void step() {
  ++s_c.progress;
  if (s_c.progress >= s_c.need) complete();
}

} // namespace

void contractsInit() {
  memset(&s_c, 0, sizeof(s_c));
  memset(&s_offer, 0, sizeof(s_offer));
  memset(&s_done, 0, sizeof(s_done));
  s_doneFresh = false;
}

Contract &contract() { return s_c; }

bool contractOffer(ContractKind prefer) { return roll(s_offer, prefer); }

bool contractAccept(const Contract &offer) {
  if (offer.kind == CK_NONE || s_c.live) return false;
  Contract c = offer;
  c.live = 1;
  c.progress = 0;

  // Cargo is acquired here, when the player actually commits. The offer roll
  // itself has no side effects.
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
  if (s_c.dest[0]) knownGateAdd(s_c.dest, s_c.destDepth);
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

  if ((s_c.kind == CK_SURVEY || s_c.kind == CK_TOUR) && unknown) { step(); return; }
  if (s_c.kind == CK_ESCORT && band <= DEPTH_SHALLOW) { step(); return; }

  if (isCargo(s_c.kind) && s_c.dest[0] && label && strncmp(s_c.dest, label, NAME_LEN) == 0) {
    s_c.progress = s_c.need;
    complete();
  }
}

void contractOnResolve(EncounterClass who, bool attacked, bool destroyedOther) {
  if (!s_c.live) return;
  if (s_c.kind == CK_BOUNTY && attacked && destroyedOther &&
      (who == ENC_PIRATE || who == ENC_SUBPIRATE || who == ENC_HOSTILE)) {
    complete(); return;
  }
  if (s_c.kind == CK_RESCUE && !attacked && (who == ENC_TRAVELER || who == ENC_ESCAPE_POD)) {
    complete(); return;
  }
  if (s_c.kind == CK_ESCORT && !attacked && (who == ENC_TRAVELER || who == ENC_MERCHANT)) {
    step(); return;
  }
  if (s_c.kind == CK_GHOST && !attacked && who == ENC_SECURITY &&
      !holdIsHot() && sheet().heat[HEAT_SECURITY] <= 35) {
    complete(); return;
  }
}

void contractOnMine() {
  if (!s_c.live) return;
  if (s_c.kind == CK_SURVEY) step();
}

void contractOnDepth(uint8_t band) {
  if (!s_c.live) return;
  if (s_c.kind == CK_DEPTHRUN) {
    if (s_c.progress == 0 && band >= DEPTH_DEEP) s_c.progress = 1;
    else if (s_c.progress >= 1 && band == DEPTH_REAL) complete();
  } else if (s_c.kind == CK_LANDMARK) {
    if (s_c.progress >= 1 && band == DEPTH_REAL) complete();
  }
}

void contractOnLandmark() {
  if (!s_c.live) return;
  if (s_c.kind == CK_LANDMARK && s_c.progress == 0) s_c.progress = 1;
}

void contractTick() {
  if (!s_c.live) return;
  // Hot cargo gets progressively more dangerous if the pilot loiters.
  // worldTick() advances once per second; only act on the tick edge.
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
    case CK_HAUL: case CK_SUPPLY: case CK_SMUGGLE: case CK_MARKET: return "thread the named gate";
    case CK_BOUNTY: return "destroy a pirate or hostile";
    case CK_RESCUE: return "hail a traveler or pod";
    case CK_SURVEY: return "thread unknown gates / salvage";
    case CK_TOUR: return "thread unknown gates";
    case CK_ESCORT: return "shallow gates, friendly hails";
    case CK_GHOST: return "pass a security hail clean";
    case CK_DEPTHRUN: return c.progress ? "now resurface" : "reach deep subspace";
    case CK_LANDMARK: return c.progress ? "now resurface" : "thread a persistent gate";
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
