#pragma once
// ============================================================
//  SpaceMantis — a trip through subspace
//  Fly into a named gate: that is the choice. It spawns the next gate, and the
//  third gate is a portal. Each portal moves exactly one layer. You dive until
//  the destination's depth, the chain turns, you climb, and you resurface at
//  the place — very far across the universe.
// ============================================================
#include "sheet_types.h"

namespace sm {

struct Trip {
  uint8_t active;
  char dest[NAME_LEN];
  uint8_t destDepth;     // 1..4: how deep the dive to get there goes
  uint8_t layer;         // current layer, 0 = real space
  uint8_t ascending;     // 1 once the turn point is passed (or the dive was abandoned)
  uint8_t step;          // gates threaded on this leg; the portal comes after 2
  uint8_t unknown;       // a place never heard of
  uint8_t fixedPoint;    // a charted deep landmark (persistent)
  uint8_t turnedBack;    // dive abandoned: the climb lands somewhere else
  uint8_t legs;          // portals crossed this trip
};

struct Crossing {
  int8_t from, to;
  bool refused;          // descending portal would not take a dry ship; trip turns back
  bool turnPoint;        // reached the destination depth on this crossing
  bool arrived;          // back in real space at the destination
  bool glitched;         // pushed past rating and reality slipped
  bool dry;              // climbed without fuel
  int16_t damage;        // hull damage for the caller to apply
  uint16_t fuel;         // fuel burned
};

Trip &trip();
void tripBegin(const char *dest, uint8_t depth, bool unknown, bool fixedPoint);
void tripEnd();
void tripGate();                 // a chain gate was threaded
bool tripPortalGoesUp();         // direction of the next portal
bool tripPortalReady();          // two gates threaded on this leg
Crossing tripCross();            // fly through the portal
uint8_t tripPortalFuel();        // what the next portal will cost

}  // namespace sm
