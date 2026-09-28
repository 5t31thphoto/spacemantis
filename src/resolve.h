#pragma once
// ============================================================
//  SpaceMantis — Hail / Attack short resolve (no dogfight sim)
// ============================================================
#include "sheet_types.h"
#include "universe.h"

namespace sm {

enum ResolveVerb : uint8_t { VERB_HAIL = 0, VERB_ATTACK };

struct ResolveIn {
  ResolveVerb verb;
  EncounterClass who;
  uint8_t band;
  uint8_t threat;          // 1..10 fiction strength of the other
};

struct ResolveOut {
  bool survived;
  bool destroyedOther;
  int16_t creditDelta;
  int16_t fuelDelta;
  int16_t hullDelta;
  int8_t heatDelta[HEAT_COUNT];
  CareerId xpTrack;
  uint16_t xpAmount;
  char flagKey[FLAG_LEN];  // empty if none
  int8_t flagValue;
  char rumorName[NAME_LEN]; // empty if none
  uint8_t rumorDepth;
  const char *blurb;       // short static string for UI
};

ResolveOut resolve(const ResolveIn &in);

}  // namespace sm
