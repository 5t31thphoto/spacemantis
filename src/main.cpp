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
static float lensX = 0, lensY = 0, lensR = 0;

static inline void applyLens(float &sx, float &sy) {
  if (!lensOn) return;
  float dx = sx - lensX, dy = sy - lensY, d2 = dx * dx + dy * dy + 1.f;
  float push = clampf(lensR * lensR / d2, 0.f, 2.2f);
  sx += dx * push; sy += dy * push;
}
static inline bool project(V3 w, float &sx, float &sy, float &z) {
  V3 c = shipB.toLocal(w - shipPos);
  z = c.z;
  if (z < 0.35f) return false;
  float k = FOCAL * fovPulse / z;
  sx = W * 0.5f + c.x * k; sy = H * 0.5f - c.y * k;
  applyLens(sx, sy);
  return true;
}
static inline bool projectDir(V3 d, float &sx, float &sy) {
  V3 c = shipB.toLocal(d);
  if (c.z < 0.05f) return false;
  float k = FOCAL * fovPulse / c.z;
  sx = W * 0.5f + c.x * k; sy = H * 0.5f - c.y * k;
  applyLens(sx, sy);
  return true;
}
static inline bool onScreen(float sx, float sy, float m = 0) { return sx >= -m && sx < W + m && sy >= -m && sy < H + m; }

// ============================================================
//  meshes: convex hulls of small point sets, flat shaded
// ============================================================
struct Mesh {
  uint8_t nv = 0, nt = 0, ne = 0;
  V3 v[14];
  uint8_t t[40][3];
  V3 n[40];
  uint8_t e[64][4];   // a, b, face0, face1 (255 = none)
};
static bool faceHas(const Mesh &m, int f, uint8_t a, uint8_t b) {
  bool ha = m.t[f][0] == a || m.t[f][1] == a || m.t[f][2] == a;
  bool hb = m.t[f][0] == b || m.t[f][1] == b || m.t[f][2] == b;
  return ha && hb;
}
static void buildHull(Mesh &m, const V3 *pts, int n) {
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

enum MeshId : uint8_t { M_STATION = 0, M_COBRA, M_VIPER, M_SIDEWINDER, M_SHUTTLE, M_KRAIT, M_POD, M_TETRA, M_GHOST, M_ROCK0, M_COUNT = M_ROCK0 + 6 };
static Mesh meshes[M_COUNT];

static void initMeshes() {
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
enum BodyType : uint8_t { BT_GIANT = 0, BT_ROCKY, BT_HOLE, BT_WRONGSTAR };

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
};
static constexpr int MAX_OBJ = 44;
static Obj objs[MAX_OBJ];

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
static void killObj(int i) { if (i >= 0 && i < MAX_OBJ) objs[i].kind = K_NONE; }
static void clearWorld() { for (auto &o : objs) o.kind = K_NONE; }
static int countKind(uint8_t k) { int n = 0; for (auto &o : objs) if (o.kind == k) n++; return n; }
static float distTo(const Obj &o) { return len(o.p - shipPos); }
static float surfaceDist(const Obj &o) { return distTo(o) - o.radius; }

// ============================================================
//  scene state
// ============================================================
static V3 sunDir{0.4f, 0.3f, 0.86f};
static uint16_t sunCol = 0;
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
static int16_t blkAngQ[BW * BH], blkRadQ[BW * BH], blkLogQ[BW * BH];
static inline float blkAng(int i) { return blkAngQ[i] * (3.1415927f / 10000.f); }
static inline float blkRad(int i) { return blkRadQ[i] * (1.f / 20000.f); }
static inline float blkLog(int i) { return blkLogQ[i] * (1.f / 8000.f); }
static void initBlocks() {
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
  sunCol = st < 0.3f ? rgb(255, 200, 140) : st < 0.8f ? rgb(255, 244, 220) : rgb(200, 220, 255);
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
      V3 d = shipB.f + shipB.r * u + shipB.u * v;
      cv.fillRect(x + shift, y, BLK, BLK, fieldColor(l, bi, d, t));
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

static void drawSun() {
  float sx, sy;
  if (!projectDir(sunDir, sx, sy) || !onScreen(sx, sy, 60)) return;
  int x = (int)sx, y = (int)sy;
  int r1, g1, b1; unrgb(sunCol, r1, g1, b1);
  for (int i = 0; i < 6; i++) {
    float k = (i + 1) / 6.f;
    cv.fillCircle(x, y, 30 - i * 5, rgb((int)(r1 * k * k * 0.9f), (int)(g1 * k * k * 0.8f), (int)(b1 * k * k * 0.7f)));
  }
  cv.fillCircle(x, y, 4, rgb(255, 255, 255));
  for (int i = 0; i < 6; i++) {
    float a = i * 1.047f + tNow * 0.03f;
    cv.drawLine(x, y, x + (int)(cosf(a) * 46), y + (int)(sinf(a) * 46), shade(sunCol, 0.35f));
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

static bool drawMesh(const Obj &ob, const Mesh &m, float scale, uint16_t base, uint16_t edgeCol, bool solid, float &outR) {
  float sx[14], sy[14], cx, cy, zc;
  V3 wv[14];
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
    float k = 0.16f + 0.84f * clampf(dot(nw, light), 0.f, 1.f);
    if (layer > 0) k = 0.3f + 0.7f * k;
    cv.fillTriangle((int)sx[m.t[f][0]], (int)sy[m.t[f][0]], (int)sx[m.t[f][1]], (int)sy[m.t[f][1]],
                    (int)sx[m.t[f][2]], (int)sy[m.t[f][2]], shade(base, k));
  }
  for (int e = 0; e < m.ne; e++) {
    bool v0 = vis[m.e[e][2]], v1 = m.e[e][3] != 255 && vis[m.e[e][3]];
    if (solid && !v0 && !v1) continue;
    cv.drawLine((int)sx[m.e[e][0]], (int)sy[m.e[e][0]], (int)sx[m.e[e][1]], (int)sy[m.e[e][1]], (v0 || v1) ? edgeCol : shade(edgeCol, 0.35f));
  }
  return true;
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
struct Bolt { float x0, y0, x1, y1; uint8_t life; uint16_t col; };
static Bolt bolts[10];
static uint8_t boltN = 0;
static void addBolt(float x0, float y0, float x1, float y1, uint16_t col) {
  if (boltN < 10) bolts[boltN++] = {x0, y0, x1, y1, 7, col};
}

// ============================================================
//  the long arc and the money
// ============================================================
static void saveAll() { sm::sheetSave(); sm::contractsSave(); lastSave = millis(); }

static void onDestroyedFlow();
static bool checkDestroy() {
  bool died = sm::sheet().lives > livesSeen;
  if (died) onDestroyedFlow();
  livesSeen = sm::sheet().lives;
  return died;
}
static bool damage(int amount) {
  if (amount > 0) {
    sm::damageHull((uint16_t)amount);
    hx::thud(clampf(amount / 25.f, 0.35f, 1.f));
    hitFlash = 0.35f;
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
static uint16_t gateColor(const Obj &g) {
  if (g.kind == K_DOCKGATE) return rgb(70, 150, 255);
  if (g.gflags & GF_JOB) return rgb(240, 200, 90);
  if (g.gflags & (GF_FIXED | GF_LOCALNAME)) return rgb(230, 175, 80);
  if (g.gflags & GF_CHAIN) return layer == 0 ? rgb(110, 240, 200) : hsv(layerHue(layer) + 60, 0.55f, 0.95f);
  if (g.gflags & GF_RUMOR) return rgb(80, 225, 215);
  if (g.gflags & GF_KNOWN) return rgb(110, 230, 150);
  return rgb(150, 150, 170);
}

static Obj *spawnGate(V3 p, V3 facing, const char *name, uint8_t depth, uint8_t flags, float R) {
  Obj *g = newObj(K_GATE);
  if (!g) return nullptr;
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
    o->p = p + shipB.f * 25.f;
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
static void placeStation(V3 p, V3 facing) {
  Obj *st = newObj(K_STATION);
  if (!st) return;
  st->p = p; st->o = Basis::facing(facing, V3{0, 1, 0}); st->radius = 22.f; st->mesh = M_STATION;
  st->spin = 0.25f; st->col = rgb(150, 160, 175); st->enc = sm::ENC_STATION;
  asciiCopy(st->name, sizeof(st->name), sm::encounterFlavor(sm::ENC_STATION, (uint8_t)layer).name);
  Obj *dg = newObj(K_DOCKGATE);
  if (!dg) { st->kind = K_NONE; return; }
  dg->p = p + st->o.f * 38.f; dg->o = st->o; dg->radius = 7.f;
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

// Named gates fan out across your view, the way a harbour lays out its lanes.
static void placeDestGates(int want, V3 avoidDir = V3{0, 0, 0}) {
  sm::GateOffer hand[sm::MAX_GATE_HAND];
  int n = sm::buildGateHand(0, hand, sm::MAX_GATE_HAND);
  if (n > want) n = want;
  sm::Contract &c = sm::contract();
  const char *names[8]; uint8_t depths[8], flags[8]; int m = 0;
  if (c.live && c.dest[0]) { names[m] = c.dest; depths[m] = c.destDepth; flags[m] = GF_DEST | GF_JOB | GF_KNOWN; m++; }
  for (int i = 0; i < n && m < 6; i++) {
    if (c.live && strncmp(hand[i].name, c.dest, sm::NAME_LEN) == 0) continue;
    names[m] = hand[i].name; depths[m] = hand[i].depthRating;
    flags[m] = GF_DEST | (hand[i].unknown ? GF_UNKNOWN : (isRumor(hand[i].name) ? GF_RUMOR : GF_KNOWN)) | (hand[i].persistent ? GF_FIXED : 0);
    m++;
  }
  float span = 2.3f;                       // about 130 degrees of sky
  for (int k = 0; k < m; k++) {
    float a = -span * 0.5f + span * (k + 0.5f) / m + rf(-0.08f, 0.08f);
    float el = ((k & 1) ? 0.16f : -0.12f) + rf(-0.06f, 0.06f);
    V3 dir = norm(shipB.f * cosf(a) + shipB.r * sinf(a) + shipB.u * el);
    if (len(avoidDir) > 0.5f && dot(dir, avoidDir) > 0.97f) dir = norm(dir + shipB.u * 0.35f);   // keep clear of the dock
    V3 p = shipPos + dir * rf(105, 150);
    spawnGate(p, norm(shipPos - p), names[k], depths[k], flags[k], 9.f);
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
  buildSky(sm::universeSeed() ^ rnd());
  asciiCopy(hereName, sizeof(hereName), place && place[0] ? place : sm::placeName(sm::urand(), 0, false));
  upcase(hereName);
  shipPos = V3{0, 0, 0}; prevShipPos = shipPos;
  shipB = Basis::facing(V3{0, 0, 1}, V3{0, 1, 0});
  layer = 0;
  resetDust();
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
  }
  placeDestGates(ri(3, 5), stationDir);
  if (rf(0, 1) < 0.45f) {
    V3 c = shipPos + randDir() * 60.f + shipB.f * 200.f;
    int n = ri(5, 8);
    for (int i = 0; i < n; i++) spawnRock(c + randDir() * rf(8, 45), rf(2.5f, 8.f));
  }
  spawnTimer = rf(4, 8);
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
      spawnLandmarkObj(lm, aheadPoint(rf(160, 230), (rf(0, 1) < 0.5f ? 1.f : -1.f) * rf(70, 120), rf(-40, 40)));
  if (layer == 4) rngState = keep ^ rnd();
  spawnTimer = rf(3, 6);
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
  if (o->enc == sm::ENC_SECURITY && !ghost && sm::sheet().heat[sm::HEAT_SECURITY] > 50) o->hostile = true;
  if (layer == 0 && (kind == sm::ENC_PIRATE || kind == sm::ENC_SECURITY || kind == sm::ENC_MERCHANT)) {
    char b[112]; snprintf(b, sizeof(b), "CONTACT: %s", o->name); noteBanner(b, 1600);
  }
}

// ============================================================
//  targeting + context verbs
// ============================================================
enum VerbId : uint8_t { VB_NONE = 0, VB_HAIL, VB_ATTACK, VB_DOCK, VB_MINE, VB_SCOOP, VB_SALVAGE, VB_RESCUE, VB_READ, VB_SCAN, VB_CHART };
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
  switch (o.kind) {
    case K_SHIP:
      add(VB_HAIL, o.enc == sm::ENC_HOSTILE ? "SIGNAL" : "HAIL", G, 160, o.done ? "SILENT" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90, nullptr);
      break;
    case K_ANOMALY:
      add(VB_SCAN, "SCAN", V, 140, o.done ? "READ" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90, nullptr);
      break;
    case K_STATION: add(VB_DOCK, "DOCK", B, 0, nullptr); break;
    case K_ROCK: add(VB_MINE, "MINE", A, 40, o.uses == 0 ? "SPENT" : (holdFull ? "HOLD FULL" : nullptr)); break;
    case K_BODY:
      if (o.bodyType == BT_GIANT)
        add(VB_SCOOP, "SCOOP", C, o.radius * 0.45f, p.fuel >= p.fuelCap ? "TANK FULL" : (o.timer > 0 ? "SETTLING" : nullptr));
      break;
    case K_POD: add(VB_RESCUE, "RESCUE", G, 40, nullptr); break;
    case K_WRECK:
      add(VB_SALVAGE, "SALVAGE", A, 40, o.done ? "STRIPPED" : (holdFull ? "HOLD FULL" : nullptr));
      add(VB_ATTACK, "ATTACK", R, 90, nullptr);
      break;
    case K_ARTIFACT:
      add(VB_READ, "READ", V, 55, o.done ? "READ" : nullptr);
      add(VB_ATTACK, "ATTACK", R, 90, nullptr);
      break;
    case K_LANDMARK: add(VB_CHART, "CHART", Y, 170, nullptr); break;
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
  // They trade in routes and fixed points. Surface money means nothing here.
  int charted = 0;
  for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && sm::landmarkDiscovered(sm::landmarkAt(i)->id)) charted++;
  if (sm::rankOf(sm::CR_DEPTHRUNNER) < 3 || charted < 3) {
    setBanner("GHOST FLEET: they do not answer pilots who still count stars.", 2800);
    sm::grantXp(sm::CR_DEPTHRUNNER, 4);
  } else if (!sm::flagHas("ghost_trade")) {
    int pay = 250 + charted * 80;
    sm::addCredits(pay);
    sm::flagSet("ghost_trade", 1, false);
    char b[112]; snprintf(b, sizeof(b), "GHOST FLEET buys your fixed points. They already knew most of them. | +%dcr", pay);
    setBanner(b, 3200);
  } else {
    setBanner("GHOST FLEET: a cold nod. The cove remembers your hull.", 2400);
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

  if (verb == VB_CHART) {
    const sm::Landmark *lm = nullptr;
    for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkAt(i) && (int)sm::landmarkAt(i)->id == o.lmId) lm = sm::landmarkAt(i);
    if (!lm) return;
    int life = sm::landmarkChartedLife(lm->id);
    bool first = life == 0;
    bool otherLife = life != 0 && life != (int)(1 + sm::sheet().lives % 120);
    sm::discoverLandmark(lm->id);
    sm::grantXp(sm::CR_DEPTHRUNNER, first ? (uint16_t)(18 + lm->band * 6) : 4);
    if (otherLife) showRecognition(lm);
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

  char tail[48] = ""; size_t k = 0;
  int dc = (int)(p.credits - cr0), dh = (int)p.hull - hull0, df = (int)p.fuel - fuel0, dhold = (int)p.holdUsed - (int)hold0;
  if (dc) k += snprintf(tail + k, sizeof(tail) - k, " %+dcr", dc);
  if (dh && k < sizeof(tail)) k += snprintf(tail + k, sizeof(tail) - k, " %+dhull", dh);
  if (df && k < sizeof(tail)) k += snprintf(tail + k, sizeof(tail) - k, " %+dfuel", df);
  if (dhold > 0 && k < sizeof(tail)) snprintf(tail + k, sizeof(tail) - k, " +%dhold", dhold);
  char b[112]; snprintf(b, sizeof(b), "%s%s%s", out.blurb ? out.blurb : "...", tail[0] ? " |" : "", tail);
  setBanner(b, 3000);

  if (attack && out.destroyedOther) {
    addBoom(o.p, o.radius * 3.f, rgb(255, 170, 60));
    hx::boom(1.f);
    if (target == oi) target = -1;
    killObj(oi);
  } else {
    o.done = true; o.engaged = false; o.timer = 20.f;
    // a fight that doesn't end in fire usually ends in someone leaving
    if (o.kind == K_SHIP && o.hostile && rf(0, 1) < 0.7f) { o.hostile = false; o.v = norm(o.p - shipPos) * 16.f; }
    if (o.kind == K_POD && !attack) { if (target == oi) target = -1; killObj(oi); }
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
    maxVolleys = (uint8_t)(verb == VB_MINE ? 4 + sm::capTier(sm::CAP_MINING) : 5);
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
  if (id == VB_HAIL) setBanner("OPENING COMM...", 900);
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
        addBolt(160, H - 6, sx + rf(-4, 4), sy + rf(-4, 4), theaterVerb == VB_MINE ? rgb(170, 255, 110) : rgb(255, 190, 110));
        hx::pop(0.25f, 0.02f);
      }
      if (volleys >= maxVolleys) finishTheater();
    }
    return;
  }
  if (theaterBeat > 0.3f) {   // combat
    theaterBeat = 0; volleys++;
    if (!ambushed || volleys > 1) {
      addBolt(120, H - 4, sx + rf(-5, 5), sy + rf(-5, 5), rgb(130, 255, 190));
      addBolt(200, H - 4, sx + rf(-5, 5), sy + rf(-5, 5), rgb(130, 255, 190));
      hx::pop(0.55f, 0.03f);
    }
    if ((rnd() % 100) < (uint32_t)(50 + layer * 6)) {
      addBolt(sx, sy, 160 + rf(-40, 40), H - 10, rgb(255, 90, 70));
      hx::thud(0.55f);
      hitFlash = 0.18f;
    }
    if (volleys >= maxVolleys) { ambushed = false; finishTheater(); }
  }
}

// ============================================================
//  station
// ============================================================
static constexpr int STATION_ROWS = 6;
static bool stationOpen = false;
static int stationChoice = 0;
static int stationIdx = -1;
static float dockAnim = 0;      // docking sequence playing
static float launchAnim = 0;    // being taxied back out
static char stationMoodText[112] = "";
static sm::Opportunity stationOpportunity{};
static bool opportunityTaken = false;
static int gearCap = 0;
static char courseName[24] = "";   // a destination committed at the board
static uint8_t courseDepth = 0, courseFlags = 0;

static int refuelCost() {
  const sm::Pilot &p = sm::sheet();
  return (p.fuelCap - p.fuel) * sm::fuelPrice() + (p.hullMax - p.hull) * sm::repairPrice();
}
static int rumorPrice() { return 12; }
static int gearPrice() { return 80 + sm::capTier((sm::CapId)gearCap) * 55; }
static int sellableValue(bool doSell) {
  sm::Pilot &p = sm::sheet();
  const char *owned = sm::contract().live ? sm::contractCargo(sm::contract().kind) : nullptr;
  int pay = 0; char names[sm::MAX_HAUL_LINES][sm::NAME_LEN]; int nn = 0;
  for (uint8_t i = 0; i < p.haulN; ++i) {
    if (owned && strncmp(p.haul[i].what, owned, sm::NAME_LEN) == 0) continue;
    pay += p.haul[i].amount * sm::marketPrice(p.haul[i].what, p.haul[i].legal != 0);
    strncpy(names[nn], p.haul[i].what, sm::NAME_LEN); nn++;
  }
  if (doSell) for (int i = 0; i < nn; i++) sm::haulRemove(names[i]);
  return pay;
}

static void openBoard() {
  stationOpen = true; stationChoice = 0;
  sm::contractOffer();
  opportunityTaken = !sm::makeOpportunity(stationOpportunity, 0);
  gearCap = (int)(sm::urand() % sm::CAP_COUNT);
  if (sm::sheet().rank[sm::CR_DEPTHRUNNER] >= 4 && (sm::urand() % 100) < 35) gearCap = sm::CAP_STABILIZER;
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
    shipPos = st->p + st->o.f * 52.f; prevShipPos = shipPos;
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
    case 1:   // work: accepting sets the course
      if (sm::contract().live) { sm::contractAbandon(); setBanner("LEAD DROPPED", 1400); break; }
      if (sm::contractAccept(off)) {
        snprintf(buf, sizeof(buf), "ACCEPTED: %s", off.title); setBanner(buf, 2000);
        if (off.dest[0]) { setCourse(off.dest, off.destDepth, GF_DEST | GF_JOB | GF_KNOWN); launch(); return; }
      } else setBanner(off.kind == sm::CK_MARKET ? "CANNOT COVER THE CARGO" : "NO ROOM IN THE HOLD", 1800);
      break;
    case 2: {   // a rumor is a place you can now fly to: buying it sets the course
      if (!sm::spendCredits(rumorPrice())) { setBanner("THE RUMOR SELLER WANTS MONEY", 1500); break; }
      char name[24]; asciiCopy(name, sizeof(name), sm::placeName(sm::urand(), 0, false));
      uint8_t d = sm::placeDepth(name);
      sm::rumorAdd(name, d, 14);
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
      } else {
        uint8_t t = sm::capTier((sm::CapId)gearCap);
        if (t >= 10) { setBanner("NOTHING HERE BEATS WHAT YOU FLY", 1500); break; }
        if (sm::spendCredits(gearPrice())) {
          sm::earnCap((sm::CapId)gearCap, (uint8_t)(t + 1));
          snprintf(buf, sizeof(buf), "%s %u - EQUIPPED", sm::capName((sm::CapId)gearCap), t + 1); upcase(buf);
          setBanner(buf, 1900);
          gearCap = (int)(sm::urand() % sm::CAP_COUNT);
        } else setBanner("GEAR IS TOO EXPENSIVE HERE", 1500);
      }
      break;
    }
    case 4:
      if (opportunityTaken) { setBanner("THE BOARD IS EMPTY", 1200); break; }
      if (sm::contract().live) { setBanner("FINISH OR DROP YOUR LEAD FIRST", 1600); break; }
      if (sm::contractFromOpportunity(stationOpportunity)) {
        opportunityTaken = true;
        snprintf(buf, sizeof(buf), "LEAD: %s", stationOpportunity.title); setBanner(buf, 2000);
        const sm::Contract &c = sm::contract();
        if (c.dest[0]) { setCourse(c.dest, c.destDepth, GF_DEST | GF_JOB | GF_KNOWN); launch(); return; }
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
  char place[24];
  bool station;
  if (turnedBack) { asciiCopy(place, sizeof(place), sm::placeName(sm::urand(), 0, false)); station = rf(0, 1) < 0.35f; }
  else { asciiCopy(place, sizeof(place), tr.dest); station = tr.unknown ? rf(0, 1) < 0.45f : rf(0, 1) < 0.85f; }
  makeRealScene(place, station);
  sm::onResurface();
  char b[112];
  if (!turnedBack) {
    sm::knownGateAdd(tr.dest, tr.destDepth);
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
    if (damage(c.damage)) return;
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
      snprintf(b, sizeof(b), "%s. THE FIXED POINT IS HERE.", tr.dest);
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
  if (g.gflags & GF_DEST) {
    // the choice: this is where we are going
    dockTarget = -1;
    sm::tripBegin(g.name, g.depth, (g.gflags & GF_UNKNOWN) != 0, (g.gflags & GF_FIXED) != 0);
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
  theater = TH_NONE; stationOpen = false; dockAnim = 0; launchAnim = 0;
  queuedN = 0;
  hx::boom(1.f);
  crossFlash = 1.f;
  makeRealScene(nullptr, rf(0, 1) < 0.5f);
  // Diegetic: the pilot thinks they are lost — not that a cosmos was replaced.
  setBanner(sm::lossLine(sm::sheet().lives, sm::urand()), 4200);
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

static constexpr int ROW_Y0 = 46, ROW_PITCH = 23, ROW_H = 20;
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
    if (score < bestScore) { bestScore = score; best = i; }
  }
  target = best;
  if (best >= 0) hx::pop(0.22f, 0.015f);
}

static void handleTap(int x, int y) {
  if (endingOpen) { endingOpen = false; setBanner("KEEP FLYING. THE NAMES WILL BE THERE.", 3000); return; }
  if (stationOpen) {
    if (y >= ROW_Y0 && y < ROW_Y0 + STATION_ROWS * ROW_PITCH) {
      int row = (y - ROW_Y0) / ROW_PITCH;
      if (row == stationChoice) stationCommit(); else { stationChoice = row; hx::pop(0.15f, 0.01f); }
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
  bool flying = !stationOpen && !endingOpen && dockAnim <= 0;
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

  if (endingOpen) { if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) handleTap(0, 0); return; }
  if (stationOpen) {
    if (M5.BtnA.wasPressed()) launch();
    else if (M5.BtnB.wasPressed()) stationCommit();
    else if (M5.BtnC.wasPressed()) { stationChoice = (stationChoice + 1) % STATION_ROWS; hx::pop(0.12f, 0.01f); }
    return;
  }
  if (M5.BtnA.wasPressed()) cycleTarget();
  if (M5.BtnB.wasPressed()) { captureNeutral(); tiltX = tiltY = tiltRoll = 0; rateYaw = ratePitch = rateRoll = 0; setBanner("ATTITUDE CENTERED", 900); hx::pop(0.2f, 0.02f); }
  if (M5.BtnC.wasPressed()) { throttleT = 0.5f; setBanner("CRUISE", 700); hx::pop(0.2f, 0.02f); }

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
    const float dead = 0.35f;
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
static float cruiseSpeed() { return layer == 0 ? 13.f : 15.f + layer * 2.5f; }
static float speedWanted() {
  // 0 .. 0.5 ramps stop..cruise, 0.5 .. 1 ramps cruise..boost (2x)
  float c = cruiseSpeed();
  return throttleT <= 0.5f ? c * (throttleT / 0.5f) : c * (1.f + (throttleT - 0.5f) * 2.f);
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
  float side = ax - 38.f;                      // distance in front of the dock gate
  if (ax < 32.f && latd < 60.f) aim = st.p + out * 85.f + f * clampf(ax, -40.f, 60.f);    // clear the hull
  else if (ax < 32.f) aim = st.p + f * 95.f + out * 25.f;                                    // round to the front
  else if (latd > side * 0.3f + 2.f) aim = st.p + f * (38.f + clampf(side * 0.5f, 12.f, 80.f)); // onto the axis
  else aim = g.p - f * 6.f;                                                                   // through the slot
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
    if (o.kind == K_NONE) continue;
    if (o.timer > 0) o.timer -= dt;
    if (o.spin != 0 && o.kind != K_SHIP && o.kind != K_STATION) { o.o.roll(o.spin * dt); o.o.yaw(o.spin * 0.37f * dt); o.o.fix(); }
    if (o.kind == K_STATION) { o.o.roll(o.spin * dt); o.o.fix(); }
    if (o.kind == K_SHIP) {
      V3 toMe = shipPos - o.p;
      float d = len(toMe);
      if (o.hostile && !o.engaged && o.timer <= 0 && theater == TH_NONE) {
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

static void collide() {
  for (int i = 0; i < MAX_OBJ; i++) {
    Obj &o = objs[i];
    if (!(o.kind == K_STATION || o.kind == K_ROCK || o.kind == K_BODY || o.kind == K_LANDMARK || o.kind == K_SHIP)) continue;
    float r = o.kind == K_STATION ? o.radius * 1.3f : o.radius * 1.05f;
    V3 d = shipPos - o.p;
    float l = len(d);
    if (l >= r || l < 1e-3f) continue;
    V3 nrm = d * (1.f / l);
    shipPos = o.p + nrm * (r + 0.2f);
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
      setBanner(o.kind == K_BODY ? "ATMOSPHERE SKIP - SHIELDS SCREAM" : "SCRAPED THE HULL", 1200);
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
    V3 x = lerp3(prevShipPos, shipPos, prev / (prev - side));
    if (len(x - g.p) < g.radius) { threadGate(g); return; }   // world may have changed
    if (g.kind == K_PORTAL && len(x - g.p) < g.radius * 2.2f) {
      setBanner("THE PORTAL REJECTS A CROOKED APPROACH - COME ROUND", 1800);
      hx::stutter(0.4f, 3, 0.08f);
    }
  }
}

static void hapticWorld() {
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
  sm::simTick(millis());
  sm::contractTick();
  serviceBanner();
  sm::Contract done;
  if (sm::contractTakeCompleted(done)) {
    char b[112]; snprintf(b, sizeof(b), "JOB DONE: %s +%dcr", done.title, done.pay);
    noteBanner(b, 2800); hx::swell(0.5f, 0.1f, 0.3f);
  }
  if (crossFlash > 0) crossFlash -= dt * 2.2f;
  if (deepFlash > 0) deepFlash -= dt * 3.5f;
  if (hitFlash > 0) hitFlash -= dt;
  fovPulse = layer >= 3 ? 1.f + 0.035f * (layer - 2) * sinf(tNow * 1.7f) + (layer == 4 ? deepFlash * 0.06f : 0.f) : 1.f;

  if (dockAnim > 0) { dockAnim -= dt; if (dockAnim <= 0) openBoard(); return; }
  if (stationOpen || endingOpen) return;
  if (launchAnim > 0) launchAnim -= dt;

  // attitude: tilt aims, with a little mass
  if (dockTarget >= 0) autopilot();
  else {
    float k = 1.f - expf(-dt / 0.12f);
    rateYaw += (-tiltX * 1.15f - rateYaw) * k;
    ratePitch += (-tiltY * 1.0f - ratePitch) * k;
    rateRoll += (tiltRoll * 1.2f - rateRoll) * k;
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
      float k = (1.f - off / 0.45f) * 0.55f * dt;
      shipB.yaw(clampf(atan2f(c.x, c.z), -k, k));
      shipB.pitch(clampf(-atan2f(c.y, c.z), -k, k));
      break;
    }
    shipB.fix();
  }
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
  for (auto &b : booms) if (b.alive) { b.t += dt; if (b.t > 1.3f) b.alive = false; }
  if (millis() > lastSave + 30000) saveAll();
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
    cv.setTextColor(col); cv.setCursor((int)sx - 12, (int)(sy - r - 11)); cv.print("DOCK");
  } else if (r > 2.5f && (o.gflags & GF_DEST) && z < 700) {
    cv.setTextColor(col);
    cv.setCursor((int)sx - (int)strlen(o.name) * 3, (int)(sy - r - 20)); cv.print(o.name);
    sm::DepthAbility da = sm::depthQuery(o.depth);
    bool risky = o.depth > da.maxBand;
    char dl[28];
    snprintf(dl, sizeof(dl), "DEPTH %u%s%s", o.depth, risky ? " !" : "", (o.gflags & GF_JOB) ? "  JOB" : (o.gflags & GF_UNKNOWN) ? "  ?" : "");
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
  lensOn = false;
  for (int k = 0; k < n; k++) {
    Obj &o = objs[order[k]];
    float sx, sy, z, outR;
    switch (o.kind) {
      case K_BODY: drawBody(o); break;
      case K_STATION:
        drawMesh(o, meshes[M_STATION], o.radius, o.col, rgb(200, 215, 230), true, outR);
        if (project(o.p + o.o.f * o.radius * 0.98f, sx, sy, z) && dot(o.o.f, shipPos - o.p) > 0) {
          int w = (int)clampf(o.radius * 0.55f * FOCAL / z, 2, 80), h = w / 3 + 1;
          cv.fillRect((int)sx - w / 2, (int)sy - h / 2, w, h, rgb(4, 6, 10));
          cv.drawRect((int)sx - w / 2, (int)sy - h / 2, w, h, ((int)(tNow * 3) & 1) ? rgb(90, 170, 255) : rgb(40, 80, 140));
        }
        break;
      case K_SHIP:
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
          drawMesh(o, meshes[o.mesh], o.radius, o.col, o.ghost ? rgb(170, 190, 200) : shade(o.col, 1.5f), true, outR);
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
    if (b.life > 4) cv.drawLine((int)b.x0 + 1, (int)b.y0, (int)b.x1 + 1, (int)b.y1, rgb(255, 255, 220));
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
  if (o.kind == K_GATE && (o.gflags & GF_DEST)) snprintf(info, sizeof(info), "%dm", sd);
  else snprintf(info, sizeof(info), "%s %dm", o.name, sd);
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
    if (c.dest[0]) cv.printf("-> %s  depth %u", c.dest, c.destDepth); else cv.print(sm::contractHint(c));
  }

  // depth ladder, left edge: where you are, and where the trip turns
  for (int l = 0; l < LAYERS; l++) {
    int y = 70 + l * 16;
    bool here = l == layer;
    cv.fillRect(2, y, here ? 5 : 3, 10, here ? hsv(layerHue(l) + 40, 0.6f, 1.f) : rgb(50, 56, 66));
    if (tr.active && l == tr.destDepth) cv.drawRect(1, y - 1, 9, 12, rgb(240, 210, 120));
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

static void drawStation() {
  sm::Contract &off = sm::contractOfferPeek();
  sm::Contract &c = sm::contract();
  sm::Pilot &p = sm::sheet();
  cv.fillRect(8, 4, 304, 206, rgb(5, 10, 16));
  cv.drawRoundRect(8, 4, 304, 206, 8, rgb(70, 150, 255));
  const char *nm = stationIdx >= 0 && objs[stationIdx].kind == K_STATION ? objs[stationIdx].name : "STATION";
  cv.setTextSize(2); cv.setTextColor(rgb(100, 180, 255)); cv.setCursor(18, 10); cv.print(nm); cv.setTextSize(1);
  cv.setTextColor(rgb(140, 150, 165)); cv.setCursor(176, 10); cv.printf("$%ld  H%d/%d", (long)p.credits, p.hull, p.hullMax);
  cv.setCursor(176, 20); cv.printf("F%d/%d  hold %u/%u", p.fuel, p.fuelCap, p.holdUsed, p.holdCap);
  char mood[48]; strncpy(mood, stationMoodText, 46); mood[46] = 0;
  cv.setTextColor(rgb(110, 125, 140)); cv.setCursor(18, 32); cv.print(mood);

  char rows[STATION_ROWS][56];
  int rc = refuelCost();
  if (rc > 0) snprintf(rows[0], 56, "REFUEL / REPAIR   %dcr", rc); else snprintf(rows[0], 56, "REFUEL / REPAIR   topped up");
  if (c.live) snprintf(rows[1], 56, "DROP LEAD: %s", c.title); else snprintf(rows[1], 56, "WORK: %s  +%dcr", off.title, off.pay);
  snprintf(rows[2], 56, "BUY A RUMOR   %dcr", rumorPrice());
  int sell = sellableValue(false);
  if (sell > 0) snprintf(rows[3], 56, "SELL HAUL   +%dcr", sell);
  else {
    char cn[16]; snprintf(cn, sizeof(cn), "%s", sm::capName((sm::CapId)gearCap)); upcase(cn);
    snprintf(rows[3], 56, "BUY %s %u   %dcr", cn, sm::capTier((sm::CapId)gearCap) + 1, gearPrice());
  }
  if (opportunityTaken) snprintf(rows[4], 56, "BOARD: (taken)"); else snprintf(rows[4], 56, "BOARD: %s +%d", stationOpportunity.title, stationOpportunity.reward);
  snprintf(rows[5], 56, "LAUNCH");
  for (int i = 0; i < STATION_ROWS; ++i) {
    int y = ROW_Y0 + i * ROW_PITCH; bool sel = i == stationChoice;
    cv.fillRoundRect(18, y, 284, ROW_H, 4, sel ? rgb(25, 90, 90) : rgb(16, 24, 32));
    cv.setTextColor(sel ? rgb(120, 255, 210) : rgb(180, 190, 200));
    cv.setCursor(28, y + 6); cv.print(rows[i]);
  }
  char dbuf[112] = "";
  switch (stationChoice) {
    case 0: snprintf(dbuf, sizeof(dbuf), "fuel %dcr/u  hull %dcr/u. you stay docked.", sm::fuelPrice(), sm::repairPrice()); break;
    case 1: {
      const sm::Contract &j = c.live ? c : off;
      if (j.dest[0]) snprintf(dbuf, sizeof(dbuf), "to %s, depth %u. taking it sets your course.", j.dest, j.destDepth);
      else snprintf(dbuf, sizeof(dbuf), "%s", sm::contractHint(j));
      break;
    }
    case 2: snprintf(dbuf, sizeof(dbuf), "a place you haven't heard of. buying it sets your course."); break;
    case 3: snprintf(dbuf, sizeof(dbuf), "%s", sell > 0 ? "hold lines your lead doesn't own" : "earned capability is equipped at once"); break;
    case 4: asciiCopy(dbuf, sizeof(dbuf), opportunityTaken ? "" : stationOpportunity.detail); break;
    case 5: snprintf(dbuf, sizeof(dbuf), "back out among the gates"); break;
  }
  cv.setTextColor(rgb(200, 180, 110));
  printWrapped(18, ROW_Y0 + STATION_ROWS * ROW_PITCH + 2, 46, 2, 10, dbuf);
  if (bannerUntil > millis()) drawBanner();
  else { cv.setTextColor(rgb(70, 95, 105)); cv.setCursor(6, 229); cv.print("tap row, tap again   A launch  C next"); }
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
  if (stationOpen) { cv.fillSprite(rgb(2, 4, 8)); drawStation(); cv.pushSprite(0, 0); return; }
  if (layer == 0) {
    cv.fillSprite(rgb(1, 2, 5));
    drawNebulae();
    drawStarsReal();
    drawSun();
    drawDustReal();
  } else {
    drawField(layer);
    drawStarsDeep();
    drawStreamers();
  }
  drawObjects();
  drawBoomsAndBolts();
  drawTargeting();
  drawHud();
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

  sm::sheetInit();
  sm::contractsInit();
  sm::simInit();
  bool restored = sm::sheetLoad();
  if (restored) { sm::universeRestoreSeed(sm::sheet().universeSeed); sm::contractsLoad(); }
  rngState = sm::universeSeed() ? sm::universeSeed() : 0xA341316Cu;
  livesSeen = sm::sheet().lives;
  initMeshes();
  makeRealScene(nullptr, true);
  setBanner(restored ? "SHEET RESTORED - STILL LOST" : "LOST IN SPACE. FLY A NAMED GATE, OR DOCK AND ASK AROUND.", 3600);
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
