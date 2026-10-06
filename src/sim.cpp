#include "sim.h"
#include "sheet.h"
#include "contracts.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace sm {
namespace {
MarketState s_market{};
uint32_t s_tick = 0;
uint32_t s_last = 0;
uint32_t s_stationCounter = 0;
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Commodity { const char *name; int base; uint8_t volatility; };
static const Commodity COMMODITIES[] = {
  {"ore", 12, 5}, {"rock", 7, 3}, {"scrap", 18, 8},
  {"crate", 16, 7}, {"sealed tin", 34, 14}, {"relic", 72, 28},
  {"spice", 29, 18}, {"water", 9, 4}, {"machine parts", 26, 11},
  {"rations", 10, 4}, {"alloys", 30, 9}, {"medicine", 42, 14}, {"electronics", 55, 16}
};

static const EncounterFlavor FLAVOR[ENC_COUNT][5] = {
  {
    {"WANDERER", "a tired pilot offers a name", "they flee when weapons wake", 2, 4, 0, 4},
    {"LOST TRAVELER", "they ask which way is out", "you scare them into the dark", 3, 6, 1, 3},
    {"DEEP PILOT", "they whisper about a place beneath places", "they carry something that bites back", 6, 15, 2, 4},
    {"ABYSSAL PILOT", "they already know your name", "the contact is not entirely human", 8, 22, 4, 4},
    {"THE RETURNING ONE", "they remember a road that should be gone", "they refuse to be forgotten", 9, 30, 4, 4}
  },
  {
    {"MERCHANT", "a trader swaps a rumor for your attention", "their escort mistakes you for competition", 3, 8, 0, 2},
    {"FREE TRADER", "their prices twitch with the local weather", "cargo escorts open fire", 4, 12, 0, 3},
    {"UNDER MERCHANT", "they sell things that should not have surfaced", "their cargo is alive", 7, 24, 2, 4},
    {"BLACK MARKET", "a quiet price for a dangerous name", "their guards are very old", 8, 32, 3, 4},
    {"DEEP BROKER", "they trade in landmarks, not goods", "the transaction becomes territorial", 9, 45, 4, 4}
  },
  {
    {"SECURITY", "routine hail; clean enough", "scan beams become weapons", 3, 7, 0, 2},
    {"PATROL", "they ask about your hold", "they have a warrant-shaped opinion", 5, 12, 0, 3},
    {"DEPTH WARDEN", "they ask what you brought back", "their rules were written below", 7, 20, 2, 4},
    {"BLACK PATROL", "they know which gate you used", "no warning light comes first", 8, 30, 3, 4},
    {"OLD CUSTOMS", "they remember civilizations you never knew", "their ship is mostly a boundary", 10, 50, 4, 4}
  },
  {
    {"PIRATE", "they offer a toll and pretend it is friendship", "weapons answer faster than words", 4, 10, 0, 2},
    {"RAIDER", "they want your cargo, not your life", "the opening volley is ugly", 6, 18, 0, 3},
    {"SUB-PIRATE", "they heard you breach the under", "they hunt your wake", 7, 26, 1, 4},
    {"DEEP RAIDER", "they pirate destinations, not cargo", "they know where the geometry breaks", 9, 40, 2, 4},
    {"ABYSSAL PIRATE", "they have no surface identity left", "their ship is an accusation", 10, 65, 4, 4}
  },
  {
    {"SUB-PIRATE", "they noticed the breach", "they follow the wake", 5, 13, 1, 2},
    {"WAKE THIEF", "they ask what you saw underneath", "they cut across your return path", 6, 20, 1, 3},
    {"UNDER RAIDER", "they know the wrong side of the gate", "they pull at your hull", 8, 34, 2, 4},
    {"DEEP WAKE", "they speak from behind the ship", "the attack bends distance", 9, 48, 3, 4},
    {"BREACH HUNTER", "they were waiting before you arrived", "they know the portal's other end", 10, 75, 4, 4}
  },
  {
    {"ANOMALY", "the scanners return a place-name", "the geometry notices you", 3, 12, 0, 2},
    {"WRONG LIGHT", "something is reflecting from nowhere", "the light turns hostile", 5, 20, 1, 3},
    {"FOLD", "you can see a gate inside the contact", "it closes around the ship", 7, 35, 2, 4},
    {"IMPOSSIBLE STAR", "the star has a memory", "its gravity is directional", 9, 55, 3, 4},
    {"THE DEEP EYE", "it recognizes the pilot sheet", "it tries to erase the distinction", 10, 90, 4, 4}
  },
  {
    {"HOSTILE", "it does not answer the hail", "the contact accelerates", 5, 16, 1, 3},
    {"ALIEN CONTACT", "its reply arrives before the hail", "it wants the ship's shadow", 7, 28, 2, 4},
    {"DEEP HOSTILE", "it has no known category", "the fight happens in several directions", 8, 42, 2, 4},
    {"ABYSSAL HOSTILE", "it circles a landmark that should be yours", "it attacks the route itself", 10, 70, 3, 4},
    {"UNNAMED THING", "your scanners call it nothing", "nothing answers with force", 10, 110, 4, 4}
  },
  {
    {"ASTEROID", "a seam of ore catches the light", "you can waste ammunition on geology", 1, 5, 0, 4},
    {"ORE FIELD", "the rock is unusually rich", "your shots fracture the best vein", 2, 10, 0, 4},
    {"DEEP ROCK", "the stone is warm in vacuum", "the fragments drift upward", 3, 20, 1, 4},
    {"BLACK ORE", "the scanner cannot classify it", "you crack something that was sealed", 5, 35, 2, 4},
    {"LANDMARK ROCK", "this asteroid has a name in the deep", "the stone remembers impacts", 6, 60, 4, 4}
  },
  {
    {"GAS GIANT", "volatile clouds glow beneath you", "the atmosphere ignores weapons", 1, 7, 0, 2},
    {"BLUE GIANT", "a fuel-rich band curls below", "you stir a violent storm", 2, 14, 0, 3},
    {"UNDER GIANT", "the gas has a second horizon", "something moves beneath it", 4, 28, 1, 4},
    {"DARK GIANT", "its clouds fall sideways", "the storm reaches back", 6, 45, 2, 4},
    {"ANCIENT GIANT", "the scanner finds an old gate inside", "the storm has teeth", 8, 75, 4, 4}
  },
  {
    {"STATION", "dock beacon confirms approach", "the station answers with guns", 1, 5, 0, 0},
    {"OUTPOST", "a tired dock light welcomes you", "the outpost is under pressure", 2, 8, 0, 2},
    {"DEEP STATION", "its docking language is unfamiliar", "the station has a defensive geometry", 4, 18, 2, 4},
    {"DEEP OUTPOST", "the station seems older than the route", "someone wants your gate label", 6, 30, 3, 4},
    {"LAST STATION", "a place like this should not be here", "docking becomes a negotiation", 8, 55, 4, 4}
  },
  {
    {"ESCAPE POD", "a voice asks for rescue", "the pod's owner panics", 1, 8, 0, 3},
    {"DRIFTING POD", "someone is still alive inside", "you are interrupted by whoever caused the wreck", 3, 18, 0, 4},
    {"DEEP POD", "the pod contains a route name", "something followed it through", 5, 30, 2, 4},
    {"OLD POD", "its beacon predates the local universe", "the beacon attracts something hostile", 7, 48, 3, 4},
    {"POD FROM BELOW", "the survivor says the deep is smaller", "the rescue beacon is not entirely theirs", 9, 70, 4, 4}
  },
  {
    {"WRECK", "salvage still glitters", "the wreck's defense system wakes", 2, 9, 0, 3},
    {"FRESH WRECK", "the hull is still warm", "someone may come back for it", 4, 18, 0, 3},
    {"DEEP WRECK", "the ship died on the wrong side of a gate", "its cargo wants out", 6, 34, 2, 4},
    {"IMPOSSIBLE WRECK", "the hull carries two incompatible registries", "the wreck fires from a memory", 8, 55, 3, 4},
    {"PERSISTENT WRECK", "you have seen this wreck before", "it has survived longer than you have", 9, 85, 4, 4}
  },
  {
    {"ARTIFACT", "a small object refuses the scanner", "the object changes the range of the fight", 3, 15, 1, 4},
    {"RELIC", "someone has left a thing here deliberately", "the relic has a guardian", 5, 30, 2, 4},
    {"DEEP RELIC", "it carries a gate label that should be dead", "the label summons a hostile contact", 7, 50, 3, 4},
    {"LANDMARK RELIC", "the object is tied to a persistent place", "the place reacts to your approach", 8, 75, 4, 4},
    {"OLD MANTIS", "the artifact is shaped like a familiar emblem", "it opens something that should remain closed", 10, 120, 4, 4}
  }
};

static uint32_t hashText(const char *s) {
  uint32_t h = 2166136261u;
  if (!s) return h;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

} // namespace

void simInit() {
  memset(&s_market, 0, sizeof(s_market));
  s_tick = 0;
  s_last = millis();
  s_stationCounter = 0;
  s_market.seed = (uint16_t)urand();
  s_market.stationType = (uint8_t)(urand() % 5);
  s_market.pressure = 18;
  s_market.fuelWeather = 0;
  s_market.repairWeather = 0;
  s_market.oreWeather = 0;
  s_market.cargoWeather = 0;
  s_market.contrabandWeather = 0;
}

void simTick(uint32_t nowMs) {
  if (nowMs - s_last < 1000) return;
  uint32_t steps = (nowMs - s_last) / 1000;
  if (steps > 12) steps = 12;
  s_last += steps * 1000;
  s_tick += steps;

  // The world changes slowly. This is deliberately not displayed as a table.
  for (uint32_t i = 0; i < steps; ++i) {
    int swing = (int)(urand() % 7) - 3;
    s_market.fuelWeather = (int8_t)clampi((int)s_market.fuelWeather + swing / 2, -12, 12);
    s_market.repairWeather = (int8_t)clampi((int)s_market.repairWeather + ((int)(urand() % 5) - 2), -10, 10);
    s_market.oreWeather = (int8_t)clampi((int)s_market.oreWeather + ((int)(urand() % 7) - 3), -16, 16);
    s_market.cargoWeather = (int8_t)clampi((int)s_market.cargoWeather + ((int)(urand() % 7) - 3), -18, 18);
    s_market.contrabandWeather = (int8_t)clampi((int)s_market.contrabandWeather + ((int)(urand() % 9) - 4), -20, 20);
    if (s_market.pressure > 0 && urandf() < 0.22f) --s_market.pressure;
    if (urandf() < 0.08f) ++s_market.pressure;
    coolHeat(1);
    knowledgeTick();
  }
}

const MarketState &market() { return s_market; }
uint32_t worldTick() { return s_tick; }
uint8_t worldPressure() { return s_market.pressure; }

int marketPrice(const char *commodity, bool legal) {
  uint32_t h = hashText(commodity);
  int base = 15;
  uint8_t vol = 5;
  for (const Commodity &c : COMMODITIES) {
    if (strcmp(c.name, commodity ? commodity : "") == 0) { base = c.base; vol = c.volatility; break; }
  }
  int wave = (int)((h ^ s_market.seed ^ (uint32_t)(s_tick / 17)) % (vol * 2 + 1)) - vol;
  int weather = s_market.cargoWeather;
  if (commodity && strstr(commodity, "ore")) weather += s_market.oreWeather;
  if (commodity && strstr(commodity, "tin")) weather += s_market.contrabandWeather;
  if (!legal) weather += s_market.contrabandWeather / 2;
  int result = base + wave + weather / 2;
  if (result < 2) result = 2;
  return result;
}

int fuelPrice() { return 4 + s_market.fuelWeather / 3 + (s_market.pressure / 25); }
int repairPrice() { return 3 + s_market.repairWeather / 3 + (s_market.pressure / 30); }

const EncounterFlavor &encounterFlavor(EncounterClass c, uint8_t band) {
  uint8_t b = band > 4 ? 4 : band;
  if (c >= ENC_COUNT) c = ENC_TRAVELER;
  return FLAVOR[c][b];
}

float careerBias(CareerId id) {
  if (id >= CR_COUNT) return 0.f;
  return 1.f + rankOf(id) * 0.035f;
}

bool careerUnlock(CareerId id, uint8_t rank) {
  return rankOf(id) >= rank;
}

bool makeOpportunity(Opportunity &out, uint8_t band) {
  memset(&out, 0, sizeof(out));
  static const struct Card { const char *title; const char *detail; CareerId track; uint8_t kind; int pay; uint16_t xp; } cards[] = {
    {"TOUR THE LONG WAY", "Take a passenger through an unmarked gate and bring them back.", CR_TRADER, 6, 95, 24},
    {"OUTPOST SUPPLY", "An isolated dock needs machine parts before its beacon fades.", CR_HAULER, 7, 120, 30},
    {"ESCORT THE LOST", "A weak ship wants company through shallow subspace.", CR_RESCUER, 8, 105, 28},
    {"MARKET RUN", "Buy ordinary cargo where it is cheap; sell when the weather turns.", CR_TRADER, 9, 110, 26},
    {"DEEP SURVEY", "Thread an unknown gate and read what the scanners refuse to name.", CR_WANDERER, 10, 145, 36},
    {"QUIET BOUNTY", "A pirate has been leaning on travelers near a gate hand.", CR_GUNHAND, 11, 155, 38},
    {"RECOVER THE POD", "A rescue pod is drifting just beyond a hostile pocket.", CR_RESCUER, 12, 170, 42},
    {"GHOST THE PATROL", "Cross a security pocket without turning your hold into evidence.", CR_GHOST, 13, 185, 44},
    {"DEPTH RUN", "Reach a deeper band and return with the route intact.", CR_DEPTHRUNNER, 14, 220, 55},
    {"LANDMARK WATCH", "Confirm a persistent deep name and survive the way back.", CR_DEPTHRUNNER, 15, 270, 65}
  };
  int candidates = 0;
  for (const auto &c : cards) if (band >= (c.kind >= 13 ? 2 : 0)) ++candidates;
  if (!candidates) return false;
  int pick = (int)(urand() % candidates), at = 0;
  const Card *chosen = nullptr;
  for (const auto &c : cards) {
    if (band < (c.kind >= 13 ? 2 : 0)) continue;
    if (at++ == pick) { chosen = &c; break; }
  }
  if (!chosen) return false;
  strncpy(out.title, chosen->title, sizeof(out.title) - 1);
  strncpy(out.detail, chosen->detail, sizeof(out.detail) - 1);
  out.reward = (int16_t)(chosen->pay + (int)band * 15 + (int)rankOf(chosen->track) * 4);
  out.xp = (uint16_t)(chosen->xp + band * 3);
  out.track = chosen->track;
  out.depth = band;
  out.kind = chosen->kind;
  return true;
}

void applyOpportunity(const Opportunity &op) {
  addCredits(op.reward);
  grantXp(op.track, op.xp);
  if (op.kind == 6) flagSet("tourist", 1, false);
  if (op.kind == 7) flagSet("outpost_help", 1, false);
  if (op.kind == 8) flagSet("escort_done", 1, false);
  if (op.kind == 10) flagSet("survey_done", 1, false);
  if (op.kind == 14) flagSet("depth_run", 1, true);
  if (op.kind == 15) flagSet("landmark_watch", 1, true);
}

void discoverLandmark(uint32_t id) {
  char key[FLAG_LEN];
  snprintf(key, sizeof(key), "lm%lu", (unsigned long)(id % 1000000UL));
  if (!flagHas(key)) flagSet(key, (int8_t)(1 + sheet().lives % 120), true);
  contractOnLandmark();
}

int landmarkChartedLife(uint32_t id) {
  char key[FLAG_LEN];
  snprintf(key, sizeof(key), "lm%lu", (unsigned long)(id % 1000000UL));
  return flagGet(key);
}

bool landmarkDiscovered(uint32_t id) {
  char key[FLAG_LEN];
  snprintf(key, sizeof(key), "lm%lu", (unsigned long)(id % 1000000UL));
  return flagHas(key);
}

} // namespace sm
