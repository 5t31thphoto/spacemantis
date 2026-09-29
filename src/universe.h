#pragma once
// ============================================================
//  SpaceMantis — universe seed, depth content, persistent landmarks
// ============================================================
#include "sheet_types.h"

namespace sm {

void universeInit();
void universeReseed(uint32_t seed);        // called from onDestroy path
void universeRestoreSeed(uint32_t seed);  // exact restore for reboot
uint32_t universeSeed();

// procedural helpers
uint32_t urand();                          // 32-bit
uint32_t urand(uint32_t lo, uint32_t hi);  // inclusive-ish range
float    urandf();                         // 0..1

// ---- persistent landmarks (cross-run) ----
static const int MAX_LANDMARKS = 48;
void landmarkRegister(const char *name, uint8_t band, bool handmade, uint32_t id);
int  landmarkCount();
const Landmark *landmarkAt(int i);
const Landmark *landmarkFind(const char *name);
// landmarks available to inject as gates at a given band (persistent subset)
int  landmarksForBand(uint8_t band, const Landmark **out, int maxOut);

// ---- gate hand generation (semantic, no map) ----
struct GateOffer {
  char    name[NAME_LEN];
  uint8_t depthRating;
  uint8_t unknown;         // 1 = place you haven't heard of
  uint8_t persistent;      // 1 = deep landmark that can survive wipes
};

static const int MAX_GATE_HAND = 8;
// Build local gate hand near a station / pocket using pilot knowledge + unknowns + deep persistents
int buildGateHand(uint8_t localBand, GateOffer *out, int maxOut);
// How deep a dive a named place needs (1..4); stable within one universe.
uint8_t placeDepth(const char *name);

// ---- encounter weight hints for flight layer ----
enum EncounterClass : uint8_t {
  ENC_TRAVELER = 0,
  ENC_MERCHANT,
  ENC_SECURITY,
  ENC_PIRATE,
  ENC_SUBPIRATE,
  ENC_ANOMALY,
  ENC_HOSTILE,
  ENC_LANDMARK_ROCK,
  ENC_LANDMARK_GIANT,
  ENC_STATION,
  ENC_ESCAPE_POD,
  ENC_WRECK,
  ENC_ARTIFACT,
  ENC_COUNT
};

// relative weights 0..100 for spawning at this band given pilot heat/caps
void encounterWeights(uint8_t band, uint8_t out[ENC_COUNT]);

}  // namespace sm
