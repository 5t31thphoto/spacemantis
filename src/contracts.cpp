#include "contracts.h"
#include "sheet.h"
#include "universe.h"
#include "sim.h"
#include <string.h>
#include <stdio.h>

namespace sm {
namespace {
Contract s_c{};
Contract s_offer{};

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
    default: return "—";
  }
}

static bool isCargo(ContractKind k) {
  return k == CK_HAUL || k == CK_SMUGGLE || k == CK_SUPPLY || k == CK_MARKET;
}

} // namespace

void contractsInit() {
  memset(&s_c, 0, sizeof(s_c));
  memset(&s_offer, 0, sizeof(s_offer));
}

Contract &contract() { return s_c; }

bool contractOffer(ContractKind prefer) {
  memset(&s_offer, 0, sizeof(s_offer));
  ContractKind k = prefer;
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
    for (int i = 0; i < 9; ++i) {
      acc += weights[i];
      if (pick < acc) { k = pool[i]; break; }
    }
  }

  s_offer.kind = k;
  s_offer.live = 0;
  strncpy(s_offer.title, kindTitle(k), sizeof(s_offer.title) - 1);

  GateOffer hand[6];
  int n = buildGateHand(DEPTH_REAL, hand, 6);
  if (n > 0) {
    int idx = (int)(urand() % (uint32_t)n);
    strncpy(s_offer.dest, hand[idx].name, NAME_LEN - 1);
    s_offer.destDepth = hand[idx].depthRating;
  } else {
    strncpy(s_offer.dest, "ANYWHERE", NAME_LEN - 1);
    s_offer.destDepth = 0;
  }

  switch (k) {
    case CK_HAUL:
      s_offer.pay = (int16_t)(60 + urand(0, 100));
      s_offer.xp = 18; s_offer.track = CR_HAULER; s_offer.need = 1;
      break;
    case CK_BOUNTY:
      s_offer.pay = (int16_t)(90 + urand(0, 130));
      s_offer.xp = 22; s_offer.track = CR_GUNHAND; s_offer.need = 1;
      break;
    case CK_RESCUE:
      s_offer.pay = (int16_t)(70 + urand(0, 80));
      s_offer.xp = 20; s_offer.track = CR_RESCUER; s_offer.need = 1;
      break;
    case CK_SURVEY:
      s_offer.pay = (int16_t)(55 + urand(0, 90));
      s_offer.xp = 24; s_offer.track = CR_WANDERER; s_offer.need = 3;
      break;
    case CK_SMUGGLE:
      s_offer.pay = (int16_t)(120 + urand(0, 140));
      s_offer.xp = 20; s_offer.track = CR_GHOST; s_offer.need = 1;
      break;
    case CK_TOUR:
      s_offer.pay = (int16_t)(80 + urand(0, 100));
      s_offer.xp = 24; s_offer.track = CR_TRADER; s_offer.need = 2;
      break;
    case CK_SUPPLY:
      s_offer.pay = (int16_t)(110 + urand(0, 120));
      s_offer.xp = 30; s_offer.track = CR_HAULER; s_offer.need = 1;
      break;
    case CK_ESCORT:
      s_offer.pay = (int16_t)(95 + urand(0, 120));
      s_offer.xp = 28; s_offer.track = CR_RESCUER; s_offer.need = 2;
      break;
    case CK_MARKET:
      s_offer.pay = (int16_t)(130 + urand(0, 150));
      s_offer.xp = 26; s_offer.track = CR_TRADER; s_offer.need = 1;
      break;
    default: return false;
  }

  return true;
}

void contractAccept(const Contract &offer) {
  if (offer.kind == CK_NONE) return;
  s_c = offer;
  s_c.live = 1;
  s_c.progress = 0;

  // Cargo is acquired here, when the player actually commits. The offer roll
  // itself has no side effects.
  if (s_c.kind == CK_HAUL) {
    if (!haulAdd("crate", (uint16_t)(3 + rankOf(CR_HAULER) / 3), true)) {
      memset(&s_c, 0, sizeof(s_c));
      return;
    }
  } else if (s_c.kind == CK_SUPPLY) {
    if (!haulAdd("machine parts", (uint16_t)(2 + rankOf(CR_HAULER) / 4), true)) {
      memset(&s_c, 0, sizeof(s_c));
      return;
    }
  } else if (s_c.kind == CK_SMUGGLE) {
    if (!haulAdd("sealed tin", (uint16_t)(2 + rankOf(CR_GHOST) / 4), false)) {
      memset(&s_c, 0, sizeof(s_c));
      return;
    }
    addHeat(HEAT_SECURITY, 6);
  } else if (s_c.kind == CK_MARKET) {
    if (!haulAdd("spice", (uint16_t)(2 + rankOf(CR_TRADER) / 4), true)) {
      memset(&s_c, 0, sizeof(s_c));
      return;
    }
    int cost = marketPrice("spice", true) * 2;
    if (!spendCredits(cost)) {
      haulClear();
      memset(&s_c, 0, sizeof(s_c));
      return;
    }
  }

  if (s_c.dest[0] && strcmp(s_c.dest, "ANYWHERE") != 0)
    knownGateAdd(s_c.dest, s_c.destDepth);
}

void contractAbandon() {
  if (isCargo(s_c.kind)) haulClear();
  memset(&s_c, 0, sizeof(s_c));
}

static void complete() {
  if (!s_c.live) return;
  addCredits(s_c.pay);
  grantXp(s_c.track, s_c.xp);
  flagSet("job_done", (int8_t)(flagGet("job_done") + 1), false);
  if (isCargo(s_c.kind)) haulClear();
  if (s_c.kind == CK_RESCUE) flagSet("saved_someone", 1, false);
  if (s_c.kind == CK_TOUR) flagSet("tourist", 1, false);
  if (s_c.kind == CK_SUPPLY) flagSet("outpost_help", 1, false);
  if (s_c.kind == CK_MARKET) flagSet("market_runner", 1, false);
  memset(&s_c, 0, sizeof(s_c));
}

void contractOnGate(const char *label, uint8_t band, bool unknown) {
  if (!s_c.live) return;

  if ((s_c.kind == CK_SURVEY || s_c.kind == CK_TOUR) && unknown) {
    ++s_c.progress;
    if (s_c.progress >= s_c.need) complete();
  }

  if (s_c.kind == CK_ESCORT && band <= DEPTH_SHALLOW) {
    ++s_c.progress;
    if (s_c.progress >= s_c.need) complete();
  }

  if (s_c.dest[0] && label && strncmp(s_c.dest, label, NAME_LEN) == 0) {
    if (s_c.kind == CK_HAUL || s_c.kind == CK_SMUGGLE || s_c.kind == CK_SUPPLY || s_c.kind == CK_MARKET) {
      s_c.progress = s_c.need;
      complete();
    }
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
  if (s_c.kind == CK_ESCORT && !attacked &&
      (who == ENC_TRAVELER || who == ENC_MERCHANT)) {
    ++s_c.progress;
    if (s_c.progress >= s_c.need) complete();
  }
}

void contractOnMine() {
  if (!s_c.live) return;
  if (s_c.kind == CK_SURVEY) {
    ++s_c.progress;
    if (s_c.progress >= s_c.need) complete();
  }
}

void contractTick() {
  if (!s_c.live) return;
  // Hot cargo gets progressively more dangerous if the pilot loiters.
  if (s_c.kind == CK_SMUGGLE && (worldTick() % 31u) == 0u)
    addHeat(HEAT_SECURITY, 1);
}

Contract &contractOfferPeek() { return s_offer; }

} // namespace sm
