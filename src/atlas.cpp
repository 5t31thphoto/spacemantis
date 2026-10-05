#include "atlas.h"
#include "sheet.h"
#include "universe.h"
#include "sim.h"
#include "content.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace sm {
namespace {
Atlas s_a{};
const uint8_t ATLAS_VERSION = 1;

int newPlace(const char *name, uint8_t depth, uint8_t flags) {
  int i = atlasFind(name);
  if (i >= 0) { s_a.place[i].flags |= flags; return i; }
  int slot = -1;
  for (int k = 0; k < ATLAS_PLACES; k++) if (!(s_a.place[k].flags & AP_USED)) { slot = k; break; }
  if (slot < 0) {
    // full: forget the stalest surface name that isn't where we are
    uint16_t best = 0xFFFF;
    for (int k = 0; k < ATLAS_PLACES; k++) {
      AtlasPlace &p = s_a.place[k];
      if ((p.flags & AP_FIXED) || k == s_a.here) continue;
      uint16_t age = (p.flags & AP_VISITED) ? p.lastSeen : (uint16_t)(p.lastSeen / 2);   // unvisited names go first
      if (age < best) { best = age; slot = k; }
    }
    if (slot < 0) return -1;
    for (auto &l : s_a.link) if ((l.flags & AL_USED) && (l.a == slot || l.b == slot)) l.flags = 0;
  }
  AtlasPlace &p = s_a.place[slot];
  memset(&p, 0, sizeof(p));
  strncpy(p.name, name, NAME_LEN - 1);
  p.depth = depth < 1 ? 1 : (depth > 4 ? 4 : depth);
  p.flags = (uint8_t)(AP_USED | flags);
  p.lastSeen = s_a.clock;
  return slot;
}

void addLink(int a, int b, uint8_t depth, uint8_t flags) {
  if (a < 0 || b < 0 || a == b) return;
  for (auto &l : s_a.link)
    if ((l.flags & AL_USED) && ((l.a == a && l.b == b) || (l.a == b && l.b == a))) {
      l.flags |= flags;
      if (flags & AL_FLOWN) l.flags &= (uint8_t)~AL_TETHER;
      return;
    }
  for (auto &l : s_a.link)
    if (!(l.flags & AL_USED)) { l = {(uint8_t)a, (uint8_t)b, depth, (uint8_t)(AL_USED | flags)}; return; }
  // full: drop a tether or an unflown lane far from here
  for (auto &l : s_a.link)
    if (!(l.flags & AL_FLOWN) && l.a != s_a.here && l.b != s_a.here) { l = {(uint8_t)a, (uint8_t)b, depth, (uint8_t)(AL_USED | flags)}; return; }
}

int laneCount(int a) {
  int n = 0;
  for (auto &l : s_a.link) if ((l.flags & AL_USED) && (l.a == a || l.b == a)) n++;
  return n;
}

}  // namespace

Atlas &atlas() { return s_a; }

int atlasFind(const char *name) {
  if (!name || !name[0]) return -1;
  for (int k = 0; k < ATLAS_PLACES; k++)
    if ((s_a.place[k].flags & AP_USED) && sameName(s_a.place[k].name, name)) return k;
  return -1;
}

bool atlasLinked(int a, int b) {
  for (auto &l : s_a.link) if ((l.flags & AL_USED) && ((l.a == a && l.b == b) || (l.a == b && l.b == a))) return true;
  return false;
}

void atlasVisit(const char *name, const char *cameFrom, uint8_t depth, bool flown) {
  s_a.clock++;
  int from = atlasFind(cameFrom);
  int here = newPlace(name, depth, 0);
  if (here < 0) return;
  AtlasPlace &h = s_a.place[here];
  bool firstTime = !(h.flags & AP_VISITED);
  h.flags = (uint8_t)((h.flags | AP_VISITED) & ~AP_RUMOR);
  h.lastSeen = s_a.clock;
  s_a.here = (uint8_t)here;
  if (from >= 0 && from != here) addLink(from, here, depth, flown ? AL_FLOWN : 0);
  if (firstTime) {
    // unvisited systems form gates that could go about anywhere
    int want = (int)urand(3, 5);
    for (int guard = 0; laneCount(here) < want && guard < 10; guard++) {
      if (urandf() < 0.3f) {
        // sometimes a lane runs back to a name already seen but never visited
        int pick = -1, n = 0;
        for (int k = 0; k < ATLAS_PLACES; k++) {
          const AtlasPlace &p = s_a.place[k];
          if (!(p.flags & AP_USED) || k == here || (p.flags & AP_FIXED) || atlasLinked(here, k)) continue;
          if (!(p.flags & AP_VISITED) && urand(0, ++n) == 0) pick = k;
        }
        if (pick >= 0) { addLink(here, pick, s_a.place[pick].depth, 0); continue; }
      }
      char nm[NAME_LEN];
      strncpy(nm, placeName(urand(), DEPTH_REAL, false), NAME_LEN - 1); nm[NAME_LEN - 1] = 0;
      if (atlasFind(nm) >= 0) continue;
      addLink(here, newPlace(nm, placeDepth(nm), 0), placeDepth(nm), 0);
    }
    // a charted fixed point is sometimes reachable from here, in any sky
    if (urandf() < 0.55f) {
      int n = landmarkCount(), start = n ? (int)(urand() % (uint32_t)n) : 0;
      for (int k = 0; k < n; k++) {
        const Landmark *lm = landmarkAt((start + k) % n);
        if (!lm || !landmarkDiscovered(lm->id)) continue;
        addLink(here, newPlace(lm->name, lm->band, AP_FIXED), lm->band, 0);
        break;
      }
    }
  }
}

void atlasRumor(const char *name, uint8_t depth, bool landmark) {
  if (!name || !name[0]) return;
  int r = atlasFind(name);
  if (r >= 0 && (s_a.place[r].flags & AP_VISITED)) return;   // already been there
  r = newPlace(name, depth, (uint8_t)(AP_RUMOR | (landmark ? AP_LANDMARK_RUMOR : 0)));
  if (s_a.here != 255 && r >= 0) addLink(s_a.here, r, depth, AL_TETHER);
}

int atlasLanesHere(AtlasLane *out, int maxOut) {
  int n = 0, h = s_a.here;
  if (h == 255) return 0;
  // flown and visited lanes first, so the way back is never the one left out
  for (int pass = 0; pass < 2; pass++)
    for (auto &l : s_a.link) {
      if (!(l.flags & AL_USED) || n >= maxOut) continue;
      int o = l.a == h ? l.b : (l.b == h ? l.a : -1);
      if (o < 0) continue;
      bool strong = (l.flags & AL_FLOWN) || (s_a.place[o].flags & AP_VISITED);
      if ((pass == 0) != strong) continue;
      const AtlasPlace &p = s_a.place[o];
      AtlasLane &L = out[n++];
      strncpy(L.name, p.name, NAME_LEN - 1); L.name[NAME_LEN - 1] = 0;
      L.depth = p.depth;
      L.visited = (p.flags & AP_VISITED) != 0;
      L.rumor = (p.flags & AP_RUMOR) != 0;
      L.fixed = (p.flags & AP_FIXED) != 0 || (p.flags & AP_LANDMARK_RUMOR) != 0;
    }
  return n;
}

void atlasWipe() {
  // the pod surfaces in a sky with no names; only fixed points are still fixed
  for (auto &l : s_a.link) l.flags = 0;
  for (auto &p : s_a.place) if (!(p.flags & AP_FIXED)) memset(&p, 0, sizeof(p));
    else p.flags = (uint8_t)(AP_USED | AP_FIXED);
  s_a.here = 255;
}

void atlasClear() { memset(&s_a, 0, sizeof(s_a)); s_a.here = 255; }

void atlasFixedPoint(const char *name, uint8_t band) {
  int f = newPlace(name, band, AP_FIXED);
  if (f < 0) return;
  s_a.place[f].flags = (uint8_t)((s_a.place[f].flags | AP_FIXED) & ~(AP_RUMOR | AP_LANDMARK_RUMOR));
  if (s_a.here != 255) addLink(s_a.here, f, band, AL_FLOWN);
}

int atlasPath(int from, int to, int *out, int maxOut) {
  if (from < 0 || to < 0 || from >= ATLAS_PLACES || to >= ATLAS_PLACES) return 0;
  int prev[ATLAS_PLACES], q[ATLAS_PLACES], qh = 0, qt = 0;
  for (int i = 0; i < ATLAS_PLACES; i++) prev[i] = -2;
  prev[from] = -1; q[qt++] = from;
  while (qh < qt) {
    int c = q[qh++];
    if (c == to) break;
    // a fixed point is somewhere you go, not a way through: its climb out lands anywhere
    if (c != from && (s_a.place[c].flags & AP_FIXED)) continue;
    for (auto &l : s_a.link) {
      if (!(l.flags & AL_USED)) continue;
      int o = l.a == c ? l.b : (l.b == c ? l.a : -1);
      if (o < 0 || prev[o] != -2) continue;
      prev[o] = c; q[qt++] = o;
    }
  }
  if (prev[to] == -2) return 0;
  int tmp[ATLAS_PLACES], n = 0;
  for (int c = to; c != -1 && n < ATLAS_PLACES; c = prev[c]) tmp[n++] = c;
  int m = n < maxOut ? n : maxOut;
  for (int i = 0; i < m; i++) out[i] = tmp[n - 1 - i];
  return m;
}

bool atlasSave() {
  Preferences prefs;
  if (!prefs.begin("sm_atlas", false)) return false;
  s_a.version = ATLAS_VERSION;
  prefs.putBytes("mem", &s_a, sizeof(s_a));
  prefs.end();
  return true;
}

bool atlasLoad() {
  Preferences prefs;
  memset(&s_a, 0, sizeof(s_a));
  s_a.here = 255;
  if (!prefs.begin("sm_atlas", true)) return false;
  bool ok = false;
  if (prefs.getBytesLength("mem") == sizeof(s_a)) {
    Atlas a;
    prefs.getBytes("mem", &a, sizeof(a));
    if (a.version == ATLAS_VERSION) { s_a = a; ok = true; }
  }
  prefs.end();
  // a restored pilot is "still lost": no current place until the next arrival
  s_a.here = 255;
  return ok;
}

}  // namespace sm
