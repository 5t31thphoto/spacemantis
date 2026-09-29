#include "trip.h"
#include "sheet.h"
#include "universe.h"
#include <string.h>

namespace sm {
namespace { Trip s_t{}; }

Trip &trip() { return s_t; }

void tripBegin(const char *dest, uint8_t depth, bool unknown, bool fixedPoint) {
  memset(&s_t, 0, sizeof(s_t));
  s_t.active = 1;
  strncpy(s_t.dest, dest ? dest : "?", NAME_LEN - 1);
  s_t.destDepth = depth < 1 ? 1 : (depth > DEPTH_BAND_COUNT - 1 ? DEPTH_BAND_COUNT - 1 : depth);
  s_t.layer = 0;
  s_t.step = 1;            // the gate you chose is the first gate of the first leg
  s_t.unknown = unknown ? 1 : 0;
  s_t.fixedPoint = fixedPoint ? 1 : 0;
}

void tripEnd() { memset(&s_t, 0, sizeof(s_t)); }

void tripGate() { if (s_t.active && s_t.step < 2) s_t.step++; }
bool tripPortalReady() { return s_t.active && s_t.step >= 2; }
bool tripPortalGoesUp() { return s_t.ascending != 0; }

uint8_t tripPortalFuel() {
  if (!s_t.active) return 0;
  return s_t.ascending ? 3 : (uint8_t)(5 + 2 * s_t.layer);
}

Crossing tripCross() {
  Crossing c{};
  c.from = c.to = (int8_t)s_t.layer;
  if (!s_t.active) return c;
  Pilot &p = sheet();
  uint16_t cost = tripPortalFuel();

  if (!s_t.ascending) {
    if (p.fuel < cost) {
      // The dive stops here. The chain turns and climbs; you will surface
      // somewhere, just not where you meant to.
      c.refused = true;
      s_t.ascending = 1;
      s_t.turnedBack = 1;
      s_t.step = 0;
      return c;
    }
    burnFuel(cost); c.fuel = cost;
    uint8_t to = (uint8_t)(s_t.layer + 1);
    DepthAbility da = depthQuery(to);
    if (to > da.maxBand && urandf() < da.glitchRisk) {
      c.glitched = true;
      c.damage = (int16_t)urand(10, 28);
    }
    s_t.layer = to;
    if (s_t.layer >= s_t.destDepth) { s_t.ascending = 1; c.turnPoint = true; }
  } else {
    if (p.fuel < cost) {
      c.dry = true;
      c.damage = (int16_t)(8 + (cost - p.fuel) * 2);
      c.fuel = p.fuel;
      setFuel(0);
    } else { burnFuel(cost); c.fuel = cost; }
    // coming up from past your rating still shakes the hull
    DepthAbility da = depthQuery(s_t.layer);
    if (s_t.layer > da.maxBand && urandf() < da.glitchRisk * 0.5f) {
      c.glitched = true;
      c.damage = (int16_t)(c.damage + urand(6, 16));
    }
    s_t.layer = (uint8_t)(s_t.layer - 1);
    if (s_t.layer == 0) c.arrived = true;
  }
  c.to = (int8_t)s_t.layer;
  s_t.legs++;
  s_t.step = 0;
  return c;
}

}  // namespace sm
