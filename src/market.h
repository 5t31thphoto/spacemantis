#pragma once
// ============================================================
//  SpaceMantis — the market at a dock
//  Prices start from the drifting commodity weather (sim) and are shaped by
//  who runs the dock (what they make cheap, what they pay a premium for), the
//  local star, the bodies in the system, and a little of the place itself.
// ============================================================
#include <stdint.h>

namespace sm {

enum Good : uint8_t { G_RATIONS = 0, G_WATER, G_ORE, G_ALLOYS, G_PARTS, G_MEDICINE, G_ELECTRONICS, G_SPICE, G_COUNT };

struct DockEcon {
  uint8_t brand;       // 0 Liminar, 1 Portex, 2 MaltaPlex, 3 Deseret, 4 Freehold
  uint8_t star;        // 0 red dwarf .. 6 white dwarf
  bool giant, rocky, rocks, outpost;
  uint8_t layer;
  uint32_t hash;       // the place
};

const char *goodName(Good g);     // display
const char *goodHold(Good g);     // hold line name
void marketSetDock(const DockEcon &d);
bool marketTrades(Good g);        // outposts carry a short list
int marketBuy(Good g);            // what the dock charges per unit
int marketSell(Good g);           // what the dock pays per unit
int8_t marketMood(Good g);        // -1 cheap here (they make it), +1 they need it, 0 neither
int marketSellLine(const char *holdName, bool legal);   // any hold line, per unit (0 = no buyer)

}  // namespace sm
