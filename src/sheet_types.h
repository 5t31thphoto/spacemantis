#pragma once
// ============================================================
//  SpaceMantis — spreadsheet types (hidden sim)
//  No slots, no loadout UI: capability is earned and present.
// ============================================================
#include <stdint.h>
#include <string.h>

namespace sm {

// ---- limits (PSRAM-friendly, still small) ----
static const int MAX_FLAGS        = 64;
static const int MAX_RUMORS       = 24;
static const int MAX_KNOWN_GATES  = 32;
static const int MAX_HAUL_LINES   = 8;
static const int MAX_CAREER       = 8;
static const int NAME_LEN         = 24;
static const int FLAG_LEN         = 20;

// ---- depth bands (travel layer indexes these) ----
enum DepthBand : uint8_t {
  DEPTH_REAL = 0,
  DEPTH_SHALLOW,
  DEPTH_DEEP,
  DEPTH_DEEPER,
  DEPTH_ABYSS,       // densest persistent / authored share
  DEPTH_BAND_COUNT
};

// ---- career tracks ----
enum CareerId : uint8_t {
  CR_HAULER = 0,
  CR_GUNHAND,
  CR_PROSPECTOR,
  CR_RESCUER,
  CR_TRADER,
  CR_WANDERER,
  CR_DEPTHRUNNER,
  CR_GHOST,
  CR_COUNT
};

static inline const char *careerName(CareerId id) {
  static const char *n[] = {
    "hauler", "gunhand", "prospector", "rescuer",
    "trader", "wanderer", "depthrunner", "ghost"
  };
  return id < CR_COUNT ? n[id] : "?";
}

// ---- capability families (tier 0 = none/baseline) ----
enum CapId : uint8_t {
  CAP_WEAPONS = 0,
  CAP_SHIELDS,
  CAP_MINING,
  CAP_SCANNERS,
  CAP_TRAILER,
  CAP_STABILIZER,    // mcguffin: +depth band help
  CAP_BULKHEADS,     // meta-material depth spine
  CAP_CLOAK,
  CAP_FUELSYS,       // scoops: faster, richer; portals: cheaper
  CAP_COUNT
};

static inline const char *capName(CapId id) {
  static const char *n[] = {
    "weapons", "shields", "mining", "scanners",
    "trailer", "stabilizer", "bulkheads", "cloak", "fuel system"
  };
  return id < CAP_COUNT ? n[id] : "?";
}

// ---- faction heat channels (weather, not a social app) ----
enum HeatId : uint8_t {
  HEAT_SECURITY = 0,
  HEAT_PIRATE,
  HEAT_HOUSE,
  HEAT_UNDER,        // sub-ecology notice
  HEAT_COUNT
};

// ---- story flag ----
struct Flag {
  char     key[FLAG_LEN];
  int8_t   value;          // usually 1; can stack lightly
  uint8_t  persist;        // 1 = survives universe wipe (rare, deep-earned)
};

// ---- rumor / known place name (wiped on destroy) ----
struct NameTag {
  char     name[NAME_LEN];
  uint8_t  depthHint;      // suggested depth rating 0..N
  uint8_t  ttl;            // rumors decay; known gates can be sticky within a universe
  uint8_t  kind;           // 0 rumor, 1 gate, 2 station, 3 lead
};

// ---- haul line (fact, not inventory UI) ----
struct HaulLine {
  char     what[NAME_LEN];
  uint16_t amount;
  uint8_t  legal;          // 0 hot, 1 clean
};

// ---- persistent deep landmark (cross-universe) ----
struct Landmark {
  char     name[NAME_LEN];
  uint8_t  band;           // DEPTH_DEEP+
  uint8_t  handmade;       // 1 = authored signature
  uint32_t id;             // stable id
};

// ---- pilot sheet (the soul) ----
// The ships a pilot can own. The Mantis is the license ship: always yours, its fit survives the pod.
enum ShipType : uint8_t { SHIP_MANTIS = 0, SHIP_FALCOR, SHIP_HONEYBEE, SHIP_MALTESE, SHIP_GHOST, SHIP_COUNT };
struct ShipRecord {
  uint8_t owned, lost, backup, pad;       // backup: the hangar teleporter has a record of it
  uint8_t cap[CAP_COUNT];                 // its fit, while it sits in a hangar
  uint8_t backupCap[CAP_COUNT];           // its fit, as last teleported (what a recovery restores)
};
struct ShipSpec {
  const char *name, *maker, *role;
  int32_t price;
  uint8_t holdBase;
  float speed, boost, turn, agility;      // cruise speed, top-end boost, turn rate, how quickly it answers
  uint8_t deepCalm;                       // stabilizer ratings it adds in subspace
};
inline const ShipSpec &shipSpec(uint8_t t) {
  static const ShipSpec S[SHIP_COUNT] = {
    {"MANTIS",          "LIMINAR LICENSE", "ALL-ROUNDER",       0,     20, 1.00f, 1.0f, 1.00f, 1.0f, 0},
    {"FALCOR",          "LIMINAR TRANSIT", "INTERCEPTOR",       6400,  10, 1.35f, 1.1f, 1.40f, 1.6f, 0},
    {"HONEYBEE",        "DESERET",         "MINING RIG",        5200,  60, 0.88f, 0.9f, 0.90f, 0.9f, 0},
    {"MALTESE",         "MALTAPLEX",       "LUXURY CRUISER",    9200,  32, 1.05f, 1.0f, 1.00f, 1.1f, 0},
    {"GHOSTFLEET FLAGSHIP", "GHOSTFLEET",  "DEEP RUNNER",       0,     24, 1.00f, 2.6f, 1.10f, 1.3f, 1}};
  return S[t < SHIP_COUNT ? t : 0];
}

struct Pilot {
  // identity / meta
  uint32_t lives;          // destructions survived
  uint32_t universeSeed;   // current cosmos
  uint32_t rng;            // soft state

  // economy
  int32_t  credits;
  uint16_t fuel;           // 0..fuelCap
  uint16_t fuelCap;
  uint16_t holdUsed;
  uint16_t holdCap;

  // integrity
  uint16_t hull;           // 0..hullMax
  uint16_t hullMax;

  // careers
  uint8_t  rank[CR_COUNT];
  uint16_t xp[CR_COUNT];

  // capabilities: tier earned (0 = baseline)
  uint8_t  cap[CAP_COUNT];

  // heat weather 0..100
  uint8_t  heat[HEAT_COUNT];

  // cargo facts
  uint8_t  haulN;
  HaulLine haul[MAX_HAUL_LINES];

  // story
  uint8_t  flagN;
  Flag     flags[MAX_FLAGS];

  // local knowledge — WIPED on destroy
  uint8_t  rumorN;
  NameTag  rumors[MAX_RUMORS];
  uint8_t  knownN;
  NameTag  known[MAX_KNOWN_GATES];

  // the hangar: owned ships and which one is flying
  uint8_t  activeShip;
  ShipRecord ships[SHIP_COUNT];
};

// ---- derived queries (pure functions of Pilot) ----
struct DepthAbility {
  uint8_t maxBand;         // highest band ship can attempt safely
  uint8_t stabilizer;      // 1 if mcguffin can push +1 under stress
  float   glitchRisk;      // 0..1 if attempting `attemptBand`
};

// Place names travel through the game in mixed case (rumors, the board) and in
// upper case (gate labels, the HUD). They are the same place either way.
inline bool sameName(const char *a, const char *b) {
  if (!a || !b) return false;
  for (int i = 0; i < NAME_LEN; i++) {
    char x = a[i], y = b[i];
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return false;
    if (!x) return true;
  }
  return true;
}

}  // namespace sm
