#include "resolve.h"
#include "sheet.h"
#include "contracts.h"
#include "sim.h"
#include "content.h"
#include "lore.h"
#include <string.h>
#include <stdio.h>

namespace sm {
namespace {
float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
const char *pick(const char **pool, int n) { return pool[urand() % n]; }
}  // namespace

ResolveOut resolve(const ResolveIn &in) {
  ResolveOut o{};
  o.survived = true;
  o.destroyedOther = false;
  o.xpTrack = CR_GUNHAND;
  o.xpAmount = 0;
  o.flagKey[0] = 0;
  o.rumorName[0] = 0;
  o.blurb = "...";

  Pilot &p = sheet();
  uint8_t teeth = p.cap[CAP_WEAPONS] < 1 ? 1 : p.cap[CAP_WEAPONS];
  uint8_t shield = p.cap[CAP_SHIELDS] < 1 ? 1 : p.cap[CAP_SHIELDS];
  float threat = (float)in.threat;
  float power = (float)teeth + rankOf(CR_GUNHAND) * 0.35f;
  float guard = (float)shield + rankOf(CR_GHOST) * 0.2f;

  static const char *taxed[] = {"they tax you and leave", "toll paid in silence", "credits change hands"};
  static const char *tense[] = {"tense words; nothing more", "comm laser locks, then dies", "you both pretend this is fine"};
  static const char *fail[] = {"parley fails — they fire", "the reply is kinetic", "talk is over"};
  static const char *nameTrade[] = {"a name changes hands", "coordinates, maybe lies", "they speak of a place"};
  static const char *wreck[] = {"wreckage and salvage", "hull blooms open", "silent debris field"};
  static const char *bleed[] = {"they break off bleeding", "both ships limp away", "stalemate in the dark"};
  static const char *hurt[] = {"you take the worst of it", "alarms, then quiet", "the sheet absorbs the hit"};

  if (in.who == ENC_ESCAPE_POD) {
    if (in.verb == VERB_HAIL) {
      int reward = 18 + in.band * 14 + rankOf(CR_RESCUER) * 3;
      o.creditDelta = (int16_t)reward;
      o.xpTrack = CR_RESCUER;
      o.xpAmount = (uint16_t)(10 + in.band * 4);
      o.blurb = "pod aboard — someone gets another life";
      flagSet("saved_someone", 1, false);
      rumorAdd("Rescue Wake", (uint8_t)in.band, 8);
      contractOnResolve(in.who, false, false);
    } else {
      o.xpTrack = CR_GUNHAND; o.xpAmount = 3;
      o.creditDelta = (int16_t)(5 + in.threat * 2);
      o.blurb = "you leave the pod behind and take its signal";
      contractOnResolve(in.who, true, true);
    }
  } else if (in.who == ENC_WRECK) {
    if (in.verb == VERB_HAIL) {
      uint8_t mining = p.cap[CAP_MINING];
      uint16_t yield = (uint16_t)(1 + mining + urand() % 4);
      if (haulAdd(in.band >= DEPTH_DEEP ? "relic" : "scrap", yield, true)) {
        o.blurb = in.band >= DEPTH_DEEP ? "wreck yields something too old" : "salvage secured";
        o.creditDelta = (int16_t)(marketPrice(in.band >= DEPTH_DEEP ? "relic" : "scrap", true) * yield / 3);
      } else o.blurb = "hold full — the wreck keeps its secrets";
      o.xpTrack = CR_PROSPECTOR; o.xpAmount = (uint16_t)(7 + in.band * 3);
      contractOnMine();
    } else {
      o.xpTrack = CR_GUNHAND; o.xpAmount = 5;
      o.hullDelta = in.band >= DEPTH_DEEP ? -8 : -3;
      o.blurb = "weapons wake a dead ship";
    }
  } else if (in.who == ENC_ARTIFACT) {
    if (in.verb == VERB_HAIL) {
      o.xpTrack = CR_DEPTHRUNNER; o.xpAmount = (uint16_t)(9 + in.band * 5);
      o.creditDelta = (int16_t)(8 + in.band * 15);
      o.blurb = p.cap[CAP_SCANNERS] >= 2 ? "artifact yields a route-shaped secret" : "artifact yields an incomplete reading";
      if (p.cap[CAP_SCANNERS] >= 2 && urandf() < 0.35f) earnCap(CAP_SCANNERS, (uint8_t)(p.cap[CAP_SCANNERS] + 1));
      contractOnMine();
    } else {
      o.xpTrack = CR_GUNHAND; o.xpAmount = 4; o.hullDelta = -6;
      o.blurb = "the artifact does not enjoy being shot";
    }
  } else if (in.who == ENC_LANDMARK_ROCK || in.who == ENC_LANDMARK_GIANT) {
    // mining / scoop as "hail"; attack is wasteful
    if (in.verb == VERB_HAIL) {
      uint8_t mine = p.cap[CAP_MINING];
      int yield = 1 + mine + (int)(urand() % 3);
      if (in.who == ENC_LANDMARK_GIANT) {
        setFuel((uint16_t)(p.fuel + 8 + mine * 2 + p.cap[CAP_FUELSYS] * 5));
        o.blurb = "scooped volatiles";
        o.xpTrack = CR_PROSPECTOR;
        o.xpAmount = (uint16_t)(4 + mine);
      } else {
        if (haulAdd(mine >= 2 ? "ore" : "rock", (uint16_t)yield, true))
          o.blurb = "cutters bite; hold takes ore";
        else
          o.blurb = "hold full — left it glittering";
        o.xpTrack = CR_PROSPECTOR;
        o.xpAmount = (uint16_t)(5 + mine);
        contractOnMine();
      }
      grantXp(o.xpTrack, o.xpAmount);
      return o;
    }
    o.blurb = "you fire at a rock. it does not care.";
    return o;
  } else if (in.verb == VERB_HAIL) {
    o.xpTrack = CR_TRADER;
    float read = 0.32f + p.cap[CAP_SCANNERS] * 0.06f + rankOf(CR_TRADER) * 0.025f;
    if (p.cap[CAP_CLOAK] && (in.who == ENC_PIRATE || in.who == ENC_SECURITY))
      read += 0.05f;
    float roll = urandf();

    if (in.who == ENC_PIRATE || in.who == ENC_SUBPIRATE) {
      if (roll < read) {
        o.creditDelta = (int16_t)(-12 - (int)threat * 3);
        o.blurb = pick(taxed, 3);
        o.xpAmount = 5;
        addHeat(in.who == ENC_SUBPIRATE ? HEAT_UNDER : HEAT_PIRATE, -3);
      } else if (roll < read + 0.28f) {
        o.blurb = pick(tense, 3);
        o.xpAmount = 3;
      } else {
        o.hullDelta = (int16_t)(-(int)clampf(threat * 2.f - guard, 2.f, 20.f));
        o.blurb = pick(fail, 3);
        o.xpTrack = CR_GUNHAND;
        o.xpAmount = 4;
      }
    } else if (in.who == ENC_MERCHANT || in.who == ENC_TRAVELER) {
      o.blurb = pick(nameTrade, 3);
      o.xpAmount = 6;
      o.creditDelta = (int16_t)((int)urand(0, 15) - 5);
      // rumor seed
      // They talk about somewhere: a new name becomes a gate you can fly to.
      strncpy(o.rumorName, placeName(urand(), in.band, false), NAME_LEN - 1);
      o.rumorName[NAME_LEN - 1] = 0;
      o.rumorDepth = placeDepth(o.rumorName);
      if (urandf() < 0.25f) {
        strncpy(o.flagKey, "spoke_kind", FLAG_LEN);
        o.flagValue = 1;
      }
      if (in.who == ENC_TRAVELER && flagHas("saved_someone")) {
        o.creditDelta = (int16_t)(o.creditDelta + 20);
        o.blurb = "they recognize you — a gift";
      }
    } else if (in.who == ENC_SECURITY) {
      bool hot = p.heat[HEAT_SECURITY] > 35;
      for (uint8_t i = 0; i < p.haulN; i++) if (!p.haul[i].legal) hot = true;
      if (hot) {
        o.blurb = "scan hits hot cargo";
        o.creditDelta = -40;
        o.hullDelta = -4;
        addHeat(HEAT_SECURITY, 8);
        o.xpTrack = CR_GHOST;
      } else {
        o.blurb = "routine hail; clean";
        o.xpAmount = 3;
        addHeat(HEAT_SECURITY, -2);
        o.xpTrack = CR_GHOST;
      }
    } else if (in.who == ENC_ANOMALY || in.who == ENC_HOSTILE) {
      if (p.cap[CAP_SCANNERS] >= 2 && urandf() < 0.5f) {
        o.blurb = "scanners map the wrong geometry";
        o.xpTrack = CR_DEPTHRUNNER;
        o.xpAmount = 8;
        if (urandf() < 0.3f) earnCap(CAP_STABILIZER, p.cap[CAP_STABILIZER] < 1 ? 1 : p.cap[CAP_STABILIZER]);
        if (in.band >= DEPTH_DEEP && urandf() < 0.18f) {
          const Landmark *lm = landmarkAt((int)(urand() % (uint32_t)landmarkCount()));
          if (lm && lm->band >= DEPTH_DEEP) {
            bool first = !landmarkDiscovered(lm->id);
            discoverLandmark(lm->id);
            if (first) {
              strncpy(o.flagKey, "deep_seen", FLAG_LEN - 1);
              o.flagValue = 1;
              strncpy(o.rumorName, lm->name, NAME_LEN - 1);
              o.rumorDepth = lm->band;
              o.blurb = landmarkLine(*lm, urand());
              o.xpAmount = (uint16_t)(o.xpAmount + 16);
            }
          }
        }
      } else {
        o.hullDelta = (int16_t)(-(int)clampf(threat - guard * 0.5f, 1.f, 18.f));
        o.blurb = "it notices you noticing it";
        o.xpTrack = CR_DEPTHRUNNER;
        o.xpAmount = 5;
      }
    } else {
      o.blurb = "static and distance";
      o.xpAmount = 2;
      o.xpTrack = CR_WANDERER;
    }
    contractOnResolve(in.who, false, false);
  } else {
    o.xpTrack = CR_GUNHAND;
    // A fresh pilot can win against soft targets sometimes; pirates want an
    // upgrade; deep hostiles stay a bad idea for a long while.
    float edge = power * 1.5f - threat * 0.6f + (urandf() - 0.5f) * 3.0f;
    if (p.cap[CAP_CLOAK] && in.band == DEPTH_REAL) edge += 0.35f;
    if (in.who == ENC_STATION) { addHeat(HEAT_SECURITY, 20); addHeat(HEAT_HOUSE, 10); }
    if (edge > 1.0f) {
      o.destroyedOther = true;
      o.creditDelta = (int16_t)(10 * in.threat + teeth * 3);
      o.blurb = pick(wreck, 3);
      o.xpAmount = (uint16_t)(8 + in.threat);
      haulAdd("scrap", (uint16_t)(1 + teeth / 2), true);
      if (in.who == ENC_PIRATE) addHeat(HEAT_PIRATE, 5);
      if (in.who == ENC_SECURITY) addHeat(HEAT_SECURITY, 14);
      if (in.who == ENC_SUBPIRATE) addHeat(HEAT_UNDER, 7);
    } else if (edge > -0.4f) {
      o.hullDelta = (int16_t)(-(int)clampf(threat - guard * 0.55f, 1.f, 14.f));
      o.creditDelta = (int16_t)(4 * in.threat);
      o.blurb = pick(bleed, 3);
      o.xpAmount = 5;
    } else {
      o.hullDelta = (int16_t)(-(int)clampf(threat * 1.6f - guard, 4.f, 42.f));
      o.blurb = pick(hurt, 3);
      o.xpAmount = 3;
    }
    contractOnResolve(in.who, true, o.destroyedOther);
  }

  if (o.creditDelta) addCredits(o.creditDelta);
  if (o.xpAmount >= 20 && urandf() < 0.35f) {
    o.blurb = careerPerkLine(o.xpTrack, rankOf(o.xpTrack));
  }
  if (o.hullDelta < 0) damageHull((uint16_t)(-o.hullDelta));
  if (o.hullDelta > 0) repairHull((uint16_t)o.hullDelta);
  if (o.xpAmount) grantXp(o.xpTrack, o.xpAmount);
  if (o.flagKey[0]) flagSet(o.flagKey, o.flagValue, false);
  if (o.rumorName[0]) rumorAdd(o.rumorName, o.rumorDepth, 12);
  return o;
}

}  // namespace sm
