#pragma once
// ============================================================
//  SpaceMantis — pilot spreadsheet API
//  Flight/nav owns presentation; this owns truth.
// ============================================================
#include "sheet_types.h"

namespace sm {

void sheetInit();                          // cold boot defaults
Pilot &sheet();                            // mutable live pilot

// ---- lifecycle ----
void onDestroy();                          // new universe; wipe place-knowledge; keep self/fit
void onResurface();                        // optional hooks after dive return

// ---- economy / hull ----
void addCredits(int32_t d);
bool spendCredits(int32_t d);
void setFuel(uint16_t v);
bool burnFuel(uint16_t v);
void repairHull(uint16_t v);
void damageHull(uint16_t v);               // may trigger destroy if 0

// ---- career ----
void grantXp(CareerId id, uint16_t amount);
uint8_t rankOf(CareerId id);

// ---- capability (earned → equipped, no slots) ----
void earnCap(CapId id, uint8_t tier);      // only raises tier, never manages slots
uint8_t capTier(CapId id);

// ---- depth ----
DepthAbility depthQuery(uint8_t attemptBand);
uint8_t depthRating();                     // bulkheads + stabilizer contribution

// ---- heat ----
void addHeat(HeatId id, int16_t d);
void coolHeat(uint8_t amount);             // global tick

// ---- haul facts ----
bool haulAdd(const char *what, uint16_t amount, bool legal);
void haulClear();
uint16_t haulUsed();

// ---- flags (emergent story) ----
void flagSet(const char *key, int8_t value = 1, bool persist = false);
int8_t flagGet(const char *key);           // 0 if absent
bool flagHas(const char *key);

// ---- knowledge (universe-local; wiped on destroy) ----
bool rumorAdd(const char *name, uint8_t depthHint, uint8_t ttl = 12);
bool knownGateAdd(const char *name, uint8_t depthHint);
void knowledgeTick();                      // TTL decay on rumors
uint8_t rumorCount();
uint8_t knownGateCount();
const NameTag *rumorAt(uint8_t i);
const NameTag *knownAt(uint8_t i);

// ---- persistence (NVS later; RAM for now) ----
bool sheetSave();
bool sheetLoad();

}  // namespace sm
