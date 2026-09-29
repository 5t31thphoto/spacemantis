#pragma once
#include "sheet_types.h"
#include "universe.h"

namespace sm {

// Hidden simulation layer: the spreadsheet exists, but the player sees cards,
// contacts, gates and consequences rather than management screens.
struct MarketState {
  uint16_t seed;
  int8_t fuelWeather;
  int8_t repairWeather;
  int8_t oreWeather;
  int8_t cargoWeather;
  int8_t contrabandWeather;
  uint8_t stationType;
  uint8_t pressure;
};

struct EncounterFlavor {
  const char *name;
  const char *hail;
  const char *attack;
  uint8_t threat;
  uint8_t reward;
  uint8_t depthMin;
  uint8_t depthMax;
};

struct Opportunity {
  char title[28];
  char detail[72];
  int16_t reward;
  uint16_t xp;
  CareerId track;
  uint8_t depth;
  uint8_t kind;
};

void simInit();
void simTick(uint32_t nowMs);
const MarketState &market();
int marketPrice(const char *commodity, bool legal);
int fuelPrice();
int repairPrice();

const EncounterFlavor &encounterFlavor(EncounterClass c, uint8_t band);

// Career depth is intentionally hidden. These are used by resolve and event
// generation to make ranks change what sort of universe the pilot encounters.
float careerBias(CareerId id);
bool careerUnlock(CareerId id, uint8_t rank);

// Opportunistic station/event cards. No quest log is created.
bool makeOpportunity(Opportunity &out, uint8_t band);
void applyOpportunity(const Opportunity &op);

// A deep landmark becomes truly persistent only after the pilot discovers it.
void discoverLandmark(uint32_t id);
bool landmarkDiscovered(uint32_t id);
// 0 if never charted, else 1 + (life number when first charted) % 120
int landmarkChartedLife(uint32_t id);

// Small hidden world clock used to make stations and encounters evolve.
uint32_t worldTick();
uint8_t worldPressure();

} // namespace sm
