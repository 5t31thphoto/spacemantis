// ============================================================
//  SpaceMantis — flight surface over the spreadsheet soul
//  "Elite Dangerous on a flippin ESP32" — career, not cartography.
//
//  Real space is where ordinary business happens: stations, traders, pirates,
//  rocks, gas giants. Nobody keeps coordinates. Fly into a named gate and you
//  have chosen where to go: it spawns the next gate, and the third is a portal.
//  Each portal is one layer down. Dive to the place's depth, the chain turns,
//  climb, and resurface there — far across a universe with no map.
//
//  Exploring real space pays experience. Finding ways through the deep pays
//  money: the deeper the layer, the more compressed and connected space is,
//  and a route through it is the most valuable thing a pilot can carry.
// ============================================================
#include <M5Unified.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vec3.h"
#include "haptics.h"
#include "mantis_pod_icon.h"
#include "mantis_body_icon.h"
#include "pilot_helmet_art.h"
#include "atlas.h"
#include "journal.h"
#include "savefile.h"
#include <Preferences.h>
#include "ship_art.h"
#include "logos_art.h"
#include "equipment_art.h"
#include "market.h"
#include "ships_art.h"
#include "goods_art.h"
#include "signals.h"
#include "trip.h"
#include "sheet.h"
#include "universe.h"
#include "resolve.h"
#include "contracts.h"
#include "sim.h"
#include "content.h"
#include "lore.h"

static constexpr int W = 320, H = 240;
static constexpr float FOCAL = 165.f;
static constexpr int LAYERS = sm::DEPTH_BAND_COUNT;   // 0 real .. 4 deep cove

static M5Canvas cv(&M5.Display);

// ============================================================
//  small helpers
// ============================================================
static float tNow = 0, dt = 0.016f;
static uint32_t rngState = 0xA341316Cu;
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
static float rf(float a, float b) { return a + (b - a) * ((rnd() & 0xFFFF) / 65535.f); }
static int ri(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static void unrgb(uint16_t c, int &r, int &g, int &b) { r = (c >> 8) & 0xF8; g = (c >> 3) & 0xFC; b = (c << 3) & 0xF8; }
static uint16_t shade(uint16_t c, float k) { int r, g, b; unrgb(c, r, g, b); return rgb((int)(r * k), (int)(g * k), (int)(b * k)); }
static uint16_t mix565(uint16_t a, uint16_t b, float t) {
  int r1, g1, b1, r2, g2, b2; unrgb(a, r1, g1, b1); unrgb(b, r2, g2, b2);
  return rgb((int)(r1 + (r2 - r1) * t), (int)(g1 + (g2 - g1) * t), (int)(b1 + (b2 - b1) * t));
}
static uint16_t hsv(float h, float s, float v) {
  h = fmodf(h, 360.f); if (h < 0) h += 360.f;
  s = clampf(s, 0, 1); v = clampf(v, 0, 1);
  float c = v * s, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
  float R = 0, G = 0, B = 0;
  if (h < 60) { R = c; G = x; } else if (h < 120) { R = x; G = c; } else if (h < 180) { G = c; B = x; }
  else if (h < 240) { G = x; B = c; } else if (h < 300) { R = x; B = c; } else { R = c; B = x; }
  return rgb((int)((R + m) * 255), (int)((G + m) * 255), (int)((B + m) * 255));
}

// fast sine for per-block fields
static float s_sinTab[1024];
static void initSin() { for (int i = 0; i < 1024; i++) s_sinTab[i] = sinf(i * 6.2831853f / 1024.f); }
static inline float fsin(float x) { int i = (int)floorf(x * 162.97466f); return s_sinTab[i & 1023]; }
static inline float fcos(float x) { return fsin(x + 1.5707963f); }

// The 6x8 font is ASCII only; content text uses em dashes.
static void asciiCopy(char *dst, size_t cap, const char *src) {
  size_t o = 0;
  if (!src) src = "";
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < cap; ) {
    if (*p < 0x80) { dst[o++] = (char)*p++; continue; }
    int n = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    for (int k = 0; k < n && *p; k++) p++;
    dst[o++] = '-';
  }
  dst[o] = 0;
}
static void upcase(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

// ---- banner: two lines, queued so nothing important is stomped ----
static char banner[112] = "";
static uint32_t bannerUntil = 0;
static char queued[3][112];
static uint16_t queuedMs[3];
static uint8_t queuedN = 0;
static void setBanner(const char *s, uint32_t ms = 2200) { asciiCopy(banner, sizeof(banner), s); bannerUntil = millis() + ms; }
static void noteBanner(const char *s, uint16_t ms = 2600) {
  if (bannerUntil <= millis()) { setBanner(s, ms); return; }
  if (queuedN >= 3) return;
  asciiCopy(queued[queuedN], sizeof(queued[0]), s);
  queuedMs[queuedN++] = ms;
}
static void serviceBanner() {
  if (bannerUntil > millis() || !queuedN) return;
  setBanner(queued[0], queuedMs[0]);
  for (int i = 1; i < queuedN; i++) { memcpy(queued[i - 1], queued[i], sizeof(queued[0])); queuedMs[i - 1] = queuedMs[i]; }
  queuedN--;
}

// ============================================================
//  ship + camera
// ============================================================
static V3 shipPos{0, 0, 0}, prevShipPos{0, 0, 0};
static Basis shipB;
static float shipSpeed = 0, rateYaw = 0, ratePitch = 0, rateRoll = 0;
static float throttleT = 0.5f;           // slider: 0 stop, 0.5 cruise, 1 boost
static int layer = 0;                    // 0 real .. 4 deep cove
static float fovPulse = 1.f;
static float deepFlash = 0.f;            // palette inversion on the deepest heartbeat
static float crossFlash = 0.f;           // white-out on a portal crossing
static float hitFlash = 0.f;             // red edge when the hull is struck

// gravitational lens (screen space), set per frame by a collapsed star in view
static bool lensOn = false;
static bool alienLensOn = false;            // a visitor bends the deep around itself
static float alienLX = 0, alienLY = 0, alienLR = 0;
static float lensX = 0, lensY = 0, lensR = 0;

static inline void applyLens(float &sx, float &sy) {
  if (!lensOn) return;
  float dx = sx - lensX, dy = sy - lensY, d2 = dx * dx + dy * dy + 1.f;
  float push = clampf(lensR * lensR / d2, 0.f, 2.2f);
  sx += dx * push; sy += dy * push;
}
static float shakeX = 0.f, shakeY = 0.f;   // turbulence: the view shakes, the HUD does not
static inline bool project(V3 w, float &sx, float &sy, float &z) {
  V3 c = shipB.toLocal(w - shipPos);
  z = c.z;
  if (z < 0.35f) return false;
  float k = FOCAL * fovPulse / z;
  sx = W * 0.5f + shakeX + c.x * k; sy = H * 0.5f + shakeY - c.y * k;
  applyLens(sx, sy);
  return true;
}
static inline bool projectDir(V3 d, float &sx, float &sy) {
  V3 c = shipB.toLocal(d);
  if (c.z < 0.05f) return false;
  float k = FOCAL * fovPulse / c.z;
  sx = W * 0.5f + shakeX + c.x * k; sy = H * 0.5f + shakeY - c.y * k;
  applyLens(sx, sy);
  return true;
}
static inline bool onScreen(float sx, float sy, float m = 0) { return sx >= -m && sx < W + m && sy >= -m && sy < H + m; }

// ============================================================
//  meshes: convex hulls of small point sets, flat shaded
// ============================================================
struct Mesh {
  uint8_t nv = 0, nt = 0, ne = 0;
  V3 v[18];
  uint8_t t[40][3];
  V3 n[40];
  uint8_t e[64][4];   // a, b, face0, face1 (255 = none)
};
static bool faceHas(const Mesh &m, int f, uint8_t a, uint8_t b) {
  bool ha = m.t[f][0] == a || m.t[f][1] == a || m.t[f][2] == a;
  bool hb = m.t[f][0] == b || m.t[f][1] == b || m.t[f][2] == b;
  return ha && hb;
}
static void buildHull(Mesh &m, const V3 *pts0, int n, bool jitter = false) {
  // jitter: a hair of noise so big flat faces (hexagons, octagons) triangulate once, not every way
  V3 pts[18];
  for (int i = 0; i < n; i++) pts[i] = jitter ? pts0[i] + V3{rf(-1e-3f, 1e-3f), rf(-1e-3f, 1e-3f), rf(-1e-3f, 1e-3f)} : pts0[i];
  m.nv = (uint8_t)n; m.nt = 0; m.ne = 0;
  for (int i = 0; i < n; i++) m.v[i] = pts[i];
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      for (int k = j + 1; k < n; k++) {
        V3 nn = cross(pts[j] - pts[i], pts[k] - pts[i]);
        if (len(nn) < 1e-5f) continue;
        nn = norm(nn);
        float d = dot(nn, pts[i]);
        int pos = 0, neg = 0;
        for (int l = 0; l < n; l++) {
          if (l == i || l == j || l == k) continue;
          float s = dot(nn, pts[l]) - d;
          if (s > 1e-4f) pos++; else if (s < -1e-4f) neg++;
        }
        if ((pos && neg) || (!pos && !neg) || m.nt >= 40) continue;
        uint8_t a = (uint8_t)i, b = (uint8_t)j, c = (uint8_t)k;
        if (pos) { uint8_t tmp = b; b = c; c = tmp; nn = -nn; }   // outward
        m.t[m.nt][0] = a; m.t[m.nt][1] = b; m.t[m.nt][2] = c; m.n[m.nt] = nn; m.nt++;
      }
  // feature edges only: where the surface bends
  for (int f = 0; f < m.nt; f++)
    for (int s = 0; s < 3; s++) {
      uint8_t a = m.t[f][s], b = m.t[f][(s + 1) % 3];
      if (a > b) { uint8_t tmp = a; a = b; b = tmp; }
      bool have = false;
      for (int q = 0; q < m.ne; q++) if (m.e[q][0] == a && m.e[q][1] == b) { have = true; break; }
      if (have) continue;
      int f1 = 255; bool anyNeighbour = false;
      for (int g = 0; g < m.nt; g++) {
        if (g == f || !faceHas(m, g, a, b)) continue;
        anyNeighbour = true;
        if (dot(m.n[g], m.n[f]) < 0.995f) f1 = g;
      }
      bool feature = f1 != 255 || !anyNeighbour;
      if (feature && m.ne < 64) { m.e[m.ne][0] = a; m.e[m.ne][1] = b; m.e[m.ne][2] = (uint8_t)f; m.e[m.ne][3] = (uint8_t)f1; m.ne++; }
    }
}

enum MeshId : uint8_t { M_STATION = 0, M_COBRA, M_VIPER, M_SIDEWINDER, M_SHUTTLE, M_KRAIT, M_POD, M_TETRA, M_GHOST,
                        M_HEXCORE, M_RINGSEG, M_OCT, M_OCTWIDE, M_MODULE, M_PANEL, M_DOME, M_BASE, M_ROCK0, M_COUNT = M_ROCK0 + 6 };
// Big working buffers live in PSRAM (internal DRAM is small and the radio needs some);
// falls back to ordinary heap if PSRAM is ever absent.
static void *bigAlloc(size_t n) { void *p = ps_calloc(1, n); return p ? p : calloc(1, n); }
static Mesh *meshes = nullptr;

static void initMeshes() {
  if (!meshes) meshes = (Mesh *)bigAlloc(sizeof(Mesh) * M_COUNT);
  V3 st[12]; int k = 0;   // cuboctahedron — a nod to the Coriolis
  for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) { st[k++] = {(float)a, (float)b, 0}; st[k++] = {(float)a, 0, (float)b}; st[k++] = {0, (float)a, (float)b}; }
  buildHull(meshes[M_STATION], st, 12);
  const V3 cobra[] = {{-0.35f, 0.06f, 1.0f}, {0.35f, 0.06f, 1.0f}, {-1.35f, -0.04f, -0.3f}, {1.35f, -0.04f, -0.3f},
                      {-0.95f, 0.14f, -0.78f}, {0.95f, 0.14f, -0.78f}, {0, 0.36f, -0.55f}, {0, -0.26f, -0.62f}, {-0.55f, -0.18f, 0.35f}, {0.55f, -0.18f, 0.35f}};
  buildHull(meshes[M_COBRA], cobra, 10);
  const V3 viper[] = {{0, 0, 1.35f}, {-0.6f, 0, -0.7f}, {0.6f, 0, -0.7f}, {0, 0.3f, -0.62f}, {0, -0.2f, -0.62f}, {-0.3f, 0.13f, -0.82f}, {0.3f, 0.13f, -0.82f}};
  buildHull(meshes[M_VIPER], viper, 7);
  const V3 side[] = {{-1.05f, 0, -0.45f}, {1.05f, 0, -0.45f}, {-0.38f, 0.22f, 0.72f}, {0.38f, 0.22f, 0.72f}, {-0.38f, -0.12f, 0.72f}, {0.38f, -0.12f, 0.72f}, {0, 0.34f, -0.5f}, {0, -0.22f, -0.5f}};
  buildHull(meshes[M_SIDEWINDER], side, 8);
  const V3 shut[] = {{-0.42f, -0.35f, -0.8f}, {0.42f, -0.35f, -0.8f}, {-0.42f, 0.35f, -0.8f}, {0.42f, 0.35f, -0.8f},
                     {-0.45f, -0.3f, 0.45f}, {0.45f, -0.3f, 0.45f}, {-0.35f, 0.3f, 0.45f}, {0.35f, 0.3f, 0.45f}, {0, 0.05f, 1.05f}};
  buildHull(meshes[M_SHUTTLE], shut, 9);
  const V3 krait[] = {{0, 0, 1.4f}, {-1.3f, 0.05f, -0.2f}, {1.3f, 0.05f, -0.2f}, {0, 0.45f, -0.35f}, {0, -0.3f, -0.4f}, {-0.45f, 0, -0.95f}, {0.45f, 0, -0.95f}};
  buildHull(meshes[M_KRAIT], krait, 7);
  const V3 pod[] = {{0, 0, 0.9f}, {0, 0, -0.9f}, {0.6f, 0, 0}, {-0.6f, 0, 0}, {0, 0.6f, 0}, {0, -0.6f, 0}};
  buildHull(meshes[M_POD], pod, 6);
  const V3 tet[] = {{0, 1, 0}, {0.94f, -0.33f, 0}, {-0.47f, -0.33f, 0.82f}, {-0.47f, -0.33f, -0.82f}};
  buildHull(meshes[M_TETRA], tet, 4);
  // ghost fleet: long, thin, too quiet
  const V3 ghost[] = {{0, 0, 2.2f}, {-0.35f, 0, -1.6f}, {0.35f, 0, -1.6f}, {0, 0.22f, -1.2f}, {0, -0.22f, -1.2f}, {-0.9f, 0.02f, -0.4f}, {0.9f, 0.02f, -0.4f}};
  buildHull(meshes[M_GHOST], ghost, 7);
  // station parts. Liminar hexcore: a faceted hex body, narrow at both ends
  {
    V3 p[18];
    for (int i = 0; i < 6; i++) {
      float a = i * 1.0471976f + 0.5235988f;
      p[i] = {0.52f * cosf(a), 0.52f * sinf(a), 0.86f};
      p[i + 6] = {0.96f * cosf(a), 0.96f * sinf(a), 0.12f};
      p[i + 12] = {0.62f * cosf(a), 0.62f * sinf(a), -0.78f};
    }
    buildHull(meshes[M_HEXCORE], p, 18, true);
  }
  { // a ring segment: long along its own forward (the ring's tangent)
    const V3 b[] = {{-0.17f, -0.13f, -0.30f}, {0.17f, -0.13f, -0.30f}, {-0.17f, 0.13f, -0.30f}, {0.17f, 0.13f, -0.30f},
                    {-0.17f, -0.13f, 0.30f}, {0.17f, -0.13f, 0.30f}, {-0.17f, 0.13f, 0.30f}, {0.17f, 0.13f, 0.30f}, {0.f, 0.19f, 0.f}};
    buildHull(meshes[M_RINGSEG], b, 9, true);
  }
  for (int w = 0; w < 2; w++) {   // octagonal drums for the spindle
    V3 p[16]; float r = w ? 0.5f : 0.34f, h = w ? 0.16f : 0.22f;
    for (int i = 0; i < 8; i++) { float a = i * 0.7853982f + 0.3926991f; p[i] = {r * cosf(a), r * sinf(a), h}; p[i + 8] = {r * cosf(a), r * sinf(a), -h}; }
    buildHull(meshes[w ? M_OCTWIDE : M_OCT], p, 16, true);
  }
  { const V3 b[] = {{-0.3f, -0.22f, -0.42f}, {0.3f, -0.22f, -0.42f}, {-0.3f, 0.22f, -0.42f}, {0.3f, 0.22f, -0.42f},
                    {-0.3f, -0.22f, 0.42f}, {0.3f, -0.22f, 0.42f}, {-0.24f, 0.26f, 0.36f}, {0.24f, 0.26f, 0.36f}};
    buildHull(meshes[M_MODULE], b, 8, true); }
  {   // Deseret dome: a faceted hemisphere, axis up
    V3 p[13];
    for (int i = 0; i < 6; i++) {
      float a = i * 1.0471976f, b = a + 0.5235988f;
      p[i] = {cosf(a), 0.f, sinf(a)};
      p[i + 6] = {0.72f * cosf(b), 0.55f, 0.72f * sinf(b)};
    }
    p[12] = {0.f, 0.92f, 0.f};
    buildHull(meshes[M_DOME], p, 13, true);
  }
  {   // the habitat's base: a broad flat octagon
    V3 p[16];
    for (int i = 0; i < 8; i++) { float a = i * 0.7853982f + 0.3926991f; p[i] = {1.25f * cosf(a), 0.1f, 1.25f * sinf(a)}; p[i + 8] = {1.1f * cosf(a), -0.18f, 1.1f * sinf(a)}; }
    buildHull(meshes[M_BASE], p, 16, true);
  }
  { const V3 b[] = {{-0.6f, -0.02f, -0.22f}, {0.6f, -0.02f, -0.22f}, {-0.6f, 0.02f, -0.22f}, {0.6f, 0.02f, -0.22f},
                    {-0.6f, -0.02f, 0.22f}, {0.6f, -0.02f, 0.22f}, {-0.6f, 0.02f, 0.22f}, {0.6f, 0.02f, 0.22f}};
    buildHull(meshes[M_PANEL], b, 8, true); }
  const float g = 1.618f;
  V3 ico[12] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g}, {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
  for (int r = 0; r < 6; r++) {
    V3 p[12];
    for (int i = 0; i < 12; i++) p[i] = norm(ico[i]) * rf(0.62f, 1.18f);
    buildHull(meshes[M_ROCK0 + r], p, 12);
  }
}

// ============================================================
//  world objects
// ============================================================
enum OKind : uint8_t { K_NONE = 0, K_SHIP, K_STATION, K_DOCKGATE, K_ROCK, K_BODY, K_POD, K_WRECK, K_ARTIFACT, K_ANOMALY, K_GATE, K_PORTAL, K_LANDMARK };
enum GateFlags : uint8_t { GF_DEST = 1, GF_CHAIN = 2, GF_JOB = 4, GF_RUMOR = 8, GF_UNKNOWN = 16, GF_FIXED = 32, GF_KNOWN = 64, GF_LOCALNAME = 128 };
enum BodyType : uint8_t { BT_GIANT = 0, BT_ROCKY, BT_HOLE, BT_WRONGSTAR, BT_STAR, BT_REDGIANT, BT_WHITEDWARF, BT_NEUTRON };
static inline bool isStarBody(uint8_t t) { return t == BT_STAR || t == BT_REDGIANT || t == BT_WHITEDWARF || t == BT_NEUTRON; }

struct Obj {
  uint8_t kind, enc, mesh, gflags, depth, bodyType, uses;
  bool hostile, engaged, done, ghost;
  V3 p, v;
  Basis o;
  float radius, spin, timer, prevSide;
  uint16_t col;
  uint32_t seed;
  int lmId;       // landmark id (0 = none)
  int link;       // station <-> dock gate
  char name[24];
  uint16_t net;   // signals: shared object id (0 = local only)
  bool remote;    // signals: the other pilot
};
static constexpr int MAX_OBJ = 44;
static Obj objs[MAX_OBJ];
static void drawRemoteShip(const Obj &o);

static Obj *newObj(uint8_t kind) {
  for (int i = 0; i < MAX_OBJ; i++)
    if (objs[i].kind == K_NONE) {
      objs[i] = Obj();
      objs[i].kind = kind; objs[i].o = Basis(); objs[i].link = -1;
      objs[i].seed = rnd();
      return &objs[i];
    }
  return nullptr;
}
static int idxOf(const Obj *o) { return o ? (int)(o - objs) : -1; }
// ---- signals (experimental multiplayer) ----
static bool inMeeting = false, netEcho = false;
static char meetOrigin[24] = "";              // where each pilot came from (their way home)
static int remoteIdx = -1;
static uint16_t netNextId = 1;
static bool itMe = false;                      // tag: who is "it"
static int tagsGiven = 0, tagsTaken = 0, tagHits = 0;
static float netSendT = 0, npcSyncT = 0;
static bool signalsOn();
static void netTick();
static void leaveMeeting(bool sayBye);
static void makeMeetingScene(uint32_t seed);
static int signalsFee();
static const char *myCallsign();
static void chatTap(int x, int y);
static void chatSend(const char *t);
static void drawChat();
static float glitchT = 0.f;               // the haywire arrival
static bool chatOpen = false;
static char chatLog[5][56]; static int chatLogN = 0;
static char chatDraft[44] = "";
static void chatLogAdd(const char *t) {
  if (chatLogN == 5) { for (int i = 1; i < 5; i++) memcpy(chatLog[i - 1], chatLog[i], 56); chatLogN = 4; }
  strncpy(chatLog[chatLogN], t, 55); chatLog[chatLogN][55] = 0; chatLogN++;
}
static void killObj(int i) {
  if (i < 0 || i >= MAX_OBJ) return;
  if (inMeeting && objs[i].net && !objs[i].remote && !netEcho) net::sendRemove(objs[i].net);   // the other sky loses it too
  if (i == remoteIdx) remoteIdx = -1;
  objs[i].kind = K_NONE;
}
static inline uint8_t flyingType() { return sm::sheet().activeShip; }
static inline bool flyingFalcor() { return flyingType() == sm::SHIP_FALCOR; }
static inline bool flyingHoney() { return flyingType() == sm::SHIP_HONEYBEE; }
static inline bool flyingMaltese() { return flyingType() == sm::SHIP_MALTESE; }
static inline bool flyingGhost() { return flyingType() == sm::SHIP_GHOST; }
static int autoNavObj = -1, orbitObj = -1;   // Maltese: gate autonav, gas giant orbit
static bool autoNav = false;
static void clearWorld() { for (auto &o : objs) o.kind = K_NONE; autoNavObj = -1; orbitObj = -1; remoteIdx = -1; }
static int countKind(uint8_t k) { int n = 0; for (auto &o : objs) if (o.kind == k) n++; return n; }
static float distTo(const Obj &o) { return len(o.p - shipPos); }
static float surfaceDist(const Obj &o) { return distTo(o) - o.radius; }

// ============================================================
//  scene state
// ============================================================
static V3 sunDir{0.4f, 0.3f, 0.86f};
static uint16_t sunCol = 0;
static uint8_t sunType = 2;      // 0 red dwarf, 1 orange, 2 yellow, 3 white, 4 blue giant, 5 red giant, 6 white dwarf
static V3 layerLight{0.3f, 0.8f, 0.5f};
static char hereName[24] = "";     // what locals call this place
static int target = -1;            // targeted object
static int navObj = -1;            // next gate / portal of the trip
static int dockTarget = -1;        // autopilot docking to this dock gate
static uint32_t lastSave = 0;
static float spawnTimer = 3.f;
static uint32_t livesSeen = 0;

struct Star { V3 d; uint8_t mag; uint16_t col; };
static constexpr int NSTARS = 240;
static Star stars[NSTARS];
struct Blob { V3 d; float r; uint16_t col; };
static constexpr int NBLOBS = 44;
static Blob blobs[NBLOBS];
static constexpr int NDUST = 34;
static V3 dust[NDUST];
struct Streamer { float x, y, z, hue, w; };
static constexpr int NSTREAM = 22;
static Streamer streamers[NSTREAM];

// per-block polar coordinates for kaleidoscope fields (screen is fixed)
static constexpr int BLK = 4, BW = W / BLK, BH = H / BLK;
// packed 16-bit so the tables stay small in DRAM
static int16_t *blkAngQ = nullptr, *blkRadQ = nullptr, *blkLogQ = nullptr;   // in PSRAM (see bigAlloc)
static inline float blkAng(int i) { return blkAngQ[i] * (3.1415927f / 10000.f); }
static inline float blkRad(int i) { return blkRadQ[i] * (1.f / 20000.f); }
static inline float blkLog(int i) { return blkLogQ[i] * (1.f / 8000.f); }
static void initBlocks() {
  if (!blkAngQ) {
    blkAngQ = (int16_t *)bigAlloc(sizeof(int16_t) * BW * BH);
    blkRadQ = (int16_t *)bigAlloc(sizeof(int16_t) * BW * BH);
    blkLogQ = (int16_t *)bigAlloc(sizeof(int16_t) * BW * BH);
  }
  for (int by = 0; by < BH; by++)
    for (int bx = 0; bx < BW; bx++) {
      float x = (bx * BLK + 2 - 160.f) / 160.f, y = (by * BLK + 2 - 120.f) / 160.f;
      float r = sqrtf(x * x + y * y);
      blkAngQ[by * BW + bx] = (int16_t)(atan2f(y, x) * (10000.f / 3.1415927f));
      blkRadQ[by * BW + bx] = (int16_t)(r * 20000.f);
      blkLogQ[by * BW + bx] = (int16_t)(logf(r + 0.02f) * 8000.f);
    }
}

static V3 randDir() {
  for (;;) {
    V3 d{rf(-1, 1), rf(-1, 1), rf(-1, 1)};
    float l = len(d);
    if (l > 0.1f && l <= 1.f) return d * (1.f / l);
  }
}

static void buildSky(uint32_t seed) {
  uint32_t keep = rngState; rngState = seed ? seed : 1;
  for (int i = 0; i < NSTARS; i++) {
    stars[i].d = randDir();
    float m = rf(0, 1);
    stars[i].mag = (uint8_t)(m < 0.72f ? 0 : m < 0.93f ? 1 : m < 0.985f ? 2 : 3);
    float t = rf(0, 1);   // stellar colour temperature
    stars[i].col = t < 0.15f ? rgb(255, 190, 150) : t < 0.35f ? rgb(255, 235, 205) : t < 0.8f ? rgb(235, 240, 255) : rgb(180, 205, 255);
  }
  // two or three dusty nebulae — eye candy, not a galaxy
  int nb = 0, clouds = ri(2, 3);
  for (int c = 0; c < clouds && nb < NBLOBS; c++) {
    V3 center = randDir();
    float hue = rf(0, 1) < 0.5f ? rf(195, 235) : rf(5, 30);
    if (rf(0, 1) < 0.2f) hue = rf(280, 320);
    int n = ri(10, 16);
    for (int k = 0; k < n && nb < NBLOBS; k++)
      blobs[nb++] = {norm(center + randDir() * rf(0.05f, 0.32f)), rf(0.05f, 0.16f), hsv(hue + rf(-12, 12), rf(0.35f, 0.6f), rf(0.035f, 0.075f))};
  }
  for (; nb < NBLOBS; nb++) blobs[nb] = {V3{0, 0, 1}, 0, 0};
  sunDir = randDir();
  float st = rf(0, 1);
  // the local star: mostly ordinary, sometimes not
  sunType = st < 0.18f ? 0 : st < 0.40f ? 1 : st < 0.62f ? 2 : st < 0.76f ? 3 : st < 0.84f ? 4 : st < 0.93f ? 5 : 6;
  static const uint16_t starCols[7] = {rgb(255, 120, 70), rgb(255, 172, 100), rgb(255, 232, 175), rgb(248, 248, 255),
                                       rgb(165, 195, 255), rgb(255, 105, 60), rgb(200, 220, 255)};
  sunCol = starCols[sunType];
  rngState = keep;
}

static void resetDust() {
  for (auto &d : dust) d = shipPos + randDir() * rf(8, 60);
  for (auto &s : streamers) { s.x = rf(-30, 30); s.y = rf(-22, 22); s.z = rf(5, 90); s.hue = rf(0, 360); s.w = rf(6, 18); }
}

// ============================================================
//  layer look
// ============================================================
static const char *layerName(int l) {
  static const char *n[] = {"REAL SPACE", "THE SHALLOWS", "SMUGGLER ROADS", "BELOW THE ROADS", "DEEP COVE"};
  return n[l < 0 ? 0 : (l > 4 ? 4 : l)];
}
static float layerHue(int l) {
  static const float h[] = {215, 188, 265, 300, 95};
  return h[l < 0 ? 0 : (l > 4 ? 4 : l)] + (l >= 3 ? tNow * (l == 4 ? 22.f : 9.f) : 0.f);
}

// The subspace sky, evaluated per 4x4 block from the view direction so turning
// the ship turns the world. Deeper layers fold into kaleidoscopes.
static float fieldGain = 1.f;           // brighter inside portal holes
static float frameYawA = 0, framePitA = 0;
static uint16_t fieldColor(int l, int bi, V3 d, float t) {
  float a = blkAng(bi), r = blkRad(bi);
  if (l <= 1) {
    float v = 0.5f + 0.5f * fsin(d.x * 3.1f + t * 0.35f + fsin(d.y * 2.3f - t * 0.27f) * 1.4f);
    float w = 0.5f + 0.5f * fsin(d.z * 4.2f - d.x * 2.f + t * 0.2f);
    return hsv(185 + v * 40 + w * 25, 0.55f, (0.035f + v * w * 0.14f) * fieldGain);
  }
  if (l == 2) {
    float v = fsin(d.x * 4.f + t * 0.5f + fsin(d.y * 5.f + t * 0.4f) * 2.f);
    float w = fsin(d.y * 3.f - d.z * 4.f - t * 0.6f + fsin(d.x * 7.f) * 1.2f);
    float band = fsin((v + w) * 3.f + r * 4.f - t);
    float val = 0.06f + (0.5f + 0.5f * band) * (0.5f + 0.25f * (v + w)) * 0.3f;
    return hsv(250 + v * 40 + band * 30, 0.7f, val * fieldGain);
  }
  int k = l == 3 ? 6 : 8;
  float wedge = 6.2831853f / k;
  float fa = fmodf(a + 12.566371f + t * (l == 3 ? 0.05f : -0.09f), wedge);
  if (fa > wedge * 0.5f) fa = wedge - fa;
  float yawA = frameYawA, pitA = framePitA;
  float px = fcos(fa) * r, py = fsin(fa) * r;
  float f1 = fsin(px * 7.f + t * 0.9f + yawA * 3.f);
  float f2 = fsin(py * 9.f - t * 1.3f + pitA * 4.f + f1 * 1.5f);
  float f3 = fsin(r * 11.f - t * 2.2f + d.x * 2.f);
  float v = (f1 + f2 + f3) * 0.33f;
  float hue = layerHue(l) + v * 90.f + r * 120.f;
  if (l == 4) {
    // a tunnel folded into itself: log-polar rings rushing outward
    float rings = fsin(blkLog(bi) * 9.f - t * 3.6f + fa * 4.f);
    v = v * 0.6f + rings * 0.6f;
    hue += rings * 70.f;
  }
  float val = 0.10f + (0.5f + 0.5f * v) * (l == 3 ? 0.36f : 0.5f);
  if (deepFlash > 0.f) { hue += 180.f * deepFlash; val = val * (1.f - deepFlash) + deepFlash * 0.7f; }
  return hsv(hue, 0.75f, val * (fieldGain > 1.f ? 1.25f : 1.f));
}

static void drawField(int l, float cx = 160, float cy = 120, float rClip = 1e9f) {
  float t = tNow;
  frameYawA = atan2f(shipB.f.x, shipB.f.z); framePitA = shipB.f.y;
  int tear = (l == 4 && fmodf(tNow, 3.7f) < 0.18f) ? ri(-3, 3) : 0;
  float r2 = rClip * rClip;
  bool clip = rClip < 1e8f;
  for (int by = 0; by < BH; by++) {
    int y = by * BLK;
    float yy = (y + 2 - cy); yy *= yy;
    if (clip && yy > r2) continue;
    int shift = (tear && (by % 7) < 2) ? tear * BLK : 0;
    for (int bx = 0; bx < BW; bx++) {
      int x = bx * BLK;
      if (clip) { float xx = x + 2 - cx; if (xx * xx + yy > r2) continue; }
      int bi = by * BW + bx;
      float u = (x + 2 - 160.f) / FOCAL, v = -(y + 2 - 120.f) / FOCAL;
      if (alienLensOn && !clip) {
        float dx = x + 2 - alienLX, dy = y + 2 - alienLY, R = alienLR * 3.5f;
        float r2 = dx * dx + dy * dy;
        if (r2 < R * R) {
          float r = sqrtf(r2), a = 1.f - r / R;
          a = a * a * (2.2f * fsin(t * 1.7f) + 1.f);
          float ca = fcos(a), sa = fsin(a);
          u = (alienLX + dx * ca - dy * sa - 160.f) / FOCAL;
          v = -(alienLY + dx * sa + dy * ca - 120.f) / FOCAL;
        }
      }
      V3 d = shipB.f + shipB.r * u + shipB.u * v;
      uint16_t fc = fieldColor(l, bi, flyingGhost() && !clip ? d : d, flyingGhost() && !clip ? t * 0.45f : t);
      if (flyingGhost() && !clip) {   // the flagship's own refined, quieter version of the deep
        int r, g, b; unrgb(fc, r, g, b);
        int m = (r + g + b) / 3;
        fc = rgb((m * 5 + r * 4) / 9 * 85 / 100, (m * 5 + g * 4) / 9 * 92 / 100, ((m * 5 + b * 4) / 9 * 108 / 100) > 255 ? 255 : (m * 5 + b * 4) / 9 * 108 / 100);
      }
      cv.fillRect(x + shift, y, BLK, BLK, fc);
    }
  }
}

// ============================================================
//  drawing: sky, bodies, meshes, rings
// ============================================================
static void drawStarsReal() {
  for (int i = 0; i < NSTARS; i++) {
    float sx, sy;
    if (!projectDir(stars[i].d, sx, sy) || !onScreen(sx, sy)) continue;
    const Star &s = stars[i];
    float tw = 0.75f + 0.25f * fsin(tNow * (1.3f + (i & 7) * 0.31f) + i);
    int x = (int)sx, y = (int)sy;
    if (s.mag == 0) cv.drawPixel(x, y, shade(s.col, 0.35f * tw));
    else if (s.mag == 1) cv.drawPixel(x, y, shade(s.col, 0.75f * tw));
    else if (s.mag == 2) cv.fillRect(x, y, 2, 2, shade(s.col, 0.85f * tw));
    else {
      uint16_t c = shade(s.col, tw);
      cv.drawPixel(x, y, rgb(255, 255, 255));
      cv.drawLine(x - 2, y, x + 2, y, shade(c, 0.6f));
      cv.drawLine(x, y - 2, x, y + 2, shade(c, 0.6f));
    }
  }
}

static void drawNebulae() {
  for (int i = 0; i < NBLOBS; i++) {
    if (blobs[i].r <= 0) continue;
    float sx, sy;
    if (!projectDir(blobs[i].d, sx, sy)) continue;
    float rr = blobs[i].r * FOCAL;
    if (!onScreen(sx, sy, rr)) continue;
    cv.fillCircle((int)sx, (int)sy, (int)rr, blobs[i].col);
    cv.fillCircle((int)(sx + rr * 0.2f), (int)(sy - rr * 0.15f), (int)(rr * 0.55f), shade(blobs[i].col, 1.25f));
  }
}

// ============================================================
//  stars: light, not paint. A white-hot core, a halo that falls off like
//  light does (tinted by temperature), and the camera's diffraction spikes.
// ============================================================
static float glowWhite = 1.f;   // how far the light runs to white near the core (cool giants: little)
static uint16_t glowColor(uint16_t col, float k) {
  // k: 1 at the core edge -> 0 far out. Far: the star's hue, dim. Near: toward white.
  int r, g, b; unrgb(col, r, g, b);
  float w = k * k * k * glowWhite;
  return rgb((int)(r * k + (255 - r) * w), (int)(g * k + (255 - g) * w), (int)(b * k + (255 - b) * w));
}

static void drawStarGlow(float x, float y, float core, uint16_t col, float halo, float spike, int nSpikes, float spikeTurn = 0.f) {
  if (!onScreen(x, y, halo + spike)) return;
  if (halo > 110.f) halo = 110.f;   // the frame budget: big halos are the costliest thing we draw
  // halo, outside in: each ring a little brighter, so the light reads as one smooth falloff
  for (float r = halo; r > core; r -= fmaxf(1.f, r * (r > 50.f ? 0.12f : 0.085f))) {
    float u = r / halo;
    float k = powf(core / r, 1.15f) * (1.f - u * u * u);   // light thins to nothing at the rim: no disk edge
    cv.fillCircle((int)x, (int)y, (int)r, glowColor(col, k * 0.95f));
  }
  // diffraction spikes: thin, bright at the root, fading out
  for (int i = 0; i < nSpikes; i++) {
    float a = spikeTurn + i * 3.1415927f / (nSpikes / 2 > 0 ? nSpikes / 2 : 1);
    float ca = cosf(a), sa = sinf(a);
    for (int seg = 0; seg < 3; seg++) {
      float r0 = core + spike * seg / 3.f, r1 = core + spike * (seg + 1) / 3.f;
      uint16_t c = glowColor(col, 0.9f - seg * 0.28f);
      cv.drawLine((int)(x + ca * r0), (int)(y + sa * r0), (int)(x + ca * r1), (int)(y + sa * r1), c);
      if (seg == 0) cv.drawLine((int)(x + ca * r0 - sa), (int)(y + sa * r0 + ca), (int)(x + ca * r1 - sa), (int)(y + sa * r1 + ca), glowColor(col, 0.55f));
    }
  }
  cv.fillCircle((int)x, (int)y, (int)fmaxf(1.f, core), rgb(255, 255, 255));
}

// a star's face, seen up close: limb darkening and slow granulation
static void drawStarDisk(float sx, float sy, float R, uint16_t col, const Obj *b, bool mottled) {
  int bs = R < 30 ? 2 : (R < 90 ? 3 : 4);
  cv.fillCircle((int)sx, (int)sy, (int)R, shade(col, mottled ? 0.55f : 0.9f));   // under the blocks: no gaps at the limb
  int x0 = (int)clampf(sx - R, 0, W), x1 = (int)clampf(sx + R, 0, W), y0 = (int)clampf(sy - R, 0, H), y1 = (int)clampf(sy + R, 0, H);
  int r1, g1, b1; unrgb(col, r1, g1, b1);
  float invR = 1.f / R;
  for (int y = y0; y < y1; y += bs) {
    float ny = (y + bs * 0.5f - sy) * invR;
    for (int x = x0; x < x1; x += bs) {
      float nx = (x + bs * 0.5f - sx) * invR;
      float q = 1.f - nx * nx - ny * ny;
      if (q <= 0) continue;
      float mu = sqrtf(q);
      float gran = 1.f;
      if (b) {
        V3 nw = shipB.toWorld(V3{nx, -ny, -mu});
        float lat = dot(nw, b->o.u), lon = dot(nw, b->o.r);
        gran = mottled ? 0.72f + 0.28f * fsin(lat * 9.f + fsin(lon * 7.f + tNow * 0.2f) * 2.f) * fsin(lon * 11.f - tNow * 0.15f)
                       : 0.95f + 0.05f * fsin(lat * 23.f + fsin(lon * 17.f + tNow * 0.3f) * 1.6f) * fsin(lon * 19.f - lat * 7.f);   // cells, not rings
      }
      float k = mottled ? (0.45f + 0.55f * mu) * gran : (0.78f + 0.22f * mu) * gran;
      float hot = mottled ? 0.f : 0.35f + mu * mu * 0.5f;   // a hot star's face burns toward white
      cv.fillRect(x, y, bs, bs, rgb((int)(r1 * k + (255 - r1) * hot), (int)(g1 * k + (255 - g1) * hot), (int)(b1 * k + (255 - b1) * hot)));
    }
  }
}

static bool nearStar = false;   // this system's star is a body you can fly to (drawn with the objects)
static void drawSun() {
  if (nearStar) return;
  float sx, sy;
  if (!projectDir(sunDir, sx, sy) || !onScreen(sx, sy, 90)) return;
  //                       red dwarf  orange  yellow  white  blue    red giant  white dwarf
  static const float core[7]  = {2.5f, 3.f,   3.5f,  3.5f,  4.5f,   0.f,      1.5f};
  static const float halo[7]  = {20.f, 28.f,  34.f,  36.f,  52.f,   44.f,     15.f};
  static const float spike[7] = {16.f, 26.f,  34.f,  40.f,  58.f,   10.f,     64.f};
  float tw = 1.f + 0.03f * fsin(tNow * 7.f);
  if (sunType == 5) {
    // a red giant fills a patch of sky: a dim, boiling disk inside a wide red glow
    glowWhite = 0.15f; drawStarGlow(sx, sy, 16.f, sunCol, halo[5], spike[5], 4); glowWhite = 1.f;
    drawStarDisk(sx, sy, 17.f, sunCol, nullptr, true);
  } else {
    drawStarGlow(sx, sy, core[sunType] * tw, sunCol, halo[sunType] * tw, spike[sunType], 4, 0.785f);
    if (sunType == 4 || sunType == 6) drawStarGlow(sx, sy, core[sunType], sunCol, core[sunType] + 1.f, spike[sunType] * 0.55f, 4, 0.f);
  }
  // lens flare ghosts along the line through the screen centre
  float fx = 160 - sx, fy = 120 - sy;
  static const float at[] = {0.45f, 0.9f, 1.3f, 1.75f};
  static const int rr[] = {5, 11, 7, 17};
  for (int i = 0; i < 4; i++) {
    int gx = (int)(sx + fx * at[i]), gy = (int)(sy + fy * at[i]);
    cv.drawCircle(gx, gy, rr[i], shade(sunCol, 0.22f + 0.06f * i));
    if (i == 1) cv.drawCircle(gx, gy, rr[i] - 2, shade(sunCol, 0.14f));
  }
}

static void drawDustReal() {
  V3 vel = shipB.f * shipSpeed;
  for (auto &d : dust) {
    float x0, y0, z0, x1, y1, z1;
    if (!project(d, x0, y0, z0)) continue;
    if (!project(d - vel * 0.05f, x1, y1, z1)) { cv.drawPixel((int)x0, (int)y0, rgb(70, 76, 90)); continue; }
    cv.drawLine((int)x0, (int)y0, (int)x1, (int)y1, z0 < 15 ? rgb(120, 128, 140) : rgb(62, 68, 80));
  }
}

// subspace: stars smear into radial light, streamers flow through
static void drawStarsDeep() {
  float stretch = 0.02f + layer * 0.05f + shipSpeed * 0.003f;
  float hue = layerHue(layer);
  for (int i = 0; i < NSTARS; i += (layer >= 3 ? 1 : 2)) {
    float sx, sy;
    if (!projectDir(stars[i].d, sx, sy) || !onScreen(sx, sy)) continue;
    float dx = sx - 160, dy = sy - 120;
    uint16_t c = hsv(hue + (i % 40) * 3, 0.35f + layer * 0.12f, 0.45f + 0.4f * fsin(tNow * 2 + i));
    cv.drawLine((int)sx, (int)sy, (int)(sx + dx * stretch), (int)(sy + dy * stretch), c);
  }
  if (layer == 4) {
    // the deep cove: constellations wire themselves into a lattice
    int px = -1, py = -1;
    for (int i = 0; i < NSTARS; i += 9) {
      float sx, sy;
      if (!projectDir(stars[i].d, sx, sy) || !onScreen(sx, sy)) { px = -1; continue; }
      if (px >= 0 && abs(px - (int)sx) + abs(py - (int)sy) < 110)
        cv.drawLine(px, py, (int)sx, (int)sy, hsv(hue + 140 + i, 0.8f, 0.35f + 0.25f * fsin(tNow * 3 + i)));
      px = (int)sx; py = (int)sy;
    }
  }
}

static void drawStreamers() {
  if (layer < 1) return;
  int n = layer == 1 ? 6 : (layer == 2 ? 14 : NSTREAM);
  float spd = 1.f + shipSpeed * 0.9f;
  for (int i = 0; i < n; i++) {
    Streamer &s = streamers[i];
    s.z -= spd * dt;
    if (s.z < 2.f) { s.z += 90.f; s.x = rf(-30, 30); s.y = rf(-22, 22); s.hue = rf(0, 360); }
    float wob = fsin(tNow * 1.3f + i) * 3.f;
    float k0 = FOCAL / s.z, k1 = FOCAL / (s.z + s.w);
    int x0 = (int)(160 + (s.x + wob) * k0), y0 = (int)(120 - s.y * k0);
    int x1 = (int)(160 + (s.x + wob * 0.5f) * k1), y1 = (int)(120 - s.y * k1);
    uint16_t c = hsv(layerHue(layer) + s.hue * (layer >= 3 ? 1.f : 0.15f), 0.8f, clampf(0.25f + 18.f / s.z, 0, 0.95f));
    cv.drawLine(x0, y0, x1, y1, c);
    if (s.z < 25) cv.drawLine(x0 + 1, y0, x1 + 1, y1, c);
  }
}

// planets and giants, lit from the local star
static void drawBody(const Obj &b) {
  float sx, sy, z;
  if (!project(b.p, sx, sy, z)) return;
  float R = b.radius * FOCAL * fovPulse / z;
  if (isStarBody(b.bodyType)) {
    if (b.bodyType == BT_NEUTRON) {
      // a dead star's pinprick, and the two beams it sweeps across everything
      float a = tNow * 5.5f;
      for (int side = -1; side <= 1; side += 2) {
        V3 dir = norm(b.o.u * cosf(0.5f) + (b.o.r * cosf(a) + b.o.f * sinf(a)) * sinf(0.5f)) * (float)side;
        float ex, ey, ez;
        if (project(b.p + dir * b.radius * 60.f, ex, ey, ez)) {
          cv.drawLine((int)sx, (int)sy, (int)ex, (int)ey, rgb(200, 170, 255));
          cv.drawLine((int)sx + 1, (int)sy, (int)ex + 1, (int)ey, rgb(110, 80, 200));
        }
      }
      drawStarGlow(sx, sy, fmaxf(1.5f, R), rgb(210, 190, 255), fmaxf(10.f, R * 4.f), 30.f, 4, tNow * 0.3f);
      return;
    }
    bool giant = b.bodyType == BT_REDGIANT;
    float halo = clampf(R * (giant ? 1.35f : b.bodyType == BT_WHITEDWARF ? 6.f : 2.1f), R + 4.f, 150.f);
    float spike = b.bodyType == BT_WHITEDWARF ? 70.f : (giant ? 0.f : clampf(R * 1.3f, 20.f, 90.f));
    if (R < 5.f) { drawStarGlow(sx, sy, fmaxf(1.5f, R), b.col, halo, spike, 4, 0.785f); return; }
    glowWhite = giant ? 0.15f : 1.f;
    if (halo > R + 6.f && R < 100.f) drawStarGlow(sx, sy, R, b.col, halo, 0.f, 0);
    else {   // too big for a filled halo: a corona of thin rings beyond the limb (cheap, and still light)
      for (int i = 16; i >= 1; i--) cv.drawCircle((int)sx, (int)sy, (int)(R + i * 1.6f), glowColor(b.col, 0.95f * (1.f - i / 17.f) * (1.f - i / 17.f)));
    }
    glowWhite = 1.f;
    drawStarDisk(sx, sy, R, b.col, &b, giant);
    if (spike > 0.f && R < 60.f) drawStarGlow(sx, sy, 2.f, b.col, 0.f, spike + R, 4, 0.785f);
    return;
  }
  if (b.bodyType == BT_WRONGSTAR && onScreen(sx, sy, R * 3.f)) {
    // a star that is wrong: its light runs inward, its colors will not hold still,
    // and it has an odd number of spikes, which no lens can make
    float halo = clampf(R * 2.4f, 12.f, 150.f);
    for (float r = halo; r > R; r -= fmaxf(1.f, r * 0.09f)) {
      float u = r / halo, k = powf(R / r, 1.3f) * (1.f - u * u * u);
      cv.fillCircle((int)sx, (int)sy, (int)r, hsv(layerHue(layer) + 150.f + r * 2.5f - tNow * 40.f, 0.7f, k * 0.85f));
    }
    for (int i = 0; i < 5; i++) {
      float a = tNow * 0.4f + i * 1.2566371f;
      cv.drawLine((int)sx, (int)sy, (int)(sx + cosf(a) * (R + halo)), (int)(sy + sinf(a) * (R + halo)), hsv(layerHue(layer) + i * 50.f, 0.6f, 0.95f));
    }
    cv.fillCircle((int)sx, (int)sy, (int)R, rgb(0, 0, 0));
    cv.drawCircle((int)sx, (int)sy, (int)R, rgb(255, 255, 255));
    cv.drawCircle((int)sx, (int)sy, (int)R - 1, hsv(tNow * 90.f, 0.5f, 1.f));
    return;
  }
  if (R < 1.5f) { cv.drawPixel((int)sx, (int)sy, shade(b.col, 0.8f)); return; }
  if (!onScreen(sx, sy, R)) return;
  if (b.bodyType == BT_HOLE) {
    float px = 0, py = 0; bool pv = false;
    for (int s = 0; s <= 40; s++) {
      float a = s * 0.15708f + tNow * 0.6f;
      V3 w = b.p + (b.o.r * cosf(a) + b.o.f * sinf(a)) * (b.radius * 2.4f);
      float x, y, zz;
      bool ok = project(w, x, y, zz);
      if (ok && pv) {
        uint16_t c = hsv(25 + 20 * fsin(a * 3 + tNow * 4), 0.8f, 0.9f);
        cv.drawLine((int)px, (int)py, (int)x, (int)y, c);
        cv.drawLine((int)px, (int)py + 1, (int)x, (int)y + 1, shade(c, 0.6f));
      }
      px = x; py = y; pv = ok;
    }
    cv.fillCircle((int)sx, (int)sy, (int)R, rgb(0, 0, 0));
    cv.drawCircle((int)sx, (int)sy, (int)(R * 1.08f) + 1, rgb(255, 220, 170));
    cv.drawCircle((int)sx, (int)sy, (int)(R * 1.18f) + 1, rgb(200, 120, 60));
    return;
  }
  V3 L = shipB.toLocal(b.bodyType == BT_WRONGSTAR ? norm(shipPos - b.p) : (layer == 0 ? sunDir : layerLight));
  int bs = R < 30 ? 2 : (R < 90 ? 3 : 4);
  int x0 = (int)clampf(sx - R, 0, W), x1 = (int)clampf(sx + R, 0, W);
  int y0 = (int)clampf(sy - R, 0, H), y1 = (int)clampf(sy + R, 0, H);
  float invR = 1.f / R;
  int h1, s1, v1; unrgb(b.col, h1, s1, v1);
  for (int y = y0; y < y1; y += bs) {
    float ny = -(y + bs * 0.5f - sy) * invR;
    for (int x = x0; x < x1; x += bs) {
      float nx = (x + bs * 0.5f - sx) * invR;
      float q = 1.f - nx * nx - ny * ny;
      if (q <= 0) continue;
      float nz = -sqrtf(q);
      float lit = nx * L.x + ny * L.y + nz * L.z;
      V3 nw = shipB.toWorld(V3{nx, ny, nz});
      float lat = dot(nw, b.o.u), lon = dot(nw, b.o.r);
      float pat;
      if (b.bodyType == BT_GIANT) pat = 0.75f + 0.25f * fsin(lat * 13.f + fsin(lon * 3.f + lat * 5.f) * 0.8f + (float)(b.seed % 7));
      else if (b.bodyType == BT_WRONGSTAR) pat = 0.8f + 0.2f * fsin(lat * 20.f + tNow * 3.f);
      else pat = 0.7f + 0.3f * (fsin(lat * 5.f + lon * 3.f + (float)(b.seed & 15)) > 0.2f ? 1.f : 0.5f);
      float k = b.bodyType == BT_WRONGSTAR ? 0.6f + 0.4f * pat : clampf(lit, 0.f, 1.f) * 0.9f * pat + 0.06f * pat + 0.1f * clampf(-lit, 0.f, 1.f) * (1.f - q);
      cv.fillRect(x, y, bs, bs, rgb((int)(h1 * k), (int)(s1 * k), (int)(v1 * k)));
    }
  }
  if (b.bodyType != BT_WRONGSTAR && R > 8) {   // thin lit rim on the sun side
    float ax = L.x, ay = -L.y, al = sqrtf(ax * ax + ay * ay) + 1e-4f;
    float base = atan2f(ay / al, ax / al);
    for (int s = -10; s <= 10; s++) {
      float a = base + s * 0.12f;
      cv.drawPixel((int)(sx + cosf(a) * (R + 1)), (int)(sy + sinf(a) * (R + 1)), shade(b.col, 1.3f - fabsf((float)s) * 0.07f));
    }
  }
}

// Livery for the next drawMesh call: side faces take the accent, the forward-top faces the canopy.
static uint16_t meshAccent = 0, meshCanopy = 0;
static float meshAmbient = 0.16f;   // floor light: stations are floodlit, ships readable, rocks honest
// Surface detail without geometry: 1 = panel seams + greebles (ships), 2 = also lit windows + an emblem (stations)
static void drawDecal(int logo, V3 up);
static uint8_t meshDetail = 0, meshEmblem = 0;
static uint16_t meshWindow = 0;
// 7x7 maker's marks. Liminar: an open ring. Portex: a doorway. MaltaPlex: a cross. Freehold: a stake.
// Deseret: a beehive.
static const uint8_t EMBLEMS[6][7] = {
  {0x1C, 0x22, 0x41, 0x41, 0x41, 0x22, 0x14}, {0x7F, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41},
  {0x08, 0x08, 0x08, 0x7F, 0x08, 0x08, 0x08}, {0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x40},
  {0x1C, 0x3E, 0x00, 0x7F, 0x00, 0x7F, 0x36}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
static void drawEmblem(int cx, int cy, int which, int sc, uint16_t col) {
  if (which < 1 || which > 5) return;
  const uint8_t *e = EMBLEMS[which - 1];
  for (int r = 0; r < 7; r++) for (int c = 0; c < 7; c++)
    if (e[r] & (0x40 >> c)) cv.fillRect(cx + (c - 3) * sc + 1, cy + (r - 3) * sc + 1, sc, sc, rgb(12, 10, 16));   // a shadow: reads on any hull
  for (int r = 0; r < 7; r++) for (int c = 0; c < 7; c++)
    if (e[r] & (0x40 >> c)) cv.fillRect(cx + (c - 3) * sc, cy + (r - 3) * sc, sc, sc, col);
}
static float emblemArea = 0.f, emblemX = 0.f, emblemY = 0.f;
static V3 decalA, decalB, decalC, decalN;   // the chosen face, in world space
static float decalK = 1.f;                  // and how it is lit
static V3 decalCenter; static float decalSize = 0.f;   // the whole flat panel the face belongs to
static int decalFace = -1;
static bool drawMesh(const Obj &ob, const Mesh &m, float scale, uint16_t base, uint16_t edgeCol, bool solid, float &outR) {
  float sx[18], sy[18], cx, cy, zc;
  V3 wv[18];
  outR = 0;
  if (!project(ob.p, cx, cy, zc)) return false;
  outR = scale * FOCAL / zc;
  if (outR < 1.6f) { cv.drawPixel((int)cx, (int)cy, base ? base : edgeCol); return true; }
  if (!onScreen(cx, cy, outR * 1.5f)) return true;
  for (int i = 0; i < m.nv; i++) {
    wv[i] = ob.p + ob.o.toWorld(m.v[i] * scale);
    float z;
    if (!project(wv[i], sx[i], sy[i], z)) return false;
  }
  bool vis[40];
  V3 light = layer == 0 ? sunDir : layerLight;
  for (int f = 0; f < m.nt; f++) {
    V3 nw = ob.o.toWorld(m.n[f]);
    vis[f] = dot(nw, shipPos - wv[m.t[f][0]]) > 0;
    if (!vis[f] || !solid) continue;
    float k = meshAmbient + (1.f - meshAmbient) * clampf(dot(nw, light), 0.f, 1.f);
    if (layer > 0) k = 0.3f + 0.7f * k;
    uint16_t fc = base;
    const V3 &mn = m.n[f];
    if (meshCanopy && mn.z > 0.3f && mn.y > 0.35f) { fc = meshCanopy; k = 0.55f + 0.45f * k; }   // canopies glow a little
    else if (meshAccent && fabsf(mn.x) > 0.72f) fc = meshAccent;
    cv.fillTriangle((int)sx[m.t[f][0]], (int)sy[m.t[f][0]], (int)sx[m.t[f][1]], (int)sy[m.t[f][1]],
                    (int)sx[m.t[f][2]], (int)sy[m.t[f][2]], shade(fc, k));
    if (meshDetail) {
      int a0 = m.t[f][0], a1 = m.t[f][1], a2 = m.t[f][2];
      float ux = sx[a1] - sx[a0], uy = sy[a1] - sy[a0], vx = sx[a2] - sx[a0], vy = sy[a2] - sy[a0];
      float area = fabsf(ux * vy - uy * vx) * 0.5f;
      if (area > 40.f) {
        uint16_t dk = shade(fc, k * 0.62f), lt = shade(fc, fminf(1.2f, k * 1.3f));
        auto at = [&](float b1, float b2, float &x, float &y) { x = sx[a0] + ux * b1 + vx * b2; y = sy[a0] + uy * b1 + vy * b2; };
        float px, py, qx, qy;
        at(0.f, 0.4f, px, py); at(0.6f, 0.4f, qx, qy);                       // a panel seam across the face
        cv.drawLine((int)px, (int)py, (int)qx, (int)qy, dk);
        if (area > 160.f) {
          at(0.f, 0.72f, px, py); at(0.28f, 0.72f, qx, qy); cv.drawLine((int)px, (int)py, (int)qx, (int)qy, dk);
          at(0.22f, 0.18f, px, py); cv.fillRect((int)px, (int)py, 2, 1, lt);  // greebles
          at(0.5f, 0.12f, px, py); cv.drawPixel((int)px, (int)py, lt);
        }
        if (meshDetail == 2 && meshWindow && area > 200.f && (f % 3) == 0) {   // a row of lit windows
          for (float b = 0.1f; b < 0.62f; b += 0.08f) {
            at(b, 0.55f, px, py);
            uint32_t hsh = (uint32_t)(f * 31 + (int)(b * 100)) * 2654435761u;
            if ((hsh >> 27) & 3) cv.drawPixel((int)px, (int)py, ((hsh >> 25) & 1) ? meshWindow : shade(meshWindow, 0.5f));
          }
        }
        // emblems go on a side face: the front carries the dock
        if (meshEmblem && m.n[f].z < 0.8f && area > emblemArea) {
          emblemArea = area; emblemX = sx[a0] + ux * 0.33f + vx * 0.33f; emblemY = sy[a0] + uy * 0.33f + vy * 0.33f;
          decalA = wv[a0]; decalB = wv[a1]; decalC = wv[a2]; decalN = ob.o.toWorld(m.n[f]); decalK = k; decalFace = f;
        }
      }
    }
  }
  if (meshEmblem && decalFace >= 0 && decalFace < m.nt) {
    // a flat panel is stored as several triangles: size the decal to the whole panel
    V3 acc{0, 0, 0}; float tot = 0.f;
    for (int f = 0; f < m.nt; f++) {
      if (!vis[f] || dot(m.n[f], m.n[decalFace]) < 0.995f) continue;
      V3 A = wv[m.t[f][0]], B = wv[m.t[f][1]], C = wv[m.t[f][2]];
      float ar = len(cross(B - A, C - A)) * 0.5f;
      acc += (A + B + C) * (ar / 3.f); tot += ar;
    }
    if (tot > 0.f) { decalCenter = acc * (1.f / tot); decalSize = sqrtf(tot) * 0.62f; }
    decalFace = -1;
  }
  for (int e = 0; e < m.ne; e++) {
    bool v0 = vis[m.e[e][2]], v1 = m.e[e][3] != 255 && vis[m.e[e][3]];
    if (solid && !v0 && !v1) continue;
    cv.drawLine((int)sx[m.e[e][0]], (int)sy[m.e[e][0]], (int)sx[m.e[e][1]], (int)sy[m.e[e][1]], (v0 || v1) ? edgeCol : shade(edgeCol, 0.35f));
  }
  return true;
}

// ============================================================
//  stations: built from parts, dressed in the colours of whoever built them
// ============================================================
enum StationStyle : uint8_t { SS_HEXCORE = 0, SS_RING, SS_SPINDLE, SS_OUTPOST, SS_CORIOLIS, SS_HABITAT };
enum Brand : uint8_t { BR_LIMINAR = 0, BR_PORTEX, BR_MALTAPLEX, BR_DESERET, BR_FREEHOLD };
struct BrandLook { const char *name; uint16_t base, accent, light; };
static const BrandLook &brandLook(uint8_t b) {
  // colours taken from the corporate logo sheet
  static const BrandLook looks[5] = {
    {"LIMINAR", rgb(200, 206, 210), rgb(36, 168, 186), rgb(140, 240, 255)},
    {"PORTEX", rgb(150, 162, 180), rgb(46, 74, 132), rgb(200, 230, 255)},
    {"MALTAPLEX", rgb(178, 180, 184), rgb(232, 158, 40), rgb(255, 214, 120)},
    {"DESERET", rgb(214, 206, 180), rgb(122, 138, 78), rgb(255, 236, 170)},
    {"FREEHOLD", rgb(120, 124, 118), rgb(178, 98, 62), rgb(255, 170, 90)}};
  return looks[b < 5 ? b : 4];
}

struct StationPart { uint8_t mesh; V3 off; float yaw, roll, scale; bool accent; };

static int stationParts(uint8_t style, StationPart *out) {
  int n = 0;
  auto add = [&](uint8_t m, V3 o, float yaw, float roll, float sc, bool acc) { out[n++] = {m, o, yaw, roll, sc, acc}; };
  switch (style) {
    case SS_HEXCORE:
      add(M_HEXCORE, V3{0, 0, 0}, 0, 0, 1.f, false);
      for (int i = 0; i < 6; i++) { float a = i * 1.0471976f + 0.5235988f; add(M_MODULE, V3{cosf(a) * 0.98f, sinf(a) * 0.98f, 0.1f}, 0, a, 0.2f, true); }
      add(M_PANEL, V3{-1.75f, 0, -0.35f}, 0, 0, 0.95f, false);
      add(M_PANEL, V3{1.75f, 0, -0.35f}, 0, 0, 0.95f, false);
      break;
    case SS_RING:
      for (int i = 0; i < 14; i++) {
        float a = i * 0.4487989f;
        add(M_RINGSEG, V3{cosf(a), sinf(a), 0}, 0, a, 1.f, (i % 7) == 0);
      }
      for (int i = 0; i < 4; i++) { float a = i * 1.5707963f + 0.785f; add(M_MODULE, V3{cosf(a) * 1.22f, sinf(a) * 1.22f, 0}, 0, a, 0.24f, true); }
      break;
    case SS_SPINDLE:
      for (int i = 0; i < 5; i++) add((i & 1) ? M_OCT : M_OCTWIDE, V3{0, 0, -1.55f + i * 0.46f}, 0, i * 0.2f, 1.f, (i & 1) == 0);
      add(M_OCT, V3{0, 0, 0.62f}, 0, 0, 0.75f, true);
      add(M_PANEL, V3{0, 0.95f, -0.85f}, 0, 1.5707963f, 0.8f, false);
      add(M_PANEL, V3{0, -0.95f, -0.85f}, 0, 1.5707963f, 0.8f, false);
      break;
    case SS_HABITAT:   // Deseret: a colony of domes on a base, a tunnel to the dock
      add(M_BASE, V3{0, -0.2f, 0}, 0, 0, 1.f, true);
      add(M_DOME, V3{0, -0.12f, 0}, 0, 0, 0.82f, false);
      for (int i = 0; i < 3; i++) { float a = i * 2.0943951f + 0.5f; add(M_DOME, V3{cosf(a) * 0.82f, -0.12f, sinf(a) * 0.82f * 0.6f - 0.25f}, 0, 0, 0.36f, false); }
      add(M_MODULE, V3{0, -0.1f, 0.95f}, 0, 0, 0.42f, true);
      break;
    case SS_OUTPOST:
      add(M_MODULE, V3{0, 0, 0}, 0, 0, 1.f, false);
      add(M_MODULE, V3{0, -0.42f, -0.55f}, 1.5707963f, 0, 0.75f, true);
      add(M_PANEL, V3{-1.1f, 0.15f, -0.2f}, 0, 0.25f, 0.8f, false);
      add(M_PANEL, V3{1.1f, 0.15f, -0.2f}, 0, -0.25f, 0.8f, false);
      break;
    default:
      add(M_STATION, V3{0, 0, 0}, 0, 0, 1.f, false);
      break;
  }
  return n;
}

// where the dock gate sits in front of a station, in units of its radius
static float stationDockFront(uint8_t style) {
  switch (style) { case SS_RING: return 0.f; case SS_SPINDLE: return 1.35f; case SS_OUTPOST: return 1.9f; case SS_HEXCORE: return 1.75f; case SS_HABITAT: return 1.75f; default: return 1.73f; }
}

// each class wears a two-tone hull and a lit canopy, like ours, in its own colours
static bool liveryOn = true;
// returns the hull colour (0 = keep the ship's own)
static uint16_t shipLivery(const Obj &o) {
  meshAccent = meshCanopy = 0;
  if (!liveryOn) return 0;
  if (o.ghost) { meshAccent = rgb(26, 30, 36); meshCanopy = rgb(190, 235, 225); return rgb(92, 100, 110); }
  if (o.uses == 1 + BR_DESERET) { meshAccent = rgb(122, 138, 78); meshCanopy = rgb(200, 220, 150); return rgb(222, 214, 190); }
  switch (o.enc) {
    case sm::ENC_TRAVELER:  meshAccent = rgb(0, 115, 115);  meshCanopy = rgb(150, 225, 30);  return rgb(214, 204, 178);
    case sm::ENC_MERCHANT:  meshAccent = rgb(214, 132, 38); meshCanopy = rgb(255, 214, 120); return rgb(226, 218, 196);
    case sm::ENC_SECURITY:  meshAccent = rgb(40, 70, 150);  meshCanopy = rgb(140, 220, 255); return rgb(222, 228, 236);
    case sm::ENC_PIRATE:    meshAccent = rgb(34, 28, 28);   meshCanopy = rgb(255, 70, 50);   return rgb(178, 92, 64);
    case sm::ENC_SUBPIRATE: meshAccent = rgb(93, 0, 93);    meshCanopy = rgb(255, 120, 255); return rgb(132, 116, 150);
    default: return 0;
  }
}

static void drawStationObj(const Obj &st) {
  uint8_t style = st.bodyType;
  const BrandLook &bl = brandLook(st.uses);
  StationPart parts[24];
  int n = stationParts(style, parts);
  // world placement for each part, then far to near
  Obj tmp[24]; float pz[24]; int ord[24];
  for (int i = 0; i < n; i++) {
    const StationPart &pp = parts[i];
    tmp[i] = st;
    tmp[i].p = st.p + st.o.toWorld(pp.off * st.radius);
    Basis b = st.o;
    if (pp.roll != 0.f) b.roll(pp.roll);
    if (pp.yaw != 0.f) b.yaw(pp.yaw);
    if (pp.mesh == M_RINGSEG) {   // segments run along the ring's tangent, their top facing outward
      V3 radial = norm(tmp[i].p - st.p);
      b = Basis::facing(norm(cross(st.o.f, radial)), radial);
    }
    b.fix();
    tmp[i].o = b;
    V3 c = shipB.toLocal(tmp[i].p - shipPos);
    pz[i] = c.z; ord[i] = i;
  }
  for (int i = 1; i < n; i++) { int k = ord[i]; int j = i - 1; while (j >= 0 && pz[ord[j]] < pz[k]) { ord[j + 1] = ord[j]; j--; } ord[j + 1] = k; }
  float outR;
  for (int q = 0; q < n; q++) {
    int i = ord[q];
    const StationPart &pp = parts[i];
    meshAccent = (pp.accent || style == SS_RING) ? 0 : bl.accent;
    meshAmbient = 0.45f;
    meshDetail = pp.mesh == M_PANEL ? 1 : 2; meshWindow = bl.light;
    bool core = (pp.mesh == M_DOME && i == 1) || pp.mesh == M_HEXCORE || (style == SS_SPINDLE && pp.mesh == M_OCTWIDE && i == 2) || (style == SS_OUTPOST && i == 0) || (style == SS_RING && pp.accent && pp.mesh == M_RINGSEG);
    meshEmblem = core ? (uint8_t)(st.uses + 1) : 0;
    if (core) { emblemArea = 0.f; decalSize = 0.f; }
    uint16_t base = pp.accent ? bl.accent : (pp.mesh == M_PANEL ? rgb(40, 52, 84) : bl.base);
    uint16_t edge = pp.mesh == M_PANEL ? rgb(90, 120, 170) : shade(base, 1.25f);
    drawMesh(tmp[i], meshes[pp.mesh], st.radius * pp.scale, base, edge, true, outR);
    if (meshEmblem && emblemArea > 110.f) {
      static const int8_t brandLogo[5] = {LOGO_LIMINAR, -1, LOGO_MALTAPLEX, LOGO_DESERET, LOGO_FREEHOLD};
      int lg = st.uses < 5 ? brandLogo[st.uses] : -1;
      if (lg >= 0) drawDecal(lg, st.o.u);
      else drawEmblem((int)emblemX, (int)emblemY, meshEmblem, emblemArea > 700.f ? 2 : 1, bl.light);
    }
    meshAccent = 0; meshAmbient = 0.16f; meshDetail = 0; meshEmblem = 0; meshWindow = 0;
  }
  float sx, sy, z;
  if (!project(st.p, sx, sy, z)) return;
  float R = st.radius * FOCAL / z;
  bool blink = ((int)(tNow * 2.f) & 1) != 0;
  // the dock: a lit slot, collar, or the ring's own heart
  if (style == SS_HEXCORE || style == SS_CORIOLIS) {
    float fx, fy, fz;
    if (project(st.p + st.o.f * st.radius * (style == SS_HEXCORE ? 0.87f : 0.98f), fx, fy, fz) && dot(st.o.f, shipPos - st.p) > 0) {
      int w = (int)clampf(st.radius * 0.5f * FOCAL / fz, 2, 80), h = w / 3 + 1;
      cv.fillRect((int)fx - w / 2, (int)fy - h / 2, w, h, rgb(4, 6, 10));
      cv.drawRect((int)fx - w / 2, (int)fy - h / 2, w, h, blink ? bl.light : shade(bl.light, 0.45f));
    }
  } else if (style == SS_SPINDLE) {
    float fx, fy, fz;
    if (project(st.p + st.o.f * st.radius * 0.8f, fx, fy, fz)) cv.drawCircle((int)fx, (int)fy, (int)clampf(st.radius * 0.22f * FOCAL / fz, 2, 40), blink ? bl.light : shade(bl.light, 0.5f));
  } else if (style == SS_OUTPOST) {
    float fx, fy, fz;
    if (project(st.p + st.o.f * st.radius * 0.45f, fx, fy, fz)) cv.fillCircle((int)fx, (int)fy, blink ? 2 : 1, bl.light);
  }
  // running lights at the extremities
  if (R > 6) {
    for (int i = 0; i < 4; i++) {
      float a = i * 1.5707963f + (style == SS_RING ? 0.f : 0.785f);
      float rr = style == SS_RING ? 1.12f : (style == SS_OUTPOST ? 1.6f : 1.05f);
      float lx, ly, lz;
      if (project(st.p + (st.o.r * cosf(a) + st.o.u * sinf(a)) * (st.radius * rr), lx, ly, lz) && ((i + (int)(tNow * 3.f)) & 1))
        cv.drawPixel((int)lx, (int)ly, i < 2 ? rgb(255, 60, 50) : rgb(80, 255, 120));
    }
  }
  // the maker's mark
  if (R > 14 && R < 400) {
    cv.setTextColor(bl.accent == rgb(46, 74, 132) ? rgb(150, 180, 230) : bl.light);
    int tw = (int)strlen(bl.name) * 6;
    cv.setCursor((int)sx - tw / 2, (int)clampf(sy + R * 1.15f + 4, 0, 200)); cv.print(bl.name);
  }
}

// ============================================================
//  decals: a logo baked onto a face in perspective. Each texel is placed on the
//  face's own plane, oriented to the station's up, and lit with the face.
// ============================================================
// a logo straight onto the screen (board headers)
static void drawLogoFlat(int x, int y, int logo) {
  if (logo < 0 || logo >= LOGO_COUNT) return;
  for (int j = 0; j < LOGO_SIZE; j++) for (int i = 0; i < LOGO_SIZE; i++) { uint16_t c = LOGOS[logo][j * LOGO_SIZE + i]; if (c) cv.drawPixel(x + i, y + j, c); }
}

static void drawDecal(int logo, V3 up) {
  if (logo < 0 || logo >= LOGO_COUNT) return;
  V3 n = norm(decalN);
  V3 c = decalCenter;
  float size = decalSize;
  if (size <= 0.f) {   // fallback: the triangle alone
    c = (decalA + decalB + decalC) * (1.f / 3.f);
    float la = len(decalB - decalC), lb = len(decalC - decalA), lc = len(decalA - decalB);
    float area3 = len(cross(decalB - decalA, decalC - decalA)) * 0.5f;
    size = 2.f * area3 / (la + lb + lc + 1e-4f) * 1.35f;
  }
  V3 e2 = up - n * dot(up, n);
  if (len(e2) < 1e-3f) e2 = decalB - decalA - n * dot(decalB - decalA, n);
  e2 = norm(e2);
  V3 e1 = cross(n, e2);
  float cx, cy, cz;
  if (!project(c, cx, cy, cz)) return;
  float px = size * FOCAL / cz / LOGO_SIZE;   // screen size of one texel
  int ts = (int)ceilf(px);
  if (px < 0.45f) return;                       // too far: nothing would read
  if (ts < 1) ts = 1;
  float k = clampf(0.55f + 0.5f * decalK, 0.55f, 1.05f);
  const uint16_t *img = LOGOS[logo];
  for (int y = 0; y < LOGO_SIZE; y++) {
    float v = 0.5f - (y + 0.5f) / LOGO_SIZE;
    for (int x = 0; x < LOGO_SIZE; x++) {
      uint16_t col = img[y * LOGO_SIZE + x];
      if (!col) continue;
      float u = (x + 0.5f) / LOGO_SIZE - 0.5f;
      float sx, sy, sz;
      if (!project(c + e1 * (u * size) + e2 * (v * size) + n * 0.05f, sx, sy, sz)) continue;
      cv.fillRect((int)(sx - px * 0.5f), (int)(sy - px * 0.5f), ts, ts, k < 0.99f ? shade(col, k) : col);
    }
  }
}

// A ring in 3D: gates and portals are real hoops, seen at an angle.
static void drawRing(const Obj &g, float R, uint16_t col, int segs, float jitter, float phase, int thick) {
  float px = 0, py = 0; bool pv = false;
  for (int s = 0; s <= segs; s++) {
    float a = s * 6.2831853f / segs;
    float rr = R * (1.f + jitter * fsin(a * 5.f + phase) + jitter * 0.6f * fsin(a * 11.f - phase * 1.7f));
    V3 w = g.p + (g.o.r * fcos(a) + g.o.u * fsin(a)) * rr;
    float x, y, z;
    bool ok = project(w, x, y, z);
    if (ok && pv) {
      cv.drawLine((int)px, (int)py, (int)x, (int)y, col);
      if (thick > 1) cv.drawLine((int)px + 1, (int)py, (int)x + 1, (int)y, col);
      if (thick > 2) cv.drawLine((int)px, (int)py + 1, (int)x, (int)y + 1, shade(col, 0.7f));
    }
    px = x; py = y; pv = ok;
  }
}

// ============================================================
//  fx
// ============================================================
struct Boom { V3 p; float t, size; uint16_t col; bool alive; };
static Boom booms[6];
static void addBoom(V3 p, float size, uint16_t col) {
  for (auto &b : booms) if (!b.alive) { b = {p, 0, size, col, true}; return; }
  booms[0] = {p, 0, size, col, true};
}
struct Bolt { float x0, y0, x1, y1; uint8_t life, thick; uint16_t col; };
static Bolt bolts[16];
static uint8_t boltN = 0;
static void addBolt(float x0, float y0, float x1, float y1, uint16_t col, uint8_t thick = 1, uint8_t life = 7) {
  if (boltN < 16) bolts[boltN++] = {x0, y0, x1, y1, life, thick, col};
}
// particle missiles: curved screen paths with trails
struct Missile { float x0, y0, cx, cy, x1, y1, t, dur; uint16_t col; bool alive; };
static Missile missiles[10];
static void addMissile(float x0, float y0, float x1, float y1, uint16_t col, float dur) {
  for (auto &m : missiles) if (!m.alive) {
    m = {x0, y0, (x0 + x1) * 0.5f + rf(-70, 70), fminf(y0, y1) - rf(20, 80), x1, y1, 0, dur, col, true};
    return;
  }
}
// short-lived sparks and flares in screen space
struct Spark { float x, y, vx, vy, t; uint16_t col; };
static Spark sparks[40];
static uint8_t sparkN = 0;
static void addSparks(float x, float y, int n, uint16_t col, float spd = 90.f) {
  for (int i = 0; i < n && sparkN < 40; i++) { float a = rf(0, 6.283f), v = rf(0.3f, 1.f) * spd; sparks[sparkN++] = {x, y, cosf(a) * v, sinf(a) * v, 0, col}; }
}
static float shieldFx = 0.f, shieldFxX = 160, shieldFxY = 200;   // a deflection flare
// mined rock: fragments fly, then a tractor beam pulls them home
struct Frag { V3 p, v; float t; uint16_t col; bool alive; };
static Frag frags[16];
static void addFrags(V3 at, int n, uint16_t col) {
  for (int i = 0; i < n; i++) for (auto &f : frags) if (!f.alive) { f = {at, randDir() * rf(6, 14), 0, col, true}; break; }
}

// ============================================================
//  the long arc and the money
// ============================================================
// Where you are, so a power cycle picks up right here.
struct Session {
  uint32_t magic;
  uint8_t layer, station, docked, pad;
  sm::Trip trip;
  char here[24], origin[24];
  float throttle;
};
static const uint32_t SESSION_MAGIC = 0x53455331u;   // "SES1"
static bool sdDirty = false;
static uint32_t sdLastWrite = 0;
static uint8_t journalSeen = 0, journalHeadSeen = 0;
static void captureSession(Session &ss);
static void saveSessionNVS() {
  Session ss; captureSession(ss);
  Preferences prefs;
  if (prefs.begin("sm_sess", false)) { prefs.putBytes("s", &ss, sizeof(ss)); prefs.end(); }
}
static void saveAll() {
  sm::sheetSave(); sm::contractsSave(); sm::atlasSave(); sm::journalSave();
  saveSessionNVS();
  sdDirty = true;   // the card catches up within a moment (see serviceSD)
  lastSave = millis();
}
static void serviceSD(bool force) {
  if (!sm::sdReady() || !sdDirty) return;
  if (!force && millis() - sdLastWrite < 1500) return;
  Session ss; captureSession(ss);
  if (sm::sdSaveGame(&ss, sizeof(ss))) {
    sdDirty = false; sdLastWrite = millis();
    if (sm::journal().count != journalSeen || sm::journal().head != journalHeadSeen) {
      sm::sdWriteJournalText(); journalSeen = sm::journal().count; journalHeadSeen = sm::journal().head;
    }
  }
}

static void onDestroyedFlow();
static bool checkDestroy() {
  bool died = sm::sheet().lives > livesSeen;
  if (died) onDestroyedFlow();
  livesSeen = sm::sheet().lives;
  return died;
}
static uint16_t shieldTint() {
  uint8_t s = sm::capTier(sm::CAP_SHIELDS);
  return s >= 7 ? rgb(255, 236, 170) : s >= 5 ? rgb(220, 200, 255) : s >= 3 ? rgb(120, 240, 255) : rgb(90, 160, 255);
}
// shielded: collisions, heat and fire are softened by the shields; reality glitches are not
static bool damage(int amount, bool shielded = true) {
  if (amount > 0) {
    uint8_t s = sm::capTier(sm::CAP_SHIELDS);
    if (shielded && s > 0) {
      amount = (int)(amount * clampf(1.f - 0.07f * s, 0.35f, 1.f) + 0.5f);
      shieldFx = 0.35f; shieldFxX = 160 + rf(-60, 60); shieldFxY = 190;
    }
    if (amount > 0) {
      sm::damageHull((uint16_t)amount);
      hx::thud(clampf(amount / 25.f, 0.35f, 1.f));
      hitFlash = s > 0 && shielded ? 0.15f : 0.35f;
    }
  }
  return checkDestroy();
}

// A way through the deep is worth real money; a shallow hop is infrastructure.
static int routeValue(uint8_t depth, bool unknown) {
  static const int base[] = {0, 0, 90, 280, 700};
  int v = base[depth > 4 ? 4 : depth];
  if (unknown) v = v * 3 / 2;
  return v;
}
// Charting a deep landmark first time is the big score of the explorer's trade.
static int chartValue(uint8_t band) { return 70 * band * band; }

static bool endingOpen = false;
static bool lostOpen = false;
static bool bootOpen = true;  // title card until first tap
static bool statusOpen = false;  // long-press C: pilot license + ship diagnostic (visual reference only)
static uint8_t statusPage = 0;   // 0 license + diagnostic, 1 journal + active lead (hold C)
static float newGameHold = 0, newGameDone = 0;   // splash: hold B + C
static float signalsHold = 0, signalsDone = 0;   // splash: hold A + C (experimental Signals)
static float cloakT = 0.f, cloakCD = 0.f;          // hold B: cloak (duration and cooldown grow with the mark)
// Something in the deep, below the roads. Rare; no damage; you never see it clearly.
struct AlienEncounter { bool active; uint8_t cls, outcome; float t, beatA, beatB, beatC; V3 p; bool applied; };
static AlienEncounter alien{};
static float alienPending = -1.f;       // seconds until it arrives (scheduled on entering a layer)
static float alienCooldown = 0.f;       // game seconds before another can come
static inline bool alienHolds() { return alien.active && alien.t >= 4.f && alien.t < 28.f; }   // power is out
static bool mapOpen = false;     // long-press A: the atlas (visual reference only)
static uint8_t mapPage = 0;      // 0 subspace memory, 1 this system (hold A again)
static int mapTrace = -1;        // atlas place traced from here
static char tripOrigin[24] = ""; // where the current trip began
static char endingName[24] = "";
static void showRecognition(const sm::Landmark *lm) {
  asciiCopy(endingName, sizeof(endingName), lm->name); upcase(endingName);
  if (!sm::flagHas("deep_small")) {
    sm::flagSet("deep_small", 1, true);
    endingOpen = true;
    hx::cut(0.4f);
  } else {
    char b[112]; snprintf(b, sizeof(b), "%s. YOU KNOW THIS PLACE FROM ANOTHER SKY.", endingName);
    setBanner(b, 3200);
  }
}

// ============================================================
//  gates, portals, trips
// ============================================================
static bool dueHereNow() { return layer == 0 && sm::contractDueHere(hereName); }   // deliveries are paid at surface docks

static uint16_t gateColor(const Obj &g) {
  if (g.kind == K_GATE && g.uses == 2) return rgb(230, 60, 230);   // a commissioned signal gate
  if (g.kind == K_DOCKGATE) return dueHereNow() ? rgb(240, 200, 90) : rgb(70, 150, 255);
  if (g.gflags & GF_JOB) return rgb(240, 200, 90);
  if (g.gflags & GF_FIXED) return rgb(185, 160, 255);        // charted deep landmark: lilac, never job-gold
  if (g.gflags & GF_LOCALNAME) return rgb(230, 175, 80);
  if (g.gflags & GF_CHAIN) return layer == 0 ? rgb(110, 240, 200) : hsv(layerHue(layer) + 60, 0.55f, 0.95f);
  if (g.gflags & GF_RUMOR) return rgb(80, 225, 215);
  if (g.gflags & GF_KNOWN) return rgb(110, 230, 150);
  return rgb(150, 150, 170);
}


// Gates only live in empty space: push a candidate point clear of fat world objects.
static bool gateBlocked(V3 p, float gateR, int ignoreIdx = -1) {
  float need = gateR + 12.f;
  for (int i = 0; i < MAX_OBJ; i++) {
    if (i == ignoreIdx) continue;
    const Obj &o = objs[i];
    if (o.kind == K_NONE) continue;
    // thin contacts / debris can share sky; fat volumes cannot swallow a ring
    if (o.kind != K_BODY && o.kind != K_LANDMARK && o.kind != K_STATION && o.kind != K_ROCK
        && o.kind != K_GATE && o.kind != K_PORTAL && o.kind != K_DOCKGATE) continue;
    float minD = o.radius + need;
    // giants: stay outside atmosphere-ish volume (scoop band is ~0.45R; use 0.55R margin)
    if (o.kind == K_BODY) minD = o.radius * 0.55f + need;
    if (o.kind == K_LANDMARK) minD = o.radius * 1.15f + need;
    if (o.kind == K_STATION) minD = o.radius * 1.5f + need;
    if (len(p - o.p) < minD) return true;
  }
  return false;
}

static V3 emptyGatePoint(V3 p, float gateR) {
  if (!gateBlocked(p, gateR)) return p;
  // nudge along / across the approach until the ring sits in clear sky
  V3 f = shipB.f, r = shipB.r, u = shipB.u;
  const float steps[] = { 20.f, 40.f, 60.f, 90.f, 120.f, 160.f };
  const float lats[] = { 0.f, 35.f, -35.f, 70.f, -70.f, 100.f, -100.f };
  const float verts[] = { 0.f, 25.f, -25.f, 50.f, -50.f };
  for (float d : steps)
    for (float lat : lats)
      for (float vert : verts) {
        V3 q = p + f * d + r * lat + u * vert;
        if (!gateBlocked(q, gateR)) return q;
        q = p - f * (d * 0.35f) + r * lat + u * vert;
        if (!gateBlocked(q, gateR)) return q;
      }
  // last resort: far off the right wing, past typical giant radii
  return p + r * 220.f + f * 40.f + u * 30.f;
}

static Obj *spawnGate(V3 p, V3 facing, const char *name, uint8_t depth, uint8_t flags, float R) {
  Obj *g = newObj(K_GATE);
  if (!g) return nullptr;
  p = emptyGatePoint(p, R);
  g->p = p; g->o = Basis::facing(facing, V3{0, 1, 0}); g->radius = R;
  g->gflags = flags; g->depth = depth;
  asciiCopy(g->name, sizeof(g->name), name); upcase(g->name);
  g->prevSide = dot(shipPos - g->p, g->o.f);
  return g;
}

// the deep is smaller: gates come closer together the further down you are
static float legSpacing() { return layer == 0 ? 95.f : clampf(125.f - layer * 18.f, 55.f, 125.f); }

static V3 aheadPoint(float dist, float lat, float vert) {
  return shipPos + shipB.f * dist + shipB.r * lat + shipB.u * vert;
}

static void spawnNextOnPath() {
  sm::Trip &tr = sm::trip();
  if (!tr.active) return;
  float d = legSpacing();
  V3 p = aheadPoint(d, rf(-0.36f, 0.36f) * d, rf(-0.22f, 0.22f) * d);
  Obj *o;
  if (sm::tripPortalReady()) {
    o = newObj(K_PORTAL);
    if (!o) return;
    o->p = emptyGatePoint(p + shipB.f * 25.f, 13.f);
    o->o = Basis::facing(norm(o->p - shipPos), shipB.u);
    o->radius = 13.f;
    o->prevSide = dot(shipPos - o->p, o->o.f);
    snprintf(o->name, sizeof(o->name), "%s", sm::tripPortalGoesUp() ? "PORTAL UP" : "PORTAL DOWN");
    setBanner(sm::tripPortalGoesUp() ? "A PORTAL TEARS OPEN - THE WAY UP" : "A PORTAL TEARS OPEN AHEAD", 1800);
    hx::swell(0.35f, 0.5f, 0.6f);
  } else {
    char label[24];
    uint8_t flags = GF_CHAIN;
    if (layer == 4) {
      // the deep cove keeps its own local names, the same in every universe
      uint32_t key = 0xD33Fu + tr.legs * 7u + tr.step * 3u + (tr.ascending ? 101u : 0u);
      asciiCopy(label, sizeof(label), sm::deepName(key));
      flags |= GF_LOCALNAME;
    } else snprintf(label, sizeof(label), "%s", tr.dest);
    o = spawnGate(p, norm(p - shipPos), label, tr.destDepth, flags, 8.5f);
  }
  navObj = idxOf(o);
}

// ---- scenes ----
// A stable number for a place name in this universe: its sky, its star, its dock.
static uint32_t placeHash(const char *name) {
  // sheet().universeSeed is fixed for a universe (universeSeed() is the running RNG state)
  uint32_t h = 2166136261u ^ sm::sheet().universeSeed;
  for (const char *c = name; c && *c; c++) { h ^= (uint8_t)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c); h *= 16777619u; }
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
  return h;
}

static uint8_t makeLook(uint8_t style, uint8_t brand) { return (uint8_t)(0x80 | (style & 7) | ((brand & 7) << 3)); }

// Who built the dock here: decided once per place and remembered until the wipe.
// Far places (deep-rated) are more often Freehold outposts.
static uint8_t stationLookFor(const char *place) {
  int ai = sm::atlasFind(place);
  if (ai >= 0 && sm::atlas().place[ai].look) return sm::atlas().place[ai].look;
  bool far = ai >= 0 && sm::atlas().place[ai].depth >= 2;
  uint32_t r = placeHash(place) % 100;
  uint8_t look;
  if (r < (uint32_t)(far ? 38 : 10)) look = makeLook(SS_OUTPOST, ((placeHash(place) >> 11) % 5) < 2 ? BR_DESERET : BR_FREEHOLD);
  else {
    uint32_t q = (r * 7u) % 100;
    look = q < 28 ? makeLook(SS_HEXCORE, BR_LIMINAR) : q < 52 ? makeLook(SS_RING, BR_MALTAPLEX)
         : q < 74 ? makeLook(SS_SPINDLE, BR_PORTEX) : makeLook(SS_HABITAT, BR_DESERET);
  }
  if (ai >= 0) sm::atlas().place[ai].look = look;
  return look;
}

static void placeStation(V3 p, V3 facing, uint8_t look = 0) {
  Obj *st = newObj(K_STATION);
  if (!st) return;
  if (!look) look = stationLookFor(hereName);
  uint8_t style = look & 7, brand = (look >> 3) & 7;
  static const float radius[] = {24.f, 32.f, 26.f, 11.f, 22.f, 26.f};
  st->p = p; st->o = Basis::facing(facing, V3{0, 1, 0}); st->radius = radius[style < 6 ? style : 0]; st->mesh = M_STATION;
  st->spin = style == SS_OUTPOST ? 0.1f : style == SS_HABITAT ? 0.08f : 0.22f; st->col = rgb(150, 160, 175); st->enc = sm::ENC_STATION;
  st->bodyType = style; st->uses = brand;
  static const char *kindName[] = {"HEXCORE", "RING", "SPINDLE", "OUTPOST", "STATION", "HABITAT"};
  snprintf(st->name, sizeof(st->name), "%s %s", brandLook(brand).name, kindName[style < 6 ? style : 4]);
  Obj *dg = newObj(K_DOCKGATE);
  if (!dg) { st->kind = K_NONE; return; }
  dg->p = p + st->o.f * (stationDockFront(style) * st->radius); dg->o = st->o;
  dg->radius = style == SS_RING ? st->radius * 0.55f : (style == SS_OUTPOST ? 5.5f : 7.f);
  snprintf(dg->name, sizeof(dg->name), "DOCK");
  dg->prevSide = dot(shipPos - dg->p, dg->o.f);
  st->link = idxOf(dg); dg->link = idxOf(st);
}

static bool isRumor(const char *name) {
  for (uint8_t r = 0; r < sm::rumorCount(); r++) {
    const sm::NameTag *t = sm::rumorAt(r);
    if (!t) continue;
    char up[24]; asciiCopy(up, sizeof(up), t->name); upcase(up);
    char nm[24]; asciiCopy(nm, sizeof(nm), name); upcase(nm);
    if (strcmp(up, nm) == 0) return true;
  }
  return false;
}

static const sm::Landmark *landmarkNamed(const char *name);

// Named gates fan out across your view, the way a harbour lays out its lanes.
static void placeDestGates(int want, V3 avoidDir = V3{0, 0, 0}) {
  (void)want;   // a place keeps its own lanes until a wipe (see atlas)
  sm::AtlasLane lanes[8];
  int n = sm::atlasLanesHere(lanes, 8);
  sm::Contract &c = sm::contract();
  const char *names[8]; const char *vias[8] = {nullptr}; uint8_t depths[8], flags[8]; int m = 0;
  for (int i = 0; i < n && m < 6; i++) {
    names[m] = lanes[i].name; depths[m] = lanes[i].depth; vias[m] = lanes[i].via[0] ? lanes[i].via : nullptr;
    flags[m] = GF_DEST | (lanes[i].fixed ? GF_FIXED : 0) |
               (lanes[i].visited ? GF_KNOWN : (lanes[i].rumor ? GF_RUMOR : GF_UNKNOWN));
    if (c.live && c.dest[0] && sm::sameName(lanes[i].name, c.dest)) flags[m] |= GF_JOB;   // the lane your job runs down
    m++;
  }
  float span = 2.3f;                       // about 130 degrees of sky
  for (int k = 0; k < m; k++) {
    float a = -span * 0.5f + span * (k + 0.5f) / m + rf(-0.08f, 0.08f);
    float el = ((k & 1) ? 0.16f : -0.12f) + rf(-0.06f, 0.06f);
    V3 dir = norm(shipB.f * cosf(a) + shipB.r * sinf(a) + shipB.u * el);
    if (len(avoidDir) > 0.5f && dot(dir, avoidDir) > 0.97f) dir = norm(dir + shipB.u * 0.35f);   // keep clear of the dock
    V3 p = shipPos + dir * rf(105, 150);
    Obj *g = spawnGate(p, norm(shipPos - p), names[k], depths[k], flags[k], 9.f);
    if (g) {
      const sm::Landmark *hub = (flags[k] & GF_FIXED) ? landmarkNamed(names[k]) : landmarkNamed(vias[k]);
      if (hub) g->lmId = (int)hub->id;          // the landmark this gate goes to, or runs past
    }
  }
}

static void spawnBody(uint8_t type, float dist, float radius, V3 dir) {
  Obj *b = newObj(K_BODY);
  if (!b) return;
  b->bodyType = type; b->radius = radius;
  b->p = shipPos + norm(dir) * dist;
  b->o = Basis::facing(randDir(), randDir());
  b->enc = sm::ENC_LANDMARK_GIANT;
  if (type == BT_GIANT) {
    static const uint16_t pal[] = {rgb(210, 170, 120), rgb(150, 190, 220), rgb(200, 140, 110), rgb(170, 200, 160), rgb(220, 200, 160)};
    b->col = pal[rnd() % 5];
    asciiCopy(b->name, sizeof(b->name), sm::encounterFlavor(sm::ENC_LANDMARK_GIANT, (uint8_t)layer).name);
  } else if (type == BT_ROCKY) {
    static const uint16_t pal[] = {rgb(170, 140, 110), rgb(120, 140, 170), rgb(190, 110, 80), rgb(150, 150, 140)};
    b->col = pal[rnd() % 4];
    snprintf(b->name, sizeof(b->name), "WORLD");
  } else if (type == BT_HOLE) {
    b->col = rgb(0, 0, 0);
    snprintf(b->name, sizeof(b->name), "COLLAPSED STAR");
  } else {
    b->col = hsv(layerHue(layer) + 150, 0.6f, 0.9f);
    snprintf(b->name, sizeof(b->name), "IMPOSSIBLE STAR");
  }
}

static void spawnRock(V3 p, float r) {
  Obj *o = newObj(K_ROCK);
  if (!o) return;
  o->p = p; o->radius = r; o->mesh = (uint8_t)(M_ROCK0 + ri(0, 5));
  o->o = Basis::facing(randDir(), randDir());
  o->spin = rf(-0.4f, 0.4f); o->col = hsv(rf(20, 40), rf(0.2f, 0.4f), rf(0.45f, 0.65f));
  o->uses = (uint8_t)ri(1, 3); o->enc = sm::ENC_LANDMARK_ROCK;
  asciiCopy(o->name, sizeof(o->name), sm::encounterFlavor(sm::ENC_LANDMARK_ROCK, (uint8_t)layer).name);
}

static void makeRealScene(const char *place, bool station) {
  clearWorld();
  target = -1; navObj = -1; dockTarget = -1;
  asciiCopy(hereName, sizeof(hereName), place && place[0] ? place : sm::placeName(sm::urand(), 0, false));
  upcase(hereName);
  buildSky(placeHash(hereName));   // a place keeps its sky (and its star) until the wipe
  sm::contractSetHere(hereName);
  sm::contractSetBand(0);
  { sm::Atlas &at = sm::atlas();
    if (at.here == 255 || !sm::sameName(at.place[at.here].name, hereName)) sm::atlasVisit(hereName, nullptr, 1, false);
    if (at.here != 255) {
      sm::AtlasPlace &ap = at.place[at.here];
      if (ap.flags & sm::AP_STATION_SET) station = (ap.flags & sm::AP_HAS_STATION) != 0;
      else ap.flags |= (uint8_t)(sm::AP_STATION_SET | (station ? sm::AP_HAS_STATION : 0));
    } }
  shipPos = V3{0, 0, 0}; prevShipPos = shipPos;
  shipB = Basis::facing(V3{0, 0, 1}, V3{0, 1, 0});
  layer = 0;
  resetDust();
  // sometimes the star is near enough to fly to; a rare cool one can even be scooped (it burns)
  {
    uint32_t h = placeHash(hereName) >> 7;
    nearStar = (h % 100) < 38;
    if (nearStar) {
      static const float rad[7] = {90.f, 120.f, 150.f, 170.f, 230.f, 520.f, 14.f};
      uint8_t bt = sunType == 5 ? BT_REDGIANT : (sunType == 6 ? BT_WHITEDWARF : BT_STAR);
      spawnBody(bt, rad[sunType] * 9.f + 900.f, rad[sunType], sunDir);
      for (auto &o : objs) if (o.kind == K_BODY && isStarBody(o.bodyType)) {
        o.col = sunCol;
        o.uses = (sunType <= 1 && ((h >> 9) % 100) < 40) ? 1 : 0;   // scoopable: cool and rare
        static const char *sn[7] = {"RED DWARF", "ORANGE STAR", "YELLOW STAR", "WHITE STAR", "BLUE GIANT", "RED GIANT", "WHITE DWARF"};
        snprintf(o.name, sizeof(o.name), "%s", sn[sunType]);
      }
    }
  }
  // a giant to scoop, or a world; with no dock there is always a giant
  bool giant = !station || rf(0, 1) < 0.6f;
  V3 side = norm(shipB.f * rf(0.3f, 0.8f) + shipB.r * (rf(0, 1) < 0.5f ? -1.f : 1.f) + shipB.u * rf(-0.4f, 0.4f));
  if (giant) spawnBody(BT_GIANT, rf(520, 820), rf(170, 290), side);
  else spawnBody(BT_ROCKY, rf(600, 900), rf(120, 220), side);
  V3 stationDir{0, 0, 0};
  if (station) {
    stationDir = norm(shipB.f + shipB.r * rf(-0.3f, 0.3f) + shipB.u * rf(-0.15f, 0.15f));
    V3 sp = shipPos + stationDir * 190.f;
    placeStation(sp, norm(shipPos - sp + shipB.r * 40.f));
    // Deseret keeps a facility beside many of the other companies' stations
    uint8_t mainLook = stationLookFor(hereName);
    if (((mainLook >> 3) & 7) != BR_DESERET && (placeHash(hereName) >> 17) % 100 < 35) {
      // behind its neighbour, past where the gates fan out, so its dock never sits in a lane's approach
      V3 fp = sp + stationDir * 110.f + norm(cross(stationDir, shipB.u)) * 70.f + shipB.u * 20.f;
      placeStation(fp, norm(shipPos - fp), makeLook(SS_OUTPOST, BR_DESERET));
      for (int i = MAX_OBJ - 1; i >= 0; i--) if (objs[i].kind == K_STATION && objs[i].uses == BR_DESERET && objs[i].bodyType == SS_OUTPOST) { snprintf(objs[i].name, sizeof(objs[i].name), "DESERET FACILITY"); break; }
    }
  }
  placeDestGates(ri(3, 5), stationDir);
  if (rf(0, 1) < 0.45f) {
    V3 c = shipPos + randDir() * 60.f + shipB.f * 200.f;
    int n = ri(5, 8);
    for (int i = 0; i < n; i++) spawnRock(c + randDir() * rf(8, 45), rf(2.5f, 8.f));
  }
  spawnTimer = rf(4, 8);
}

static const sm::Landmark *landmarkNamed(const char *name) {
  if (!name || !name[0]) return nullptr;
  for (int i = 0; i < sm::landmarkCount(); i++) { const sm::Landmark *lm = sm::landmarkAt(i); if (lm && sm::sameName(lm->name, name)) return lm; }
  return nullptr;
}

static void spawnLandmarkObj(const sm::Landmark *lm, V3 p) {
  Obj *o = newObj(K_LANDMARK);
  if (!o || !lm) return;
  o->p = p; o->radius = 34.f; o->lmId = (int)lm->id;
  o->o = Basis::facing(norm(shipPos - p), V3{0, 1, 0});
  o->col = lm->handmade ? rgb(240, 200, 110) : rgb(170, 200, 255);
  asciiCopy(o->name, sizeof(o->name), lm->name); upcase(o->name);
}

static const sm::Landmark *pickLandmark(int band, bool preferUncharted) {
  const sm::Landmark *fallback = nullptr;
  int n = sm::landmarkCount(), start = n ? (int)(rnd() % (uint32_t)n) : 0;
  for (int k = 0; k < n; k++) {
    const sm::Landmark *lm = sm::landmarkAt((start + k) % n);
    if (!lm || lm->band > band || lm->band + 1 < band) continue;
    if (preferUncharted && sm::landmarkDiscovered(lm->id)) { if (!fallback) fallback = lm; continue; }
    return lm;
  }
  return fallback;
}

static void makeLayerScene() {
  clearWorld();
  sm::contractSetBand((uint8_t)layer);
  target = -1; navObj = -1; dockTarget = -1;
  sm::Trip &tr = sm::trip();
  // The deep cove is persistent: same light, same bodies, same places, every life.
  uint32_t keep = rngState;
  if (layer == 4) rngState = 0xC0FEu + tr.legs * 977u;
  layerLight = norm(V3{rf(-1, 1), rf(0.2f, 1), rf(-1, 1)});
  resetDust();
  if (layer >= 2 && rf(0, 1) < 0.35f)
    spawnBody(layer >= 3 && rf(0, 1) < 0.6f ? BT_HOLE : BT_WRONGSTAR, rf(260, 420), layer >= 3 ? rf(10, 16) : rf(40, 70),
              norm(shipB.f + shipB.r * rf(-0.8f, 0.8f) + shipB.u * rf(-0.4f, 0.4f)));
  // uncharted landmarks at the edge of the path — deeper means more of them
  float lmChance = layer == 1 ? 0.12f : layer == 2 ? 0.3f : layer == 3 ? 0.45f : 0.65f;
  if (rf(0, 1) < lmChance)
    if (const sm::Landmark *lm = pickLandmark(layer, layer < 4))
      {
        spawnLandmarkObj(lm, aheadPoint(rf(160, 230), (rf(0, 1) < 0.5f ? 1.f : -1.f) * rf(70, 120), rf(-40, 40)));
        sm::Trip &tp = sm::trip();
        if (tp.active && !tp.fixedPoint && !tp.via[0]) asciiCopy(tp.via, sizeof(tp.via), lm->name);   // you passed it
      }
  if (layer == 4) rngState = keep ^ rnd();
  if (sm::trip().active && sm::trip().meeting && layer == 1) {
    Obj *an = newObj(K_ANOMALY);
    if (an) { an->p = aheadPoint(150.f, 40.f, 15.f); an->radius = 9.f; an->col = rgb(230, 60, 230); snprintf(an->name, sizeof(an->name), "EXPERIMENTAL WAKE"); }
    setBanner("EXPERIMENTAL GATE TECH: A COMMISSIONED PORTAL IS FOLDING TWO SKIES TOGETHER", 3600);
  } else
  if (layer == 1 && rf(0, 1) < 0.12f) {   // a Freehold outpost, clinging to the shallows
    V3 op = aheadPoint(rf(170, 230), (rf(0, 1) < 0.5f ? 1.f : -1.f) * rf(60, 110), rf(-30, 30));
    placeStation(op, norm(shipPos - op), makeLook(SS_OUTPOST, BR_FREEHOLD));
    for (auto &o : objs) if (o.kind == K_STATION) snprintf(o.name, sizeof(o.name), "DEEP OUTPOST");
  }
  spawnTimer = rf(3, 6);
  if (layer >= 3 && countKind(K_LANDMARK) == 0 && alienCooldown <= 0.f && !alien.active && rf(0, 1) < 0.07f)
    alienPending = rf(6.f, 14.f);
  else alienPending = -1.f;
}

// ---- contacts ----
static void spawnContact() {
  uint8_t w[sm::ENC_COUNT];
  sm::encounterWeights((uint8_t)layer, w);
  int sum = 0; for (int i = 0; i < sm::ENC_COUNT; i++) sum += w[i];
  bool ghost = layer == 4 && rf(0, 1) < 0.4f;
  if (sum <= 0 && !ghost) return;
  int kind = sm::ENC_SECURITY;
  if (!ghost) {
    int pick = (int)(rnd() % (uint32_t)sum), acc = 0;
    for (int i = 0; i < sm::ENC_COUNT; i++) { acc += w[i]; if (pick < acc) { kind = i; break; } }
  }
  if (inMeeting && rf(0, 1) < 0.3f) kind = sm::ENC_PIRATE;   // the shared sky draws raiders: something to fight together
  if (flyingMaltese() && layer == 0 && !ghost) {   // a luxury hull draws eyes, more so with a full trailer
    const sm::Pilot &pp = sm::sheet();
    if (rf(0, 1) < 0.12f + (pp.holdUsed * 2 > pp.holdCap ? 0.1f : 0.f)) kind = sm::ENC_PIRATE;
  }
  V3 dir = norm(shipB.f * rf(0.4f, 1.f) + shipB.r * rf(-1, 1) + shipB.u * rf(-0.5f, 0.5f));
  V3 p = shipPos + dir * rf(230, 320);
  if (kind == sm::ENC_STATION) {
    if (layer > 0 || countKind(K_STATION) > 0) return;
    placeStation(p, norm(shipPos - p));
    setBanner("AN OUTPOST BEACON RESOLVES OUT OF THE DARK", 2400);
    return;
  }
  if (kind == sm::ENC_LANDMARK_ROCK) { spawnRock(p, rf(4, 9)); return; }
  if (kind == sm::ENC_LANDMARK_GIANT) return;
  Obj *o = newObj(K_SHIP);
  if (!o) return;
  o->enc = (uint8_t)kind; o->p = p; o->radius = 3.2f;
  const sm::EncounterFlavor &fl = sm::encounterFlavor((sm::EncounterClass)kind, (uint8_t)layer);
  asciiCopy(o->name, sizeof(o->name), fl.name);
  V3 across = norm(cross(dir, shipB.u) * (rf(0, 1) < 0.5f ? 1.f : -1.f) - dir * 0.3f);
  o->v = across * rf(6, 11);
  switch (kind) {
    case sm::ENC_TRAVELER: o->mesh = M_SHUTTLE; o->col = rgb(200, 190, 160); break;
    case sm::ENC_MERCHANT: o->mesh = M_COBRA; o->col = rgb(140, 175, 165); o->radius = 4.2f; break;
    case sm::ENC_SECURITY: o->mesh = M_VIPER; o->col = rgb(120, 160, 230); break;
    case sm::ENC_PIRATE: o->mesh = M_SIDEWINDER; o->col = rgb(190, 90, 70); o->hostile = true; break;
    case sm::ENC_SUBPIRATE: o->mesh = M_KRAIT; o->col = rgb(150, 90, 200); o->hostile = true; break;
    case sm::ENC_HOSTILE: o->mesh = 255; o->col = rgb(230, 70, 220); o->hostile = true; o->radius = 4.5f; break;
    case sm::ENC_ANOMALY: o->kind = K_ANOMALY; o->mesh = 255; o->col = hsv(layerHue(layer) + 120, 0.6f, 1.f); o->v = V3{0, 0, 0}; o->radius = 6.f; break;
    case sm::ENC_ESCAPE_POD: o->kind = K_POD; o->mesh = M_POD; o->col = rgb(220, 210, 130); o->v = o->v * 0.15f; o->radius = 1.4f; break;
    case sm::ENC_WRECK: o->kind = K_WRECK; o->mesh = M_COBRA; o->col = rgb(90, 90, 100); o->v = o->v * 0.1f; o->radius = 4.2f; o->spin = rf(-0.3f, 0.3f); break;
    case sm::ENC_ARTIFACT: o->kind = K_ARTIFACT; o->mesh = M_TETRA; o->col = rgb(200, 150, 255); o->v = V3{0, 0, 0}; o->radius = 2.f; o->spin = 0.8f; break;
    default: break;
  }
  if (ghost) {
    // the ghost fleet: the deep cove's quiet owners
    o->ghost = true; o->mesh = M_GHOST; o->col = rgb(70, 80, 90); o->radius = 5.f;
    o->v = o->v * 0.6f;
    snprintf(o->name, sizeof(o->name), "GHOST FLEET");
  }
  if (o->kind == K_SHIP) o->o = Basis::facing(norm(o->v), V3{0, 1, 0});
  if (o->kind == K_SHIP && layer == 0 && (kind == sm::ENC_MERCHANT || kind == sm::ENC_TRAVELER) && rf(0, 1) < 0.3f) {
    o->uses = 1 + BR_DESERET; snprintf(o->name, sizeof(o->name), "DESERET %s", kind == sm::ENC_MERCHANT ? "FREIGHTER" : "SETTLER");
  }
  if (o->enc == sm::ENC_SECURITY && !ghost && sm::sheet().heat[sm::HEAT_SECURITY] > 50) o->hostile = true;
  if (layer == 0 && (kind == sm::ENC_PIRATE || kind == sm::ENC_SECURITY || kind == sm::ENC_MERCHANT)) {
    char b[112]; snprintf(b, sizeof(b), "CONTACT: %s", o->name); noteBanner(b, 1600);
  }
}

// ============================================================
//  targeting + context verbs
// ============================================================
enum VerbId : uint8_t { VB_NONE = 0, VB_HAIL, VB_ATTACK, VB_DOCK, VB_MINE, VB_SCOOP, VB_SALVAGE, VB_RESCUE, VB_READ, VB_SCAN, VB_CHART, VB_AUTO, VB_ORBIT };
struct Chip { uint8_t id; const char *label; uint16_t col; bool enabled; char note[16]; int x, y, w, h; };
static Chip chips[3];
static int chipN = 0;

// What the target offers, given what it is, what the ship can do, and the moment.
static int verbsFor(const Obj &o, Chip *out) {
  int n = 0;
  float sd = surfaceDist(o);
  const sm::Pilot &p = sm::sheet();
  auto add = [&](uint8_t id, const char *label, uint16_t col, float range, const char *blocked) {
    if (n >= 3) return;
    Chip &c = out[n++];
    memset(&c, 0, sizeof(c));
    c.id = id; c.label = label; c.col = col; c.enabled = true;
    if (blocked) { c.enabled = false; snprintf(c.note, sizeof(c.note), "%s", blocked); }
    else if (range > 0 && sd > range) { c.enabled = false; snprintf(c.note, sizeof(c.note), "%dm", (int)(sd * 10)); }
  };
  const uint16_t G = rgb(60, 200, 110), R = rgb(230, 70, 70), B = rgb(70, 140, 255), A = rgb(230, 170, 60),
                 C = rgb(80, 200, 230), V = rgb(180, 120, 255), Y = rgb(240, 205, 90);
  bool holdFull = p.holdUsed >= p.holdCap;
  float reach = 1.f + 0.25f * sm::capTier(sm::CAP_SCANNERS);   // scanners: hail, scan, read and chart from further out
  float gun = 1.f + 0.1f * sm::capTier(sm::CAP_WEAPONS);
  float cut = 1.f + 0.12f * sm::capTier(sm::CAP_MINING);
  switch (o.kind) {
    case K_SHIP:
      if (o.remote) { add(VB_HAIL, "HAIL", G, 600, nullptr); add(VB_ATTACK, "TAG", R, 160 * gun, nullptr); break; }   // the other pilot
      add(VB_HAIL, o.enc == sm::ENC_HOSTILE ? "SIGNAL" : "HAIL", G, 160 * reach, o.done ? "SILENT" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90 * gun, nullptr);
      break;
    case K_ANOMALY:
      add(VB_SCAN, "SCAN", V, 140 * reach, o.done ? "READ" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90 * gun, nullptr);
      break;
    case K_STATION: add(VB_DOCK, "DOCK", B, 0, nullptr); break;
    case K_ROCK: add(VB_MINE, "MINE", A, 40 * cut, o.uses == 0 ? "SPENT" : (holdFull ? "HOLD FULL" : nullptr)); break;
    case K_BODY:
      if (isStarBody(o.bodyType)) {
        if (o.uses == 1) add(VB_SCOOP, "SCOOP", C, o.radius * 0.5f, p.fuel >= p.fuelCap ? "TANK FULL" : (o.timer > 0 ? "SETTLING" : nullptr));
        else add(VB_SCOOP, "SCOOP", C, 0, "TOO HOT");
        break;
      }
      if (o.bodyType == BT_GIANT) {
        add(VB_SCOOP, "SCOOP", C, o.radius * 0.45f, p.fuel >= p.fuelCap ? "TANK FULL" : (o.timer > 0 ? "SETTLING" : nullptr));
        if (flyingMaltese()) add(VB_ORBIT, "ORBIT", B, o.radius * 1.2f, orbitObj >= 0 ? "HOLDING" : nullptr);
      }
      break;
    case K_POD: add(VB_RESCUE, "RESCUE", G, 40 * reach, nullptr); break;
    case K_WRECK:
      add(VB_SALVAGE, "SALVAGE", A, 40 * cut, o.done ? "STRIPPED" : (holdFull ? "HOLD FULL" : nullptr));
      add(VB_ATTACK, "ATTACK", R, 90 * gun, nullptr);
      break;
    case K_ARTIFACT:
      add(VB_READ, "READ", V, 55 * reach, o.done ? "READ" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90 * gun, nullptr);
      break;
    case K_LANDMARK: add(VB_CHART, "CHART", Y, 170 * reach, nullptr); break;
    case K_GATE: case K_PORTAL:
      if (flyingMaltese()) add(VB_AUTO, "AUTO", Y, 600, nullptr);   // the Maltese threads chains for you
      break;
    default: break;
  }
  return n;
}

static bool targetable(const Obj &o) { return o.kind != K_NONE && o.kind != K_DOCKGATE; }

// ============================================================
//  the short theater: fire, cut, beam, hail — then the sheet resolves
// ============================================================
enum Theater : uint8_t { TH_NONE = 0, TH_COMBAT, TH_BEAM, TH_COMM };
static uint8_t theater = TH_NONE;
static uint8_t theaterVerb = VB_NONE;
static int theaterObj = -1;
static float theaterT = 0, theaterBeat = 0;
static uint8_t volleys = 0, maxVolleys = 0;
static bool ambushed = false;

static void ghostFleetHail(Obj &o) {
  // They trade in fixed points. Surface money means little here; a hull of theirs means everything.
  int charted = 0;
  for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && sm::landmarkDiscovered(sm::landmarkAt(i)->id)) charted++;
  sm::Pilot &p = sm::sheet();
  if (sm::rankOf(sm::CR_DEPTHRUNNER) < 3 || charted < 3) {
    setBanner("GHOST FLEET: they do not answer pilots who still count stars.", 2800);
    sm::grantXp(sm::CR_DEPTHRUNNER, 4);
  } else {
    int scans = sm::haulCount("fixed point scan");
    if (scans <= 0) setBanner("GHOST FLEET: bring us what you have charted. The scans, not the stories.", 2800);
    else {
      sm::haulTake("fixed point scan", (uint16_t)scans);
      int pay = scans * 180;
      sm::addCredits(pay);
      int8_t sold = sm::flagGet("gf_scans");
      sold = (int8_t)(sold + scans > 120 ? 120 : sold + scans);
      sm::flagSet("gf_scans", sold, true);
      char b[112];
      if (sold >= 5 && !p.ships[sm::SHIP_GHOST].owned) {
        sm::shipGrant(sm::SHIP_GHOST);
        snprintf(b, sizeof(b), "GHOST FLEET: you see what we see. A hull waits for you in any hangar. | +%dcr", pay);
        sm::journalAdd("The Ghost Fleet gave me one of theirs. It waits in the hangars, quiet.");
        hx::swell(0.8f, 0.5f, 1.2f);
      } else snprintf(b, sizeof(b), "GHOST FLEET buys %d scan%s (%d of 5 they want). | +%dcr", scans, scans == 1 ? "" : "s", sold, pay);
      setBanner(b, 3400);
    }
  }
  o.done = true;
  saveAll();
}

static void finishTheater() {
  int oi = theaterObj;
  uint8_t verb = theaterVerb;
  theater = TH_NONE; theaterVerb = VB_NONE; theaterObj = -1;
  if (oi < 0 || objs[oi].kind == K_NONE) return;
  Obj &o = objs[oi];
  if (o.remote) {   // tag: no resolve, no damage
    char b[72];
    if (tagHits > 0) { net::sendTag((uint8_t)tagHits); tagsGiven += tagHits; itMe = false; snprintf(b, sizeof(b), "TAGGED %s x%d - THEY'RE IT", o.name, tagHits); hx::swell(0.5f, 0.1f, 0.3f); }
    else snprintf(b, sizeof(b), "MISSED - %s IS SLIPPERY", o.name);
    setBanner(b, 1800); tagHits = 0;
    return;
  }

  if (verb == VB_CHART) {
    const sm::Landmark *lm = nullptr;
    for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && (int)sm::landmarkAt(i)->id == o.lmId) lm = sm::landmarkAt(i);
    if (!lm) return;
    int life = sm::landmarkChartedLife(lm->id);
    bool first = life == 0;
    bool otherLife = life != 0 && life != (int)(1 + sm::sheet().lives % 120);
    sm::discoverLandmark(lm->id);
    sm::atlasFixedPoint(lm->name, lm->band);
    if (sm::haulAdd("fixed point scan", 1, true)) noteBanner("+1 FIXED POINT SCAN - SOMEONE IN THE DEEP WILL WANT IT", 2000);
    if (first) { char jb[72]; snprintf(jb, sizeof(jb), "Charted %s. It was where the stories said.", lm->name); sm::journalAdd(jb); }
    sm::grantXp(sm::CR_DEPTHRUNNER, first ? (uint16_t)(18 + lm->band * 6) : 4);
    if (otherLife) { showRecognition(lm); char jb[72]; snprintf(jb, sizeof(jb), "%s again. Another sky, the same place.", lm->name); sm::journalAdd(jb); }
    else if (first) {
      int pay = chartValue(lm->band);
      sm::addCredits(pay);
      char b[112]; snprintf(b, sizeof(b), "CHARTED: %s | +%dcr. SOMEONE WILL PAY FOR THIS.", o.name, pay);
      setBanner(b, 3000);
      noteBanner(sm::landmarkLine(*lm, sm::urand()), 3000);
    } else setBanner("CHARTED. IT HAS NOT MOVED.", 1800);
    hx::swell(0.6f, 0.25f, 0.6f);
    saveAll();
    return;
  }
  if (verb == VB_SCOOP && isStarBody(o.bodyType)) {
    uint16_t before = sm::sheet().fuel;
    sm::setFuel((uint16_t)(before + 22 + sm::capTier(sm::CAP_FUELSYS) * 6));
    sm::grantXp(sm::CR_PROSPECTOR, 6);
    char b[112]; snprintf(b, sizeof(b), "SKIMMED THE CORONA | +%dfuel", (int)sm::sheet().fuel - (int)before);
    setBanner(b, 2200);
    o.timer = 6.f;
    hx::swell(0.6f, 0.1f, 0.4f);
    return;
  }
  if (verb == VB_SCOOP) {
    uint16_t before = sm::sheet().fuel;
    sm::ResolveIn in{sm::VERB_HAIL, sm::ENC_LANDMARK_GIANT, (uint8_t)layer, 1};
    sm::ResolveOut out = sm::resolve(in);
    char b[112]; snprintf(b, sizeof(b), "%s | +%dfuel", out.blurb ? out.blurb : "scooped", (int)sm::sheet().fuel - (int)before);
    setBanner(b, 2200);
    o.timer = 12.f;
    hx::swell(0.5f, 0.1f, 0.4f);
    return;
  }
  if (verb == VB_HAIL && o.ghost) { ghostFleetHail(o); return; }

  sm::EncounterClass who = (sm::EncounterClass)o.enc;
  bool attack = verb == VB_ATTACK;
  sm::Pilot &p = sm::sheet();
  int32_t cr0 = p.credits; int hull0 = p.hull, fuel0 = p.fuel; uint16_t hold0 = p.holdUsed;
  uint8_t threat = o.ghost ? 10 : sm::encounterFlavor(who, (uint8_t)layer).threat;
  sm::ResolveIn in{attack ? sm::VERB_ATTACK : sm::VERB_HAIL, who, (uint8_t)layer, threat ? threat : (uint8_t)1};
  sm::ResolveOut out = sm::resolve(in);
  if (checkDestroy()) return;
  if (out.rumorName[0]) {
    bool lmRumor = sm::landmarkFind(out.rumorName) != nullptr;
    sm::atlasRumor(out.rumorName, out.rumorDepth, lmRumor);
    if (layer == 0) {   // the lane opens where you are
      Obj *g = spawnGate(aheadPoint(rf(110, 140), rf(-50, 50), rf(-18, 18)), -shipB.f, out.rumorName, out.rumorDepth,
                         (uint8_t)(GF_DEST | GF_RUMOR | (lmRumor ? GF_FIXED : 0)), 9.f);
      if (g) g->o = Basis::facing(norm(shipPos - g->p), V3{0, 1, 0});
      char nb[112]; snprintf(nb, sizeof(nb), "A NEW GATE: %s", g ? g->name : out.rumorName);
      noteBanner(nb, 2000);
    }
  }

  char tail[48] = ""; size_t k = 0;
  int dc = (int)(p.credits - cr0), dh = (int)p.hull - hull0, df = (int)p.fuel - fuel0, dhold = (int)p.holdUsed - (int)hold0;
  if (dc) k += snprintf(tail + k, sizeof(tail) - k, " %+dcr", dc);
  if (dh && k < sizeof(tail)) k += snprintf(tail + k, sizeof(tail) - k, " %+dhull", dh);
  if (df && k < sizeof(tail)) k += snprintf(tail + k, sizeof(tail) - k, " %+dfuel", df);
  if (who == sm::ENC_ANOMALY && !attack && layer > 0 && sm::haulAdd("anomaly scan", 1, true) && k < sizeof(tail))
    k += snprintf(tail + k, sizeof(tail) - k, " +1 scan");   // deep outposts pay for these
  if (dhold > 0 && k < sizeof(tail)) snprintf(tail + k, sizeof(tail) - k, " +%dhold", dhold);
  char b[112]; snprintf(b, sizeof(b), "%s%s%s", out.blurb ? out.blurb : "...", tail[0] ? " |" : "", tail);
  setBanner(b, 3000);

  if (attack && out.destroyedOther) {
    if (inMeeting && o.net) { net::sendKill(o.net); netEcho = true; }   // their screen loses it too, and it counts for their work
    addBoom(o.p, o.radius * 3.f, rgb(255, 170, 60));
    hx::boom(1.f);
    if (target == oi) target = -1;
    killObj(oi);
    netEcho = false;
  } else {
    o.done = true; o.engaged = false; o.timer = 20.f;
    // a fight that doesn't end in fire usually ends in someone leaving
    if (o.kind == K_SHIP && o.hostile && rf(0, 1) < 0.7f) { o.hostile = false; o.v = norm(o.p - shipPos) * 16.f; }
    if (o.kind == K_POD && !attack) { if (target == oi) target = -1; killObj(oi); }
    else if (o.kind == K_ROCK && verb == VB_MINE && (sm::capTier(sm::CAP_MINING) >= 3 || flyingHoney())) {
      // high-mark cutters (and the HoneyBee at any mark): the rock goes, the pieces come home
      bool hb = flyingHoney();
      addBoom(o.p, o.radius * (hb ? 3.2f : 2.f), rgb(255, 210, 140));
      if (hb) { addBoom(o.p, o.radius * 1.6f, rgb(200, 230, 140)); crossFlash = 0.25f; }
      addFrags(o.p, 6 + sm::capTier(sm::CAP_MINING) + (hb ? 8 : 0), o.col);
      int extra = sm::capTier(sm::CAP_MINING) + (hb ? 3 : 0);
      if (sm::haulAdd("ore", (uint16_t)extra, true)) { char nb[48]; snprintf(nb, sizeof(nb), "TRACTOR: +%d ORE", extra); noteBanner(nb, 1400); }
      hx::boom(0.6f);
      if (target == oi) target = -1;
      killObj(oi);
    }
    else if (o.kind == K_ROCK && o.uses) o.uses--;
    else if (o.kind == K_SHIP && !o.hostile) o.v = norm(o.p - shipPos) * 14.f;
  }
  saveAll();
}

static void beginTheater(int oi, uint8_t verb) {
  if (oi < 0 || theater != TH_NONE) return;
  theaterObj = oi; theaterVerb = verb; theaterT = 0; theaterBeat = 0; volleys = 0;
  Obj &o = objs[oi];
  if (verb == VB_ATTACK && (o.kind == K_SHIP || o.kind == K_ANOMALY)) {
    theater = TH_COMBAT;
    maxVolleys = (uint8_t)clampf(3 + sm::capTier(sm::CAP_WEAPONS) / 2, 3, 6);
    o.engaged = true;
  } else if (verb == VB_HAIL) {
    theater = TH_COMM; maxVolleys = 0;
  } else {
    theater = TH_BEAM;
    maxVolleys = (uint8_t)(verb == VB_MINE ? 4 + sm::capTier(sm::CAP_MINING)
                         : verb == VB_SCOOP ? (5 - sm::capTier(sm::CAP_FUELSYS) / 2 < 2 ? 2 : 5 - sm::capTier(sm::CAP_FUELSYS) / 2) : 5);
    if (maxVolleys > 9) maxVolleys = 9;
  }
}

static void runVerb(uint8_t id) {
  if (target < 0 || theater != TH_NONE) return;
  hx::pop(0.3f, 0.02f);
  if (id == VB_DOCK) {
    dockTarget = objs[target].link;
    setBanner("DOCKING COMPUTER ENGAGED", 1400);
    return;
  }
  if (id == VB_AUTO) { autoNav = true; autoNavObj = target; orbitObj = -1; setBanner("AUTONAV - THREADING THE CHAIN", 1500); return; }
  if (id == VB_ORBIT) { orbitObj = target; autoNav = false; setBanner("ORBIT HELD - SETTLE IN", 1500); return; }
  if (id == VB_HAIL && objs[target].remote) { chatOpen = true; return; }
  if (id == VB_ATTACK && objs[target].remote) tagHits = 0;
  if (id == VB_HAIL) setBanner("OPENING COMM...", 900);
  if (id == VB_ATTACK && cloakT > 0.f && !flyingGhost()) { cloakT = 0.f; cloakCD = fmaxf(15.f, 45.f - 4.f * sm::capTier(sm::CAP_CLOAK)); setBanner("CLOAK DROPS AS THE GUNS FIRE", 1200); }
  beginTheater(target, id);
}

static void theaterTick() {
  if (theater == TH_NONE) return;
  if (theaterObj < 0 || objs[theaterObj].kind == K_NONE) { theater = TH_NONE; return; }
  Obj &o = objs[theaterObj];
  float sx, sy, z;
  if (!project(o.p, sx, sy, z)) { sx = 160; sy = -20; }
  theaterT += dt; theaterBeat += dt;
  if (theater == TH_COMM) { if (theaterT > 0.9f) finishTheater(); return; }
  if (theater == TH_BEAM) {
    if (theaterVerb == VB_SCOOP) hx::hum(hx::HUM_BEAM, 0.35f, 3.f, 0.25f);
    else if (theaterVerb == VB_CHART || theaterVerb == VB_READ || theaterVerb == VB_SCAN) hx::hum(hx::HUM_BEAM, 0.22f, 9.f, 0.1f);
    else hx::hum(hx::HUM_BEAM, 0.38f, 21.f, 0.6f);
    if (theaterBeat > 0.24f) {
      theaterBeat = 0; volleys++;
      if (theaterVerb == VB_MINE || theaterVerb == VB_SALVAGE) {
        uint8_t mt = sm::capTier(sm::CAP_MINING);
        addBolt(160, H - 6, sx + rf(-4, 4), sy + rf(-4, 4), theaterVerb == VB_MINE ? rgb(170, 255, 110) : rgb(255, 190, 110), mt >= 2 ? 2 : 1);
        if (theaterVerb == VB_MINE && mt >= 2) addSparks(sx, sy, 2 + mt, mt >= 3 ? rgb(255, 240, 160) : rgb(255, 200, 120), 50.f + mt * 15.f);
        hx::pop(0.25f + mt * 0.03f, 0.02f);
      }
      if (volleys >= maxVolleys) finishTheater();
    }
    return;
  }
  if (theaterBeat > 0.3f) {   // combat
    theaterBeat = 0; volleys++;
    if (!ambushed || volleys > 1) {
      // the guns, by mark: bolts, heavier bolts, lances, missiles, then something dazzling
      uint8_t w = sm::capTier(sm::CAP_WEAPONS);
      if (flyingFalcor()) w = (uint8_t)(w + 2);   // the Falcor dazzles at every mark
      bool prism = w >= 6;
      uint16_t c1 = prism ? hsv(tNow * 220.f, 0.6f, 1.f) : w >= 3 ? rgb(120, 245, 255) : rgb(130, 255, 190);
      if (flyingFalcor()) c1 = hsv(180.f + rf(-25.f, 45.f), 0.5f, 1.f);
      if (flyingGhost()) c1 = (volleys & 1) ? rgb(176, 96, 255) : rgb(70, 235, 215);   // purple and teal
      float tx = sx + rf(-5, 5), ty = sy + rf(-5, 5);
      if (flyingFalcor()) { addBolt(48, H - 30, tx, ty, hsv(170.f + rf(0, 60), 0.45f, 1.f), 2, 8); addBolt(272, H - 30, tx, ty, hsv(170.f + rf(0, 60), 0.45f, 1.f), 2, 8); }
      if (w <= 1) { addBolt(120, H - 4, tx, ty, c1); addBolt(200, H - 4, tx, ty, c1); }
      else if (w == 2) {
        addBolt(110, H - 4, tx, ty, c1, 2); addBolt(210, H - 4, tx, ty, c1, 2);
        addSparks(110, H - 6, 3, rgb(255, 255, 200), 40); addSparks(210, H - 6, 3, rgb(255, 255, 200), 40);
      } else {
        addBolt(104, H - 4, tx, ty, c1, prism ? 4 : 3, 9); addBolt(216, H - 4, tx, ty, c1, prism ? 4 : 3, 9);
        addSparks(tx, ty, prism ? 10 : 5, prism ? hsv(tNow * 300.f, 0.4f, 1.f) : rgb(200, 255, 255), 70);
        if (w >= 4) for (int m = 0; m < w - 2 && m < 5; m++)
          addMissile(m & 1 ? 230.f : 90.f, (float)(H - 8), tx + rf(-6, 6), ty + rf(-6, 6),
                     flyingGhost() ? ((m & 1) ? rgb(176, 96, 255) : rgb(70, 235, 215)) : flyingFalcor() ? hsv(175.f + m * 22.f, 0.4f, 1.f)
                     : prism ? hsv(m * 60.f + tNow * 200.f, 0.5f, 1.f) : rgb(255, 210, 120), 0.35f + m * 0.05f);
      }
      hx::pop(clampf(0.45f + w * 0.06f, 0.4f, 0.9f), 0.03f + w * 0.004f);
    }
    if (theaterObj >= 0 && objs[theaterObj].remote) {   // tag: a hit only if you're on them
      V3 to = objs[theaterObj].p - shipPos;
      float off = acosf(clampf(dot(norm(to), shipB.f), -1.f, 1.f));
      if (off < 0.14f) { tagHits++; addSparks(sx, sy, 10, rgb(255, 120, 230), 120); hx::pop(0.5f, 0.03f); }
    } else
    if ((rnd() % 100) < (uint32_t)(50 + layer * 6)) {
      // incoming: the shields take it, and sometimes turn it away
      uint8_t sh = sm::capTier(sm::CAP_SHIELDS);
      float ix = 160 + rf(-50, 50), iy = H - 30.f;
      if (sh > 0 && rf(0, 1) < clampf(0.1f * sh, 0.f, 0.6f)) {
        addBolt(sx, sy, ix, iy, rgb(255, 90, 70));
        addBolt(ix, iy, ix + rf(-120, 120), iy - rf(60, 140), rgb(255, 160, 120));   // ricochet
        shieldFx = 0.35f; shieldFxX = ix; shieldFxY = iy;
        hx::pop(0.35f, 0.02f);
      } else {
        addBolt(sx, sy, ix, H - 10, rgb(255, 90, 70));
        if (sh > 0) { shieldFx = 0.3f; shieldFxX = ix; shieldFxY = iy; hitFlash = 0.08f; }
        else hitFlash = 0.18f;
        hx::thud(sh > 0 ? 0.4f : 0.55f);
      }
    }
    if (volleys >= maxVolleys) { ambushed = false; finishTheater(); }
  }
}

// ============================================================
//  station
// ============================================================
static constexpr int STATION_ROWS = 6;
// station services: the board, the hangar (S.H.A.W. equipment), the market
static uint8_t stationPage = 0;            // 0 board, 1 hangar, 2 market
struct HangarOffer { uint8_t cap, mark; int price; bool fitted; };
static HangarOffer hangar[3];
static uint8_t hangarN = 0, hangarSel = 0, marketSel = 0;
static uint8_t hangarTab = 0, shipSel = 0;   // hangar: 0 equipment, 1 ships
static bool stationOpen = false;
static int stationChoice = 0;
static int stationIdx = -1;
static float dockAnim = 0;      // docking sequence playing
static float launchAnim = 0;    // being taxied back out
static char stationMoodText[112] = "";
static sm::Opportunity stationOpportunity{};
static bool opportunityTaken = false;
static char courseName[24] = "";   // a destination committed at the board
static uint8_t courseDepth = 0, courseFlags = 0;

static const Obj *dockedStation();
static int refuelCost() {
  const sm::Pilot &p = sm::sheet();
  int fp = sm::fuelPrice() * (layer > 0 ? 3 : 2) / 2;   // fuel hauled down to an outpost costs more
  { const Obj *st = dockedStation(); if (st && st->uses == BR_DESERET) fp = fp * 7 / 10; }   // Deseret undercuts on fuel
  return (p.fuelCap - p.fuel) * fp + (p.hullMax - p.hull) * sm::repairPrice();
}
static int rumorPrice() { return 12; }

static uint8_t equipStep(uint8_t mark) { return mark <= 2 ? 0 : mark <= 4 ? 1 : mark <= 6 ? 2 : 3; }
static const Obj *dockedStation() { return stationIdx >= 0 && objs[stationIdx].kind == K_STATION ? &objs[stationIdx] : nullptr; }

// The market here: who runs the dock, the star, the bodies, the place.
static void setDockEcon() {
  const Obj *st = dockedStation();
  sm::DockEcon e{};
  e.brand = st ? st->uses : 0; e.star = sunType; e.layer = (uint8_t)layer;
  e.outpost = st && st->bodyType == SS_OUTPOST;
  for (auto &o : objs) { if (o.kind == K_BODY && o.bodyType == BT_GIANT) e.giant = true; if (o.kind == K_BODY && o.bodyType == BT_ROCKY) e.rocky = true; if (o.kind == K_ROCK) e.rocks = true; }
  e.hash = placeHash(hereName) ^ (uint32_t)layer * 7919u;
  sm::marketSetDock(e);
}

// S.H.A.W. equipment names, four art steps per capability
static const char *equipName(uint8_t cap, uint8_t mark) {
  static const char *n[9][4] = {
    {"LASER TURRET", "AUTOCANNON", "TWIN AUTOCANNON", "PLASMA CANNON"},
    {"SHIELD GENERATOR", "DEFLECTOR RINGS", "DEFLECTOR ARRAY", "SHIELD MATRIX"},
    {"HARVESTER DRILL", "CORE DRILL", "MULTI-DEBRIS RIG", "ANTI-GRAV CRUSHER"},
    {"RADAR DISH", "SENSOR ARRAY", "SCANNER DOME", "INTEGRATED SUITE"},
    {"CARGO TRAILER", "CARGO HAULER", "MODULAR TRAIN", "MODULAR TRAIN XL"},
    {"STABILIZER FIN", "VECTOR FIN", "MANEUVER WINGLETS", "GIMBAL THRUSTERS"},
    {"META-HULL PLATING", "META-HULL PLATING", "META-HULL LATTICE", "META-HULL LATTICE"},
    {"CLOAK DEVICE", "PHASE CLOAK", "STEALTH PLATING", "STEALTH PLATING XL"},
    {"FUEL TANK", "TWIN TANKS", "CAGED TANKS", "EXTERNAL POD"}};
  return n[cap < 9 ? cap : 0][equipStep(mark)];
}
static const char *equipEffect(uint8_t cap, uint8_t mark, char *buf, size_t n) {
  static const char *layerShort[5] = {"REAL", "SHALLOWS", "ROADS", "BELOW", "THE COVE"};
  switch (cap) {
    case sm::CAP_WEAPONS: snprintf(buf, n, "%s", mark >= 6 ? "PRISMATIC LANCES" : mark >= 4 ? "LANCES + MISSILES" : mark >= 3 ? "LANCE BEAMS" : "HEAVIER BOLTS"); break;
    case sm::CAP_SHIELDS: snprintf(buf, n, "-%d%% DAMAGE", (int)(clampf(0.07f * mark, 0.f, 0.65f) * 100)); break;
    case sm::CAP_MINING: snprintf(buf, n, "%s", mark >= 3 ? "SHATTER + TRACTOR" : "MORE YIELD"); break;
    case sm::CAP_SCANNERS: snprintf(buf, n, "+%d%% REACH", 25 * mark); break;
    case sm::CAP_TRAILER: snprintf(buf, n, "HOLD %d", 20 + 14 * mark + 3 * (sm::sheet().rank[sm::CR_HAULER] / 4)); break;
    case sm::CAP_STABILIZER: snprintf(buf, n, "RATED: %s", layerShort[mark > 4 ? 4 : mark]); break;
    case sm::CAP_BULKHEADS: snprintf(buf, n, "DEPTH RATING +"); break;
    case sm::CAP_CLOAK: snprintf(buf, n, "CLOAK %dS", 8 + 6 * mark); break;
    default: snprintf(buf, n, "TANK %d, PORTALS -%d%%", 100 + 6 * mark, (int)(clampf(0.07f * mark, 0.f, 0.4f) * 100)); break;
  }
  return buf;
}

// Three pieces of equipment (two at an outpost), leaning on what the builder makes.
static void rollHangar() {
  static const uint8_t prefs[5][4] = {
    {sm::CAP_SCANNERS, sm::CAP_STABILIZER, sm::CAP_FUELSYS, sm::CAP_SHIELDS},     // Liminar
    {sm::CAP_WEAPONS, sm::CAP_CLOAK, sm::CAP_SHIELDS, sm::CAP_STABILIZER},        // Portex
    {sm::CAP_TRAILER, sm::CAP_SHIELDS, sm::CAP_WEAPONS, sm::CAP_BULKHEADS},       // MaltaPlex
    {sm::CAP_MINING, sm::CAP_FUELSYS, sm::CAP_BULKHEADS, sm::CAP_TRAILER},        // Deseret
    {sm::CAP_MINING, sm::CAP_TRAILER, sm::CAP_FUELSYS, sm::CAP_WEAPONS}};         // Freehold
  static const float priceMul[5] = {1.15f, 1.f, 1.f, 0.95f, 0.85f};
  const Obj *st = dockedStation();
  uint8_t br = st ? (st->uses < 5 ? st->uses : 4) : 0;
  bool outpost = st && st->bodyType == SS_OUTPOST;
  uint8_t want = outpost ? 2 : 3;
  hangarN = 0; hangarSel = 0;
  for (int tries = 0; hangarN < want && tries < 30; tries++) {
    uint8_t cap = (hangarN < want - 1) ? prefs[br][sm::urand() % 4] : (uint8_t)(sm::urand() % sm::CAP_COUNT);
    bool dup = false; for (int i = 0; i < hangarN; i++) if (hangar[i].cap == cap) dup = true;
    uint8_t mark = (uint8_t)(sm::capTier((sm::CapId)cap) + 1);
    if (dup || mark > 10) continue;
    int price = (int)((80 + mark * 55 + layer * 30) * priceMul[br]);
    hangar[hangarN++] = {cap, mark, price, false};
  }
}

static void hangarBuy() {
  if (hangarSel >= hangarN) return;
  HangarOffer &h = hangar[hangarSel];
  char buf[64];
  if (h.fitted || sm::capTier((sm::CapId)h.cap) >= h.mark) { setBanner("ALREADY FITTED", 1000); return; }
  if (!sm::spendCredits(h.price)) { setBanner("NOT ENOUGH CREDIT FOR THAT", 1300); hx::pop(0.12f, 0.01f); return; }
  sm::earnCap((sm::CapId)h.cap, h.mark);
  h.fitted = true;
  snprintf(buf, sizeof(buf), "%s MK%u FITTED", equipName(h.cap, h.mark), h.mark);
  setBanner(buf, 1800);
  hx::swell(0.55f, 0.15f, 0.4f);
  saveAll();
}

// ships: who sells what (the license Mantis is always yours; the flagship is never for sale)
static bool shipSoldHere(uint8_t t) {
  const Obj *st = dockedStation();
  uint8_t br = st ? st->uses : 255;
  if (layer > 0) return false;   // nobody sells hulls down here
  return (t == sm::SHIP_FALCOR && br == BR_LIMINAR) || (t == sm::SHIP_MALTESE && br == BR_MALTAPLEX) ||
         (t == sm::SHIP_HONEYBEE && br == BR_DESERET);
}
static void shipAction(uint8_t t) {
  sm::Pilot &p = sm::sheet();
  const sm::ShipRecord &r = p.ships[t];
  char buf[64];
  if (t == p.activeShip) { setBanner("YOU ARE FLYING HER", 900); return; }
  if (r.owned && r.lost) {
    if (sm::shipRecover(t)) { snprintf(buf, sizeof(buf), "%s REBUILT FROM THE RECORD", sm::shipSpec(t).name); setBanner(buf, 1800); hx::swell(0.5f, 0.2f, 0.5f); saveAll(); }
    else setBanner("NOT ENOUGH CREDIT FOR THE REBUILD", 1300);
    return;
  }
  if (r.owned) {
    // scans are data and ride with the pilot (they no longer count as hold)
    if (p.holdUsed > 0) { setBanner("EMPTY THE HOLD: COMMODITIES DON'T SURVIVE THE PHASE", 2000); hx::pop(0.12f, 0.01f); return; }
    if (sm::shipSwap(t)) {
      snprintf(buf, sizeof(buf), "TELEPORTED - NOW FLYING THE %s", sm::shipSpec(t).name);
      setBanner(buf, 2000); hx::swell(0.7f, 0.25f, 0.6f); crossFlash = 0.6f;
      rollHangar(); saveAll();
    }
    return;
  }
  if (!shipSoldHere(t)) { setBanner(t == sm::SHIP_GHOST ? "NOT FOR SALE. NOT ANYWHERE." : "NOT SOLD AT THIS DOCK", 1300); return; }
  if (sm::shipBuy(t)) {
    snprintf(buf, sizeof(buf), "THE %s IS YOURS. SWAP IN WHEN YOU'RE READY.", sm::shipSpec(t).name);
    setBanner(buf, 2200); hx::swell(0.6f, 0.2f, 0.5f);
    char jb[72]; snprintf(jb, sizeof(jb), "Bought the %s. %s.", sm::shipSpec(t).name, sm::shipSpec(t).role); sm::journalAdd(jb);
    saveAll();
  } else setBanner("NOT ENOUGH CREDIT FOR HER", 1300);
}

// the market: buy and sell by the unit
static bool leadOwns(const char *hold) {
  const sm::Contract &c = sm::contract();
  const char *owned = c.live ? sm::contractCargo(c.kind) : nullptr;
  return owned && strcmp(owned, hold) == 0;
}
static void marketTrade(bool buy, int qty) {
  sm::Good g = (sm::Good)marketSel;
  if (!sm::marketTrades(g)) { setBanner("THIS DOCK DOES NOT TRADE THAT", 1100); return; }
  const char *hold = sm::goodHold(g);
  sm::Pilot &p = sm::sheet();
  char buf[64];
  if (buy) {
    int each = sm::marketBuy(g);
    int room = p.holdCap - p.holdUsed;
    int can = (int)(p.credits / (each > 0 ? each : 1));
    int n = qty < room ? qty : room; n = n < can ? n : can;
    if (n <= 0) { setBanner(room <= 0 ? "THE HOLD IS FULL" : "NOT ENOUGH CREDIT", 1100); hx::pop(0.12f, 0.01f); return; }
    sm::spendCredits(n * each);
    sm::haulAdd(hold, (uint16_t)n, true);
    snprintf(buf, sizeof(buf), "BOUGHT %d %s  -%dcr", n, sm::goodName(g), n * each);
  } else {
    if (leadOwns(hold)) { setBanner("THAT LOAD BELONGS TO YOUR LEAD", 1300); return; }
    int have = sm::haulCount(hold);
    int n = qty < have ? qty : have;
    if (n <= 0) { setBanner("NONE IN THE HOLD", 1000); return; }
    int each = sm::marketSell(g);
    sm::haulTake(hold, (uint16_t)n);
    sm::addCredits(n * each);
    sm::grantXp(sm::CR_TRADER, (uint16_t)(1 + n * each / 40));
    snprintf(buf, sizeof(buf), "SOLD %d %s  +%dcr", n, sm::goodName(g), n * each);
  }
  setBanner(buf, 1400);
  hx::pop(0.25f, 0.02f);
  saveAll();
}
static int sellableValue(bool doSell) {
  sm::Pilot &p = sm::sheet();
  const char *owned = sm::contract().live ? sm::contractCargo(sm::contract().kind) : nullptr;
  int pay = 0; char names[sm::MAX_HAUL_LINES][sm::NAME_LEN]; int nn = 0;
  for (uint8_t i = 0; i < p.haulN; ++i) {
    if (owned && strncmp(p.haul[i].what, owned, sm::NAME_LEN) == 0) continue;
    int each = sm::marketSellLine(p.haul[i].what, p.haul[i].legal != 0);   // this dock's prices
    if (each <= 0) continue;                                                // (no buyer for deep scans up here)
    pay += p.haul[i].amount * each;
    strncpy(names[nn], p.haul[i].what, sm::NAME_LEN); nn++;
  }
  { const Obj *st = dockedStation(); if (st && st->uses == BR_DESERET) pay = pay * 3 / 4; }   // Deseret skimps on a dumped hold (their market is fair)
  if (doSell) for (int i = 0; i < nn; i++) sm::haulRemove(names[i]);
  return pay;
}

static void boardLanes() {
  sm::AtlasLane lanes[8];
  int n = sm::atlasLanesHere(lanes, 8), m = 0;
  const char *names[8]; uint8_t depths[8], visited[8];
  for (int i = 0; i < n; i++) {
    if (lanes[i].fixed) continue;                       // a landmark is not a delivery address
    int ai = sm::atlasFind(lanes[i].name);
    if (ai >= 0) {
      const sm::AtlasPlace &ap = sm::atlas().place[ai];
      if ((ap.flags & sm::AP_STATION_SET) && !(ap.flags & sm::AP_HAS_STATION)) continue;   // no dock to deliver to
    }
    names[m] = lanes[i].name; depths[m] = lanes[i].depth; visited[m] = lanes[i].visited ? 1 : 0; m++;
  }
  sm::contractSetLanes(names, depths, visited, m);
}

static void openBoard() {
  stationOpen = true; stationChoice = 0;
  stationPage = 0; marketSel = 0; hangarTab = 0; shipSel = sm::sheet().activeShip;
  if (inMeeting) {   // the meeting sky's board: work for two
    sm::contractOffer(sm::CK_BOUNTY);
    sm::Contract &o2 = sm::contractOfferPeek();
    if (o2.kind == sm::CK_BOUNTY) { snprintf(o2.title, sizeof(o2.title), "PIRATE NEST (CO-OP)"); o2.need = 4; o2.pay = 640; }
  }
  { const Obj *st = dockedStation(); sm::contractSetIssuer(st ? st->uses : 0); }
  setDockEcon();
  rollHangar();
  if (layer > 0) sm::contractOfferDeep();   // deep outposts post deep work
  else {
    boardLanes();
    const Obj *st = dockedStation();
    bool faithful = st && st->uses == BR_DESERET && sm::flagGet("deseret") >= 5 && rf(0, 1) < 0.35f;
    if (!(faithful && sm::contractOffer(sm::CK_DEPTHRUN) && sm::contractOfferPeek().kind == sm::CK_DEPTHRUN)) sm::contractOffer();
  }
  opportunityTaken = layer > 0 || !sm::makeOpportunity(stationOpportunity, 0);
  asciiCopy(stationMoodText, sizeof(stationMoodText), sm::stationMood((uint8_t)(sm::urand() % 5), sm::worldPressure(), sm::urand()));
  sm::grantXp(sm::CR_TRADER, 1);
  courseName[0] = 0;
}

static void dockNow(int stationObj) {
  stationIdx = stationObj;
  dockAnim = 1.2f;
  dockTarget = -1; target = -1; theater = TH_NONE;
  hx::swell(0.55f, 0.7f, 0.5f);
}

static void launch() {
  stationOpen = false;
  launchAnim = 0.9f;
  Obj *st = (stationIdx >= 0 && objs[stationIdx].kind == K_STATION) ? &objs[stationIdx] : nullptr;
  if (st) {
    V3 gp = (st->link >= 0 && objs[st->link].kind == K_DOCKGATE) ? objs[st->link].p : st->p + st->o.f * 40.f;
    shipPos = gp + st->o.f * 14.f; prevShipPos = shipPos;
    shipB = Basis::facing(st->o.f, st->o.u);
    for (auto &o : objs) if (o.kind == K_GATE || o.kind == K_DOCKGATE || o.kind == K_PORTAL) o.prevSide = dot(shipPos - o.p, o.o.f);
  }
  // the course you committed to waits right in front of you
  if (courseName[0]) {
    for (int i = 0; i < MAX_OBJ; i++)
      if (objs[i].kind == K_GATE && (objs[i].gflags & GF_DEST) && strcmp(objs[i].name, courseName) == 0) killObj(i);
    Obj *g = spawnGate(aheadPoint(60.f, rf(-8, 8), rf(-5, 5)), -shipB.f, courseName, courseDepth, courseFlags, 9.f);
    target = idxOf(g);
    char b[112]; snprintf(b, sizeof(b), "COURSE: %s - DEPTH %u. FLY THE GATE.", courseName, courseDepth);
    noteBanner(b, 2600);
  }
  hx::swell(0.7f, 0.15f, 0.5f);
  throttleT = 0.5f;
  shipSpeed = 12.f;
  saveAll();
}

// After taking a job: its lane in this system turns gold and is targeted. No new gate.
static void lightJobLane() {
  const sm::Contract &c = sm::contract();
  if (!c.live || !c.dest[0]) return;
  for (int i = 0; i < MAX_OBJ; i++)
    if (objs[i].kind == K_GATE && (objs[i].gflags & GF_DEST) && sm::sameName(objs[i].name, c.dest)) { objs[i].gflags |= GF_JOB; target = i; }
}

static void setCourse(const char *name, uint8_t depth, uint8_t flags) {
  asciiCopy(courseName, sizeof(courseName), name); upcase(courseName);
  courseDepth = depth < 1 ? 1 : depth;
  courseFlags = flags;
}

static void stationCommit() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Pilot &p = sm::sheet();
  char buf[112];
  hx::pop(0.3f, 0.02f);
  switch (stationChoice) {
    case 0: {   // business: stay docked
      int fp = sm::fuelPrice(); if (fp < 1) fp = 1;
      if (p.fuel >= p.fuelCap && p.hull >= p.hullMax) { setBanner("ALREADY TOPPED UP", 1200); break; }
      if (sm::spendCredits(refuelCost())) { sm::setFuel(p.fuelCap); sm::repairHull(p.hullMax); setBanner("REFUELED / REPAIRED", 1600); }
      else {
        int partial = p.credits / fp, need = p.fuelCap - p.fuel;
        if (partial > need) partial = need;
        if (partial > 0) { sm::spendCredits(partial * fp); sm::setFuel((uint16_t)(p.fuel + partial)); setBanner("PARTIAL REFUEL - CREDIT THIN", 1700); }
        else if (p.fuel < 15) { sm::setFuel(15); sm::addHeat(sm::HEAT_HOUSE, 6); setBanner("THE DOCK FRONTS YOU 15 FUEL. THEY WILL REMEMBER.", 2400); }
        else setBanner("TOO BROKE FOR THE DOCK", 1400);
      }
      break;
    }
    case 1:   // work: deliver here, drop the lead, or take the board's job
      if (dueHereNow()) {
        if (sm::contractDeliverHere(hereName)) hx::swell(0.5f, 0.1f, 0.3f);
        else { sm::contractAbandon(); setBanner("THE CARGO IS GONE - THE JOB IS VOID", 1800); hx::pop(0.15f, 0.02f); }
        break;
      }
      if (sm::contract().live) { sm::contractAbandon(); setBanner("LEAD DROPPED", 1400); break; }
      if (sm::contractAccept(off)) {
        snprintf(buf, sizeof(buf), "ACCEPTED: %s", off.title); setBanner(buf, 2000);
        if (off.dest[0]) { launch(); lightJobLane(); return; }
      } else setBanner(off.kind == sm::CK_MARKET ? "CANNOT COVER THE CARGO" : "NO ROOM IN THE HOLD", 1800);
      break;
    case 2: {   // a rumor is a place you can now fly to: buying it sets the course
      if (layer > 0) { setBanner("NOBODY SELLS NAMES DOWN HERE", 1400); break; }
      if (!sm::spendCredits(rumorPrice())) { setBanner("THE RUMOR SELLER WANTS MONEY", 1500); break; }
      char name[24]; asciiCopy(name, sizeof(name), sm::placeName(sm::urand(), 0, false));
      uint8_t d = sm::placeDepth(name);
      sm::rumorAdd(name, d, 14);
      sm::atlasRumor(name, d, false);
      sm::grantXp(sm::CR_TRADER, 4);
      snprintf(buf, sizeof(buf), "RUMOR: %s. %s", name, sm::marketRumor(sm::worldPressure(), sm::urand()));
      setBanner(buf, 3000);
      setCourse(name, d, GF_DEST | GF_RUMOR);
      launch();
      return;
    }
    case 3: {   // business
      int pay = sellableValue(false);
      if (pay > 0) {
        sellableValue(true); sm::addCredits(pay); sm::grantXp(sm::CR_TRADER, (uint16_t)(6 + pay / 25));
        snprintf(buf, sizeof(buf), "HAUL SOLD +%dcr", pay); setBanner(buf, 1900);
      } else { stationPage = 1; setBanner("EQUIPMENT IS IN THE HANGAR", 1200); }
      break;
    }
    case 4:
      if (opportunityTaken) { setBanner("THE BOARD IS EMPTY", 1200); break; }
      if (sm::contract().live) { setBanner("FINISH OR DROP YOUR LEAD FIRST", 1600); break; }
      if (sm::contractFromOpportunity(stationOpportunity)) {
        opportunityTaken = true;
        snprintf(buf, sizeof(buf), "LEAD: %s", stationOpportunity.title); setBanner(buf, 2000);
        const sm::Contract &c = sm::contract();
        if (c.dest[0]) { launch(); lightJobLane(); return; }
      } else setBanner("NO ROOM FOR THAT WORK", 1500);
      break;
    case 5: launch(); return;
  }
  saveAll();
}

// ============================================================
//  trips: threading, crossing, arrival
// ============================================================
static void arriveReal(bool turnedBack) {
  sm::Trip tr = sm::trip();
  sm::tripEnd();
  if (tr.meeting) {   // climbing out into the shared sky: something might have gone haywire
    makeMeetingScene(net::seed());
    float pp[3];
    if (net::peerArrived(pp)) shipPos = V3{pp[0], pp[1], pp[2]} + V3{net::role() == net::ROLE_ANCHOR ? -18.f : 18.f, 0.f, 0.f};
    prevShipPos = shipPos;
    for (auto &o : objs) o.prevSide = dot(shipPos - o.p, o.o.f);
    float me[3] = {shipPos.x, shipPos.y, shipPos.z};
    net::setArrived(me);
    inMeeting = true; itMe = false; tagsGiven = tagsTaken = 0;
    glitchT = 1.4f; crossFlash = 1.f; hx::stutter(0.7f, 5, 0.05f);
    setBanner("SOMETHING MIGHT HAVE GONE HAYWIRE...", 3000);
    char jb[72]; snprintf(jb, sizeof(jb), "Flew a commissioned gate to meet %s.", net::peerName()); sm::journalAdd(jb);
    return;
  }
  char place[24];
  bool station;
  if (turnedBack) { asciiCopy(place, sizeof(place), sm::placeName(sm::urand(), 0, false)); station = rf(0, 1) < 0.6f; }
  else if (tr.fixedPoint) {
    // a fixed point is in the deep; the climb out surfaces somewhere new, in any sky
    asciiCopy(place, sizeof(place), sm::placeName(sm::urand(), 0, false)); station = rf(0, 1) < 0.65f;
  }
  else { asciiCopy(place, sizeof(place), tr.dest); station = tr.unknown ? rf(0, 1) < 0.7f : rf(0, 1) < 0.85f; }
  upcase(place);
  sm::atlasVisit(place, tripOrigin, tr.destDepth, true, tr.fixedPoint ? tr.dest : (tr.via[0] ? tr.via : nullptr));
  if (!turnedBack && sm::contract().live && sm::sameName(place, sm::contract().dest)) station = true;
  { // a place you have been keeps its dock, or its lack of one
    int ai = sm::atlasFind(place);
    if (ai >= 0) {
      sm::AtlasPlace &ap = sm::atlas().place[ai];
      if (ap.flags & sm::AP_STATION_SET) station = (ap.flags & sm::AP_HAS_STATION) != 0;
      else ap.flags |= (uint8_t)(sm::AP_STATION_SET | (station ? sm::AP_HAS_STATION : 0));
    }
  }
  makeRealScene(place, station);
  sm::onResurface();
  char b[112];
  if (!turnedBack) {
    if (!tr.fixedPoint) sm::knownGateAdd(tr.dest, tr.destDepth);
    sm::contractOnGate(tr.dest, 0, tr.unknown != 0);
    // real-space exploring is experience; a deep way through is money
    if (tr.unknown) sm::grantXp(sm::CR_WANDERER, (uint16_t)(10 + tr.destDepth * 4));
    sm::grantXp(sm::CR_DEPTHRUNNER, (uint16_t)(2 + tr.destDepth * 3));
    int pay = routeValue(tr.destDepth, tr.unknown != 0);
    if (pay > 0) {
      sm::addCredits(pay);
      snprintf(b, sizeof(b), "RESURFACED: %s. A WAY THROUGH DEPTH %u IS WORTH MONEY | +%dcr", hereName, tr.destDepth, pay);
    } else snprintf(b, sizeof(b), "RESURFACED: %s - FAR ACROSS THE UNIVERSE", hereName);
  } else snprintf(b, sizeof(b), "YOU SURFACE SOMEWHERE ELSE: %s", hereName);
  setBanner(b, 3400);
  hx::swell(0.5f, 0.3f, 0.8f);
  saveAll();
}

static void crossPortal(Obj &portal) {
  sm::Crossing c = sm::tripCross();
  if (c.refused) {
    setBanner("THE PORTAL WON'T TAKE A DRY SHIP - THE CHAIN TURNS BACK", 2800);
    hx::stutter(0.6f, 4, 0.09f);
    killObj(idxOf(&portal));
    navObj = -1;
    if (layer == 0) sm::tripEnd(); else spawnNextOnPath();
    return;
  }
  // the crossing: the hum peaks, then total silence
  hx::cut(0.7f);
  crossFlash = 1.f;
  layer = c.to;
  if (c.damage > 0) {
    if (damage(c.damage, false)) return;
    setBanner(c.dry ? "DRY CLIMB - THE HULL PAYS FOR IT" : "GLITCH - REALITY SLIPS", 2200);
    hx::stutter(0.85f, 7, 0.07f);
  }
  if (c.arrived) { arriveReal(sm::trip().turnedBack != 0); return; }
  makeLayerScene();
  sm::Trip &tr = sm::trip();
  char b[112];
  if (c.turnPoint) {
    if (tr.fixedPoint) {
      const sm::Landmark *lm = nullptr;
      for (int i = 0; i < sm::landmarkCount(); i++) {
        const sm::Landmark *q = sm::landmarkAt(i);
        char up[24]; asciiCopy(up, sizeof(up), q->name); upcase(up);
        if (strcmp(up, tr.dest) == 0) lm = q;
      }
      if (lm) spawnLandmarkObj(lm, aheadPoint(140.f, 0, 10.f));
      // the hub's gates: every known place that has routed through it
      sm::AtlasLane hub[6];
      int nh = sm::atlasHubLanes(lm ? lm->name : tr.dest, hub, 6);
      for (int k = 0; k < nh; k++) {
        float a = -1.0f + 2.0f * (k + 0.5f) / nh;
        V3 p = aheadPoint(95.f, sinf(a) * 70.f, cosf(a * 2.f) * 18.f - 9.f);
        Obj *hg = spawnGate(p, norm(shipPos - p), hub[k].name, hub[k].depth, GF_DEST | GF_KNOWN, 9.f);
        if (hg) { hg->uses = 1; if (lm) hg->lmId = (int)lm->id; }
      }
      if (nh) snprintf(b, sizeof(b), "%s. %d KNOWN WAY%s RUN THROUGH HERE.", tr.dest, nh, nh == 1 ? "" : "S");
      else snprintf(b, sizeof(b), "%s. THE FIXED POINT IS HERE.", tr.dest);
    } else if (tr.via[0]) {
      // passing a hub: it hangs beside the way, and the chain stays on your destination
      const sm::Landmark *lm = landmarkNamed(tr.via);
      if (lm && countKind(K_LANDMARK) == 0) spawnLandmarkObj(lm, aheadPoint(170.f, (rnd() & 1u) ? 55.f : -55.f, 12.f));
      snprintf(b, sizeof(b), "PASSING %s. THE CHAIN TURNS UP.", tr.via); upcase(b);
    } else snprintf(b, sizeof(b), "%s - DEPTH REACHED. THE CHAIN TURNS UP.", layerName(layer));
    setBanner(b, 2600);
  } else {
    setBanner(layerName(layer), 1600);
    if (layer >= 2 && (rnd() & 1u)) noteBanner(sm::deepWhisper((uint8_t)layer, sm::urand()), 3000);
  }
  if (layer >= 3) sm::grantXp(sm::CR_DEPTHRUNNER, (uint16_t)(4 + layer * 2));
  spawnNextOnPath();
  saveAll();
}

static void threadGate(Obj &g) {
  int gi = idxOf(&g);
  if (g.kind == K_DOCKGATE) {
    if (g.link >= 0 && objs[g.link].kind == K_STATION) dockNow(g.link);
    return;
  }
  if (g.kind == K_PORTAL) { crossPortal(g); return; }
  hx::swell(0.45f, 0.04f, 0.14f);
  if ((g.gflags & GF_DEST) && g.uses == 1 && sm::trip().active && layer > 0) {
    // a hub gate at a landmark: the climb now goes to a place you know, past this hub
    sm::Trip &tr = sm::trip();
    const sm::Landmark *hub = landmarkNamed(tr.dest);
    if (hub) asciiCopy(tr.via, sizeof(tr.via), hub->name);
    asciiCopy(tr.dest, sizeof(tr.dest), g.name);
    tr.fixedPoint = 0; tr.unknown = 0; tr.ascending = 1; tr.step = 1;
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && objs[i].uses == 1) killObj(i);
    if (navObj >= 0) { killObj(navObj); navObj = -1; }
    if (target == gi) target = -1;
    char b[112]; snprintf(b, sizeof(b), "COURSE: %s - THROUGH %s", tr.dest, hub ? hub->name : "THE HUB");
    setBanner(b, 2400);
    spawnNextOnPath();
    return;
  }
  if (g.gflags & GF_DEST) {
    // the choice: this is where we are going
    dockTarget = -1;
    asciiCopy(tripOrigin, sizeof(tripOrigin), hereName);
    if (inMeeting) leaveMeeting(true);   // leaving the shared sky ends the session
    sm::tripBegin(g.name, g.depth, (g.gflags & GF_UNKNOWN) != 0, (g.gflags & GF_FIXED) != 0);
    if (g.uses == 2) { sm::trip().meeting = 1; net::setTravelling(); }
    if (g.lmId && !(g.gflags & GF_FIXED))   // a lane that runs past a landmark hub
      for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && (int)sm::landmarkAt(i)->id == g.lmId) asciiCopy(sm::trip().via, sizeof(sm::trip().via), sm::landmarkAt(i)->name);
    char b[112]; snprintf(b, sizeof(b), "COURSE: %s - DEPTH %u", g.name, g.depth);
    setBanner(b, 2200);
    if (target == gi) target = -1;
    killObj(gi);
    spawnNextOnPath();
    return;
  }
  if (g.gflags & GF_CHAIN) {
    sm::tripGate();
    if (sm::contract().live) sm::contractOnGate("", (uint8_t)layer, false);
    if (target == gi) target = -1;
    killObj(gi);
    spawnNextOnPath();
    char b[32]; snprintf(b, sizeof(b), "GATE %u/3", sm::trip().step);
    setBanner(b, 900);
  }
}

// ============================================================
//  destruction
// ============================================================
static void onDestroyedFlow() {
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed() ^ 0x9E3779B9u;
  sm::contractAbandon();
  sm::tripEnd();
  sm::atlasWipe();
  { uint8_t lost = sm::takeLostShip();
    if (lost != 255) {
      char jb[72];
      snprintf(jb, sizeof(jb), sm::sheet().ships[lost].lost ? "Lost the %s. The teleporter has a record of her." : "Lost the %s. No record. She's gone.", sm::shipSpec(lost).name);
      sm::journalAdd(jb);
    } }
  { char jb[72]; snprintf(jb, sizeof(jb), "Pod launched. Life %lu ends. No names out here.", (unsigned long)sm::sheet().lives); sm::journalAdd(jb); }
  theater = TH_NONE; stationOpen = false; mapOpen = false; statusOpen = false; endingOpen = false; dockAnim = 0; launchAnim = 0;
  queuedN = 0;
  hx::boom(1.f);
  crossFlash = 1.f;
  makeRealScene(nullptr, rf(0, 1) < 0.65f);
  // Beat between skies: pod recovered, still lost — no meta about reseeding.
  lostOpen = true;
  saveAll();
}

// ============================================================
//  input
// ============================================================
static float neutralAx = 0, neutralAy = 0;
static bool neutralValid = false;
static float tiltX = 0, tiltY = 0, tiltRoll = 0;
static bool touchDown = false, dragging = false, sliding = false;
static int touchX0 = 0, touchY0 = 0, touchLX = 0, touchLY = 0;
static uint32_t touchT0 = 0;

static constexpr int ROW_Y0 = 48, ROW_PITCH = 23, ROW_H = 20;
static constexpr int SVC_X = 272, SVC_W = 44;
static int svcCount() { return signalsOn() && layer == 0 ? 4 : 3; }
static int svcH() { return svcCount() == 4 ? 48 : 62; }
static int svcY(int i) { return 6 + i * (svcH() + 4); }

static constexpr int SLIDER_X = W - 22, SLIDER_Y0 = 44, SLIDER_Y1 = 196;

static void captureNeutral() {
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    neutralAx = d.accel.x; neutralAy = d.accel.y; neutralValid = true;
  }
}

static void cycleTarget() {
  float bestScore = 1e9f; int best = -1;
  for (int i = 0; i < MAX_OBJ; i++) {
    if (!targetable(objs[i]) || i == target) continue;
    float sx, sy, z;
    if (!project(objs[i].p, sx, sy, z) || !onScreen(sx, sy)) continue;
    float score = fabsf(sx - 160) + fabsf(sy - 120) + z * 0.2f;
    if (objs[i].kind == K_GATE || objs[i].kind == K_PORTAL) score += 60;
    if (flyingMaltese() && objs[i].kind == K_SHIP && (objs[i].hostile || objs[i].enc == sm::ENC_PIRATE || objs[i].enc == sm::ENC_SUBPIRATE)) score -= 1000;   // deterrent targeting
    if (score < bestScore) { bestScore = score; best = i; }
  }
  target = best;
  if (best >= 0) hx::pop(0.22f, 0.015f);
}

static void mapTap(int x, int y);

static void startNewGame();
static void alienTick();
static void handleTap(int x, int y) {
  if (chatOpen) { chatTap(x, y); return; }
  if (mapOpen) { mapTap(x, y); return; }
  if (statusOpen) { statusOpen = false; hx::pop(0.15f, 0.01f); return; }
  if (bootOpen) {
    bootOpen = false;
    setBanner("FIND A GATE", 2000);
    return;
  }
  if (lostOpen) {
    lostOpen = false;
    setBanner(sm::lossLine(sm::sheet().lives, sm::urand()), 3200);
    return;
  }
  if (endingOpen) { endingOpen = false; setBanner("KEEP FLYING. THE NAMES WILL BE THERE.", 3000); return; }
  if (stationOpen) {
    if (x >= SVC_X - 2) {   // the services column
      for (int i = 0; i < svcCount(); i++) if (y >= svcY(i) && y < svcY(i) + svcH() && stationPage != i) { stationPage = (uint8_t)i; hx::pop(0.18f, 0.015f); }
      return;
    }
    if (stationPage == 3) {
      int row = (y >= 52 && y < 52 + 3 * 28) ? (y - 52) / 28 : -1;
      if (row == 0 && (net::phase() == net::PH_OFF || net::phase() == net::PH_LOST)) {
        if (layer > 0) setBanner("NO SIGNAL THIS DEEP", 1200);
        else if (sm::spendCredits(signalsFee())) { net::commission(myCallsign(), sm::sheet().activeShip); setBanner("PORTAL COMMISSIONED - SEEKING", 1600); hx::swell(0.4f, 0.2f, 0.4f); }
        else setBanner("NOT ENOUGH CREDIT FOR A PORTAL", 1300);
      } else if (row == 1) {
        if (net::phase() != net::PH_READY) { setBanner("THE SKIES HAVEN'T LINED UP YET", 1100); return; }
        // like a rumor: a gate in this sky, magenta, to the place you'll meet
        char nm[24]; asciiCopy(nm, sizeof(nm), sm::placeName(net::seed(), 0, false)); upcase(nm);
        asciiCopy(meetOrigin, sizeof(meetOrigin), hereName);
        setCourse(nm, 1, GF_DEST | GF_KNOWN);
        launch();
        for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && sm::sameName(objs[i].name, nm)) { objs[i].uses = 2; target = i; }
        return;
      } else if (row == 2) { net::cancel(); setBanner("SIGNAL CANCELLED", 1000); }
      return;
    }
    if (stationPage == 0) {
      if (y >= ROW_Y0 && y < ROW_Y0 + STATION_ROWS * ROW_PITCH) {
        int row = (y - ROW_Y0) / ROW_PITCH;
        if (row == stationChoice) stationCommit(); else { stationChoice = row; hx::pop(0.15f, 0.01f); }
      }
    } else if (stationPage == 1) {
      if (y >= 46 && y < 62) { uint8_t t = x < 136 ? 0 : 1; if (t != hangarTab) { hangarTab = t; hx::pop(0.15f, 0.01f); } }
      else if (hangarTab == 1) {
        if (y >= 64 && y < 64 + sm::SHIP_COUNT * 25) { uint8_t t = (uint8_t)((y - 64) / 25); if (t == shipSel) shipAction(t); else { shipSel = t; hx::pop(0.15f, 0.01f); } }
      } else if (y >= 64 && y < 188 && x >= 12) {
        int card = (x - 12) / 84;
        if (card < hangarN) { if (card == hangarSel) hangarBuy(); else { hangarSel = (uint8_t)card; hx::pop(0.15f, 0.01f); } }
      }
    } else {
      if (y >= 58 && y < 58 + sm::G_COUNT * 15) { marketSel = (uint8_t)((y - 58) / 15); hx::pop(0.12f, 0.01f); }
      else if (y >= 180 && y < 202 && x >= 12) {
        int b = (x - 12) / 63;
        if (b == 0) marketTrade(true, 1); else if (b == 1) marketTrade(true, 5);
        else if (b == 2) marketTrade(false, 1); else if (b == 3) marketTrade(false, 999);
      }
    }
    return;
  }
  for (int i = 0; i < chipN; i++) {
    const Chip &c = chips[i];
    if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) {
      if (c.enabled) runVerb(c.id);
      else {
        hx::pop(0.12f, 0.01f);
        size_t n = strlen(c.note);
        setBanner(n && c.note[n - 1] == 'm' ? "OUT OF RANGE - FLY CLOSER" : c.note, 1000);
      }
      return;
    }
  }
  float best = 1e9f; int pick = -1;
  for (int i = 0; i < MAX_OBJ; i++) {
    if (!targetable(objs[i])) continue;
    float sx, sy, z;
    if (!project(objs[i].p, sx, sy, z)) continue;
    float rr = objs[i].radius * FOCAL / z;
    if (objs[i].kind == K_BODY) rr = clampf(rr, 6, 400);
    float dd = sqrtf((sx - x) * (sx - x) + (sy - y) * (sy - y));
    float slack = 16.f + clampf(rr, 0, 60);
    if (dd < slack && dd - rr * 0.5f < best) { best = dd - rr * 0.5f; pick = i; }
  }
  if (pick >= 0 && pick != target) { target = pick; hx::pop(0.25f, 0.015f); }
  else if (pick < 0) target = -1;
}

static void setThrottleFromY(int y) {
  float t = 1.f - (float)(y - SLIDER_Y0) / (SLIDER_Y1 - SLIDER_Y0);
  t = clampf(t, 0, 1);
  if (fabsf(t - 0.5f) < 0.04f) t = 0.5f;   // a soft detent at cruise
  if ((throttleT < 0.5f) != (t < 0.5f) || (throttleT == 0.5f) != (t == 0.5f)) hx::pop(0.15f, 0.01f);
  throttleT = t;
}

static void updateInput() {
  M5.update();
  auto td = M5.Touch.getDetail();
  bool flying = !stationOpen && !endingOpen && !lostOpen && !bootOpen && !statusOpen && !mapOpen && !chatOpen && dockAnim <= 0 && !alienHolds();
  if (td.wasPressed()) {
    touchDown = true; dragging = false; sliding = false;
    touchX0 = touchLX = td.x; touchY0 = touchLY = td.y; touchT0 = millis();
    if (flying && td.x >= SLIDER_X - 6 && td.y >= SLIDER_Y0 - 12 && td.y <= SLIDER_Y1 + 12) { sliding = true; setThrottleFromY(td.y); }
  }
  if (touchDown && td.isPressed()) {
    int dx = td.x - touchLX, dy = td.y - touchLY;
    if (sliding) setThrottleFromY(td.y);
    else {
      if (!dragging && (abs(td.x - touchX0) + abs(td.y - touchY0)) > 7 && flying) dragging = true;
      if (dragging && flying && launchAnim <= 0) {
        // slide = look around: direct yaw + pitch (not roll)
        autoNav = false; orbitObj = -1;   // the pilot takes the stick
        shipB.yaw(dx * 0.0095f);
        shipB.pitch(-dy * 0.011f);
        shipB.fix();
        if (dockTarget >= 0 && (abs(dx) + abs(dy)) > 3) { dockTarget = -1; setBanner("DOCKING COMPUTER OFF", 900); }
      }
    }
    touchLX = td.x; touchLY = td.y;
  }
  if (td.wasReleased()) {
    if (touchDown && !dragging && !sliding && millis() - touchT0 < 450) handleTap(touchX0, touchY0);
    touchDown = false; dragging = false; sliding = false;
  }

  if (chatOpen) {
    if (M5.BtnB.wasPressed()) { chatSend(chatDraft); chatDraft[0] = 0; }
    else if (M5.BtnA.wasPressed() || M5.BtnC.wasPressed()) chatOpen = false;
    return;
  }
  if (mapOpen) {
    if (M5.BtnA.wasHold()) { mapPage ^= 1; mapTrace = -1; hx::pop(0.2f, 0.02f); }
    else if (M5.BtnA.wasClicked() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) { mapOpen = false; hx::pop(0.15f, 0.01f); }
    return;
  }
  if (statusOpen) {
    if (M5.BtnC.wasHold()) { statusPage ^= 1; hx::pop(0.2f, 0.02f); }
    else if (M5.BtnC.wasClicked() || M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) { statusOpen = false; hx::pop(0.15f, 0.01f); }
    return;
  }
  if (bootOpen) {
    if (newGameDone > 0) { newGameDone -= dt; return; }
    if (M5.BtnA.isPressed() && M5.BtnC.isPressed()) {   // A + C: switch the experimental Signals on or off
      signalsHold += dt;
      if (signalsHold >= 2.0f) {
        bool on = !signalsOn();
        sm::flagSet("signals", on ? 1 : 0, true); saveAll();
        signalsHold = 0; signalsDone = 1.6f; hx::swell(0.5f, 0.2f, 0.4f);
      }
      return;
    }
    signalsHold = 0;
    if (signalsDone > 0) { signalsDone -= dt; }
    if (M5.BtnB.isPressed() && M5.BtnC.isPressed()) {
      // B + C held: the bar fills; it turns red before the save is cleared
      float before = newGameHold;
      newGameHold += dt;
      if (before < 1.5f && newGameHold >= 1.5f) hx::pop(0.35f, 0.03f);
      if (newGameHold >= 2.0f) { startNewGame(); newGameHold = 0; newGameDone = 1.6f; hx::boom(0.7f); }
      return;
    }
    newGameHold = 0;
    if (M5.BtnA.wasClicked() || M5.BtnB.wasClicked() || M5.BtnC.wasClicked()) handleTap(0, 0);
    return;
  }
  if (lostOpen || endingOpen) { if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) handleTap(0, 0); return; }
  if (stationOpen) {
    if (M5.BtnA.wasPressed()) launch();
    else if (M5.BtnB.wasPressed()) { if (stationPage == 0) stationCommit(); else if (stationPage == 1) { if (hangarTab) shipAction(shipSel); else hangarBuy(); } else marketTrade(true, 1); }
    else if (M5.BtnC.wasPressed()) {
      if (stationPage == 0) stationChoice = (stationChoice + 1) % STATION_ROWS;
      else if (stationPage == 1) { if (hangarTab) shipSel = (uint8_t)((shipSel + 1) % sm::SHIP_COUNT); else hangarSel = (uint8_t)(hangarN ? (hangarSel + 1) % hangarN : 0); }
      else marketSel = (uint8_t)((marketSel + 1) % sm::G_COUNT);
      hx::pop(0.12f, 0.01f);
    }
    return;
  }
  if (M5.BtnA.wasHold()) { mapOpen = true; mapPage = 0; mapTrace = -1; hx::pop(0.25f, 0.02f); return; }
  if (M5.BtnA.wasClicked()) cycleTarget();   // release edge: a hold never cycles
  if (M5.BtnC.wasHold()) { statusOpen = true; statusPage = 0; hx::pop(0.25f, 0.02f); return; }
  if (M5.BtnB.wasHold() && flyingGhost()) {   // the flagship's cloak: on and off, for as long as you like
    if (cloakT > 0.f) { cloakT = 0.f; cloakCD = 0.f; setBanner("CLOAK DOWN", 800); }
    else {
      cloakT = 1e6f; cloakCD = 0.f;
      if (theater == TH_COMBAT) { theater = TH_NONE; theaterObj = -1; }
      for (auto &o : objs) if (o.kind == K_SHIP && o.hostile) { o.engaged = false; o.timer = 30.f; }
      setBanner("CLOAKED", 900);
      hx::swell(0.35f, 0.3f, 0.5f);
    }
    return;
  }
  if (M5.BtnB.wasHold()) {
    uint8_t ct = sm::capTier(sm::CAP_CLOAK);
    if (!ct) { setBanner("NO CLOAK FITTED", 1000); hx::pop(0.12f, 0.01f); }
    else if (cloakT > 0.f) { cloakT = 0.f; cloakCD = fmaxf(15.f, 45.f - 4.f * ct) * 0.5f; setBanner("CLOAK DOWN", 900); }
    else if (cloakCD > 0.f) { char b[32]; snprintf(b, sizeof(b), "CLOAK RECHARGING %ds", (int)cloakCD + 1); setBanner(b, 900); }
    else {
      cloakT = 8.f + 6.f * ct;
      if (theater == TH_COMBAT) { theater = TH_NONE; theaterObj = -1; }
      for (auto &o : objs) if (o.kind == K_SHIP && o.hostile) { o.engaged = false; o.timer = cloakT; o.v = o.v + randDir() * 6.f; }
      setBanner("CLOAKED - THEY CANNOT SEE YOU", 1600);
      hx::swell(0.45f, 0.3f, 0.6f);
    }
    return;
  }
  if (M5.BtnB.wasClicked()) { autoNav = false; orbitObj = -1; captureNeutral(); tiltX = tiltY = tiltRoll = 0; rateYaw = ratePitch = rateRoll = 0; setBanner("ATTITUDE CENTERED", 900); hx::pop(0.2f, 0.02f); }
  if (M5.BtnC.wasClicked()) { throttleT = 0.5f; setBanner("CRUISE", 700); hx::pop(0.2f, 0.02f); }   // release edge: a hold opens status

  if (!neutralValid) captureNeutral();
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    float ax = d.accel.x - neutralAx, ay = d.accel.y - neutralAy;
    // soft deadzone + expo so a resting hand holds course
    auto shape = [](float v) {
      float a = fabsf(v);
      if (a < 0.05f) return 0.f;
      a = clampf((a - 0.05f) / 0.45f, 0, 1.4f);
      return (v < 0 ? -1.f : 1.f) * (a * 0.45f + a * a * 0.55f);
    };
    // Device tip: X → pitch stick, Y → yaw stick (flip signs on device if mirrored)
    tiltY = tiltY * 0.8f + shape(ay) * 0.2f;   // pitch
    tiltX = tiltX * 0.8f + shape(ax) * 0.2f;   // yaw
    // Steering-wheel roll: gyro about Z, intentional deadzone + ease-in
    float gz = d.gyro.z;   // if no roll on device, try d.gyro.x or d.gyro.y
    float ga = fabsf(gz);
    float twist = 0.f;
    const float dead = 0.45f;
    if (ga > dead) {
      float u = clampf((ga - dead) / 0.8f, 0.f, 1.f);
      u = u * u;
      twist = (gz < 0.f ? -1.f : 1.f) * u;
    }
    tiltRoll = tiltRoll * 0.85f + twist * 0.15f;
  }
}

// ============================================================
//  world update
// ============================================================
// stabilizers make the ship faster where the going is rough: a little in real space, more below
static const sm::ShipSpec &flying() { return sm::shipSpec(sm::sheet().activeShip); }
static float cruiseSpeed() {
  float st = sm::capTier(sm::CAP_STABILIZER);
  float sp = flying().speed;
  return sp * (layer == 0 ? 13.f * (1.f + 0.015f * st) : (15.f + layer * 2.5f) * (1.f + 0.04f * st));
}

// Turbulence. Real space: at most a faint tremble near portals and heavy bodies.
// Subspace: it is the depth that bites. A stabilizer rated for the layer (MK1 the
// shallows ... MK4 the cove) damps most of it to a tremble and a softened rattle;
// unrated, it grows with every layer it lacks, and past the hull's depth rating it
// stresses the hull. An unrated ship in the deep cove should not expect to survive.
// Each tier of marks has its own character: MK0-1 jolts, MK2-3 a damped sway,
// MK4-5 a slow glide, MK6+ phase-locked (no kicks at all, portals draw you true).
static float turbulence = 0.f, turbFilt[2] = {0, 0}, turbSeverity = 0.f, hullStress = 0.f, stressT = 0.f;
static bool turbRated = true;
static uint8_t stabMode() { uint8_t s = sm::capTier(sm::CAP_STABILIZER); return s <= 1 ? 0 : s <= 3 ? 1 : s <= 5 ? 2 : 3; }
static void applyTurbulence() {
  static const float layerT[5] = {0.f, 0.08f, 0.18f, 0.32f, 0.45f};
  float T = layerT[layer < 0 ? 0 : (layer > 4 ? 4 : layer)];
  for (auto &o : objs) {
    if (o.kind == K_PORTAL) { float d = distTo(o); if (d < 140.f) T += 0.6f * (1.f - d / 140.f); }
    if (o.kind == K_BODY) {
      float sd = surfaceDist(o);
      if (o.bodyType == BT_HOLE && sd < o.radius * 14.f) T += 0.7f * (1.f - sd / (o.radius * 14.f));
      else if (o.bodyType == BT_GIANT && sd < o.radius * 0.3f) T += 0.3f * (1.f - sd / (o.radius * 0.3f));
      else if (isStarBody(o.bodyType) && sd < o.radius * 0.6f) T += 0.25f * (1.f - sd / (o.radius * 0.6f));
    }
  }
  turbulence = clampf(T, 0.f, 1.f);
  shakeX = shakeY = 0.f;
  turbSeverity = 0.f; hullStress = 0.f; turbRated = true;
  if (turbulence < 0.01f) return;
  if (layer == 0) {   // real space: only the faintest tremble
    shakeX = rf(-1, 1) * turbulence * 0.8f; shakeY = rf(-1, 1) * turbulence * 0.8f;
    return;
  }
  uint8_t st = (uint8_t)(sm::capTier(sm::CAP_STABILIZER) + flying().deepCalm);
  int lacking = layer - (st > 4 ? 4 : st);                     // layers this stabilizer is not rated for
  int overHull = layer - (int)sm::depthQuery((uint8_t)layer).maxBand;
  turbRated = lacking <= 0;
  float S = turbulence * (turbRated ? 0.25f : 1.f + 0.8f * lacking) * (overHull > 0 ? 1.f + 0.6f * overHull : 1.f);
  turbSeverity = S;
  hullStress = (overHull > 0 ? (float)overHull : 0.f) + (lacking > 1 ? (lacking - 1) * 0.5f : 0.f);
  float kick = S / (1.f + 0.5f * st);                           // heading kicks: what the stabilizer removes
  // the shake is not damped by being rated: a rated ship still sees and feels the road
  float shakeS = turbulence * (turbRated ? 1.f : 1.f + 0.8f * lacking) * (overHull > 0 ? 1.f + 0.6f * overHull : 1.f);
  float shk = shakeS / (1.f + 0.12f * st);
  uint8_t m = stabMode();
  if (m == 0) {           // undamped: jolts
    float jx = rf(-1, 1), jy = rf(-1, 1);
    shipB.yaw(jx * kick * 2.7f * dt); shipB.pitch(jy * kick * 2.7f * dt);
    shakeX = jx * shk * 6.f; shakeY = jy * shk * 6.f;
  } else if (m == 1) {    // gyro: the same forces, smoothed into a sway
    float k = 1.f - expf(-dt / 0.25f);
    turbFilt[0] += (rf(-1, 1) - turbFilt[0]) * k; turbFilt[1] += (rf(-1, 1) - turbFilt[1]) * k;
    shipB.yaw(turbFilt[0] * kick * 1.4f * dt); shipB.pitch(turbFilt[1] * kick * 1.4f * dt);
    shakeX = turbFilt[0] * shk * 5.f + rf(-1, 1) * shk; shakeY = turbFilt[1] * shk * 5.f + rf(-1, 1) * shk;
  } else {                // inertial glide, or phase-locked: no kicks, only the road under you
    float gx = sinf(tNow * 0.6f * 6.283f) * 0.6f + sinf(tNow * 0.37f * 6.283f) * 0.4f, gy = cosf(tNow * 0.45f * 6.283f);
    if (m == 2) { shipB.yaw(gx * kick * 0.6f * dt); shipB.pitch(gy * kick * 0.4f * dt); }
    shakeX = gx * shk * 3.f + rf(-1, 1) * shk * 2.5f; shakeY = gy * shk * 1.5f + rf(-1, 1) * shk * 2.5f;
  }
  shipB.fix();
  shakeX = clampf(shakeX, -12.f, 12.f); shakeY = clampf(shakeY, -12.f, 12.f);   // the danger is the kicks and the stress, not a leaping screen
  // past the rating the hull takes the strain
  if (hullStress > 0.f) {
    stressT += dt * (0.4f + turbulence * 2.f);
    float every = clampf(2.8f / hullStress, 0.5f, 4.f);
    if (stressT >= every) {
      stressT = 0.f;
      setBanner(hullStress >= 2.f ? "HULL STRESS - THE FRAME IS SCREAMING" : "HULL STRESS - PAST RATING", 1000);
      hx::thud(clampf(0.4f + hullStress * 0.15f, 0.4f, 1.f));
      damage((int)(hullStress + 0.5f), false);
    }
  } else stressT = 0.f;
}
static float speedWanted() {
  // 0 .. 0.5 ramps stop..cruise, 0.5 .. 1 ramps cruise..boost (2x)
  float c = cruiseSpeed();
  // the top end is the ship's: the ghostfleet flagship is manageable at cruise and wild flat out
  return throttleT <= 0.5f ? c * (throttleT / 0.5f) : c * (1.f + (throttleT - 0.5f) * 2.f * flying().boost);
}

static void steerAt(V3 aim, float rate) {
  V3 loc = shipB.toLocal(aim - shipPos);
  shipB.yaw(clampf(atan2f(loc.x, loc.z), -rate * dt, rate * dt));
  shipB.pitch(clampf(-atan2f(loc.y, loc.z), -rate * dt, rate * dt));
  shipB.fix();
}
static void autoNavStep() {
  sm::Trip &tr = sm::trip();
  int gi = (tr.active && navObj >= 0 && objs[navObj].kind != K_NONE) ? navObj : autoNavObj;
  if (gi < 0 || (objs[gi].kind != K_GATE && objs[gi].kind != K_PORTAL)) {
    autoNav = false; autoNavObj = -1;
    if (!tr.active) setBanner("AUTONAV - ARRIVED", 1200);
    return;
  }
  autoNavObj = gi; target = gi;
  Obj &g = objs[gi];
  V3 n = g.o.f, rel = shipPos - g.p;
  float ax = dot(rel, n), latd = len(rel - n * ax), sg = ax >= 0.f ? 1.f : -1.f, d = fabsf(ax);
  V3 aim = (latd > d * 0.35f + 2.f && d > 12.f) ? g.p + n * (sg * clampf(d * 0.5f, 12.f, 70.f)) : g.p - n * (sg * 10.f);
  steerAt(aim, 1.3f);
  throttleT = 0.5f;
}
static void orbitStep() {
  if (orbitObj < 0 || objs[orbitObj].kind != K_BODY) { orbitObj = -1; return; }
  Obj &b = objs[orbitObj];
  V3 rel = shipPos - b.p; float r = len(rel);
  V3 radial = rel * (1.f / (r > 1e-3f ? r : 1.f));
  V3 tan = cross(b.o.u, radial); if (len(tan) < 0.1f) tan = cross(shipB.u, radial);
  tan = norm(tan);
  float want = b.radius * 1.3f;   // inside scooping range, above the cloud tops
  V3 dir = norm(tan + radial * clampf((want - r) / (b.radius * 0.3f), -0.8f, 0.8f));
  steerAt(shipPos + dir * 40.f, 1.0f);
  throttleT = 0.32f;
  if (b.timer <= 0.f && sm::sheet().fuel < sm::sheet().fuelCap && theater == TH_NONE && surfaceDist(b) < b.radius * 0.45f) { target = orbitObj; runVerb(VB_SCOOP); }
}

static void autopilot() {
  if (dockTarget < 0 || objs[dockTarget].kind != K_DOCKGATE || objs[dockTarget].link < 0) { dockTarget = -1; return; }
  Obj &g = objs[dockTarget];
  Obj &st = objs[g.link];
  // waypoints around the station, never through it: out to the side, round to
  // the front, onto the slot's axis, then straight through the blue gate
  V3 f = st.o.f;
  V3 rel = shipPos - st.p;
  float ax = dot(rel, f);
  V3 latv = rel - f * ax;
  float latd = len(latv);
  V3 out = latd > 1.f ? latv * (1.f / latd) : st.o.r;
  V3 aim;
  float front = stationDockFront(st.bodyType) * st.radius;   // the gate, along the station's axis
  float side;
  if (st.bodyType == SS_RING) {
    // the ring is open both ways: come in along the axis from whichever side you are on
    float sg = ax >= 0.f ? 1.f : -1.f, d = fabsf(ax);
    side = d;
    if (d < 30.f && latd > st.radius * 0.45f) aim = st.p + f * (sg * 70.f) + out * (latd * 0.5f);     // stand off the plane
    else if (latd > d * 0.3f + 2.f) aim = st.p + f * (sg * clampf(d * 0.5f, 12.f, 80.f));             // onto the axis
    else aim = st.p - f * (sg * 6.f);                                                                   // through the middle
  } else {
    float clear = st.radius * (st.bodyType == SS_OUTPOST ? 4.f : 3.4f);
    float hullFront = front - 6.f;
    side = ax - front;                         // distance in front of the dock gate
    if (ax < hullFront && latd < clear * 0.72f) aim = st.p + out * clear + f * clampf(ax, -2.f * st.radius, 60.f);   // clear the hull
    else if (ax < hullFront) aim = st.p + f * (front + 57.f) + out * 25.f;                                          // round to the front
    else if (latd > side * 0.3f + 2.f) aim = st.p + f * (front + clampf(side * 0.5f, 12.f, 80.f));                 // onto the axis
    else aim = g.p - f * 6.f;                                                                                       // through the slot
  }
  V3 loc = shipB.toLocal(norm(aim - shipPos));
  shipB.yaw(clampf(atan2f(loc.x, loc.z), -1.2f * dt, 1.2f * dt));
  shipB.pitch(clampf(-atan2f(loc.y, loc.z), -1.2f * dt, 1.2f * dt));
  shipB.roll(clampf(dot(shipB.r, st.o.u), -0.8f * dt, 0.8f * dt));   // level to the station
  shipB.fix();
  throttleT = (side > -2.f && side < 45.f) ? 0.3f : 0.5f;
  hx::hum(hx::HUM_ENGINE, 0.12f, 35.f, 0.1f);
}

static void updateContacts() {
  for (int i = 0; i < MAX_OBJ; i++) {
    Obj &o = objs[i];
    if (o.kind == K_NONE || o.remote) continue;   // the other pilot moves by their own hand
    if (inMeeting && net::role() == net::ROLE_GUEST && o.net && o.net < 1000 && o.kind == K_SHIP) { o.p += o.v * dt; continue; }   // the anchor's ships: puppets here
    if (o.timer > 0) o.timer -= dt;
    if (o.spin != 0 && o.kind != K_SHIP && o.kind != K_STATION) { o.o.roll(o.spin * dt); o.o.yaw(o.spin * 0.37f * dt); o.o.fix(); }
    if (o.kind == K_STATION) { o.o.roll(o.spin * dt); o.o.fix(); }
    if (o.kind == K_SHIP) {
      V3 toMe = shipPos - o.p;
      float d = len(toMe);
      if (o.hostile && !o.engaged && o.timer <= 0 && theater == TH_NONE && cloakT <= 0.f) {
        // hunters bend toward you
        o.v = o.v + (norm(toMe) * 12.f - o.v) * clampf(dt * 0.9f, 0, 1);
        if (d < 42.f) {
          float slip = sm::capTier(sm::CAP_CLOAK) * 0.12f + sm::rankOf(sm::CR_GHOST) * 0.02f;
          o.engaged = true;
          if (rf(0, 1) < slip) {
            setBanner("THEY LOSE YOU IN THE DARK", 1600); sm::grantXp(sm::CR_GHOST, 5);
            o.hostile = false; o.v = norm(o.p - shipPos) * 12.f;
          } else {
            char b[112]; snprintf(b, sizeof(b), "%s: %s", o.name, sm::encounterFlavor((sm::EncounterClass)o.enc, (uint8_t)layer).attack);
            setBanner(b, 1800);
            target = i; ambushed = true;
            beginTheater(i, VB_ATTACK);
            hx::thud(0.8f);
          }
        }
      }
      if (theater != TH_NONE && theaterObj == i) {
        if (d > 25.f) o.v = o.v + (norm(toMe) * 6.f - o.v) * clampf(dt, 0, 1);
        else if (d < 20.f) o.v = o.v + (norm(-toMe) * 6.f - o.v) * clampf(dt, 0, 1);
      }
      if (len(o.v) > 0.5f) o.o = Basis::facing(norm(o.v), V3{0, 1, 0});
    }
    if (o.kind == K_SHIP || o.kind == K_POD || o.kind == K_WRECK) o.p += o.v * dt;
    bool transient = o.kind == K_SHIP || o.kind == K_POD || o.kind == K_WRECK || o.kind == K_ARTIFACT || o.kind == K_ANOMALY || o.kind == K_ROCK;
    float far = o.kind == K_SHIP ? 520.f : 700.f;
    if (transient && i != theaterObj && distTo(o) > far) { if (target == i) target = -1; killObj(i); }
  }
}

// The nearest point of a station's hull to p, and how thick the hull is there.
static void stationHull(const Obj &st, V3 p, V3 &c, float &cr) {
  V3 rel = p - st.p, f = st.o.f;
  float ax = dot(rel, f);
  switch (st.bodyType) {
    case SS_RING: {   // a torus: the middle is open (that is the dock)
      V3 radial = rel - f * ax;
      float rl = len(radial);
      c = st.p + (rl > 1e-3f ? radial * (1.f / rl) : st.o.r) * st.radius;
      cr = st.radius * 0.3f;
      return;
    }
    case SS_SPINDLE: {   // a capsule along its axis
      float t = clampf(ax, -1.75f * st.radius, 0.45f * st.radius);   // stops short of the dock gate at the tip
      c = st.p + f * t; cr = st.radius * 0.58f;
      return;
    }
    case SS_OUTPOST: c = st.p; cr = st.radius * 1.35f; return;
    default: c = st.p; cr = st.radius * 1.15f; return;
  }
}

static void collide() {
  for (int i = 0; i < MAX_OBJ; i++) {
    Obj &o = objs[i];
    if (!(o.kind == K_STATION || o.kind == K_ROCK || o.kind == K_BODY || o.kind == K_LANDMARK || o.kind == K_SHIP) || o.remote) continue;
    float r = o.radius * 1.05f;
    V3 centre = o.p;
    if (o.kind == K_STATION) stationHull(o, shipPos, centre, r);
    V3 d = shipPos - centre;
    float l = len(d);
    if (l >= r || l < 1e-3f) continue;
    V3 nrm = d * (1.f / l);
    shipPos = centre + nrm * (r + 0.2f);
    // glance off: turn the nose along the surface and lose speed
    float into = -dot(shipB.f, nrm);
    if (into > 0.f) { shipB.f = norm(shipB.f + nrm * (into * 1.3f)); shipB.fix(); }
    float impact = shipSpeed * clampf(into, 0.2f, 1.f);
    shipSpeed *= 0.35f;
    if (o.kind == K_BODY && o.bodyType == BT_HOLE) {
      setBanner("THE COLLAPSED STAR TAKES A BITE OF THE HULL", 2000);
      if (damage(18)) return;
    } else if (o.timer <= 0) {
      o.timer = 1.2f;
      int dmg = (int)clampf(impact * 0.35f, 1.f, o.kind == K_BODY ? 8.f : 6.f);
      setBanner(o.kind == K_BODY ? (isStarBody(o.bodyType) ? "CORONA BURN - PULL AWAY" : "ATMOSPHERE SKIP - SHIELDS SCREAM") : "SCRAPED THE HULL", 1200);
      if (damage(dmg)) return;
    }
  }
}

static void gateCrossings() {
  for (int i = 0; i < MAX_OBJ; i++) {
    Obj &g = objs[i];
    if (g.kind != K_GATE && g.kind != K_PORTAL && g.kind != K_DOCKGATE) continue;
    float side = dot(shipPos - g.p, g.o.f);
    float prev = g.prevSide;
    g.prevSide = side;
    bool crossed = (prev > 0 && side <= 0) || (prev < 0 && side >= 0);
    if (!crossed || fabsf(prev - side) < 1e-5f) continue;
    if (dockTarget >= 0 && i != dockTarget) continue;   // the docking computer only threads the dock
    V3 x = lerp3(prevShipPos, shipPos, prev / (prev - side));
    if (len(x - g.p) < g.radius) { threadGate(g); return; }   // world may have changed
    if (g.kind == K_PORTAL && len(x - g.p) < g.radius * 2.2f) {
      setBanner("THE PORTAL REJECTS A CROOKED APPROACH - COME ROUND", 1800);
      hx::stutter(0.4f, 3, 0.08f);
    }
  }
}

static float starHeat = 0.f;
static void heatWorld() {
  for (auto &o : objs) {
    if (o.kind != K_BODY || !isStarBody(o.bodyType)) continue;
    float sd = surfaceDist(o), heatR = o.radius * 0.6f;
    if (sd > heatR) continue;
    float close = 1.f - clampf(sd / heatR, 0.f, 1.f);
    hx::hum(hx::HUM_BODY, 0.3f + 0.5f * close, 9.f + 14.f * close, 0.7f);   // a rasping heat
    starHeat += dt * (1.f + close * 2.5f);
    if (starHeat >= 0.9f) {
      starHeat = 0.f;
      char b[48]; snprintf(b, sizeof(b), "HULL HEATING - %d%%", (int)(40 + close * 60));
      setBanner(b, 900);
      if (damage(1 + (int)(close * 3.f))) return;
    }
  }
}

static void hapticWorld() {
  // turbulence, felt (subspace only, on its own channel). Rated: a dampened texture.
  if (layer > 0 && turbulence > 0.08f) {
    float lv = turbRated ? 0.1f + 0.2f * turbulence : clampf(0.25f + 0.5f * turbSeverity, 0.f, 0.95f);
    switch (stabMode()) {
      case 0: hx::hum(hx::HUM_TURB, lv, 0.f, turbRated ? 0.4f : 0.9f); break;   // rattle
      case 1: hx::hum(hx::HUM_TURB, lv, 3.f, turbRated ? 0.15f : 0.35f); break; // sway
      case 2: hx::hum(hx::HUM_TURB, lv, 0.8f, 0.f); break;                      // heave
      default: hx::hum(hx::HUM_TURB, lv * 0.8f, 6.f, 0.f); break;               // purr
    }
  }
  // heavy bodies: a slow tidal throb that grows as you close in
  for (auto &o : objs) {
    if (o.kind != K_BODY) continue;
    float sd = surfaceDist(o), range = o.bodyType == BT_HOLE ? o.radius * 14.f : o.radius * 1.6f;
    if (sd > range) continue;
    float k = 1.f - sd / range; k = k * k;
    if (o.bodyType == BT_HOLE) hx::hum(hx::HUM_BODY, 0.2f + 0.7f * k, 0.6f + 2.4f * k, 0.3f);
    else hx::hum(hx::HUM_BODY, 0.45f * k, 0.55f + 0.5f * k, 0.12f);
  }
  // portals: spacetime tearing, building to the crossing
  for (auto &o : objs) {
    if (o.kind != K_PORTAL) continue;
    float d = distTo(o);
    if (d > 260) continue;
    float k = clampf(1.f - d / 260.f, 0, 1);
    hx::hum(hx::HUM_PORTAL, 0.08f + 0.9f * powf(k, 1.7f), 1.5f + 16.f * k, 0.15f + 0.35f * k);
  }
  // subspace pressure and the heartbeat of the deep
  if (layer > 0) {
    hx::hum(hx::HUM_DEEP, 0.04f + layer * 0.035f, 0.25f + layer * 0.12f, 0.05f * layer);
    static float beatT = 0;
    beatT -= dt;
    if (layer >= 3 && beatT <= 0) {
      hx::heartbeat(0.35f + (layer - 3) * 0.3f);
      if (layer == 4) deepFlash = 1.f;
      beatT = layer == 4 ? rf(0.9f, 1.7f) : 1.35f;
    }
  }
  if (throttleT > 0.75f && shipSpeed > 5) hx::hum(hx::HUM_ENGINE, 0.08f + (throttleT - 0.75f) * 0.3f, 42.f, 0.2f);
}

static void updateWorld() {
  shakeX = shakeY = 0.f;
  netTick();                       // signals (no-op unless commissioned)
  if (glitchT > 0.f) glitchT -= dt;   // turbulence sets it each flying frame; overlays never inherit a stale shake
  sm::simTick(millis());
  sm::contractTick();
  serviceBanner();
  sm::Contract done;
  if (sm::contractTakeCompleted(done)) {
    if (done.issuer == BR_DESERET) {
      static const struct { int8_t at; const char *line; } beats[] = {
        {2, "A Deseret clerk calls the frontier 'the outer courts'. Odd phrasing."},
        {4, "Deseret charts name places below the roads. Nobody else's do."},
        {7, "Deseret asked if I'd consider a berth 'below'. Said I'd been faithful."},
        {10, "Their elder said the deep is not a road. It is land. And land is held."}};
      int8_t st = sm::flagGet("deseret");
      for (auto &b : beats) if (st == b.at) sm::journalAdd(b.line);
    }
    char b[112]; snprintf(b, sizeof(b), "JOB DONE: %s +%dcr", done.title, done.pay);
    { char jb[72]; snprintf(jb, sizeof(jb), "Finished %s%s%s. Paid %d.", done.title, done.dest[0] ? " to " : "", done.dest, done.pay); sm::journalAdd(jb); }
    noteBanner(b, 2800); hx::swell(0.5f, 0.1f, 0.3f);
  }
  if (crossFlash > 0) crossFlash -= dt * 2.2f;
  if (deepFlash > 0) deepFlash -= dt * 3.5f;
  if (hitFlash > 0) hitFlash -= dt;
  fovPulse = layer >= 3 ? 1.f + 0.035f * (layer - 2) * sinf(tNow * 1.7f) + (layer == 4 ? deepFlash * 0.06f : 0.f) : 1.f;

  if (dockAnim > 0) { dockAnim -= dt; if (dockAnim <= 0) openBoard(); return; }
  if (stationOpen || endingOpen || lostOpen || bootOpen || statusOpen || mapOpen) return;
  alienTick();
  if (alienHolds() || (alien.active && alien.t >= 22.f)) {
    // dead stick: the ship coasts to a halt, nothing else moves
    shipSpeed *= expf(-dt * 1.4f); rateYaw = ratePitch = 0;
    prevShipPos = shipPos; shipPos += shipB.f * (shipSpeed * dt);
    for (auto &o : objs) if (o.kind != K_NONE) o.prevSide = dot(shipPos - o.p, o.o.f);
    return;
  }
  if (launchAnim > 0) launchAnim -= dt;

  // attitude: tilt aims, with a little mass
  if (dockTarget >= 0) autopilot();
  else if (autoNav) autoNavStep();
  else if (orbitObj >= 0) orbitStep();
  else {
    const sm::ShipSpec &fs = flying();   // the Mantis is 1.0 on both: the original feel
    float k = 1.f - expf(-dt / (0.12f / fs.agility));
    rateYaw += (-tiltX * 1.15f * fs.turn - rateYaw) * k;
    ratePitch += (-tiltY * 1.0f * fs.turn - ratePitch) * k;
    rateRoll += (tiltRoll * 1.2f * fs.turn - rateRoll) * k;
    shipB.yaw(rateYaw * dt);
    shipB.pitch(ratePitch * dt);
    shipB.roll(rateRoll * dt);
    // light assistance only when nearly threaded: a nudge toward the ring's heart
    for (auto &g : objs) {
      if (g.kind != K_GATE && g.kind != K_PORTAL && g.kind != K_DOCKGATE) continue;
      V3 c = shipB.toLocal(g.p - shipPos);
      if (c.z < 4.f || c.z > 50.f) continue;
      float off = sqrtf(c.x * c.x + c.y * c.y) / c.z;
      if (off > 0.45f) continue;
      float k = (1.f - off / 0.45f) * 0.55f * dt * (1.f + 0.12f * sm::capTier(sm::CAP_STABILIZER)) * (stabMode() == 3 && g.kind == K_PORTAL ? 1.8f : 1.f);
      shipB.yaw(clampf(atan2f(c.x, c.z), -k, k));
      shipB.pitch(clampf(-atan2f(c.y, c.z), -k, k));
      break;
    }
    shipB.fix();
  }
  applyTurbulence();
  float want = speedWanted();
  if (theater == TH_COMBAT || theater == TH_BEAM) want = fminf(want, cruiseSpeed() * 0.4f);
  if (sm::sheet().fuel == 0 && layer == 0) want *= 0.6f;
  shipSpeed += (want - shipSpeed) * (1.f - expf(-dt / 0.6f));
  prevShipPos = shipPos;
  shipPos += shipB.f * (shipSpeed * dt);

  for (auto &d : dust) {   // dust wraps around the ship
    V3 rel = d - shipPos;
    if (len(rel) > 60.f || dot(rel, shipB.f) < -20.f) d = shipPos + norm(shipB.f * rf(0.6f, 1.f) + randDir() * 0.7f) * rf(25, 60);
  }

  int layerBefore = layer;
  uint32_t livesBefore = sm::sheet().lives;
  gateCrossings();
  if (layer != layerBefore || sm::sheet().lives != livesBefore || stationOpen || dockAnim > 0) return;
  collide();
  updateContacts();
  theaterTick();
  heatWorld();
  hapticWorld();

  spawnTimer -= dt;
  if (spawnTimer <= 0) {
    spawnTimer = layer == 0 ? rf(6, 12) : rf(5, 10);
    int contacts = countKind(K_SHIP) + countKind(K_POD) + countKind(K_WRECK) + countKind(K_ARTIFACT) + countKind(K_ANOMALY);
    if (contacts < 4 && rf(0, 1) < 0.7f) spawnContact();
  }

  // the trip path never vanishes: if the next gate is lost, lay another ahead
  sm::Trip &tr = sm::trip();
  if (tr.active && navObj >= 0 && (objs[navObj].kind == K_NONE || distTo(objs[navObj]) > 900.f)) { killObj(navObj); navObj = -1; }
  if (tr.active && navObj < 0) spawnNextOnPath();

  // real space: slow idle burn; subspace costs are paid at the portals
  static float burn = 0;
  if (layer == 0) { burn += dt * (throttleT > 0.75f ? 3.f : 1.f); if (burn > 12.f) { burn = 0; if (sm::sheet().fuel > 0) sm::burnFuel(1); } }

  // wandered off in real space with nothing named nearby: new gates drift into view
  if (layer == 0 && !tr.active) {
    int dests = 0;
    for (int i = 0; i < MAX_OBJ; i++) {
      if (objs[i].kind != K_GATE || !(objs[i].gflags & GF_DEST)) continue;
      if (distTo(objs[i]) > 900) { if (target == i) target = -1; killObj(i); } else dests++;
    }
    if (dests == 0) placeDestGates(ri(3, 4));
  }

  for (int i = 0; i < boltN; ) { if (--bolts[i].life == 0) bolts[i] = bolts[--boltN]; else i++; }
  for (auto &m : missiles) if (m.alive) {
    m.t += dt;
    if (m.t >= m.dur) { m.alive = false; addSparks(m.x1, m.y1, 8, m.col, 110); hx::pop(0.3f, 0.02f); }
  }
  for (int i = 0; i < sparkN; ) {
    Spark &sp = sparks[i]; sp.t += dt; sp.x += sp.vx * dt; sp.y += sp.vy * dt;
    if (sp.t > 0.45f) sparks[i] = sparks[--sparkN]; else i++;
  }
  for (auto &f : frags) if (f.alive) {
    f.t += dt;
    if (f.t < 0.55f) f.p += f.v * dt;
    else {   // the tractor: pull it home
      V3 home = shipPos + shipB.f * 3.f - shipB.u * 2.f;
      f.p = lerp3(f.p, home, clampf(dt * 3.5f, 0, 1));
      if (len(f.p - home) < 1.5f || f.t > 2.5f) { f.alive = false; hx::pop(0.12f, 0.01f); }
    }
  }
  if (shieldFx > 0.f) shieldFx -= dt;
  if (cloakT > 0.f) {
    if (!flyingGhost()) cloakT -= dt;
    else for (auto &o : objs) if (o.kind == K_SHIP && o.hostile && o.timer < 5.f) o.timer = 5.f;   // they never find the flagship
    hx::hum(hx::HUM_ENGINE, 0.06f, 2.f, 0.f);
    if (cloakT <= 0.f) { cloakT = 0.f; cloakCD = fmaxf(15.f, 45.f - 4.f * sm::capTier(sm::CAP_CLOAK)); setBanner("CLOAK DOWN", 1000); hx::pop(0.25f, 0.03f); }
  } else if (cloakCD > 0.f) cloakCD -= dt;
  for (auto &b : booms) if (b.alive) { b.t += dt; if (b.t > 1.3f) b.alive = false; }
  if (millis() > lastSave + 30000) saveAll();
  serviceSD(false);
}

// ============================================================
//  draw
// ============================================================
static int order[MAX_OBJ];
static float orderZ[MAX_OBJ];

static void drawGateLike(Obj &o, float sx, float sy, float z) {
  float r = o.radius * FOCAL / z;
  if (o.kind == K_PORTAL) {
    int next = sm::tripPortalGoesUp() ? layer - 1 : layer + 1;
    if (r > 3 && onScreen(sx, sy, r)) {
      // the next layer is already visible through the hole
      if (next <= 0) {
        cv.fillCircle((int)sx, (int)sy, (int)(r * 0.92f), rgb(2, 3, 8));
        if (r > 10) for (int s = 0; s < NSTARS; s += 3) {
          float px, py;
          if (!projectDir(stars[s].d, px, py)) continue;
          if ((px - sx) * (px - sx) + (py - sy) * (py - sy) < r * r * 0.8f) cv.drawPixel((int)px, (int)py, stars[s].col);
        }
      } else { fieldGain = 1.9f; drawField(next, sx, sy, r * 0.92f); fieldGain = 1.f; }
      for (int k = 1; k <= 3; k++)
        cv.drawCircle((int)sx, (int)sy, (int)(r * (0.25f + 0.2f * k + 0.05f * fsin(tNow * 3 - k))), hsv(layerHue(next < 0 ? 0 : next) + k * 25, 0.7f, 0.55f));
    }
    drawRing(o, o.radius, rgb(200, 80, 220), 32, 0.05f, tNow * 7.f, 3);
    drawRing(o, o.radius * 1.07f, rgb(160, 255, 120), 24, 0.08f, -tNow * 5.f, 1);
    if (layer >= 3) drawRing(o, o.radius * 1.15f, hsv(layerHue(layer), 0.8f, 0.7f), 20, 0.12f, tNow * 3.f, 1);
    return;
  }
  uint16_t col = gateColor(o);
  V3 c = shipB.toLocal(o.p - shipPos);
  bool aligned = c.z > 0 && c.z < 80 && sqrtf(c.x * c.x + c.y * c.y) < o.radius * 0.8f;
  if (layer >= 3) {   // chromatic ghosts in the deep
    Obj gh = o; gh.p = o.p + shipB.r * 0.6f;
    drawRing(gh, o.radius, hsv(layerHue(layer) + 120, 0.9f, 0.6f), 18, 0.02f, tNow, 1);
  }
  drawRing(o, o.radius, col, 22, 0.f, 0, aligned ? 3 : 2);
  drawRing(o, o.radius * 0.9f, shade(col, 0.5f), 18, 0.f, 0, 1);
  if (o.gflags & GF_CHAIN) {   // chevrons turning inward
    for (int s = 0; s < 4; s++) {
      float a = s * 1.5708f + tNow * 0.8f;
      V3 dir = o.o.r * cosf(a) + o.o.u * sinf(a);
      float bx, by, bz, tx, ty, tz;
      if (project(o.p + dir * (o.radius * 1.14f), bx, by, bz) && project(o.p + dir * (o.radius * 0.98f), tx, ty, tz))
        cv.drawLine((int)bx, (int)by, (int)tx, (int)ty, col);
    }
  }
  if (aligned) cv.drawCircle((int)sx, (int)sy, (int)(r * 1.25f) + 2, rgb(240, 240, 210));
  if (o.kind == K_DOCKGATE && r > 3) {
    bool due = dueHereNow();
    cv.setTextColor(col); cv.setCursor((int)sx - (due ? 24 : 12), (int)(sy - r - 11)); cv.print(due ? "DOCK - JOB" : "DOCK");
  } else if (r > 2.5f && (o.gflags & GF_DEST) && z < 700) {
    cv.setTextColor(col);
    cv.setCursor((int)sx - (int)strlen(o.name) * 3, (int)(sy - r - 20)); cv.print(o.name);
    sm::DepthAbility da = sm::depthQuery(o.depth);
    bool risky = o.depth > da.maxBand;
    char dl[28];
    bool viaHub = o.lmId && !(o.gflags & GF_FIXED);
    snprintf(dl, sizeof(dl), "DEPTH %u%s%s", o.depth, risky ? " !" : "", (o.gflags & GF_JOB) ? "  JOB" : (o.gflags & GF_FIXED) ? "  FIXED" : viaHub ? "  VIA" : (o.gflags & GF_UNKNOWN) ? "  ?" : "");
    cv.setTextColor(risky ? rgb(255, 110, 90) : shade(col, 0.8f));
    cv.setCursor((int)sx - (int)strlen(dl) * 3, (int)(sy - r - 10)); cv.print(dl);
  } else if (r > 3 && (o.gflags & GF_LOCALNAME)) {
    cv.setTextColor(col); cv.setCursor((int)sx - (int)strlen(o.name) * 3, (int)(sy - r - 11)); cv.print(o.name);
  }
}

static void drawObjects() {
  int n = 0;
  for (int i = 0; i < MAX_OBJ; i++) {
    if (objs[i].kind == K_NONE) continue;
    V3 c = shipB.toLocal(objs[i].p - shipPos);
    if (c.z < -objs[i].radius) continue;
    order[n] = i; orderZ[n] = c.z; n++;
  }
  for (int i = 1; i < n; i++) {   // far to near
    int oi = order[i]; float z = orderZ[i]; int j = i - 1;
    while (j >= 0 && orderZ[j] < z) { order[j + 1] = order[j]; orderZ[j + 1] = orderZ[j]; j--; }
    order[j + 1] = oi; orderZ[j + 1] = z;
  }
  lensOn = alienLensOn;
  if (alienLensOn) { lensX = alienLX; lensY = alienLY; lensR = alienLR; }
  for (int k = 0; k < n; k++) {
    Obj &o = objs[order[k]];
    float sx, sy, z, outR;
    switch (o.kind) {
      case K_BODY: drawBody(o); break;
      case K_STATION:
        if (o.bodyType != SS_CORIOLIS) { drawStationObj(o); break; }
        drawMesh(o, meshes[M_STATION], o.radius, o.col, rgb(200, 215, 230), true, outR);
        if (project(o.p + o.o.f * o.radius * 0.98f, sx, sy, z) && dot(o.o.f, shipPos - o.p) > 0) {
          int w = (int)clampf(o.radius * 0.55f * FOCAL / z, 2, 80), h = w / 3 + 1;
          cv.fillRect((int)sx - w / 2, (int)sy - h / 2, w, h, rgb(4, 6, 10));
          cv.drawRect((int)sx - w / 2, (int)sy - h / 2, w, h, ((int)(tNow * 3) & 1) ? rgb(90, 170, 255) : rgb(40, 80, 140));
        }
        break;
      case K_SHIP:
        if (o.remote) { drawRemoteShip(o); break; }
        if (o.mesh == 255) {   // the hostile: a thing of spines
          if (project(o.p, sx, sy, z)) {
            float r = o.radius * FOCAL / z;
            for (int s = 0; s < 7; s++) {
              float a = tNow * 1.3f + s * 0.8976f, rr = r * (1.f + 0.35f * fsin(tNow * 5.f + s));
              cv.drawLine((int)sx, (int)sy, (int)(sx + fcos(a) * rr), (int)(sy + fsin(a) * rr), o.col);
            }
            cv.fillCircle((int)sx, (int)sy, (int)clampf(r * 0.25f, 1, 12), rgb(255, 150, 255));
          }
        } else {
          if (layer >= 3) {   // the deep leaves afterimages
            Obj echo = o; echo.p = o.p - o.v * 0.25f;
            drawMesh(echo, meshes[o.mesh], o.radius, 0, hsv(layerHue(layer) + 180, 0.8f, 0.5f), false, outR);
          }
          uint16_t hull = shipLivery(o);
          if (!hull) hull = o.col;
          meshAmbient = 0.3f; meshDetail = 1;
          if (o.ghost || o.uses == 1 + BR_DESERET) { meshEmblem = 1; emblemArea = 0.f; decalSize = 0.f; }
          drawMesh(o, meshes[o.mesh], o.radius, hull, o.ghost ? rgb(170, 190, 200) : shade(hull, 1.3f), true, outR);
          if (o.ghost && emblemArea > 90.f) drawDecal(LOGO_GHOSTFLEET, o.o.u);
          if (o.uses == 1 + BR_DESERET && emblemArea > 90.f) drawDecal(LOGO_DESERET, o.o.u);
          meshAccent = meshCanopy = 0; meshAmbient = 0.16f; meshDetail = 0; meshEmblem = 0;
          if (outR > 2 && project(o.p - o.o.f * o.radius, sx, sy, z))   // engine glow
            cv.fillCircle((int)sx, (int)sy, (int)clampf(outR * 0.12f, 1, 4), o.ghost ? rgb(200, 255, 230) : o.hostile ? rgb(255, 120, 80) : rgb(140, 200, 255));
        }
        break;
      case K_ROCK: drawMesh(o, meshes[o.mesh], o.radius, o.uses ? o.col : shade(o.col, 0.5f), shade(o.col, 1.25f), true, outR); break;
      case K_POD:
        drawMesh(o, meshes[M_POD], o.radius, o.col, rgb(255, 255, 200), true, outR);
        if (project(o.p, sx, sy, z) && ((int)(tNow * 3) & 1)) cv.fillCircle((int)sx, (int)sy, 2, rgb(80, 255, 120));
        break;
      case K_WRECK: drawMesh(o, meshes[M_COBRA], o.radius, rgb(40, 40, 46), rgb(120, 110, 100), true, outR); break;
      case K_ARTIFACT:
        drawMesh(o, meshes[M_TETRA], o.radius, 0, hsv(270 + 30 * fsin(tNow * 2), 0.6f, 1.f), false, outR);
        if (project(o.p, sx, sy, z)) cv.fillCircle((int)sx, (int)sy, (int)clampf(outR * 0.25f, 1, 6), rgb(230, 200, 255));
        break;
      case K_ANOMALY:
        if (project(o.p, sx, sy, z)) {
          float r = o.radius * FOCAL / z;
          for (int s = 0; s < 3; s++) cv.drawCircle((int)sx, (int)sy, (int)(r * (0.4f + 0.3f * s + 0.1f * fsin(tNow * 3 + s))), hsv(layerHue(layer) + 120 + s * 30, 0.6f, 0.9f));
          for (int s = 0; s < 8; s++) { float a = tNow * 2 + s * 0.785f; cv.drawPixel((int)(sx + fcos(a) * r), (int)(sy + fsin(a) * r * 0.6f), rgb(255, 230, 255)); }
        }
        break;
      case K_LANDMARK:
        if (project(o.p, sx, sy, z)) {
          float r = o.radius * FOCAL / z;
          // a monument: a slow ring of stones around a steady light
          for (int s = 0; s < 12; s++) {
            float a = tNow * 0.15f + s * 0.5236f;
            float x, y, zz;
            if (project(o.p + (o.o.r * cosf(a) + o.o.u * sinf(a)) * o.radius, x, y, zz)) cv.fillRect((int)x - 1, (int)y - 1, 3, (int)clampf(6.f * FOCAL / zz, 2, 18), o.col);
          }
          cv.fillCircle((int)sx, (int)sy, (int)clampf(r * 0.18f, 2, 24), shade(o.col, 0.9f + 0.1f * fsin(tNow * 2)));
          cv.drawCircle((int)sx, (int)sy, (int)clampf(r * 0.3f, 3, 40), shade(o.col, 0.5f));
          if (r > 4) { cv.setTextColor(o.col); cv.setCursor((int)sx - (int)strlen(o.name) * 3, (int)(sy - r - 12)); cv.print(o.name); }
        }
        break;
      case K_DOCKGATE: case K_GATE: case K_PORTAL:
        if (project(o.p, sx, sy, z)) drawGateLike(o, sx, sy, z);
        break;
      default: break;
    }
    // a collapsed star bends light around itself for everything drawn after it
    if (o.kind == K_BODY && o.bodyType == BT_HOLE && project(o.p, sx, sy, z)) {
      float r = o.radius * FOCAL / z;
      if (r > 2 && onScreen(sx, sy, r * 6)) { lensOn = true; lensX = sx; lensY = sy; lensR = r * 1.6f; }
    }
  }
}

static void drawBoomsAndBolts() {
  for (auto &b : booms) {
    if (!b.alive) continue;
    float sx, sy, z;
    if (!project(b.p, sx, sy, z)) continue;
    float r = b.size * FOCAL / z * (0.3f + b.t * 1.4f);
    cv.drawCircle((int)sx, (int)sy, (int)r, mix565(rgb(255, 255, 220), b.col, clampf(b.t * 2, 0, 1)));
    cv.drawCircle((int)sx, (int)sy, (int)(r * 0.6f), shade(b.col, 1.f - b.t * 0.6f));
    for (int k = 0; k < 10; k++) {
      float a = k * 0.628f + b.p.x;
      cv.drawPixel((int)(sx + cosf(a) * r * 1.3f), (int)(sy + sinf(a) * r * 1.3f), rgb(255, 200, 120));
    }
  }
  for (int i = 0; i < boltN; i++) {
    const Bolt &b = bolts[i];
    cv.drawLine((int)b.x0, (int)b.y0, (int)b.x1, (int)b.y1, b.col);
    for (int k = 1; k < b.thick; k++) {   // lances: a glow either side, a white-hot core
      cv.drawLine((int)b.x0 + k, (int)b.y0, (int)b.x1 + k, (int)b.y1, k == 1 && b.life > 3 ? rgb(255, 255, 240) : shade(b.col, 0.6f));
      cv.drawLine((int)b.x0 - k, (int)b.y0, (int)b.x1 - k, (int)b.y1, shade(b.col, 0.45f));
    }
    if (b.thick <= 1 && b.life > 4) cv.drawLine((int)b.x0 + 1, (int)b.y0, (int)b.x1 + 1, (int)b.y1, rgb(255, 255, 220));
  }
  for (auto &m : missiles) {   // particle missiles: a bright head and a fading trail
    if (!m.alive) continue;
    for (int k = 0; k < 6; k++) {
      float u = clampf(m.t / m.dur - k * 0.05f, 0.f, 1.f), iu = 1.f - u;
      float x = iu * iu * m.x0 + 2 * u * iu * m.cx + u * u * m.x1, y = iu * iu * m.y0 + 2 * u * iu * m.cy + u * u * m.y1;
      if (k == 0) cv.fillCircle((int)x, (int)y, 2, rgb(255, 255, 230));
      else cv.drawPixel((int)x, (int)y, shade(m.col, 1.f - k * 0.15f));
    }
  }
  for (int i = 0; i < sparkN; i++) cv.drawPixel((int)sparks[i].x, (int)sparks[i].y, shade(sparks[i].col, 1.f - sparks[i].t * 2.f));
  for (auto &f : frags) {   // fragments and the tractor beam bringing them in
    if (!f.alive) continue;
    float x, y, z;
    if (!project(f.p, x, y, z)) continue;
    if (f.t > 0.55f) {   // the tractor: a steady pale line, pulsing brighter along its length
      cv.drawLine((int)x, (int)y, 160, H - 8, shade(rgb(110, 230, 255), 0.5f + 0.3f * fsin(tNow * 12.f + f.p.x)));
      float u = fmodf(tNow * 2.f + f.p.y, 1.f);
      cv.fillRect((int)(x + (160 - x) * u), (int)(y + (H - 8 - y) * u), 2, 2, rgb(200, 250, 255));
    }
    cv.fillRect((int)x - 1, (int)y - 1, 3, 3, f.col);
    cv.drawPixel((int)x, (int)y, rgb(255, 230, 180));
  }
  if (flyingHoney()) {   // limpet drones: circling the rock while it is cut, then hauling the pieces home
    int dn = 0;
    if (theater == TH_BEAM && theaterVerb == VB_MINE && theaterObj >= 0 && objs[theaterObj].kind == K_ROCK) {
      const Obj &rk = objs[theaterObj];
      for (int k = 0; k < 3; k++) {
        float a = tNow * 2.2f + k * 2.094f;
        V3 dp = rk.p + (shipB.r * cosf(a) + shipB.u * sinf(a) * 0.6f + shipB.f * sinf(a * 0.7f) * 0.4f) * (rk.radius * 1.7f);
        float x, y, z, rx, ry, rz;
        if (project(dp, x, y, z)) {
          cv.fillRect((int)x - 1, (int)y - 1, 3, 3, rgb(122, 138, 78)); cv.drawPixel((int)x, (int)y, rgb(200, 255, 140));
          if (project(rk.p, rx, ry, rz) && ((int)(tNow * 8.f + k) & 1)) cv.drawLine((int)x, (int)y, (int)rx, (int)ry, rgb(170, 230, 120));
        }
      }
    }
    for (auto &f : frags) {
      if (!f.alive || f.t < 0.4f || dn >= 4) continue;
      float x, y, z;
      if (project(f.p + shipB.u * 0.6f, x, y, z)) { cv.fillRect((int)x - 1, (int)y - 2, 3, 3, rgb(122, 138, 78)); cv.drawPixel((int)x, (int)y - 1, rgb(200, 255, 140)); dn++; }
    }
  }
  if (shieldFx > 0.f) {   // the shield takes the hit: arcs flaring around the impact
    uint16_t sc = shieldTint();
    float k = shieldFx / 0.35f;
    int rings = flyingFalcor() ? 5 : 3;
    for (int r = 0; r < rings; r++) {
      if (flyingFalcor()) sc = hsv(180.f + r * 28.f + tNow * 90.f, 0.45f, 1.f);
      int rad = (int)(14 + r * 9 + (1.f - k) * 22);
      for (int a = 0; a < 7; a++) {
        float ang = 3.6f + a * 0.32f + r * 0.11f;
        int x0 = (int)(shieldFxX + cosf(ang) * rad), y0 = (int)(shieldFxY + sinf(ang) * rad * 0.6f);
        int x1 = (int)(shieldFxX + cosf(ang + 0.22f) * rad), y1 = (int)(shieldFxY + sinf(ang + 0.22f) * rad * 0.6f);
        cv.drawLine(x0, y0, x1, y1, shade(sc, k * (1.f - r * 0.25f)));
      }
    }
    if (k > 0.6f) { cv.drawRect(0, 0, W, H, shade(sc, k * 0.8f)); }
  }
  if (theaterObj >= 0 && objs[theaterObj].kind != K_NONE) {
    float sx, sy, z;
    if (!project(objs[theaterObj].p, sx, sy, z)) return;
    if (theater == TH_BEAM) {
      uint16_t c = theaterVerb == VB_SCOOP ? rgb(120, 210, 255) : theaterVerb == VB_CHART ? rgb(255, 215, 120)
                 : (theaterVerb == VB_READ || theaterVerb == VB_SCAN) ? rgb(200, 150, 255) : rgb(170, 255, 110);
      if (theaterVerb == VB_SCOOP) {
        for (int k = 0; k < 8; k++) {
          float u = fmodf(tNow * 1.4f + k * 0.125f, 1.f);
          cv.fillCircle((int)(160 + (sx - 160) * (1 - u)), (int)(H - 8 + (sy - H + 8) * (1 - u)), 1, c);
        }
      } else {
        cv.drawLine(160, H - 6, (int)sx, (int)sy, c);
        cv.drawLine(159, H - 6, (int)sx - 1, (int)sy, shade(c, 0.5f));
      }
    } else if (theater == TH_COMM) {
      for (int k = 0; k < 4; k++) {
        float u = fmodf(theaterT * 1.8f + k * 0.25f, 1.f);
        cv.drawCircle((int)(160 + (sx - 160) * u), (int)(H - 12 + (sy - H + 12) * u), 3 + k, rgb(90, 230, 140));
      }
    }
  }
}

static void drawEdgeArrow(const Obj &o, uint16_t col) {
  V3 c = shipB.toLocal(o.p - shipPos);
  float sx, sy, z;
  if (c.z > 0.5f && project(o.p, sx, sy, z) && onScreen(sx, sy, -6)) return;
  float ax = c.x, ay = -c.y;
  float l = sqrtf(ax * ax + ay * ay) + 1e-4f; ax /= l; ay /= l;
  float ex = clampf(160 + ax * 150, 12, SLIDER_X - 12), ey = clampf(120 + ay * 105, 36, 204);
  float px = -ay, py = ax;
  cv.fillTriangle((int)(ex + ax * 7), (int)(ey + ay * 7), (int)(ex + px * 5), (int)(ey + py * 5), (int)(ex - px * 5), (int)(ey - py * 5), col);
}

static void drawTargeting() {
  if (flyingMaltese()) {   // the Maltese's deterrent: pirates wear a soft red bracket (A targets them first)
    for (auto &o : objs) {
      if (o.kind != K_SHIP || !(o.hostile || o.enc == sm::ENC_PIRATE || o.enc == sm::ENC_SUBPIRATE)) continue;
      float x, y, z;
      if (!project(o.p, x, y, z) || !onScreen(x, y)) continue;
      int r = (int)clampf(o.radius * FOCAL / z * 1.6f, 6.f, 30.f), c = r / 2;
      uint16_t col = rgb(200, 70, 60);
      cv.drawLine((int)x - r, (int)y - r, (int)x - r + c, (int)y - r, col); cv.drawLine((int)x - r, (int)y - r, (int)x - r, (int)y - r + c, col);
      cv.drawLine((int)x + r, (int)y + r, (int)x + r - c, (int)y + r, col); cv.drawLine((int)x + r, (int)y + r, (int)x + r, (int)y + r - c, col);
    }
  }
  chipN = 0;
  if (target < 0 || objs[target].kind == K_NONE) { target = -1; return; }
  Obj &o = objs[target];
  float sx, sy, z;
  V3 c = shipB.toLocal(o.p - shipPos);
  if (c.z <= 0.5f || !project(o.p, sx, sy, z) || !onScreen(sx, sy, -4)) { drawEdgeArrow(o, rgb(240, 240, 255)); return; }
  float r = clampf(o.radius * FOCAL / z, 6, 90);
  uint16_t bc = o.hostile ? rgb(255, 90, 80) : rgb(230, 235, 245);
  int x0 = (int)(sx - r - 3), x1 = (int)(sx + r + 3), y0 = (int)(sy - r - 3), y1 = (int)(sy + r + 3), L = 5;
  cv.drawLine(x0, y0, x0 + L, y0, bc); cv.drawLine(x0, y0, x0, y0 + L, bc);
  cv.drawLine(x1, y0, x1 - L, y0, bc); cv.drawLine(x1, y0, x1, y0 + L, bc);
  cv.drawLine(x0, y1, x0 + L, y1, bc); cv.drawLine(x0, y1, x0, y1 - L, bc);
  cv.drawLine(x1, y1, x1 - L, y1, bc); cv.drawLine(x1, y1, x1, y1 - L, bc);
  char info[40];
  int sd = (int)(surfaceDist(o) * 10); if (sd < 0) sd = 0;
  bool known = o.kind != K_SHIP || surfaceDist(o) < 120.f + 60.f * sm::capTier(sm::CAP_SCANNERS);   // scanners identify at range
  if (o.kind == K_GATE && (o.gflags & GF_DEST)) snprintf(info, sizeof(info), "%dm", sd);
  else snprintf(info, sizeof(info), "%s %dm", known ? o.name : "UNIDENTIFIED", sd);
  cv.setTextColor(bc);
  cv.setCursor((int)clampf(sx - (float)strlen(info) * 3, 2, (float)(SLIDER_X - 4) - (float)strlen(info) * 6), (int)clampf((float)y1 + 3, 36, 206));
  cv.print(info);
  if (theater != TH_NONE) return;
  chipN = verbsFor(o, chips);
  int w = 62, h = 19;
  int x = x1 + 6;
  if (x + w > SLIDER_X - 6) x = x0 - 6 - w;
  x = (int)clampf((float)x, 4, (float)(SLIDER_X - 6 - w));
  int y = (int)clampf(sy - (chipN * (h + 3)) / 2.f, 36, (float)(206 - chipN * (h + 3)));
  for (int i = 0; i < chipN; i++) {
    Chip &ch = chips[i];
    ch.x = x; ch.y = y + i * (h + 3); ch.w = w; ch.h = h;
    cv.fillRoundRect(ch.x, ch.y, ch.w, ch.h, 4, ch.enabled ? shade(ch.col, 0.35f) : rgb(22, 24, 30));
    cv.drawRoundRect(ch.x, ch.y, ch.w, ch.h, 4, ch.enabled ? ch.col : rgb(70, 72, 80));
    cv.setTextColor(ch.enabled ? rgb(245, 250, 245) : rgb(120, 124, 132));
    const char *lab = ch.enabled ? ch.label : ch.note;
    cv.setCursor(ch.x + (ch.w - (int)strlen(lab) * 6) / 2, ch.y + 6);
    cv.print(lab);
  }
}

static void printWrapped(int x, int y, int cols, int maxLines, int lineH, const char *s) {
  char line[64];
  if (cols > 63) cols = 63;
  for (int ln = 0; ln < maxLines && s && *s; ln++) {
    int n = (int)strlen(s), cut = n;
    if (n > cols) { cut = cols; while (cut > cols / 3 && s[cut] != ' ') cut--; if (s[cut] != ' ') cut = cols; }
    memcpy(line, s, (size_t)cut); line[cut] = 0;
    cv.setCursor(x, y + ln * lineH); cv.print(line);
    s += cut; while (*s == ' ') s++;
  }
}

static void drawBanner() {
  if (bannerUntil > millis()) {
    cv.setTextColor(rgb(240, 245, 250));
    printWrapped(6, 218, 50, 2, 11, banner);
  }
}

static void drawHud() {
  sm::Pilot &p = sm::sheet();
  sm::Trip &tr = sm::trip();
  // reticle leans with the turn
  int rx = 160 + (int)(rateYaw * 10), ry = 120 - (int)(ratePitch * 10);
  uint16_t rc = layer == 0 ? rgb(90, 140, 140) : hsv(layerHue(layer) + 180, 0.4f, 0.8f);
  cv.drawLine(rx - 11, ry, rx - 5, ry, rc); cv.drawLine(rx + 5, ry, rx + 11, ry, rc);
  cv.drawLine(rx, ry - 9, rx, ry - 4, rc); cv.drawLine(rx, ry + 4, rx, ry + 9, rc);

  cv.setTextSize(1);
  cv.setTextColor(layer == 0 ? rgb(90, 210, 200) : hsv(layerHue(layer) + 40, 0.6f, 1.f));
  cv.setCursor(4, 4);
  cv.print(layer == 0 ? hereName : layerName(layer));
  bool lowHull = p.hull * 4 < p.hullMax, lowFuel = p.fuel < 15;
  cv.setCursor(196, 4);
  cv.setTextColor(lowHull ? rgb(255, 90, 80) : rgb(200, 210, 220)); cv.printf("H%d", p.hull);
  cv.setTextColor(lowFuel ? rgb(255, 170, 60) : rgb(200, 210, 220)); cv.printf(" F%d", p.fuel);
  cv.setTextColor(rgb(200, 210, 220)); cv.printf(" $%ld", (long)p.credits);

  if (tr.active) {   // where, how deep, which way, and the three beats of the leg
    cv.setTextColor(rgb(240, 210, 120));
    cv.setCursor(4, 15); cv.printf("> %s", tr.dest);
    cv.setTextColor(rgb(170, 160, 120));
    cv.setCursor(4, 25); cv.printf("depth %u  %s", tr.destDepth, tr.ascending ? "climbing" : "diving");
    int bx = 4 + 6 * 18;
    for (int s = 0; s < 3; s++) {
      if (s == 2) cv.drawCircle(bx + s * 10, 28, 3, sm::tripPortalReady() ? rgb(210, 110, 230) : rgb(80, 70, 90));
      else cv.fillCircle(bx + s * 10, 28, 2, s < tr.step ? rgb(120, 240, 200) : rgb(60, 70, 70));
    }
  } else if (sm::contract().live) {
    const sm::Contract &c = sm::contract();
    cv.setTextColor(rgb(230, 200, 100)); cv.setCursor(4, 15);
    cv.printf("JOB %s %u/%u", c.title, c.progress, c.need);
    cv.setTextColor(rgb(150, 130, 80)); cv.setCursor(4, 25);
    if (dueHereNow()) cv.print("-> deliver at the dock here");
    else if (c.dest[0]) cv.printf("-> %s  depth %u", c.dest, c.destDepth); else cv.print(sm::contractHint(c));
  }

  // depth ladder, left edge: where you are, and where the trip turns
  for (int l = 0; l < LAYERS; l++) {
    int y = 70 + l * 16;
    bool here = l == layer;
    cv.fillRect(2, y, here ? 5 : 3, 10, here ? hsv(layerHue(l) + 40, 0.6f, 1.f) : rgb(50, 56, 66));
    if (tr.active && l == tr.destDepth) cv.drawRect(1, y - 1, 9, 12, rgb(240, 210, 120));
  }

  // the stabilizer at work, when there is something to work against
  if (layer > 0 && turbulence > 0.15f) {
    static const char *names[4] = {"TURBULENCE", "GYRO DAMPING", "INERTIAL GLIDE", "PHASE-LOCKED"};
    if (hullStress > 0.f || !turbRated) {
      const char *w = hullStress > 0.f ? "PAST RATING" : "STABILIZER UNRATED";
      cv.setTextColor(((int)(tNow * 4.f) & 1) ? rgb(255, 80, 60) : rgb(150, 40, 30)); cv.setCursor(160 - (int)strlen(w) * 3, 46); cv.print(w);
    }
    static const uint16_t cols[4] = {rgb(255, 120, 90), rgb(240, 200, 110), rgb(140, 220, 255), rgb(120, 255, 210)};
    uint8_t m = stabMode();
    cv.setTextColor(cols[m]); cv.setCursor(160 - (int)strlen(names[m]) * 3, 36); cv.print(names[m]);
    if (m == 3) cv.drawCircle(160, 120, 108 + (int)(4.f * sinf(tNow * 6.f)), shade(cols[3], 0.3f + 0.4f * turbulence));
    if (m == 1) cv.drawCircle(160 + (int)(shakeX * 3.f), 120 + (int)(shakeY * 3.f), 14, shade(cols[1], 0.5f));
  }
  // cloak: a shimmer at the edges, and the time left (or the recharge)
  if (cloakT > 0.f) {
    uint16_t cc = rgb(140, 230, 220);
    for (int i = 0; i < 6; i++) {
      int y = (int)fmodf(tNow * 90.f + i * 40.f, (float)H);
      cv.drawLine(0, y, 6, y + 8, cc); cv.drawLine(W - 1, H - y, W - 7, H - y - 8, cc);
    }
    cv.setTextColor(cc); cv.setCursor(118, 56);
    if (flyingGhost()) cv.print("CLOAKED"); else cv.printf("CLOAKED %ds", (int)cloakT + 1);
  } else if (cloakCD > 0.f && sm::capTier(sm::CAP_CLOAK)) {
    cv.setTextColor(rgb(80, 110, 110)); cv.setCursor(124, 56); cv.printf("CLOAK %ds", (int)cloakCD + 1);
  }
  // throttle slider, right edge: drag to set; a detent at cruise
  int ty = SLIDER_Y1 - (int)(throttleT * (SLIDER_Y1 - SLIDER_Y0));
  int cy = (SLIDER_Y0 + SLIDER_Y1) / 2;
  cv.drawRect(W - 12, SLIDER_Y0, 5, SLIDER_Y1 - SLIDER_Y0, rgb(40, 50, 60));
  cv.fillRect(W - 11, ty, 3, SLIDER_Y1 - ty, throttleT > 0.75f ? rgb(255, 170, 80) : rgb(70, 160, 170));
  cv.drawLine(W - 15, cy, W - 5, cy, rgb(90, 110, 120));
  cv.fillRect(W - 16, ty - 2, 13, 4, rgb(220, 230, 235));
  if (dockTarget >= 0) { cv.setTextColor(rgb(90, 160, 255)); cv.setCursor(W - 70, 15); cv.print("AUTODOCK"); }

  // guidance: the next beat of the trip, and the job's gate
  if (navObj >= 0 && objs[navObj].kind != K_NONE) drawEdgeArrow(objs[navObj], objs[navObj].kind == K_PORTAL ? rgb(210, 110, 230) : rgb(110, 240, 200));
  for (auto &o : objs) if (o.kind == K_GATE && (o.gflags & GF_JOB)) drawEdgeArrow(o, rgb(240, 200, 90));

  if (hitFlash > 0) {
    uint16_t c = rgb(200, 30, 20);
    cv.drawRect(0, 0, W, H, c); cv.drawRect(1, 1, W - 2, H - 2, c);
  }
  if (bannerUntil > millis()) drawBanner();
  else { cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(14, 229); cv.print(target >= 0 ? "tap a verb   A: next target" : "tilt: aim   slide: yaw/roll   tap: target"); }
}

// a ship's side view: the Mantis is the original sprite; the others are indexed art.
// scale 1 = full (156x83 box), 3 = a third (hangar thumbnails); centred in the box
static void drawShipArt(uint8_t t, int x, int y, int scale) {
  int bw = 156 / scale, bh = 83 / scale;
  if (t == sm::SHIP_MANTIS) {
    for (int j = 0; j < SHIP_ART_H; j += scale) for (int i = 0; i < SHIP_ART_W; i += scale) {
      uint16_t c = SHIP_ART[j * SHIP_ART_W + i];
      if (c) cv.drawPixel(x + (bw - SHIP_ART_W / scale) / 2 + i / scale, y + (bh - SHIP_ART_H / scale) / 2 + j / scale, c);
    }
    return;
  }
  const ShipArt *a = t == sm::SHIP_FALCOR ? &SHIP_FALCOR : t == sm::SHIP_HONEYBEE ? &SHIP_HONEYBEE : t == sm::SHIP_MALTESE ? &SHIP_MALTESE : &SHIP_GHOST;
  int ox = x + (bw - a->w / scale) / 2, oy = y + (bh - a->h / scale) / 2;
  for (int j = 0; j < a->h; j += scale) for (int i = 0; i < a->w; i += scale) {
    uint8_t ix = a->px[j * a->w + i];
    if (ix) cv.drawPixel(ox + i / scale, oy + j / scale, a->pal[ix]);
  }
}

// The fitted equipment, mounted on the ship's picture: half size, accents in the ship's livery.
static void drawMounts(uint8_t t, int x, int y) {
  int w = SHIP_ART_W, h = SHIP_ART_H;
  if (t != sm::SHIP_MANTIS) {
    const ShipArt *a = t == sm::SHIP_FALCOR ? &SHIP_FALCOR : t == sm::SHIP_HONEYBEE ? &SHIP_HONEYBEE : t == sm::SHIP_MALTESE ? &SHIP_MALTESE : &SHIP_GHOST;
    w = a->w; h = a->h;
  }
  int ox = x + (156 - w) / 2, oy = y + (83 - h) / 2;
  static const uint16_t accent[sm::SHIP_COUNT] = {0, rgb(40, 190, 210), rgb(122, 138, 78), rgb(214, 160, 60), rgb(150, 80, 220)};
  //                           weapons  shields mining  scanners trailer stabilz bulkhd  cloak  fuel
  static const float fx[9] = {0.80f, 0.50f, 0.93f, 0.30f, 0.10f, 0.03f, 0.f, 0.62f, 0.42f};
  static const float fy[9] = {0.72f, 0.04f, 0.86f, 0.02f, 0.86f, 0.25f, 0.f, 0.50f, 0.90f};
  for (int cap = 0; cap < sm::CAP_COUNT && cap < 9; cap++) {
    uint8_t mk = sm::capTier((sm::CapId)cap);
    if (!mk || cap == sm::CAP_BULKHEADS) continue;   // bulkheads are inside the hull
    const EquipArt &a = EQUIP[cap][equipStep(mk)];
    uint16_t pal[16];
    for (int i = 0; i < 16; i++) {
      pal[i] = a.pal[i];
      if (!accent[t]) continue;
      int r, g, b; unrgb(a.pal[i], r, g, b);
      int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      if (mx > 90 && mx - mn > 60 && r >= g && g > b) pal[i] = shade(accent[t], mx / 255.f * 1.1f);   // orange accents take the livery
    }
    int cx = ox + (int)(fx[cap] * w) - 12, cy = oy + (int)(fy[cap] * h) - 8;
    for (int j = 0; j < EQUIP_H; j += 2) for (int i = 0; i < EQUIP_W; i += 2) {
      uint8_t bb = a.px[(j * EQUIP_W + i) >> 1]; uint8_t ix = bb >> 4;
      int px = cx + i / 2, py = cy + j / 2;
      if (ix && px >= x && px < x + 156 && py >= y && py < y + 83) cv.drawPixel(px, py, pal[ix]);
    }
  }
}

static void drawEquip(int x, int y, uint8_t cap, uint8_t mark, bool dim) {
  const EquipArt &a = EQUIP[cap < 9 ? cap : 0][equipStep(mark)];
  for (int j = 0; j < EQUIP_H; j++)
    for (int i = 0; i < EQUIP_W; i++) {
      uint8_t b = a.px[(j * EQUIP_W + i) >> 1];
      uint8_t ix = (i & 1) ? (b & 15) : (b >> 4);
      if (ix) cv.drawPixel(x + i, y + j, dim ? shade(a.pal[ix], 0.45f) : a.pal[ix]);
    }
}

// The docking screen: a page on the left, the services down the right edge.
static void drawStation() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Contract &c = sm::contract();
  sm::Pilot &p = sm::sheet();
  const Obj *stn = dockedStation();
  uint8_t br = stn ? stn->uses : (uint8_t)BR_LIMINAR;
  const BrandLook &bl = brandLook(br);
  cv.fillRect(6, 4, 262, 206, rgb(5, 10, 16));
  cv.drawRoundRect(6, 4, 262, 206, 8, bl.accent == rgb(46, 74, 132) ? rgb(90, 130, 200) : bl.accent);
  // header: maker, kind, the ship's numbers
  static const int8_t brandLogo[5] = {LOGO_LIMINAR, -1, LOGO_MALTAPLEX, LOGO_DESERET, LOGO_FREEHOLD};
  int lg = br < 5 ? brandLogo[br] : -1, tx = 14;
  if (lg >= 0) { drawLogoFlat(12, 8, lg); tx = 40; }
  cv.setTextSize(2); cv.setTextColor(bl.light); cv.setCursor(tx, 9); cv.print(bl.name); cv.setTextSize(1);
  const char *kind = stn ? strchr(stn->name, ' ') : nullptr;
  cv.setTextColor(rgb(120, 134, 150)); cv.setCursor(tx, 26); cv.print(stn && strncmp(stn->name, "DEEP", 4) == 0 ? "DEEP OUTPOST" : (kind ? kind + 1 : "STATION"));
  cv.setTextColor(rgb(150, 160, 172)); cv.setCursor(12, 37);
  cv.printf("$%ld  H%d/%d  F%d/%d  HOLD %u/%u", (long)p.credits, p.hull, p.hullMax, p.fuel, p.fuelCap, p.holdUsed, p.holdCap);
  // services
  static const char *svc[4] = {"BOARD", "HANGAR", "MARKET", "SIGNAL"};
  for (int i = 0; i < svcCount(); i++) {
    bool on = stationPage == i;
    int SY = svcY(i), SH = svcH();
    uint16_t sig = rgb(200, 70, 200);
    cv.fillRoundRect(SVC_X, SY, SVC_W, SH, 6, on ? shade(i == 3 ? sig : bl.accent, 0.55f) : rgb(14, 20, 28));
    cv.drawRoundRect(SVC_X, SY, SVC_W, SH, 6, on ? bl.light : (i == 3 ? shade(sig, 0.6f) : rgb(50, 60, 72)));
    int cx = SVC_X + SVC_W / 2, cy = SY + SH / 2 - 8;
    uint16_t ic = on ? rgb(240, 245, 250) : rgb(130, 140, 150);
    if (i == 0) { for (int k = 0; k < 3; k++) cv.fillRect(cx - 10, cy - 8 + k * 6, 20, 3, ic); }                    // a list
    else if (i == 1) {                                                                                             // a turret, half size
      const EquipArt &a = EQUIP[sm::CAP_WEAPONS][0];
      for (int j = 0; j < EQUIP_H; j += 2) for (int k2 = 0; k2 < EQUIP_W; k2 += 2) {
        uint8_t b = a.px[(j * EQUIP_W + k2) >> 1]; uint8_t ix = b >> 4;
        if (ix) cv.drawPixel(cx - 12 + k2 / 2, cy - 9 + j / 2, on ? a.pal[ix] : shade(a.pal[ix], 0.55f));
      }
    }
    else if (i == 2) { cv.drawLine(cx - 11, cy + 6, cx - 4, cy - 2, ic); cv.drawLine(cx - 4, cy - 2, cx + 2, cy + 2, ic); cv.drawLine(cx + 2, cy + 2, cx + 11, cy - 8, ic); }   // a price line
    else { for (int k = 0; k < 3; k++) cv.drawCircle(cx, cy + 4, 4 + k * 4, on ? rgb(255, 160, 255) : rgb(150, 80, 150)); }   // a signal
    cv.setTextColor(on ? rgb(255, 255, 255) : rgb(140, 150, 160));
    cv.setCursor(cx - (int)strlen(svc[i]) * 3, SY + SH - 12); cv.print(svc[i]);
  }
  char dbuf[112] = "";
  if (stationPage == 0) {
    // ---------------- the board ----------------
    char rows[STATION_ROWS][44];
    int rc = refuelCost();
    if (rc > 0) snprintf(rows[0], 44, "REFUEL / REPAIR  %dcr", rc); else snprintf(rows[0], 44, "REFUEL / REPAIR  topped up");
    bool dueHere = dueHereNow();
    if (dueHere && sm::contractCargoGone()) snprintf(rows[1], 44, "CARGO GONE: %s (VOID)", c.title);
    else if (dueHere) snprintf(rows[1], 44, "DELIVER: %s +%d", c.title, c.pay);
    else if (c.live) snprintf(rows[1], 44, "DROP LEAD: %s", c.title);
    else snprintf(rows[1], 44, "WORK: %s +%d", off.title, off.pay);
    if (layer > 0) snprintf(rows[2], 44, "RUMORS: nobody sells names");
    else snprintf(rows[2], 44, "BUY A RUMOR  %dcr", rumorPrice());
    int sell = sellableValue(false);
    if (sell > 0) snprintf(rows[3], 44, "SELL ALL HAUL  +%dcr", sell);
    else if (p.holdUsed > 0) snprintf(rows[3], 44, "NOTHING THEY BUY HERE (%u ABOARD)", p.holdUsed);   // scans, your lead's cargo
    else snprintf(rows[3], 44, "HOLD EMPTY - SEE MARKET");
    if (opportunityTaken) snprintf(rows[4], 44, "BOARD: (taken)"); else snprintf(rows[4], 44, "BOARD: %s +%d", stationOpportunity.title, stationOpportunity.reward);
    snprintf(rows[5], 44, "LAUNCH");
    for (int i = 0; i < STATION_ROWS; ++i) {
      int y = ROW_Y0 + i * ROW_PITCH; bool sel = i == stationChoice;
      cv.fillRoundRect(12, y, 250, ROW_H, 4, sel ? rgb(25, 90, 90) : rgb(16, 24, 32));
      cv.setTextColor(i == 1 && dueHere ? rgb(255, 215, 110) : sel ? rgb(120, 255, 210) : rgb(180, 190, 200));
      cv.setCursor(20, y + 6); cv.print(rows[i]);
    }
    switch (stationChoice) {
      case 0: asciiCopy(dbuf, sizeof(dbuf), stationMoodText); break;
      case 1: {
        if (dueHere) { snprintf(dbuf, sizeof(dbuf), "the cargo is expected here. hand it over."); break; }
        const sm::Contract &j = c.live ? c : off;
        if (j.dest[0]) snprintf(dbuf, sizeof(dbuf), "to %s, depth %u.", j.dest, j.destDepth);
        else snprintf(dbuf, sizeof(dbuf), "%s", sm::contractHint(j));
        break;
      }
      case 2: snprintf(dbuf, sizeof(dbuf), "a place you haven't heard of. sets your course."); break;
      case 3: snprintf(dbuf, sizeof(dbuf), "%s", layer > 0 ? "rock and ore fetch double down here." : "everything your lead doesn't own."); break;
      case 4: asciiCopy(dbuf, sizeof(dbuf), opportunityTaken ? "" : stationOpportunity.detail); break;
      case 5: snprintf(dbuf, sizeof(dbuf), "back out among the gates"); break;
    }
  } else if (stationPage == 3) {
    // ---------------- signals: commission a portal, open the gate ----------------
    net::Phase ph = net::phase();
    bool commissioned = ph != net::PH_OFF && ph != net::PH_LOST;
    const char *rows[3]; char r0[44], r1[44];
    snprintf(r0, sizeof(r0), commissioned ? "PORTAL COMMISSIONED" : "COMMISSION A PORTAL  %dcr", signalsFee()); rows[0] = r0;
    snprintf(r1, sizeof(r1), "OPEN GATE"); rows[1] = r1; rows[2] = "CANCEL SIGNAL";
    for (int i = 0; i < 3; i++) {
      int y = 52 + i * 28; bool ready = i == 1 && ph == net::PH_READY;
      uint16_t bg = i == 1 ? (ready ? rgb(30, 110, 50) : rgb(26, 30, 36)) : i == 0 ? (commissioned ? rgb(40, 30, 50) : rgb(70, 24, 70)) : rgb(30, 20, 24);
      cv.fillRoundRect(12, y, 250, 22, 5, bg);
      cv.drawRoundRect(12, y, 250, 22, 5, i == 1 ? (ready ? rgb(140, 255, 160) : rgb(60, 66, 72)) : rgb(150, 70, 150));
      cv.setTextColor(i == 1 && !ready ? rgb(90, 96, 104) : rgb(240, 230, 245)); cv.setCursor(22, y + 7); cv.print(rows[i]);
    }
    cv.setTextColor(rgb(200, 150, 210)); cv.setCursor(14, 140);
    if (!commissioned) cv.print("EXPERIMENTAL: fold two skies together.");
    else if (ph == net::PH_SEEKING) { cv.print("seeking another commissioned pilot"); for (int k = 0; k < ((int)(tNow * 2) % 4); k++) cv.print("."); }
    else {
      cv.printf("PAIRED: %s  (%s)", net::peerName(), sm::shipSpec(net::peerShip()).name);
      cv.setCursor(14, 152); cv.setTextColor(rgb(160, 140, 180));
      cv.print(net::role() == net::ROLE_ANCHOR ? "you hold the anchor: the sky follows you" : "you follow their anchor");
    }
    cv.setTextColor(rgb(120, 110, 130)); cv.setCursor(14, 168); cv.print("both pilots commission; the gate turns");
    cv.setCursor(14, 178); cv.print("green when the skies line up.");
    snprintf(dbuf, sizeof(dbuf), "fly it, then stay together. leave by any gate.");
  } else if (stationPage == 1) {
    // ---------------- the hangar ----------------
    static const char *tabs[2] = {"EQUIPMENT", "SHIPS"};
    for (int t = 0; t < 2; t++) {
      int x = 12 + t * 126; bool on = hangarTab == t;
      cv.fillRoundRect(x, 47, 122, 14, 4, on ? shade(bl.accent, 0.5f) : rgb(14, 20, 28));
      cv.setTextColor(on ? rgb(255, 255, 255) : rgb(130, 140, 150)); cv.setCursor(x + (122 - (int)strlen(tabs[t]) * 6) / 2, 50); cv.print(tabs[t]);
    }
    if (hangarTab == 1) {
      // ---- ships: swap any you own, recover the lost, buy what this dock sells ----
      for (int t = 0; t < sm::SHIP_COUNT; t++) {
        const sm::ShipRecord &r = p.ships[t]; const sm::ShipSpec &sp = sm::shipSpec(t);
        bool secret = t == sm::SHIP_GHOST && !r.owned;
        int y = 64 + t * 25; bool sel = t == shipSel;
        cv.fillRoundRect(10, y, 254, 23, 4, sel ? rgb(18, 40, 46) : rgb(12, 18, 26));
        if (!secret) drawShipArt((uint8_t)t, 12, y - 3, 3);
        cv.setTextColor(secret ? rgb(90, 80, 110) : sel ? rgb(120, 255, 210) : rgb(220, 226, 232));
        cv.setCursor(68, y + 3); cv.print(secret ? "???" : sp.name);
        cv.setTextColor(rgb(110, 124, 134)); cv.setCursor(68, y + 13); cv.print(secret ? "rumoured. not for sale." : sp.maker);
        char act[24]; uint16_t ac = rgb(180, 190, 200);
        if (t == p.activeShip) { snprintf(act, sizeof(act), "ACTIVE"); ac = rgb(150, 225, 30); }
        else if (r.owned && r.lost) { snprintf(act, sizeof(act), "REBUILD %ld", (long)sm::shipRecoverFee((uint8_t)t)); ac = rgb(255, 160, 90); }
        else if (r.owned) { snprintf(act, sizeof(act), "SWAP"); ac = rgb(120, 220, 255); }
        else if (secret) snprintf(act, sizeof(act), " ");
        else if (shipSoldHere((uint8_t)t)) { snprintf(act, sizeof(act), "BUY %ld", (long)sp.price); ac = p.credits >= sp.price ? rgb(255, 215, 110) : rgb(200, 90, 80); }
        else snprintf(act, sizeof(act), "NOT SOLD HERE");
        cv.setTextColor(ac); cv.setCursor(260 - (int)strlen(act) * 6, y + 8); cv.print(act);
      }
      const sm::ShipSpec &sp = sm::shipSpec(shipSel);
      if (shipSel == sm::SHIP_GHOST && !p.ships[shipSel].owned) snprintf(dbuf, sizeof(dbuf), "the ghost fleet keeps it for those who bring them what they want.");
      else snprintf(dbuf, sizeof(dbuf), "%s. speed x%.2f  turn x%.2f  hold %u.", sp.role, sp.speed, sp.turn, sp.holdBase);
    } else {
    if (!hangarN) snprintf(dbuf, sizeof(dbuf), "nothing on the racks today.");
    for (int i = 0; i < hangarN; i++) {
      const HangarOffer &h = hangar[i];
      int x = 12 + i * 84, y = 64, w = 80, hh = 124;
      bool sel = i == hangarSel, owned = h.fitted || sm::capTier((sm::CapId)h.cap) >= h.mark;
      cv.fillRoundRect(x, y, w, hh, 5, sel ? rgb(18, 40, 46) : rgb(12, 18, 26));
      cv.drawRoundRect(x, y, w, hh, 5, sel ? bl.light : rgb(48, 58, 70));
      drawEquip(x + (w - EQUIP_W) / 2, y + 6, h.cap, h.mark, owned);
      const char *nm = equipName(h.cap, h.mark);
      char l1[16], l2[16]; l1[0] = l2[0] = 0;
      const char *sp = strlen(nm) > 13 ? strrchr(nm, ' ') : nullptr;
      if (sp) { snprintf(l1, sizeof(l1), "%.*s", (int)(sp - nm), nm); snprintf(l2, sizeof(l2), "%s", sp + 1); } else snprintf(l1, sizeof(l1), "%s", nm);
      cv.setTextColor(owned ? rgb(110, 120, 130) : rgb(225, 230, 235));
      cv.setCursor(x + (w - (int)strlen(l1) * 6) / 2, y + 42); cv.print(l1);
      cv.setCursor(x + (w - (int)strlen(l2) * 6) / 2, y + 51); cv.print(l2);
      cv.setTextSize(2); cv.setTextColor(owned ? rgb(90, 100, 110) : bl.light);
      char mk[8]; snprintf(mk, sizeof(mk), "MK%u", h.mark);
      cv.setCursor(x + (w - (int)strlen(mk) * 12) / 2, y + 62); cv.print(mk); cv.setTextSize(1);
      char eff[24]; equipEffect(h.cap, h.mark, eff, sizeof(eff));
      cv.setTextColor(owned ? rgb(90, 100, 110) : rgb(150, 220, 200));
      printWrapped(x + 4, y + 82, 12, 2, 10, eff);
      cv.setTextColor(owned ? rgb(90, 160, 110) : (p.credits >= h.price ? rgb(255, 215, 110) : rgb(200, 90, 80)));
      char pr[16]; if (owned) snprintf(pr, sizeof(pr), "FITTED"); else snprintf(pr, sizeof(pr), "%dcr", h.price);
      cv.setCursor(x + (w - (int)strlen(pr) * 6) / 2, y + hh - 14); cv.print(pr);
    }
    if (hangarSel < hangarN) {
      const HangarOffer &h = hangar[hangarSel];
      char cn[16]; snprintf(cn, sizeof(cn), "%s", sm::capName((sm::CapId)h.cap)); upcase(cn);
      snprintf(dbuf, sizeof(dbuf), "%s: fitted MK%u. tap again to fit MK%u.", cn, sm::capTier((sm::CapId)h.cap), h.mark);
    }
    }
  } else {
    // ---------------- the market ----------------
    cv.setTextColor(rgb(110, 124, 134)); cv.setCursor(30, 49); cv.print("GOOD           BUY  SELL  HOLD");
    for (int g = 0; g < sm::G_COUNT; g++) {
      int y = 60 + g * 15; bool sel = g == marketSel, trades = sm::marketTrades((sm::Good)g);
      if (sel) cv.fillRect(10, y - 2, 254, 13, rgb(20, 60, 66));
      cv.setTextColor(!trades ? rgb(70, 76, 84) : sel ? rgb(120, 255, 210) : rgb(200, 206, 212));
      for (int j = 0; j < 14; j++) for (int i = 0; i < 14; i++) { uint16_t ic = GOOD_ICONS[g][j * 14 + i]; if (ic) cv.drawPixel(12 + i, y - 3 + j, trades ? ic : shade(ic, 0.4f)); }
      cv.setCursor(30, y); cv.print(sm::goodName((sm::Good)g));
      int8_t mood = sm::marketMood((sm::Good)g);
      if (trades) {
        cv.setTextColor(mood < 0 ? rgb(130, 230, 150) : rgb(200, 206, 212)); cv.setCursor(116, y); cv.printf("%4d", sm::marketBuy((sm::Good)g));
        cv.setTextColor(mood > 0 ? rgb(255, 210, 110) : rgb(200, 206, 212)); cv.setCursor(152, y); cv.printf("%4d", sm::marketSell((sm::Good)g));
      } else { cv.setCursor(128, y); cv.print("--    --"); }
      int have = sm::haulCount(sm::goodHold((sm::Good)g));
      cv.setTextColor(have ? rgb(220, 226, 232) : rgb(70, 76, 84)); cv.setCursor(194, y); cv.printf("%3d", have);
      if (mood > 0 && trades) { cv.setTextColor(rgb(255, 210, 110)); cv.setCursor(222, y); cv.print("NEED"); }
      else if (mood < 0 && trades) { cv.setTextColor(rgb(130, 230, 150)); cv.setCursor(222, y); cv.print("MAKE"); }
    }
    static const char *chip[4] = {"BUY 1", "BUY 5", "SELL 1", "SELL ALL"};
    for (int i = 0; i < 4; i++) {
      int x = 12 + i * 63;
      cv.fillRoundRect(x, 182, 59, 18, 4, i < 2 ? rgb(20, 56, 40) : rgb(60, 40, 18));
      cv.drawRoundRect(x, 182, 59, 18, 4, i < 2 ? rgb(90, 200, 130) : rgb(230, 170, 70));
      cv.setTextColor(rgb(235, 240, 240)); cv.setCursor(x + (59 - (int)strlen(chip[i]) * 6) / 2, 187); cv.print(chip[i]);
    }
    snprintf(dbuf, sizeof(dbuf), "%s", layer > 0 ? "deep prices: rock and scans sell best" : "MAKE: cheap here  NEED: premium");
  }
  cv.setTextColor(rgb(200, 180, 110));
  printWrapped(12, stationPage == 2 ? 202 : ROW_Y0 + STATION_ROWS * ROW_PITCH + 2, 42, stationPage == 2 ? 1 : 2, 10, dbuf);
  if (bannerUntil > millis()) drawBanner();
  else {
    static const char *hint[3] = {"tap row, tap again   A launch  C next", "tap a card, tap again to fit   A launch", "tap a good, then a button   A launch"};
    cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(6, 229); cv.print(hint[stationPage]);
  }
}

static void drawDockSequence() {
  // the slot swallows the view, lights streaming past
  cv.fillSprite(rgb(2, 3, 6));
  for (int i = 0; i < 9; i++) {
    float z = fmodf(i * 0.11f + tNow * 1.6f, 1.f);
    int w = (int)(30 + z * z * 330), h = w / 3;
    cv.drawRect(160 - w / 2, 120 - h / 2, w, h, shade(rgb(90, 170, 255), 0.3f + z * 0.7f));
  }
  for (int i = 0; i < 6; i++)
    cv.fillRect(70 + i * 36, 150, 4, 2, (((int)(tNow * 8) + i) & 1) ? rgb(255, 200, 90) : rgb(80, 60, 30));
  cv.setTextColor(rgb(120, 180, 255)); cv.setCursor(139, 200); cv.print("DOCKING");
}



static void drawMantisBodyIcon(int ox, int oy) {
  for (int y = 0; y < MANTIS_BODY_H; y++) {
    for (int x = 0; x < MANTIS_BODY_W; x++) {
      uint16_t c = MANTIS_BODY_ICON[y * MANTIS_BODY_W + x];
      if (c) cv.drawPixel(ox + x, oy + y, c);
    }
  }
}

static void drawBoot() {
  cv.fillRect(10, 28, 300, 184, rgb(3, 5, 12));
  cv.drawRoundRect(10, 28, 300, 184, 8, rgb(70, 120, 140));
  int ix = 22;
  int iy = 40 + (160 - MANTIS_BODY_H) / 2;
  if (iy < 36) iy = 36;
  drawMantisBodyIcon(ix, iy);
  cv.setTextSize(2);
  cv.setTextColor(rgb(140, 220, 210));
  cv.setCursor(118, 72);
  cv.print("SpaceMantis");
  cv.setTextSize(1);
  cv.setTextColor(rgb(180, 190, 200));
  cv.setCursor(118, 100);
  cv.print("by BasaltSoftWorks");
  cv.setTextColor(rgb(100, 120, 130));
  cv.setCursor(118, 130);
  cv.print("lost in space");
  cv.setCursor(22, 190);
  cv.print("tap to continue");
  cv.setTextColor(rgb(90, 104, 116));
  cv.setCursor(118, 150);
  cv.print("hold B + C: new game");
  cv.setCursor(118, 160);
  cv.setTextColor(signalsOn() ? rgb(230, 90, 230) : rgb(90, 104, 116));
  cv.print(signalsOn() ? "hold A + C: signals ON" : "hold A + C: signals (exp.)");
  if (signalsHold > 0.f || signalsDone > 0.f) {
    float u = signalsDone > 0.f ? 1.f : clampf(signalsHold / 2.0f, 0.f, 1.f);
    cv.drawRect(118, 172, 180, 8, rgb(60, 70, 80));
    cv.fillRect(119, 173, (int)(178 * u), 6, rgb(200, 70, 200));
    cv.setTextColor(rgb(230, 150, 230)); cv.setCursor(118, 182);
    cv.print(signalsDone > 0.f ? (signalsOn() ? "signals on: see the dock" : "signals off") : "keep holding");
  }
  if (newGameHold > 0.f || newGameDone > 0.f) {
    float u = newGameDone > 0.f ? 1.f : clampf(newGameHold / 2.0f, 0.f, 1.f);
    bool red = u >= 0.75f;
    cv.drawRect(118, 172, 180, 8, rgb(60, 70, 80));
    cv.fillRect(119, 173, (int)(178 * u), 6, red ? rgb(230, 50, 40) : rgb(0, 115, 115));
    cv.setTextColor(red ? rgb(255, 120, 100) : rgb(150, 200, 200));
    cv.setCursor(118, 182);
    cv.print(newGameDone > 0.f ? "save cleared. new pilot." : (red ? "clearing the saved game..." : "keep holding"));
  }
}

static void drawMantisPodIcon(int ox, int oy) {
  for (int y = 0; y < MANTIS_POD_H; y++) {
    for (int x = 0; x < MANTIS_POD_W; x++) {
      uint16_t c = MANTIS_POD_ICON[y * MANTIS_POD_W + x];
      if (c) cv.drawPixel(ox + x, oy + y, c);
    }
  }
}

static void drawLost() {
  cv.fillRect(10, 28, 300, 184, rgb(3, 5, 12));
  cv.drawRoundRect(10, 28, 300, 184, 8, rgb(70, 120, 140));
  // icon left; copy right — clean conversion, black skipped
  drawMantisPodIcon(22, 52);
  cv.setTextSize(1);
  cv.setTextColor(rgb(140, 190, 200));
  cv.setCursor(104, 48);
  cv.print("ESCAPE POD RECOVERED");
  cv.setTextColor(rgb(210, 220, 230));
  static const char *lines[] = {
    "Your escape pod was",
    "recovered somewhere",
    "far away.",
    "",
    "You continue to wander,",
    "lost in space..."
  };
  for (int i = 0; i < 6; i++) {
    cv.setCursor(104, 70 + i * 14);
    cv.print(lines[i]);
  }
  cv.setTextColor(rgb(100, 120, 130));
  cv.setCursor(22, 190);
  cv.printf("life %lu   tap to continue", (unsigned long)sm::sheet().lives);
}


// ============================================================
//  status: pilot license (survives the pod) + ship diagnostic (this hull)
//  A visual reference only — nothing here can be acted on.
// ============================================================
static void drawSprite565(const uint16_t *px, int w, int h, int ox, int oy) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint16_t c = px[y * w + x];
      if (c) cv.drawPixel(ox + x, oy + y, c);
    }
}

static void statusBar(int x, int y, int w, int h, int v, int mx, uint16_t c) {
  cv.drawRect(x, y, w, h, rgb(40, 50, 60));
  int k = mx > 0 ? (w - 2) * clampf((float)v / mx, 0, 1) : 0;
  if (k > 0) cv.fillRect(x + 1, y + 1, k, h - 2, c);
}

static void statusPips(int x, int y, int lit, int n, uint16_t on) {
  for (int i = 0; i < n; i++) cv.fillRect(x + i * 5, y, 4, 5, i < lit ? on : rgb(40, 46, 56));
}

static void drawStatus() {
  const sm::Pilot &p = sm::sheet();
  const uint16_t TEAL = rgb(0, 115, 115), TEAL_D = rgb(0, 52, 56), TEAL_L = rgb(60, 170, 160);
  const uint16_t MAG = rgb(93, 0, 93), MAG_L = rgb(150, 50, 150);
  const uint16_t LIME = rgb(150, 225, 30), LIME_L = rgb(210, 255, 120), LILAC = rgb(185, 160, 255);
  const uint16_t TXT = rgb(205, 215, 220), DIM = rgb(110, 124, 134);
  char callsign[16];
  snprintf(callsign, sizeof(callsign), "MANTIS-%04X", (unsigned)((ESP.getEfuseMac() >> 24) & 0xFFFF));
  int charted = 0;
  for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && sm::landmarkDiscovered(sm::landmarkAt(i)->id)) charted++;

  cv.fillSprite(rgb(4, 6, 12));
  cv.setTextSize(1);
  // ---- pilot license: these records ride in the escape pod ----
  cv.fillRoundRect(3, 3, 314, 106, 6, rgb(6, 14, 18));
  cv.drawRoundRect(3, 3, 314, 106, 6, TEAL);
  cv.fillRect(4, 4, 312, 12, MAG);
  cv.setTextColor(LIME_L); cv.setCursor(9, 6); cv.print("LIMINAR TRANSIT - PILOT LICENSE");
  cv.setTextColor(rgb(230, 190, 230)); cv.setCursor(252, 6); cv.print("POD RECORD");
  cv.fillRect(8, 20, 76, 78, TEAL_D); cv.drawRect(8, 20, 76, 78, TEAL_L);
  drawSprite565(PILOT_HELMET, PILOT_HELMET_W, PILOT_HELMET_H, 10, 22);
  cv.setTextColor(LIME); cv.setCursor(8, 100); cv.print(callsign);
  int x = 92;
  cv.setTextColor(DIM); cv.setCursor(x, 21); cv.print("CALLSIGN");
  cv.setTextColor(TXT); cv.setCursor(x + 54, 21); cv.print(callsign);
  cv.setTextColor(DIM); cv.setCursor(x, 32); cv.print("LIFE");
  cv.setTextColor(TXT); cv.setCursor(x + 54, 32); cv.printf("#%lu", (unsigned long)(p.lives + 1));
  cv.setTextColor(DIM); cv.setCursor(x + 96, 32); cv.print("BANK");
  cv.setTextColor(LIME); cv.setCursor(x + 124, 32); cv.printf("%ld cr", (long)p.credits);
  cv.setTextColor(DIM); cv.setCursor(x, 43); cv.print("FIXED POINTS");
  cv.setTextColor(LILAC); cv.setCursor(x + 78, 43); cv.printf("%d / %d", charted, sm::landmarkCount());
  cv.drawLine(x, 54, 311, 54, TEAL_D);
  static const char *careers[] = {"HAULER", "GUN HAND", "PROSPECTOR", "RESCUER", "TRADER", "WANDERER", "DEPTH RUNNER", "GHOST"};
  // strongest careers first, two columns of four
  int ord[sm::CR_COUNT];
  for (int i = 0; i < sm::CR_COUNT; i++) ord[i] = i;
  for (int i = 1; i < sm::CR_COUNT; i++) {
    int k = ord[i], j = i - 1;
    while (j >= 0 && p.rank[ord[j]] < p.rank[k]) { ord[j + 1] = ord[j]; j--; }
    ord[j + 1] = k;
  }
  for (int i = 0; i < sm::CR_COUNT && i < 8; i++) {
    int c = ord[i], r = p.rank[c];
    int cx = x + (i / 4) * 112, cy = 58 + (i % 4) * 11;
    cv.setTextColor(r ? TXT : DIM); cv.setCursor(cx, cy); cv.print(careers[c]);
    statusPips(cx + 74, cy + 1, (r + 4) / 5, 4, LIME);   // a pip per five ranks (ranks run to 20)
    if (r) { cv.setTextColor(DIM); cv.setCursor(cx + 96, cy); cv.printf("%d", r); }
  }

  // ---- ship diagnostic: this hull, lost with it ----
  cv.fillRoundRect(3, 112, 314, 125, 6, rgb(8, 6, 14));
  cv.drawRoundRect(3, 112, 314, 125, 6, MAG_L);
  cv.fillRect(4, 113, 312, 12, TEAL_D);
  cv.setTextColor(TEAL_L); cv.setCursor(9, 115); cv.printf("SHIP DIAGNOSTIC - %s", flying().name);
  {   // what happens to this hull if it is lost
    const sm::Pilot &pp = sm::sheet();
    bool lic = pp.activeShip == sm::SHIP_MANTIS, rec = pp.ships[pp.activeShip].backup;
    const char *t = lic ? "LICENSE SHIP" : rec ? "ON RECORD" : "NO RECORD";
    cv.setTextColor(lic || rec ? DIM : rgb(255, 120, 90)); cv.setCursor(314 - (int)strlen(t) * 6, 115); cv.print(t);
  }
  for (int gx = 8; gx < 170; gx += 12) cv.drawLine(gx, 128, gx, 222, rgb(16, 22, 30));
  for (int gy = 128; gy < 224; gy += 12) cv.drawLine(8, gy, 170, gy, rgb(16, 22, 30));
  drawShipArt(sm::sheet().activeShip, 10, 134, 1);
  drawMounts(sm::sheet().activeShip, 10, 134);
  int X = 176;
  struct G { const char *l; int v, mx; uint16_t c; } gs[] = {
    {"HULL", p.hull, p.hullMax, TEAL_L}, {"FUEL", p.fuel, p.fuelCap, LIME}, {"HOLD", p.holdUsed, p.holdCap, MAG_L}};
  for (int i = 0; i < 3; i++) {
    bool low = i < 2 && gs[i].v * 4 < gs[i].mx;
    cv.setTextColor(DIM); cv.setCursor(X, 130 + i * 14); cv.print(gs[i].l);
    statusBar(X + 28, 131 + i * 14, 80, 6, gs[i].v, gs[i].mx, low ? rgb(255, 120, 70) : gs[i].c);
    cv.setTextColor(TXT); cv.setCursor(X + 112, 130 + i * 14); cv.printf("%d", gs[i].v);
  }
  // depth rating: which layers this hull is rated to reach without glitching
  int safe = sm::depthQuery(4).maxBand; if (safe > 4) safe = 4;
  cv.setTextColor(DIM); cv.setCursor(X, 174); cv.print("RATED");
  statusPips(X + 34, 175, safe, 4, LILAC);
  cv.setTextColor(LILAC); cv.setCursor(X + 56, 174); cv.print(layerName(safe));
  static const char *capShort[] = {"WEAPONS", "SHIELDS", "MINING", "SCANNER", "TRAILER", "STABILZ", "BULKHD", "CLOAK", "FUELSYS"};
  for (int i = 0; i < sm::CAP_COUNT && i < 10; i++) {
    int v = p.cap[i];
    int cx = X + (i % 2) * 70, cy = 186 + (i / 2) * 9;
    cv.setTextColor(v ? TXT : DIM); cv.setCursor(cx, cy); cv.print(capShort[i]);
    cv.setTextColor(v ? LIME : DIM); cv.setCursor(cx + 46, cy);
    if (v) cv.printf("MK%d", v); else cv.print("--");
  }
  cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(10, 226); cv.print("tap: close");
  cv.setTextColor(rgb(110, 130, 140)); cv.setCursor(82, 226); cv.print("hold C: journal");
}

// ============================================================
//  journal + active lead (status page 2, hold C)
// ============================================================
static void drawJournal() {
  const uint16_t TEAL = rgb(0, 115, 115), TEAL_D = rgb(0, 52, 56), TEAL_L = rgb(60, 170, 160), MAG = rgb(93, 0, 93);
  const uint16_t LIME_L = rgb(210, 255, 120), GOLD = rgb(240, 200, 90), TXT = rgb(205, 215, 220), DIM = rgb(110, 124, 134);
  cv.fillSprite(rgb(4, 6, 12));
  cv.setTextSize(1);
  // the active lead
  cv.fillRoundRect(3, 3, 314, 62, 6, rgb(10, 10, 8));
  cv.drawRoundRect(3, 3, 314, 62, 6, GOLD);
  cv.fillRect(4, 4, 312, 12, rgb(70, 56, 16));
  cv.setTextColor(rgb(255, 230, 150)); cv.setCursor(9, 6); cv.print("ACTIVE LEAD");
  const sm::Contract &c = sm::contract();
  const sm::Trip &tr = sm::trip();
  if (c.live) {
    cv.setTextColor(GOLD); cv.setCursor(9, 20); cv.printf("%s", c.title);
    cv.setTextColor(LIME_L); cv.setCursor(240, 20); cv.printf("+%dcr", c.pay);
    cv.setTextColor(TXT); cv.setCursor(9, 31);
    if (c.dest[0]) cv.printf("to %s, depth %u", c.dest, c.destDepth); else cv.print(sm::contractHint(c));
    cv.setTextColor(DIM); cv.setCursor(9, 42); cv.printf("progress %u/%u   %s", c.progress, c.need, c.dest[0] ? "find its gate and fly it" : "");
  } else { cv.setTextColor(DIM); cv.setCursor(9, 24); cv.print("no lead. the boards are always hiring."); }
  if (tr.active) { cv.setTextColor(TEAL_L); cv.setCursor(9, 53); cv.printf("COURSE %s  depth %u  %s", tr.dest, tr.destDepth, tr.ascending ? "climbing" : "diving"); }
  // the journal: newest first
  cv.fillRoundRect(3, 68, 314, 168, 6, rgb(6, 10, 14));
  cv.drawRoundRect(3, 68, 314, 168, 6, TEAL);
  cv.fillRect(4, 69, 312, 12, MAG);
  cv.setTextColor(LIME_L); cv.setCursor(9, 71); cv.print("PILOT JOURNAL");
  cv.setTextColor(rgb(230, 190, 230)); cv.setCursor(252, 71); cv.print("POD RECORD");
  int y = 85;
  for (int k = 0; y < 214; k++) {
    const sm::JournalEntry *e = sm::journalNewest(k);
    if (!e) { if (k == 0) { cv.setTextColor(DIM); cv.setCursor(9, y); cv.print("nothing written yet."); } break; }
    cv.setTextColor(k == 0 ? TEAL_L : TEAL_D); cv.setCursor(9, y); cv.printf("L%u", e->life);
    cv.setTextColor(k == 0 ? TXT : DIM);
    printWrapped(33, y, 46, 2, 10, e->text);
    y += (strlen(e->text) > 46 ? 21 : 11);
  }
  cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(10, 226); cv.print("tap to close");
  cv.setTextColor(rgb(110, 130, 140)); cv.setCursor(226, 226); cv.print("hold C: license");
}

// ============================================================
//  the atlas: subspace memory drawn as "the deep is small"
//  Rings are layers (real space outside, the cove at the centre). Places you
//  have been sit on the rim, in network order; places only seen as gates sit
//  just outside; fixed points sit on their own rings. No coordinates, ever.
// ============================================================
static const float MAP_CX = 160.f, MAP_CY = 116.f, MAP_SX = 1.22f, MAP_SY = 0.80f;
static const float MAP_R[5] = {96, 74, 54, 35, 16};
static float mapX[sm::ATLAS_PLACES], mapY[sm::ATLAS_PLACES], mapAng[sm::ATLAS_PLACES];
static uint8_t mapRole[sm::ATLAS_PLACES];   // 0 hidden, 1 rim, 2 outer, 3 fixed

static uint16_t depthCol(int d) {
  switch (d) { case 1: return rgb(80, 200, 215); case 2: return rgb(140, 110, 230); case 3: return rgb(205, 70, 190); default: return rgb(150, 225, 30); }
}

static void layoutMap() {
  sm::Atlas &a = sm::atlas();
  int vis[sm::ATLAS_PLACES], nv = 0;
  for (int k = 0; k < sm::ATLAS_PLACES; k++) {
    mapRole[k] = 0;
    if ((a.place[k].flags & sm::AP_USED) && (a.place[k].flags & sm::AP_VISITED) && !(a.place[k].flags & sm::AP_FIXED)) vis[nv++] = k;
  }
  for (int i = 1; i < nv; i++) { int v = vis[i], j = i - 1; while (j >= 0 && a.place[vis[j]].lastSeen < a.place[v].lastSeen) { vis[j + 1] = vis[j]; j--; } vis[j + 1] = v; }
  if (nv > sm::ATLAS_VISITED_KEEP) nv = sm::ATLAS_VISITED_KEEP;
  bool inSet[sm::ATLAS_PLACES] = {false}, done[sm::ATLAS_PLACES] = {false};
  for (int i = 0; i < nv; i++) inSet[vis[i]] = true;
  // ring order: walk the network from here so linked places sit side by side
  int order[sm::ATLAS_PLACES], no = 0, stack[sm::ATLAS_PLACES * 2], sp = 0;
  int start = (a.here != 255 && inSet[a.here]) ? a.here : (nv ? vis[0] : -1);
  if (start >= 0) stack[sp++] = start;
  while (sp > 0 || no < nv) {
    if (sp == 0) { for (int i = 0; i < nv; i++) if (!done[vis[i]]) { stack[sp++] = vis[i]; break; } if (sp == 0) break; }
    int c = stack[--sp];
    if (done[c]) continue;
    done[c] = true; order[no++] = c;
    for (auto &l : a.link) {
      if (!(l.flags & sm::AL_USED)) continue;
      int o = l.a == c ? l.b : (l.b == c ? l.a : -1);
      if (o >= 0 && inSet[o] && !done[o] && sp < sm::ATLAS_PLACES * 2) stack[sp++] = o;
    }
  }
  for (int i = 0; i < no; i++) {
    int k = order[i];
    mapAng[k] = i * 6.2831853f / (no > 0 ? no : 1);
    mapX[k] = MAP_CX + MAP_R[0] * MAP_SX * cosf(mapAng[k]); mapY[k] = MAP_CY + MAP_R[0] * MAP_SY * sinf(mapAng[k]); mapRole[k] = 1;
  }
  // names seen as gates (and rumors) sit just outside the place they hang from
  int perAnchor[sm::ATLAS_PLACES] = {0};
  static const float off[] = {0.24f, -0.24f, 0.46f, -0.46f, 0.66f, -0.66f};
  for (int k = 0; k < sm::ATLAS_PLACES; k++) {
    const sm::AtlasPlace &p = a.place[k];
    if (!(p.flags & sm::AP_USED) || mapRole[k] || (p.flags & sm::AP_VISITED)) continue;
    int anchor = -1;
    for (auto &l : a.link) {
      if (!(l.flags & sm::AL_USED)) continue;
      int o = l.a == k ? l.b : (l.b == k ? l.a : -1);
      if (o >= 0 && mapRole[o] == 1) { anchor = o; break; }
    }
    if (anchor < 0) continue;
    bool fixed = p.flags & sm::AP_FIXED;
    int n = perAnchor[anchor]++;
    float ang = mapAng[anchor] + off[n % 6] * (fixed ? 0.8f : 1.f);
    float r = fixed ? MAP_R[p.depth > 4 ? 4 : p.depth] : MAP_R[0] + 14.f;
    mapAng[k] = ang;
    mapX[k] = MAP_CX + r * MAP_SX * cosf(ang); mapY[k] = MAP_CY + r * MAP_SY * sinf(ang);
    mapRole[k] = fixed ? 3 : 2;
  }
  // fixed points with no surface thread this life still float on their rings
  int loose = 0;
  for (int k = 0; k < sm::ATLAS_PLACES; k++) {
    const sm::AtlasPlace &p = a.place[k];
    if (!(p.flags & sm::AP_USED) || mapRole[k] || !(p.flags & sm::AP_FIXED)) continue;
    float ang = 0.7f + loose++ * 1.3f;
    float r = MAP_R[p.depth > 4 ? 4 : p.depth];
    mapAng[k] = ang; mapX[k] = MAP_CX + r * MAP_SX * cosf(ang); mapY[k] = MAP_CY + r * MAP_SY * sinf(ang); mapRole[k] = 3;
  }
}

static void mapDashed(float x0, float y0, float x1, float y1, uint16_t c, int on, int offp) {
  float L = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  if (L < 1) return;
  for (float s = 0; s < L; s += on + offp) {
    float e = fminf(L, s + on);
    cv.drawLine((int)(x0 + (x1 - x0) * s / L), (int)(y0 + (y1 - y0) * s / L), (int)(x0 + (x1 - x0) * e / L), (int)(y0 + (y1 - y0) * e / L), c);
  }
}

// a lane between two rim places bows inward as deep as it goes
static void mapArc(int a, int b, int depth, uint16_t col, int style, int width, int via = -1) {
  float x0 = mapX[a], y0 = mapY[a], x1 = mapX[b], y1 = mapY[b];
  bool throughHub = via >= 0 && via < sm::ATLAS_PLACES && mapRole[via];
  bool rimPair = (mapRole[a] == 1 && mapRole[b] == 1) || throughHub;
  float px = 0, py = 0;
  int steps = rimPair ? 16 : 1;
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  if (throughHub) { cx = 2.f * mapX[via] - cx; cy = 2.f * mapY[via] - cy; }   // the curve passes the hub
  else if (rimPair) {
    float m = atan2f((cy - MAP_CY) / MAP_SY, (cx - MAP_CX) / MAP_SX);
    float span = fabsf(fmodf(mapAng[a] - mapAng[b] + 9.42477796f, 6.2831853f) - 3.1415927f);
    float rr = span > 0.5f ? MAP_R[depth > 4 ? 4 : depth] : MAP_R[0] - 10.f * depth;
    cx = MAP_CX + rr * MAP_SX * cosf(m); cy = MAP_CY + rr * MAP_SY * sinf(m);
  }
  for (int k = 0; k <= steps; k++) {
    float u = (float)k / steps;
    float x = rimPair ? (1 - u) * (1 - u) * x0 + 2 * u * (1 - u) * cx + u * u * x1 : x0 + (x1 - x0) * u;
    float y = rimPair ? (1 - u) * (1 - u) * y0 + 2 * u * (1 - u) * cy + u * u * y1 : y0 + (y1 - y0) * u;
    if (k > 0) {
      if (style == 0 || (style == 1 && (k & 1))) {
        cv.drawLine((int)px, (int)py, (int)x, (int)y, col);
        if (width > 1) cv.drawLine((int)px + 1, (int)py, (int)x + 1, (int)y, col);
        if (width > 2) cv.drawLine((int)px, (int)py + 1, (int)x, (int)y + 1, col);
      } else if (style == 2) mapDashed(px, py, x, y, col, 1, 3);
      else if (style == 1 && !rimPair) mapDashed(px, py, x, y, col, 3, 3);
    }
    px = x; py = y;
  }
}

static void drawMap() {
  sm::Atlas &a = sm::atlas();
  layoutMap();
  const uint16_t TEAL_D = rgb(0, 52, 56), TEAL_L = rgb(60, 170, 160), LIME = rgb(150, 225, 30), LIME_L = rgb(210, 255, 120);
  const uint16_t LILAC = rgb(185, 160, 255), GOLD = rgb(240, 200, 90), TXT = rgb(205, 215, 220), DIM = rgb(100, 114, 124), WHITE = rgb(245, 250, 240);
  cv.fillSprite(rgb(4, 6, 12));
  for (int i = 0; i < 5; i++) {
    cv.fillEllipse((int)MAP_CX, (int)MAP_CY, (int)(MAP_R[i] * MAP_SX), (int)(MAP_R[i] * MAP_SY), rgb(4 + i * 3, 8 + i * 2, 14 + i * 4));
    cv.drawEllipse((int)MAP_CX, (int)MAP_CY, (int)(MAP_R[i] * MAP_SX), (int)(MAP_R[i] * MAP_SY), rgb(14 + i * 6, 26 + i * 3, 34 + i * 6));
  }
  static const char *rn[] = {"SHALLOWS", "ROADS", "BELOW", "COVE"};
  cv.setTextColor(rgb(56, 70, 80));
  for (int i = 1; i < 5; i++) { cv.setCursor((int)MAP_CX - (int)strlen(rn[i - 1]) * 3, (int)(MAP_CY - MAP_R[i] * MAP_SY) + 2); cv.print(rn[i - 1]); }
  // lanes
  for (auto &l : a.link) {
    if (!(l.flags & sm::AL_USED) || !mapRole[l.a] || !mapRole[l.b]) continue;
    bool fixed = mapRole[l.a] == 3 || mapRole[l.b] == 3;
    if (fixed) { mapDashed(mapX[l.a], mapY[l.a], mapX[l.b], mapY[l.b], rgb(120, 100, 190), 2, 3); continue; }
    if (l.flags & sm::AL_TETHER) { mapDashed(mapX[l.a], mapY[l.a], mapX[l.b], mapY[l.b], TEAL_L, 1, 3); continue; }
    bool flown = l.flags & sm::AL_FLOWN;
    mapArc(l.a, l.b, l.depth, depthCol(l.depth), flown ? 0 : 1, flown && l.depth > 1 ? 2 : 1, l.via != 255 ? l.via : -1);
  }
  // the traced way from here
  int path[sm::ATLAS_PLACES], pn = 0;
  if (mapTrace >= 0 && a.here != 255) pn = sm::atlasPath(a.here, mapTrace, path, sm::ATLAS_PLACES);
  for (int i = 0; i + 1 < pn; i++) {
    int d = 1;
    int via = -1;
    for (auto &l : a.link) if ((l.flags & sm::AL_USED) && ((l.a == path[i] && l.b == path[i + 1]) || (l.b == path[i] && l.a == path[i + 1]))) { d = l.depth; via = l.via != 255 ? l.via : -1; }
    mapArc(path[i], path[i + 1], d, WHITE, 0, 3, via);
  }
  // places and names (names placed so they don't sit on each other)
  int boxes[sm::ATLAS_PLACES][4], nb = 0;
  const sm::Contract &c = sm::contract();
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < sm::ATLAS_PLACES; k++) {
      if (!mapRole[k]) continue;
      const sm::AtlasPlace &p = a.place[k];
      bool here = k == a.here, job = c.live && sm::sameName(p.name, c.dest), traced = false;
      for (int i = 1; i < pn; i++) if (path[i] == k) traced = true;
      bool important = here || job || traced || k == mapTrace;
      if ((pass == 0) != important) continue;   // the important names claim space first
      int x = (int)mapX[k], y = (int)mapY[k];
      uint16_t col = TXT;
      if (here) { cv.drawCircle(x, y, 5, LIME_L); cv.fillCircle(x, y, 3, LIME); col = LIME; }
      else if (mapRole[k] == 3) { cv.fillTriangle(x, y - 4, x + 4, y, x, y + 4, LILAC); cv.fillTriangle(x, y - 4, x - 4, y, x, y + 4, LILAC); col = LILAC; }
      else if (job) { cv.fillRect(x - 3, y - 3, 7, 7, GOLD); col = GOLD; }
      else if (p.flags & sm::AP_RUMOR) { cv.drawCircle(x, y, 3, TEAL_L); col = TEAL_L; }
      else if (mapRole[k] == 2) { cv.fillCircle(x, y, 3, rgb(4, 6, 12)); cv.drawCircle(x, y, 3, TXT); col = DIM; }
      else cv.fillCircle(x, y, 3, TXT);
      if (traced || k == mapTrace) cv.drawCircle(x, y, 6, WHITE);
      char lab[24]; asciiCopy(lab, sizeof(lab), p.name); upcase(lab);
      if (mapRole[k] == 3 && strlen(lab) > 12) { char *sp2 = strchr(lab + 4, ' '); if (sp2) *sp2 = 0; }
      if (p.flags & sm::AP_RUMOR) strncat(lab, " ?", sizeof(lab) - strlen(lab) - 1);
      int w = (int)strlen(lab) * 6;
      bool right = x >= (int)MAP_CX;
      int cand[4][2] = {{right ? x + 7 : x - 7 - w, y - 4}, {right ? x - 7 - w : x + 7, y - 4}, {x - w / 2, y - 13}, {x - w / 2, y + 6}};
      for (int ci = 0; ci < 4; ci++) {
        int lx = cand[ci][0], ly = cand[ci][1];
        lx = lx < 1 ? 1 : (lx + w > 319 ? 319 - w : lx);
        ly = ly < 14 ? 14 : (ly > 202 ? 202 : ly);
        bool clash = false;
        for (int b = 0; b < nb && !clash; b++)
          clash = !(lx + w < boxes[b][0] || lx > boxes[b][2] || ly + 8 < boxes[b][1] || ly > boxes[b][3]);
        if (clash && !(important && ci == 3)) continue;
        cv.setTextColor(col); cv.setCursor(lx, ly); cv.print(lab);
        if (nb < sm::ATLAS_PLACES) { boxes[nb][0] = lx - 1; boxes[nb][1] = ly - 1; boxes[nb][2] = lx + w + 1; boxes[nb][3] = ly + 9; nb++; }
        break;
      }
    }
  // header + way strip
  int places = 0; for (int k = 0; k < sm::ATLAS_PLACES; k++) if (mapRole[k] == 1) places++;
  cv.fillRect(0, 0, 320, 12, TEAL_D);
  cv.setTextColor(TEAL_L); cv.setCursor(4, 2); cv.print("THE DEEP IS SMALL");
  cv.setTextColor(DIM); cv.setCursor(196, 2); cv.printf("LIFE #%lu  %d PLACES", (unsigned long)(sm::sheet().lives + 1), places);
  if (mapTrace >= 0) {
    cv.fillRect(0, 203, 320, 23, rgb(18, 24, 30));
    char way[160] = "WAY:"; size_t o = 4;
    if (pn < 2) snprintf(way, sizeof(way), "NO REMEMBERED WAY FROM HERE");
    else for (int i = 1; i < pn && o < sizeof(way) - 30; i++) {
      int d = 1;
      for (auto &l : a.link) if ((l.flags & sm::AL_USED) && ((l.a == path[i - 1] && l.b == path[i]) || (l.b == path[i - 1] && l.a == path[i]))) d = l.depth;
      char nm[24]; asciiCopy(nm, sizeof(nm), a.place[path[i]].name); upcase(nm);
      o += snprintf(way + o, sizeof(way) - o, "%s %s d%d", i > 1 ? " >" : "", nm, d);
    }
    cv.setTextColor(WHITE); printWrapped(3, 205, 52, 2, 10, way);
  } else {
    cv.setTextColor(DIM); cv.setCursor(4, 213); cv.print("solid flown  dashed seen gate  ? rumor");
  }
  cv.setTextColor(rgb(110, 130, 140)); cv.setCursor(4, 228); cv.print("hold A: this system");
  cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(160, 228); cv.print("tap a place: trace");
}

static void mapTap(int x, int y) {
  if (mapPage == 0) {
    layoutMap();
    int best = -1; float bd = 15.f;
    for (int k = 0; k < sm::ATLAS_PLACES; k++) {
      if (!mapRole[k]) continue;
      float d = sqrtf((mapX[k] - x) * (mapX[k] - x) + (mapY[k] - y) * (mapY[k] - y));
      if (d < bd) { bd = d; best = k; }
    }
    if (best >= 0 && best != sm::atlas().here) { mapTrace = best; hx::pop(0.18f, 0.015f); return; }
  }
  mapOpen = false; hx::pop(0.15f, 0.01f);
}

// ============================================================
//  this system (map page 2): what the scanner sees right here
// ============================================================
static const char *gateMaker(const char *name) {
  // every lane was built by someone; one of them keeps an older calendar
  uint32_t h = 2166136261u;
  for (const char *c = name; *c; c++) { h ^= (uint8_t)(*c | 32); h *= 16777619u; }
  switch (h % 11) { case 0: case 1: case 2: case 3: return "PORTEX"; case 4: case 5: case 6: return "MALTAPLEX";
                    case 7: case 8: return "LIMINAR RELAY"; case 9: return "DESERET"; default: return "NO MAKER'S MARK"; }
}

static void sysRow(int &y, uint16_t glyph, const char *label, const char *detail, uint16_t col) {
  if (y > 208) return;
  cv.fillRect(8, y + 1, 6, 6, glyph);
  cv.setTextColor(col); cv.setCursor(20, y); cv.print(label);
  if (detail && detail[0]) { cv.setTextColor(rgb(110, 124, 134)); cv.setCursor(316 - (int)strlen(detail) * 6, y); cv.print(detail); }
  y += 11;
}

static void distLabel(char *out, size_t n, float d) {
  float m = d * 10.f;
  if (m < 1000.f) snprintf(out, n, "%dm", (int)m); else snprintf(out, n, "%.1fkm", m / 1000.f);
}

static void drawSystemMap() {
  const uint16_t TEAL_D = rgb(0, 52, 56), TEAL_L = rgb(60, 170, 160), LILAC = rgb(185, 160, 255), GOLD = rgb(240, 200, 90);
  const uint16_t TXT = rgb(205, 215, 220), DIM = rgb(110, 124, 134), BLUE = rgb(70, 150, 255);
  cv.fillSprite(rgb(4, 6, 12));
  cv.fillRect(0, 0, 320, 12, TEAL_D);
  cv.setTextColor(TEAL_L); cv.setCursor(4, 2);
  char title[48];
  if (layer == 0) snprintf(title, sizeof(title), "THIS SYSTEM - %s", hereName); else snprintf(title, sizeof(title), "%s", layerName(layer));
  cv.print(title);
  int y = 18; char d[24], l[64];
  if (layer == 0) {
    static const char *st[] = {"RED DWARF", "ORANGE STAR", "YELLOW STAR", "WHITE STAR", "BLUE GIANT", "RED GIANT", "WHITE DWARF"};
    if (!nearStar) sysRow(y, sunCol, st[sunType], "the local sun", TXT);
    for (auto &o : objs) if (o.kind == K_BODY) { distLabel(d, sizeof(d), surfaceDist(o)); sysRow(y, o.col, o.name, d, TXT); }
    for (auto &o : objs) if (o.kind == K_STATION) { distLabel(d, sizeof(d), distTo(o)); snprintf(l, sizeof(l), "%s - docking", o.name); sysRow(y, BLUE, l, d, rgb(120, 180, 255)); }
    int rocks = countKind(K_ROCK), ships = countKind(K_SHIP);
    if (rocks) { snprintf(l, sizeof(l), "%d asteroids on scope", rocks); sysRow(y, rgb(150, 120, 80), l, "", TXT); }
    if (ships) { snprintf(l, sizeof(l), "%d contacts on scope", ships); sysRow(y, rgb(200, 200, 210), l, "", TXT); }
    y += 3; cv.drawLine(8, y, 312, y, rgb(20, 30, 40)); y += 4;
    cv.setTextColor(DIM); cv.setCursor(8, y); cv.print("GATES IN THIS SYSTEM"); y += 11;
    for (auto &o : objs) {
      if (o.kind != K_GATE || !(o.gflags & GF_DEST)) continue;
      const char *tag = (o.gflags & GF_JOB) ? "JOB" : (o.gflags & GF_FIXED) ? "FIXED" : (o.gflags & GF_RUMOR) ? "RUMOR" : (o.gflags & GF_KNOWN) ? "BEEN" : "NEW";
      snprintf(l, sizeof(l), "%-14.14s d%u %-5s", o.name, o.depth, tag);
      char viaName[24] = "";
      if (o.lmId && !(o.gflags & GF_FIXED))
        for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && (int)sm::landmarkAt(i)->id == o.lmId) { snprintf(viaName, sizeof(viaName), "via %.14s", sm::landmarkAt(i)->name); }
      sysRow(y, gateColor(o), l, viaName[0] ? viaName : gateMaker(o.name), gateColor(o));
    }
  } else {
    static const char *lore[5][2] = {
      {"", ""},
      {"The liminal wake. Sub-pirates drift here,", "between the surface and the roads."},
      {"Worn lanes of the deep trade. Ghost fleet", "couriers keep to the dark edges."},
      {"Deseret and Liminar are building something", "down here. Tunnelers camp in its shadow."},
      {"Here the layer remembers itself: same light,", "same names, every life. The ghost fleet's home."}};
    cv.setTextColor(TXT);
    cv.setCursor(8, y); cv.print(lore[layer][0]); y += 10;
    cv.setCursor(8, y); cv.print(lore[layer][1]); y += 14;
    const sm::Trip &tr = sm::trip();
    if (tr.active) { snprintf(l, sizeof(l), "COURSE %s", tr.dest); snprintf(d, sizeof(d), "d%u %s", tr.destDepth, tr.ascending ? "up" : "down"); sysRow(y, GOLD, l, d, GOLD); }
    if (navObj >= 0 && objs[navObj].kind != K_NONE) {
      distLabel(d, sizeof(d), distTo(objs[navObj]));
      sysRow(y, objs[navObj].kind == K_PORTAL ? rgb(210, 110, 230) : rgb(110, 240, 200), objs[navObj].kind == K_PORTAL ? "PORTAL" : "NEXT GATE", d, TXT);
    }
    for (auto &o : objs) {
      if (o.kind == K_BODY) { distLabel(d, sizeof(d), surfaceDist(o)); sysRow(y, o.col ? o.col : rgb(255, 200, 120), o.name, d, TXT); }
      if (o.kind == K_LANDMARK) {
        distLabel(d, sizeof(d), distTo(o)); sysRow(y, LILAC, o.name, d, LILAC);
        for (int i = 0; i < sm::landmarkCount(); i++) {
          const sm::Landmark *lm = sm::landmarkAt(i);
          if (lm && (int)lm->id == o.lmId) {
            char ln[112]; asciiCopy(ln, sizeof(ln), sm::landmarkLine(*lm, (uint32_t)o.lmId * 7u));
            cv.setTextColor(DIM); printWrapped(20, y, 49, 2, 10, ln); y += 21;
          }
        }
      }
      if (o.kind == K_SHIP && o.ghost) sysRow(y, rgb(170, 190, 200), "GHOST FLEET hull", "running dark", TXT);
    }
    y += 3; cv.drawLine(8, y, 312, y, rgb(20, 30, 40)); y += 5;
    cv.setTextColor(DIM); cv.setCursor(8, y); cv.print("SCANNER PICKS UP"); y += 11;
    char w[112]; asciiCopy(w, sizeof(w), sm::deepWhisper((uint8_t)layer, (uint32_t)(millis() / 20000u) * 2654435761u));
    cv.setTextColor(LILAC); printWrapped(20, y, 49, 3, 10, w);
  }
  cv.setTextColor(rgb(110, 130, 140)); cv.setCursor(4, 228); cv.print("hold A: subspace map");
  cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(226, 228); cv.print("tap to close");
}

// ============================================================
//  the visitors: an encounter below the roads
// ============================================================
static const char *alienName(int c) { static const char *n[] = {"THE CHOIR", "THE LATTICE", "THE MOTH"}; return n[c % 3]; }

static void alienBegin() {
  alien = AlienEncounter{};
  alien.active = true;
  alien.cls = (uint8_t)(rnd() % 3);
  alien.outcome = (uint8_t)(rnd() % 6);
  alien.p = shipPos + shipB.f * 95.f + shipB.r * rf(-20, 20) + shipB.u * rf(-10, 10);
  theater = TH_NONE; target = -1; dockTarget = -1;
  setBanner("SOMETHING IS PACING YOU", 2600);
}

static void alienApply() {
  sm::Pilot &p = sm::sheet();
  const char *toast = "", *note = "";
  uint8_t out = alien.outcome;
  auto bump = [&](sm::CapId c) -> bool { uint8_t t = sm::capTier(c); if (t >= 10) return false; sm::earnCap(c, (uint8_t)(t + 1)); return true; };
  if (out == 1 && !bump(sm::CAP_SHIELDS)) out = 5;
  if (out == 2 && !bump(sm::CAP_STABILIZER)) out = 5;
  if (out == 4 && !bump(sm::CAP_SCANNERS)) out = 5;
  switch (out) {
    case 0: sm::setFuel(p.fuelCap); toast = "SYSTEMS UP. FUEL READS FULL. YOU DID NOT FILL IT."; note = "fuel full when the lights came back."; break;
    case 1: toast = "SHIELDS REPORT A NEW MARK. NOBODY INSTALLED IT."; note = "the shields are stronger. no idea how."; break;
    case 2: toast = "THE STABILIZER HUMS A NOTE IT NEVER KNEW."; note = "the stabilizer sings now."; break;
    case 3: sm::repairHull(p.hullMax); toast = "HULL INTEGRITY 100%. THE SCARS ARE GONE. ALL OF THEM."; note = "every scar on the hull is gone."; break;
    case 4: toast = "SCANNERS SEE FURTHER NOW. SOMETHING LOOKED THROUGH THEM FIRST."; note = "it looked through my scanners."; break;
    default: toast = "NOTHING IS MISSING. YOU CHECK TWICE. NOTHING IS MISSING."; note = "nothing taken. I checked twice."; break;
  }
  setBanner(toast, 5200);
  char jb[72]; snprintf(jb, sizeof(jb), "%s held me in the dark. %s", alienName(alien.cls), note);
  sm::journalAdd(jb);
  sm::flagSet("visited", (int8_t)(sm::flagGet("visited") < 120 ? sm::flagGet("visited") + 1 : 120), true);
  saveAll();
}

static void alienTick() {
  if (alienCooldown > 0.f) alienCooldown -= dt;
  if (!alien.active && alienPending > 0.f && layer >= 3 && !stationOpen) {
    alienPending -= dt;
    if (alienPending <= 0.f) { alienPending = -1.f; if (countKind(K_LANDMARK) == 0) alienBegin(); }
  }
  if (!alien.active) return;
  float t0 = alien.t;
  alien.t += dt;
  float t = alien.t;
  auto crossed = [&](float at) { return t0 < at && t >= at; };
  // it comes in fast and stops too close
  V3 hold = shipPos + shipB.f * 26.f + shipB.u * 3.f;
  // it holds station on the ship the whole time: you cannot drift away from it
  if (t < 22.f) alien.p = lerp3(alien.p, hold, clampf(dt * (t < 4.f ? 1.6f : 4.f), 0, 1));
  if (crossed(4.f)) { hx::cut(0.6f); setBanner("POWER LOSS", 1800); }
  if (crossed(6.5f)) {
    static const char *scan[] = {"IT IS SINGING AT THE HULL", "IT IS MEASURING EVERYTHING", "IT IS TOUCHING THE SHIP ALL OVER"};
    setBanner(scan[alien.cls], 3200);
  }
  if (crossed(15.f)) setBanner(alien.cls == 1 ? "THE GRID GOES THROUGH YOU" : alien.cls == 0 ? "THE NOTE IS INSIDE THE COCKPIT" : "THEY ARE ON THE GLASS", 3000);
  // the alien rhythm: two pulse trains against each other (3 over 2), each visitor its own way
  bool scanning = t >= 4.6f && t < 22.f;
  float amp = t < 4.f ? 0.25f + 0.1f * t : (scanning ? 0.6f : 0.f);
  if (amp > 0.f) {
    alien.beatA -= dt; alien.beatB -= dt; alien.beatC -= dt;
    float pa = alien.cls == 2 ? 0.27f : 0.6f, pb = pa * 2.f / 3.f;
    if (alien.beatA <= 0) { hx::pop(amp, alien.cls == 1 ? 0.02f : 0.06f); alien.beatA += pa; }
    if (alien.beatB <= 0) { hx::pop(amp * 0.6f, 0.03f); alien.beatB += pb; }
    if (alien.cls == 0 && scanning) hx::hum(hx::HUM_DEEP, 0.32f, 1.66f, 0.05f);
    if (alien.cls == 1 && scanning && alien.beatC <= 0) { hx::stutter(0.55f, 3, 0.05f); alien.beatC = 1.2f; }
    if (alien.cls == 2 && scanning && alien.beatC <= 0) { hx::pop(0.2f, 0.01f); alien.beatC = rf(0.04f, 0.16f); }
  }
  // leaving: everything is pulled inward, then gone
  if (crossed(22.f)) { hx::swell(1.f, 0.85f, 0.04f); setBanner("", 1); }
  if (crossed(22.9f)) { hx::cut(0.5f); crossFlash = 1.f; }
  if (crossed(27.6f)) setBanner("SYSTEMS REBOOTING...", 1400);
  if (crossed(29.f) && !alien.applied) { alien.applied = true; alienApply(); }
  if (t >= 30.f) { alien.active = false; alienCooldown = 480.f; }
}

// the scan itself, drawn over the world
static void drawAlienScan(float ax, float ay) {
  float t = alien.t;
  if (t < 4.6f || t >= 22.f) return;
  float k = t - 4.6f;
  if (alien.cls == 0) {   // the choir: rings that pass through you
    for (int i = 0; i < 4; i++) {
      float r = fmodf(k * 95.f + i * 80.f, 340.f);
      cv.drawCircle((int)ax, (int)ay, (int)r, hsv(170 + i * 30 + k * 40, 0.4f, 0.9f - r / 500.f));
    }
  } else if (alien.cls == 1) {   // the lattice: a grid that sweeps the cockpit
    int sy = (int)fmodf(k * 70.f, 240.f), sx = (int)fmodf(k * 110.f, 320.f);
    cv.drawLine(0, sy, 319, sy, rgb(255, 80, 230)); cv.drawLine(0, sy + 1, 319, sy + 1, rgb(120, 20, 110));
    cv.drawLine(sx, 0, sx, 239, rgb(255, 80, 230));
    if (fmodf(k, 1.2f) < 0.12f) for (int g = 0; g < 320; g += 32) { cv.drawLine(g, 0, g, 239, rgb(70, 10, 70)); if (g < 240) cv.drawLine(0, g, 319, g, rgb(70, 10, 70)); }
  } else {   // the moth: dust that crawls toward the glass
    for (int i = 0; i < 40; i++) {
      float u = fmodf(k * 0.35f + i * 0.0251f, 1.f);
      float a = i * 2.39996f + fsin(k * 3.f + i) * 0.4f;
      int x = (int)(ax + cosf(a) * u * 260.f), y = (int)(ay + sinf(a) * u * 200.f);
      cv.fillRect(x, y, 1 + (int)(u * 3), 1 + (int)(u * 3), hsv(280 + i * 3, 0.5f, 0.4f + u * 0.6f));
    }
  }
}

// the visitor: drawn through its own lens, three times over, never quite in focus
static void drawAlienShape() {
  float t = alien.t;
  if (t >= 22.9f) return;
  float collapse = t > 22.f ? 1.f - (t - 22.f) / 0.9f : 1.f;
  float sc = 9.f * collapse;
  for (int ghost = 0; ghost < 3; ghost++) {
    float jx = fsin(t * 17.f + ghost * 2.1f) * 2.5f, jy = fcos(t * 13.f + ghost * 1.3f) * 2.5f;
    uint16_t col = alien.cls == 0 ? hsv(180 + ghost * 40 + t * 30, 0.35f, 1.f - ghost * 0.25f)
                 : alien.cls == 1 ? hsv(300 + ghost * 25, 0.7f, 1.f - ghost * 0.25f)
                                  : hsv(265 + ghost * 30, 0.5f, 0.85f - ghost * 0.2f);
    if (alien.cls == 0) {   // three rings in three planes
      for (int r = 0; r < 3; r++) {
        float px = 0, py = 0; bool pv = false;
        for (int s = 0; s <= 24; s++) {
          float a = s * 0.2618f + t * (0.6f + r * 0.3f);
          V3 axis1 = r == 0 ? shipB.r : (r == 1 ? shipB.u : norm(shipB.r + shipB.f));
          V3 axis2 = r == 2 ? shipB.u : shipB.f;
          V3 w = alien.p + (axis1 * cosf(a) + axis2 * sinf(a)) * (sc * (1.f + 0.15f * r));
          float x, y, z;
          bool ok = project(w, x, y, z);
          if (ok && pv) cv.drawLine((int)(px + jx), (int)(py + jy), (int)(x + jx), (int)(y + jy), col);
          px = x; py = y; pv = ok;
        }
      }
    } else if (alien.cls == 1) {   // a lattice that turns the wrong way
      float sx[27], sy[27]; bool ok[27];
      for (int i = 0; i < 27; i++) {
        V3 m{(float)(i % 3 - 1), (float)((i / 3) % 3 - 1), (float)(i / 9 - 1)};
        float a = t * 0.7f, b = -t * 0.45f;
        V3 r1{m.x * cosf(a) - m.z * sinf(a), m.y, m.x * sinf(a) + m.z * cosf(a)};
        V3 r2{r1.x, r1.y * cosf(b) - r1.z * sinf(b), r1.y * sinf(b) + r1.z * cosf(b)};
        float z; ok[i] = project(alien.p + shipB.toWorld(r2 * sc * 0.8f), sx[i], sy[i], z);
      }
      for (int i = 0; i < 27; i++) {
        int x = i % 3, y = (i / 3) % 3, zz = i / 9;
        int nbr[3] = {x < 2 ? i + 1 : -1, y < 2 ? i + 3 : -1, zz < 2 ? i + 9 : -1};
        for (int nidx : nbr) if (nidx >= 0 && ok[i] && ok[nidx]) cv.drawLine((int)(sx[i] + jx), (int)(sy[i] + jy), (int)(sx[nidx] + jx), (int)(sy[nidx] + jy), col);
      }
    } else {   // the moth: two lobes of dust, beating
      float flap = 0.5f + 0.5f * fsin(t * 7.f);
      for (int i = 0; i < 48; i++) {
        float a = i * 0.1309f, side = (i & 1) ? 1.f : -1.f;
        float rr = sc * (0.6f + 0.6f * fabsf(fsin(a * 2.f)));
        V3 w = alien.p + shipB.r * (side * rr * cosf(a) * (0.4f + flap)) + shipB.u * (rr * sinf(a) * 0.7f) + shipB.f * (side * flap * 2.f);
        float x, y, z;
        if (project(w, x, y, z)) cv.fillRect((int)(x + jx), (int)(y + jy), 2, 2, col);
      }
    }
  }
  // its core: a hole that the eye refuses
  float cx, cy, cz;
  if (project(alien.p, cx, cy, cz)) cv.fillCircle((int)cx, (int)cy, (int)clampf(sc * 0.35f * FOCAL / cz, 1, 14), rgb(0, 0, 0));
}

// dim the cockpit: power is out
static void drawPowerLoss() {
  for (int y = 0; y < H; y += 2) cv.drawLine(0, y, W - 1, y, rgb(0, 0, 0));
  if (((int)(tNow * 3)) & 1) { cv.setTextColor(rgb(200, 40, 30)); cv.setCursor(6, 228); cv.print("NO POWER"); }
}

// after it leaves: dark, then the glitch of systems coming back
static bool drawAlienAftermath() {
  float t = alien.t;
  if (!alien.active || t < 23.15f || t >= 27.6f) return false;
  cv.fillSprite(rgb(0, 0, 0));
  float k = 1.f - (t - 23.15f) / 4.45f;
  int bars = (int)(14 * k * k) + (rnd() % 3);
  for (int i = 0; i < bars; i++) {
    int y = (int)(rnd() % H), h = 1 + (int)(rnd() % 6), x = (int)(rnd() % W), w = 10 + (int)(rnd() % 140);
    static const uint16_t cols[] = {rgb(150, 225, 30), rgb(93, 0, 93), rgb(0, 115, 115), rgb(200, 200, 210)};
    cv.fillRect(x, y, w, h, shade(cols[rnd() % 4], 0.3f + 0.7f * k));
  }
  if (k > 0.6f) cv.drawCircle((int)alienLX, (int)alienLY, (int)(30 * k), rgb((int)(120 * k), (int)(120 * k), (int)(140 * k)));
  return true;
}

// ============================================================
//  SIGNALS (experimental): two pilots, one sky
// ============================================================
static bool signalsOn() { return sm::flagGet("signals") > 0; }
static const char *myCallsign() {
  static char cs[16];   // the same callsign the license shows
  snprintf(cs, sizeof(cs), "MANTIS-%04X", (unsigned)((ESP.getEfuseMac() >> 24) & 0xFFFF));
  return cs;
}
static int signalsFee() { const Obj *st = dockedStation(); return st && st->uses == BR_LIMINAR ? 140 : 180; }   // Liminar builds gates: cheapest
static float meetSpot[3] = {0, 0, 0};

// The meeting sky: built from the shared seed alone, on its own random stream, and
// never written to the atlas or the save. The regular game's randomness is restored after.
static void makeMeetingScene(uint32_t seed) {
  uint32_t keepRng = rngState;
  rngState = seed ? seed : 1;
  clearWorld();
  layer = 0; nearStar = false;
  asciiCopy(hereName, sizeof(hereName), sm::placeName(seed, 0, false)); upcase(hereName);
  buildSky(seed ^ 0x5EEDu);
  shipPos = V3{0, 0, 0}; prevShipPos = shipPos;
  shipB = Basis::facing(V3{0, 0, 1}, V3{0, 1, 0});
  spawnBody(BT_GIANT, 760.f, 210.f, norm(V3{-0.7f, 0.15f, 0.7f}));
  uint8_t looks[4] = {makeLook(SS_HEXCORE, BR_LIMINAR), makeLook(SS_RING, BR_MALTAPLEX), makeLook(SS_HABITAT, BR_DESERET), makeLook(SS_SPINDLE, BR_PORTEX)};
  V3 sp = V3{60.f, 10.f, 210.f};
  placeStation(sp, norm(V3{0, 0, 0} - sp), looks[rnd() % 4]);
  for (int i = 0; i < 6; i++) spawnRock(V3{rf(-180, 180), rf(-40, 40), rf(120, 320)}, rf(3.5f, 6.f));
  uint16_t sid = 1000;
  for (auto &o : objs) if (o.kind != K_NONE) o.net = sid++;   // seeded: the same ids on both devices
  rngState = keepRng;
  // each pilot's own way home (not shared)
  Obj *hg = spawnGate(V3{-90.f, 0.f, -40.f}, norm(V3{1.f, 0.f, 0.3f}), meetOrigin, 1, GF_DEST | GF_KNOWN, 9.f);
  if (hg) hg->net = 0;
  sm::contractSetHere(hereName); sm::contractSetBand(0);
  sunDir = norm(V3{-0.4f, 0.5f, 0.6f});
}

static Obj *ensureRemote() {
  if (remoteIdx >= 0 && objs[remoteIdx].kind == K_SHIP && objs[remoteIdx].remote) return &objs[remoteIdx];
  Obj *o = newObj(K_SHIP);
  if (!o) return nullptr;
  o->remote = true; o->radius = 4.f; o->mesh = M_COBRA; o->col = rgb(200, 200, 210); o->enc = sm::ENC_TRAVELER;
  asciiCopy(o->name, sizeof(o->name), net::peerName());
  remoteIdx = idxOf(o);
  return o;
}

static void leaveMeeting(bool sayBye) {
  if (sayBye) net::sendBye();
  net::cancel();
  inMeeting = false;
  if (remoteIdx >= 0) { objs[remoteIdx].kind = K_NONE; remoteIdx = -1; }
}

// Every frame: hear the other pilot, share what the anchor spawns, tell them where we are.
static void netTick() {
  if (net::phase() == net::PH_OFF) return;
  net::poll();
  if (!inMeeting) return;
  if (net::takeBye()) {
    char b[64]; snprintf(b, sizeof(b), "%s LEFT THE SKY", net::peerName()); setBanner(b, 2200);
    if (remoteIdx >= 0) { addBoom(objs[remoteIdx].p, 6.f, rgb(140, 230, 220)); objs[remoteIdx].kind = K_NONE; remoteIdx = -1; }
    return;
  }
  if (net::phase() == net::PH_LOST) {
    if (remoteIdx >= 0) { objs[remoteIdx].kind = K_NONE; remoteIdx = -1; setBanner("SIGNAL LOST", 1600); }
    return;
  }
  // the other pilot
  const net::RemoteState &r = net::remote();
  if (r.atMs) {
    Obj *o = ensureRemote();
    if (o) {
      float age = (millis() - r.atMs) / 1000.f; if (age > 0.6f) age = 0.6f;
      V3 want = V3{r.p[0], r.p[1], r.p[2]} + V3{r.v[0], r.v[1], r.v[2]} * age;   // dead reckoning
      o->p = lerp3(o->p, want, clampf(dt * 8.f, 0.f, 1.f));
      o->v = V3{r.v[0], r.v[1], r.v[2]};
      V3 f = V3{r.f[0], r.f[1], r.f[2]}, u = V3{r.u[0], r.u[1], r.u[2]};
      if (len(f) > 0.5f && len(u) > 0.5f) { o->o = Basis::facing(norm(f), norm(u)); }
      o->uses = r.ship; o->hostile = false; o->ghost = (r.flags & 2) != 0;
    }
  }
  netSendT -= dt;
  if (netSendT <= 0.f) {
    netSendT = 0.08f;   // ~12 Hz
    V3 v = shipB.f * shipSpeed;
    float p[3] = {shipPos.x, shipPos.y, shipPos.z}, vv[3] = {v.x, v.y, v.z}, f[3] = {shipB.f.x, shipB.f.y, shipB.f.z}, u[3] = {shipB.u.x, shipB.u.y, shipB.u.z};
    uint8_t flags = (uint8_t)((theater == TH_COMBAT ? 1 : 0) | (cloakT > 0.f ? 2 : 0) | (itMe ? 4 : 0));
    net::sendState(p, vv, f, u, sm::sheet().activeShip, flags);
  }
  // shared spawns: the anchor shares what appears; the guest only shows what it is told
  if (net::role() == net::ROLE_ANCHOR) {
    for (auto &o : objs) {
      if (o.net || o.remote || (o.kind != K_SHIP && o.kind != K_POD && o.kind != K_WRECK && o.kind != K_ROCK)) continue;
      o.net = netNextId++;
      net::SpawnMsg m{}; m.id = o.net; m.enc = o.enc; m.mesh = o.mesh; m.hostile = o.hostile; m.ghost = o.ghost; m.col = o.col; m.radius = o.radius;
      m.p[0] = o.p.x; m.p[1] = o.p.y; m.p[2] = o.p.z; m.v[0] = o.v.x; m.v[1] = o.v.y; m.v[2] = o.v.z;
      asciiCopy(m.name, sizeof(m.name), o.name);
      m.ghost |= (uint8_t)(o.kind << 4);   // the object kind rides in the high bits
      net::sendSpawn(m);
    }
    npcSyncT -= dt;
    if (npcSyncT <= 0.f) {
      npcSyncT = 0.25f;
      net::NpcMsg ms[8]; int n = 0;
      for (auto &o : objs) if (o.net && o.net < 1000 && o.kind == K_SHIP && !o.remote && n < 8) {
        ms[n].id = o.net; ms[n].p[0] = o.p.x; ms[n].p[1] = o.p.y; ms[n].p[2] = o.p.z; ms[n].v[0] = o.v.x; ms[n].v[1] = o.v.y; ms[n].v[2] = o.v.z; n++;
      }
      if (n) net::sendNpcs(ms, n);
    }
  } else {
    spawnTimer = 99.f;   // the guest never spawns its own contacts here
    net::SpawnMsg m;
    while (net::takeSpawn(m)) {
      uint8_t kind = (uint8_t)(m.ghost >> 4);
      Obj *o = newObj(kind ? kind : (uint8_t)K_SHIP);
      if (!o) break;
      o->net = m.id; o->enc = m.enc; o->mesh = m.mesh; o->hostile = m.hostile; o->ghost = (m.ghost & 1) != 0; o->col = m.col; o->radius = m.radius;
      o->p = V3{m.p[0], m.p[1], m.p[2]}; o->v = V3{m.v[0], m.v[1], m.v[2]};
      if (len(o->v) > 0.1f) o->o = Basis::facing(norm(o->v), V3{0, 1, 0});
      asciiCopy(o->name, sizeof(o->name), m.name);
      if (o->kind == K_ROCK) o->uses = 3;
    }
    net::NpcMsg n;
    while (net::takeNpc(n)) for (auto &o : objs) if (o.net == n.id && o.kind != K_NONE) { o.p = lerp3(o.p, V3{n.p[0], n.p[1], n.p[2]}, 0.6f); o.v = V3{n.v[0], n.v[1], n.v[2]}; }
  }
  uint16_t id;
  while (net::takeRemove(id)) for (int i = 0; i < MAX_OBJ; i++) if (objs[i].net == id && objs[i].kind != K_NONE) { netEcho = true; if (target == i) target = -1; killObj(i); netEcho = false; }
  while (net::takeKill(id)) {   // they shot down a shared hostile: it counts for your work too
    sm::contractOnResolve(sm::ENC_PIRATE, true, true);
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].net == id && objs[i].kind != K_NONE) { addBoom(objs[i].p, 5.f, rgb(255, 160, 90)); netEcho = true; killObj(i); netEcho = false; }
  }
  char chat[56];
  while (net::takeChat(chat, sizeof(chat))) {
    char b[112]; snprintf(b, sizeof(b), "%s: %s", net::peerName(), chat); setBanner(b, 3200); hx::pop(0.3f, 0.03f);
    chatLogAdd(b);
  }
  int hits = net::takeTag();
  if (hits > 0) {   // tagged: their fire, your shields, and now you're it
    tagsTaken += hits; itMe = true;
    float sx = 160, sy = 40, sz;
    if (remoteIdx >= 0) project(objs[remoteIdx].p, sx, sy, sz);
    for (int k = 0; k < hits && k < 4; k++) addBolt(sx, sy, 160 + rf(-50, 50), H - 30, rgb(255, 90, 200), 2, 8);
    shieldFx = 0.35f; shieldFxX = 160; shieldFxY = H - 30;
    hx::thud(0.5f);
    char b[64]; snprintf(b, sizeof(b), "TAGGED BY %s x%d - YOU'RE IT", net::peerName(), hits); setBanner(b, 2200);
  }
}

// ============================================================
//  session: capture, resume, new game
// ============================================================
static void captureSession(Session &ss) {
  memset(&ss, 0, sizeof(ss));
  ss.magic = SESSION_MAGIC;
  ss.layer = (uint8_t)layer;
  ss.station = countKind(K_STATION) > 0 ? 1 : 0;
  ss.docked = (stationOpen || dockAnim > 0) ? 1 : 0;
  ss.trip = sm::trip();
  asciiCopy(ss.here, sizeof(ss.here), hereName);
  if (inMeeting || sm::trip().meeting) { asciiCopy(ss.here, sizeof(ss.here), meetOrigin[0] ? meetOrigin : hereName); ss.layer = 0; memset(&ss.trip, 0, sizeof(ss.trip)); ss.station = 0; ss.docked = 0; }
  asciiCopy(ss.origin, sizeof(ss.origin), tripOrigin);
  ss.throttle = throttleT;
}

// Pick up where the pilot left off: same place, same lanes, same leg of the dive.
static bool resumeSession(const Session &ss) {
  if (ss.magic != SESSION_MAGIC || !ss.here[0] || ss.layer > 4) return false;
  asciiCopy(tripOrigin, sizeof(tripOrigin), ss.origin);
  if (ss.layer == 0 || !ss.trip.active) {
    makeRealScene(ss.here, ss.station != 0);
    if (ss.trip.active && ss.trip.layer == 0) { sm::trip() = ss.trip; spawnNextOnPath(); }
    if (ss.docked) for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) { stationIdx = i; openBoard(); break; }
  } else {
    // mid-dive: back in the same layer, the chain waiting ahead
    makeRealScene(ss.here, false);       // sky, place and lanes
    sm::trip() = ss.trip;
    layer = ss.layer;
    makeLayerScene();
    spawnNextOnPath();
  }
  throttleT = clampf(ss.throttle, 0.f, 1.f);
  return true;
}

static void startNewGame() {
  sm::sdDeleteSave();
  sm::sheetInit();
  sm::universeReseed(millis() * 2654435761u ^ rnd() ^ (uint32_t)ESP.getEfuseMac());
  sm::sheet().universeSeed = sm::universeSeed();
  sm::contractsInit();
  sm::tripEnd();
  sm::atlasClear();
  sm::journalClear();
  livesSeen = sm::sheet().lives;
  rngState = sm::universeSeed() ? sm::universeSeed() : 0xA341316Cu;
  theater = TH_NONE; stationOpen = false; dockAnim = 0; launchAnim = 0; endingOpen = false; lostOpen = false;
  mapOpen = false; statusOpen = false; tripOrigin[0] = 0;
  makeRealScene(nullptr, true);
  sm::journalAdd("New license issued. Every name ahead is unwritten.");
  saveAll();
  serviceSD(true);
  setBanner("NEW PILOT. NEW SKY.", 3000);
}

// the other pilot, drawn as their ship's own side-view art, scaled by distance, facing their way
static void drawRemoteShip(const Obj &o) {
  float sx, sy, z;
  if (!project(o.p, sx, sy, z) || !onScreen(sx, sy, 80)) return;
  uint8_t t = o.uses < sm::SHIP_COUNT ? o.uses : 0;
  int aw = SHIP_ART_W, ah = SHIP_ART_H;
  const ShipArt *a = nullptr;
  if (t != sm::SHIP_MANTIS) { a = t == sm::SHIP_FALCOR ? &SHIP_FALCOR : t == sm::SHIP_HONEYBEE ? &SHIP_HONEYBEE : t == sm::SHIP_MALTESE ? &SHIP_MALTESE : &SHIP_GHOST; aw = a->w; ah = a->h; }
  float w = clampf(14.f * FOCAL / z, 6.f, 140.f), h = w * ah / aw;
  bool flip = dot(o.o.f, shipB.r) < 0.f;   // heading left on screen: mirror the side view
  int x0 = (int)(sx - w / 2), y0 = (int)(sy - h / 2), iw = (int)w, ih = (int)h;
  bool cloaked = o.ghost;
  for (int j = 0; j < ih; j++) for (int i = 0; i < iw; i++) {
    if (cloaked && ((i + j + (int)(tNow * 20)) % 5)) continue;   // cloaked: a shimmer, not a ship
    int u = (flip ? iw - 1 - i : i) * aw / iw, v = j * ah / ih;
    uint16_t c = a ? (a->px[v * aw + u] ? a->pal[a->px[v * aw + u]] : 0) : SHIP_ART[v * SHIP_ART_W + u];
    if (c) cv.drawPixel(x0 + i, y0 + j, cloaked ? rgb(140, 230, 220) : c);
  }
  const net::RemoteState &r = net::remote();
  if (r.flags & 4) cv.drawCircle((int)sx, (int)sy, (int)(w * 0.6f), ((int)(tNow * 6) & 1) ? rgb(255, 80, 230) : rgb(150, 30, 140));   // they're it
  cv.setTextColor(rgb(230, 150, 230)); cv.setCursor((int)sx - (int)strlen(o.name) * 3, y0 - 10); cv.print(o.name);
}

// ---- chat: a log, some presets, a keyboard ----
static const char *KEYS[3] = {"QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.?"};
static const char *ROASTS[3] = {"NICE PARKING", "YOUR TRAILER IS SHOWING", "TAG. YOU'RE IT."};
static void chatSend(const char *t) {
  if (!t || !t[0]) return;
  net::sendChat(t);
  char b[64]; snprintf(b, sizeof(b), "YOU: %s", t); chatLogAdd(b);
  hx::pop(0.25f, 0.02f);
}
static void drawChat() {
  cv.fillRoundRect(4, 4, 312, 232, 8, rgb(10, 6, 14));
  cv.drawRoundRect(4, 4, 312, 232, 8, rgb(200, 70, 200));
  cv.setTextColor(rgb(230, 150, 230)); cv.setCursor(12, 10); cv.printf("SIGNAL  %s", net::peerName());
  cv.setTextColor(rgb(120, 100, 130)); cv.setCursor(196, 10); cv.printf("TAGS %d:%d", tagsGiven, tagsTaken);
  for (int i = 0; i < chatLogN; i++) { cv.setTextColor(strncmp(chatLog[i], "YOU:", 4) ? rgb(220, 200, 230) : rgb(140, 220, 200)); cv.setCursor(12, 24 + i * 11); cv.print(chatLog[i]); }
  cv.fillRect(10, 82, 300, 14, rgb(24, 16, 30)); cv.setTextColor(rgb(255, 255, 255)); cv.setCursor(14, 85);
  cv.print(chatDraft); if ((int)(tNow * 2) & 1) cv.print("_");
  for (int i = 0; i < 3; i++) {
    int x = 10 + i * 100; cv.fillRoundRect(x, 100, 96, 16, 4, rgb(50, 20, 50));
    char sh[17]; snprintf(sh, sizeof(sh), "%.15s", ROASTS[i]); cv.setTextColor(rgb(240, 200, 240)); cv.setCursor(x + 4, 104); cv.print(sh);
  }
  for (int r = 0; r < 3; r++) for (int k = 0; k < 10; k++) {
    int x = 8 + k * 30, y = 122 + r * 26;
    cv.fillRoundRect(x, y, 28, 22, 3, rgb(30, 26, 40)); cv.setTextColor(rgb(230, 230, 240)); cv.setCursor(x + 11, y + 7); char ks[2] = {KEYS[r][k], 0}; cv.print(ks);
  }
  static const char *bot[4] = {"SPACE", "DEL", "SEND", "CLOSE"};
  for (int i = 0; i < 4; i++) {
    int x = 8 + i * 76; cv.fillRoundRect(x, 200, 72, 24, 4, i == 2 ? rgb(30, 100, 50) : i == 3 ? rgb(70, 24, 30) : rgb(30, 26, 40));
    cv.setTextColor(rgb(240, 240, 240)); cv.setCursor(x + (72 - (int)strlen(bot[i]) * 6) / 2, 208); cv.print(bot[i]);
  }
}
static void chatTap(int x, int y) {
  size_t n = strlen(chatDraft);
  if (y >= 100 && y < 116) { int i = (x - 10) / 100; if (i >= 0 && i < 3) chatSend(ROASTS[i]); return; }
  if (y >= 122 && y < 200) {
    int r = (y - 122) / 26, k = (x - 8) / 30;
    if (r >= 0 && r < 3 && k >= 0 && k < 10 && n < sizeof(chatDraft) - 1) { chatDraft[n] = KEYS[r][k]; chatDraft[n + 1] = 0; hx::pop(0.1f, 0.01f); }
    return;
  }
  if (y >= 200) {
    int i = (x - 8) / 76;
    if (i == 0 && n < sizeof(chatDraft) - 1) { chatDraft[n] = ' '; chatDraft[n + 1] = 0; }
    else if (i == 1 && n > 0) chatDraft[n - 1] = 0;
    else if (i == 2) { chatSend(chatDraft); chatDraft[0] = 0; }
    else if (i == 3) chatOpen = false;
  }
}

static void drawEnding() {
  cv.fillRect(18, 34, 284, 160, rgb(4, 4, 10));
  cv.drawRoundRect(18, 34, 284, 160, 8, rgb(220, 190, 90));
  cv.setTextSize(2); cv.setTextColor(rgb(240, 215, 120)); cv.setCursor(58, 48); cv.print("THE DEEP IS SMALL"); cv.setTextSize(1);
  cv.setTextColor(rgb(230, 210, 150)); cv.setCursor(34, 76); cv.print(endingName);
  cv.setTextColor(rgb(200, 205, 215));
  static const char *lines[] = {"You have charted this place before.", "Not in this sky. In one you lost.",
                                "Up there the stars change every life.", "Down here the roads keep their names."};
  for (int i = 0; i < 4; i++) { cv.setCursor(34, 96 + i * 14); cv.print(lines[i]); }
  cv.setTextColor(rgb(120, 130, 150)); cv.setCursor(34, 174); cv.printf("lives %lu   tap to keep flying", (unsigned long)sm::sheet().lives);
}

static void draw() {
  if (dockAnim > 0) { drawDockSequence(); cv.pushSprite(0, 0); return; }
  if (drawAlienAftermath()) { cv.pushSprite(0, 0); return; }
  if (stationOpen) { cv.fillSprite(rgb(2, 4, 8)); drawStation(); cv.pushSprite(0, 0); return; }
  if (layer == 0) {
    alienLensOn = false;
    cv.fillSprite(rgb(1, 2, 5));
    drawNebulae();
    drawStarsReal();
    drawSun();
    drawDustReal();
  } else {
    alienLensOn = false;
    if (alien.active && alien.t < 22.9f) {
      float ax, ay, az;
      lensOn = false;
      if (project(alien.p, ax, ay, az)) {
        alienLensOn = true; alienLX = ax; alienLY = ay;
        alienLR = (20.f + 10.f * fsin(tNow * (alien.cls == 2 ? 9.f : 2.3f))) * (alien.t > 22.f ? 1.f + (alien.t - 22.f) * 3.f : 1.f);
      }
    }
    drawField(layer);
    if (alienLensOn) { lensOn = true; lensX = alienLX; lensY = alienLY; lensR = alienLR; }
    drawStarsDeep();
    drawStreamers();
  }
  drawObjects();
  drawBoomsAndBolts();
  if (alienHolds()) drawPowerLoss();            // the cockpit goes dark...
  if (alien.active && layer > 0) { drawAlienScan(alienLX, alienLY); drawAlienShape(); }   // ...it does not
  if (!alienHolds()) { drawTargeting(); drawHud(); }
  if (inMeeting) {   // who you're with, the tag score, and who's it
    cv.setTextColor(rgb(200, 110, 200)); cv.setCursor(112, 66);
    if (net::phase() == net::PH_LOST) cv.print("SIGNAL LOST");
    else cv.printf("%s  %d:%d", net::peerName(), tagsGiven, tagsTaken);
    if (itMe) { cv.setTextColor(((int)(tNow * 4) & 1) ? rgb(255, 80, 230) : rgb(160, 40, 150)); cv.setCursor(136, 78); cv.print("YOU'RE IT"); }
  }
  if (glitchT > 0.f) {   // the haywire arrival
    int bars = (int)(glitchT * 14);
    for (int i = 0; i < bars; i++) { int y = (int)(rnd() % H), hh = 1 + (int)(rnd() % 5), x = (int)(rnd() % W); cv.fillRect(x, y, 30 + (int)(rnd() % 160), hh, (rnd() & 1) ? rgb(230, 60, 230) : rgb(60, 230, 210)); }
  }
  if (launchAnim > 0) {
    float u = launchAnim / 0.9f;
    for (int i = 0; i < 12; i++) {
      float a = i * 0.5236f;
      int r0 = (int)(40 + (1 - u) * 160), r1 = r0 + 30;
      cv.drawLine(160 + (int)(cosf(a) * r0), 120 + (int)(sinf(a) * r0), 160 + (int)(cosf(a) * r1), 120 + (int)(sinf(a) * r1), rgb(90, 170, 255));
    }
  }
  if (crossFlash > 0.f) {
    // the crossing: a moment of pure white that falls away into the new layer
    float k = clampf(crossFlash, 0, 1);
    if (k > 0.55f) cv.fillSprite(mix565(hsv(layerHue(layer), 0.3f, 1.f), rgb(255, 255, 255), (k - 0.55f) / 0.45f));
    else if (k > 0.2f) for (int i = 0; i < 6; i++) cv.drawCircle(160, 120, (int)((1 - k) * 260) + i * 9, hsv(layerHue(layer) + i * 20, 0.5f, 1.f));
  }
  if (statusOpen) { if (statusPage) drawJournal(); else drawStatus(); }
  if (mapOpen) { if (mapPage) drawSystemMap(); else drawMap(); }
  if (chatOpen) drawChat();
  if (bootOpen) drawBoot();
  if (lostOpen) drawLost();
  if (endingOpen) drawEnding();
  cv.pushSprite(0, 0);
}

// ============================================================
//  Arduino entry points
// ============================================================
void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;
  cfg.internal_imu = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(110);
  cv.setColorDepth(16);
  cv.setPsram(true);          // the 150 KB frame lives in PSRAM
  cv.createSprite(W, H);
  initSin();
  initBlocks();
  hx::begin();
  M5.BtnB.setHoldThresh(600);
  M5.BtnA.setHoldThresh(600);   // hold A for the map
  M5.BtnC.setHoldThresh(600);   // hold C: status; hold C again: journal

  sm::sheetInit();
  sm::contractsInit();
  sm::simInit();
  bool restored = sm::sheetLoad();
  if (restored) { sm::universeRestoreSeed(sm::sheet().universeSeed); sm::contractsLoad(); sm::atlasLoad(); sm::journalLoad(); }
  else { sm::atlasClear(); sm::journalClear(); }
  // SD card: pilot.sav wins. A managed folder with no pilot.sav means the pilot
  // deleted it on purpose: new game. No folder marker yet: migrate flash to card.
  Session bootSession{};
  bool haveSession = false;
  if (sm::sdBegin()) {
    if (sm::sdHasSave() && sm::sdLoadGame(&bootSession, sizeof(bootSession))) {
      restored = true; haveSession = true;
      sm::universeRestoreSeed(sm::sheet().universeSeed);
    } else if (sm::sdManaged() && !sm::sdHasSave()) {
      sm::sheetInit();
      sm::universeReseed(millis() * 2654435761u ^ (uint32_t)ESP.getEfuseMac());
      sm::sheet().universeSeed = sm::universeSeed();
      sm::contractsInit(); sm::atlasClear(); sm::journalClear();
      restored = false;
    }
  }
  if (!haveSession && restored) {
    Preferences prefs;
    if (prefs.begin("sm_sess", true)) {
      if (prefs.getBytesLength("s") == sizeof(bootSession)) { prefs.getBytes("s", &bootSession, sizeof(bootSession)); haveSession = true; }
      prefs.end();
    }
  }
  rngState = sm::universeSeed() ? sm::universeSeed() : 0xA341316Cu;
  livesSeen = sm::sheet().lives;
  initMeshes();
  if (haveSession && resumeSession(bootSession)) {
    char b[112]; snprintf(b, sizeof(b), "RESUMED: %s", layer == 0 ? hereName : layerName(layer));
    setBanner(b, 3000);
  } else {
    makeRealScene(nullptr, true);
    setBanner(restored ? "SHEET RESTORED - STILL LOST" : "LOST IN SPACE. FLY A NAMED GATE, OR DOCK AND ASK AROUND.", 3600);
    if (!restored) sm::journalAdd("New license issued. Every name ahead is unwritten.");
  }
  saveAll();
  serviceSD(true);   // first boot with a card: the flash save migrates onto it
  crossFlash = 0.8f;
}

void loop() {
  uint32_t now = millis();
  static uint32_t prev = now;
  dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f);
  prev = now;
  tNow += dt;
  updateInput();
  updateWorld();
  hx::update(dt);
  draw();
}
