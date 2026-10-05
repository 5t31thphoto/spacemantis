#pragma once
// ============================================================
//  SpaceMantis — the atlas: the pilot's memory of subspace lanes
//  No coordinates. A place is a name; a lane is a gate that joins two names
//  through subspace, with a depth. Places keep their lanes until the pod is
//  launched (a wipe); charted fixed points survive every wipe.
// ============================================================
#include "sheet_types.h"

namespace sm {

static const int ATLAS_PLACES = 40;
static const int ATLAS_LINKS = 72;
static const int ATLAS_VISITED_KEEP = 20;   // the pilot forgets the oldest beyond this

enum AtlasPlaceFlags : uint8_t {
  AP_USED = 1, AP_VISITED = 2, AP_RUMOR = 4, AP_FIXED = 8, AP_LANDMARK_RUMOR = 16,
  AP_STATION_SET = 32, AP_HAS_STATION = 64     // a place keeps its dock (or lack of one) until a wipe
};
enum AtlasLinkFlags : uint8_t { AL_USED = 1, AL_FLOWN = 2, AL_TETHER = 4 };

struct AtlasPlace {
  char name[NAME_LEN];
  uint8_t depth;       // how deep a dive reaches it
  uint8_t flags;
  uint16_t lastSeen;   // visit clock, for forgetting
};
struct AtlasLink { uint8_t a, b, depth, flags; };

struct Atlas {
  uint8_t version;
  uint8_t here;        // index of the current place (255 = none)
  uint16_t clock;
  AtlasPlace place[ATLAS_PLACES];
  AtlasLink link[ATLAS_LINKS];
};

Atlas &atlas();
int atlasFind(const char *name);
// Arrive at a place. A place seen for the first time deals its own lanes:
// mostly to places nobody has heard of, sometimes back to names already seen.
void atlasVisit(const char *name, const char *cameFrom, uint8_t depth, bool flown);
// A rumor heard here opens a lane from here to somewhere new.
void atlasRumor(const char *name, uint8_t depth, bool landmark);
// Lanes from the current place, for the gate field. Returns count.
struct AtlasLane { char name[NAME_LEN]; uint8_t depth; bool visited, rumor, fixed; };
int atlasLanesHere(AtlasLane *out, int maxOut);
// The pod launched: surface memory is gone; fixed points stay.
void atlasWipe();
void atlasClear();                                   // a brand-new pilot remembers nothing
// A charted deep landmark, threaded to the surface place the dive started from.
void atlasFixedPoint(const char *name, uint8_t band);
bool atlasLinked(int a, int b);
int atlasPath(int from, int to, int *out, int maxOut);   // fewest hops; returns length or 0
bool atlasSave();
bool atlasLoad();

}  // namespace sm
