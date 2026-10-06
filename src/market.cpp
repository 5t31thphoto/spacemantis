#include "market.h"
#include "sim.h"
#include <string.h>

namespace sm {
namespace {
DockEcon s_d{};
const char *NAMES[G_COUNT] = {"RATIONS", "WATER", "ORE", "ALLOYS", "MACHINE PARTS", "MEDICINE", "ELECTRONICS", "SPICE"};
const char *HOLD[G_COUNT] = {"rations", "water", "ore", "alloys", "machine parts", "medicine", "electronics", "spice"};

// what each builder makes (cheap here) and needs (premium here)
const uint8_t MAKES[5] = {
  1 << G_ELECTRONICS,                          // Liminar: transit tech
  (1 << G_RATIONS) | (1 << G_SPICE),           // Portex: provisions and luxuries
  (1 << G_ALLOYS) | (1 << G_PARTS),            // MaltaPlex: mega-structures
  (1 << G_WATER) | (1 << G_RATIONS),           // Deseret: colonial habitats
  1 << G_ORE};                                 // Freehold: the frontier digs
const uint8_t NEEDS[5] = {
  (1 << G_RATIONS) | (1 << G_MEDICINE),
  (1 << G_PARTS) | (1 << G_ELECTRONICS),
  (1 << G_ORE) | (1 << G_WATER),
  (1 << G_MEDICINE) | (1 << G_ELECTRONICS),
  (1 << G_RATIONS) | (1 << G_WATER) | (1 << G_MEDICINE) | (1 << G_PARTS)};

float unitValue(Good g) {
  float p = (float)marketPrice(HOLD[g], true);
  uint8_t b = s_d.brand < 5 ? s_d.brand : 4;
  if (MAKES[b] & (1 << g)) p *= 0.72f;
  if (NEEDS[b] & (1 << g)) p *= (b == 4 ? 1.5f : (b == 3 ? 1.6f : 1.38f));   // Freehold and Deseret pay most for what they need
  bool hot = s_d.star == 3 || s_d.star == 4 || s_d.star == 6, cool = s_d.star == 0 || s_d.star == 1 || s_d.star == 5;
  if (hot && (g == G_ALLOYS || g == G_ELECTRONICS)) p *= 0.86f;                // cheap energy: smelting and fabs
  if (cool && (g == G_SPICE || g == G_MEDICINE)) p *= 1.12f;
  if (s_d.giant && g == G_WATER) p *= 0.75f;
  if (s_d.rocky && g == G_RATIONS) p *= 0.85f;
  if (s_d.rocks && g == G_ORE) p *= 0.8f;
  uint32_t h = s_d.hash * 2654435761u + g * 40503u; h ^= h >> 13;
  p *= 0.92f + (h % 17) * 0.01f;                                               // a little of the place itself
  return p < 2.f ? 2.f : p;
}
}  // namespace

const char *goodName(Good g) { return g < G_COUNT ? NAMES[g] : "?"; }
const char *goodHold(Good g) { return g < G_COUNT ? HOLD[g] : ""; }
void marketSetDock(const DockEcon &d) { s_d = d; }

bool marketTrades(Good g) {
  if (!s_d.outpost) return true;
  uint8_t b = s_d.brand < 5 ? s_d.brand : 4;
  return ((MAKES[b] | NEEDS[b]) & (1 << g)) || g == G_RATIONS || g == G_WATER;   // outposts carry a short list
}

int marketBuy(Good g) { return (int)(unitValue(g) * 1.1f + 0.5f); }
int marketSell(Good g) {
  float v = unitValue(g) * 0.9f;
  return (int)(v + 0.5f);
}
int8_t marketMood(Good g) {
  uint8_t b = s_d.brand < 5 ? s_d.brand : 4;
  if (NEEDS[b] & (1 << g)) return 1;
  if (MAKES[b] & (1 << g)) return -1;
  return 0;
}

int marketSellLine(const char *name, bool legal) {
  if (!name) return 0;
  for (int g = 0; g < G_COUNT; g++) if (strcmp(name, HOLD[g]) == 0) return marketSell((Good)g);
  if (strcmp(name, "fixed point scan") == 0) return 0;          // only the ghost fleet buys these
  bool scan = strcmp(name, "anomaly scan") == 0;
  if (scan) return s_d.layer > 0 ? 60 + 40 * s_d.layer : 0;     // nobody up here believes in them
  int p = marketPrice(name, legal);
  bool rock = strcmp(name, "rock") == 0 || strcmp(name, "ore") == 0 || strcmp(name, "scrap") == 0;
  if (s_d.layer > 0 && (strcmp(name, "rock") == 0 || strcmp(name, "ore") == 0)) p *= 2;   // deep outposts pay double for rock
  (void)rock;
  return p;
}

}  // namespace sm
