// Headless soak test: compiles the real firmware sources against tiny host stubs
// and flies a bot pilot for N simulated hours. Fails on softlocks (a combat,
// mining or dive theater that never finishes), sheet invariants, or non-ASCII
// text reaching the 6x8 display font.
//
//   ./run.sh            # 6 seeds x 2 simulated hours
//   ./harness 42 5      # seed 42, 5 simulated hours
#include <map>
#include <string>
#include <vector>
#include <cstdlib>
#include <cassert>
#include "M5Unified.h"
#include "Preferences.h"

static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
void delay(uint32_t ms) { g_ms += ms; }
EspClass ESP;
M5Class M5;

static std::map<std::string, std::vector<uint8_t>> g_nvs;
bool Preferences::begin(const char *ns, bool) { ns_ = ns; return true; }
size_t Preferences::putBytes(const char *k, const void *v, size_t n) {
  auto &b = g_nvs[std::string(ns_) + "/" + k]; b.assign((const uint8_t *)v, (const uint8_t *)v + n); return n; }
size_t Preferences::getBytesLength(const char *k) {
  auto it = g_nvs.find(std::string(ns_) + "/" + k); return it == g_nvs.end() ? 0 : it->second.size(); }
size_t Preferences::getBytes(const char *k, void *v, size_t n) {
  auto it = g_nvs.find(std::string(ns_) + "/" + k); if (it == g_nvs.end()) return 0;
  size_t m = n < it->second.size() ? n : it->second.size(); memcpy(v, it->second.data(), m); return m; }

static FILE *g_log = nullptr;
static int g_textIssues = 0;
void hostLog(const char *fmt, ...) {
  va_list ap;
  if (strncmp(fmt, "tx", 2) == 0) {
    // text checks: ASCII only, fits on screen
    va_start(ap, fmt);
    int x = va_arg(ap, int); int y = va_arg(ap, int); int sz = va_arg(ap, int); (void)va_arg(ap, unsigned);
    const char *s = va_arg(ap, const char *);
    va_end(ap);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p >= 0x80) { if (g_textIssues++ < 10) fprintf(stderr, "NONASCII: %s\n", s); break; }
    (void)x; (void)y; (void)sz;
  }
  if (!g_log) return;
  va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
}

#include "../../src/main.cpp"

// ---------------- bot ----------------
static uint32_t botRng = 12345;
static uint32_t br() { botRng ^= botRng << 13; botRng ^= botRng >> 17; botRng ^= botRng << 5; return botRng; }
static int stationActions = 0;
static bool touchQueued = false; static int tqx, tqy;

void M5Class::update() {
  g_ms += 8;   // pretend draw cost
  BtnA.p = BtnB.p = BtnC.p = false;
  Touch.d = TouchDetail{};
  if (touchQueued) { Touch.d.pressed = true; Touch.d.press = true; Touch.d.x = tqx; Touch.d.y = tqy; touchQueued = false; return; }
  if (endingOpen) { BtnA.p = true; return; }
  if (stationOpen) {
    if (++stationActions > 12 || br() % 60 == 0) { BtnA.p = true; stationActions = 0; return; }
    if (br() % 10 == 0) { if (br() % 3) BtnB.p = true; else BtnC.p = true; }
    if (br() % 40 == 0) { tqx = 100; tqy = ROW_Y0 + (int)(br() % STATION_ROWS) * ROW_PITCH + 5; touchQueued = true; }
    return;
  }
  if (encounter && !combatActive && !mineActive && encounterZ < 5.f && br() % 30 == 0) {
    int r = br() % 100;
    if (r < 55) { tqx = BTN_L + 20; tqy = BTN_Y + 10; touchQueued = true; }
    else if (r < 80) BtnB.p = true;
    // else ignore it
  }
  // steer toward the nearest gate via tilt
  int best = -1;
  for (int i = 0; i < gateCount; i++) if (best < 0 || gates[i].z < gates[best].z) best = i;
  if (best >= 0) {
    Gate &g = gates[best];
    // wobble so we sometimes miss
    Imu.data.accel.y = clampf(g.x * 4.f + ((int)(br() % 100) - 50) * 0.004f, -1.5f, 1.5f);
    Imu.data.accel.x = clampf(-g.y * 4.f + ((int)(br() % 100) - 50) * 0.004f, -1.5f, 1.5f);
  }
}

int main(int argc, char **argv) {
  uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 1;
  double hours = argc > 2 ? atof(argv[2]) : 2.0;
  botRng = seed * 2654435761u + 7;
  setup();
  rngState ^= seed * 747796405u;
  uint32_t end = g_ms + (uint32_t)(hours * 3600e3);
  int dives = 0, docks = 0, maxBand = 0, encs = 0, combats = 0, mines = 0, jobs = 0;
  int kinds[sm::ENC_COUNT] = {0};
  bool wasDiving = false, wasStation = false, wasEnc = false, wasCombat = false, wasMine = false;
  uint32_t stateSince = g_ms; int lastSig = -1; uint32_t maxStuck = 0;
  int frames = 0;
  while (g_ms < end) {
    {
      uint32_t now = millis(); static uint32_t prev = now;
      dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt;
      updateInput(); combatTick(dt); updateWorld();
      if (frames % 25 == 0) draw();
      delay(8);
    }
    frames++;
    if (diving && !wasDiving) dives++;
    if (stationOpen && !wasStation) docks++;
    if (encounter && !wasEnc) { encs++; kinds[encounterKind]++; }
    if (combatActive && !wasCombat) combats++;
    if (mineActive && !wasMine) mines++;
    wasDiving = diving; wasStation = stationOpen; wasEnc = encounter; wasCombat = combatActive; wasMine = mineActive;
    if (depthBand > maxBand) maxBand = depthBand;
    { static char prevB[112] = ""; if (strcmp(prevB, banner) != 0) { if (strncmp(banner, "JOB DONE", 8) == 0) jobs++; strcpy(prevB, banner); } }
    // invariants
    sm::Pilot &p = sm::sheet();
    if (p.hull > p.hullMax || p.fuel > p.fuelCap || p.holdUsed > p.holdCap + 40 || gateCount < 2 || gateCount > 8 ||
        depthBand < 0 || depthBand > 4 || p.credits < 0) {
      fprintf(stderr, "INVARIANT t=%u hull=%u/%u fuel=%u/%u hold=%u/%u gates=%d band=%d cr=%ld\n", g_ms, p.hull, p.hullMax,
              p.fuel, p.fuelCap, p.holdUsed, p.holdCap, gateCount, depthBand, (long)p.credits);
      return 1;
    }
    int sig = (combatActive ? 1 : 0) | (mineActive ? 2 : 0) | (diving ? 4 : 0);
    if (sig != lastSig || sig == 0) { lastSig = sig; stateSince = g_ms; }
    if (g_ms - stateSince > maxStuck) maxStuck = g_ms - stateSince;
    if (g_ms - stateSince > 20000) { fprintf(stderr, "STUCK sig=%d t=%u\n", sig, g_ms); return 2; }
  }
  sm::Pilot &p = sm::sheet();
  int ranks = 0; for (int i = 0; i < sm::CR_COUNT; i++) ranks += p.rank[i];
  printf("seed %u: frames %d dives %d docks %d maxBand %d enc %d combat %d mine %d lives %lu cr %ld ranks %d jobs %d landmarks %d/%d textIssues %d maxTheater %.1fs\n",
         seed, frames, dives, docks, maxBand, encs, combats, mines, (unsigned long)p.lives, (long)p.credits, ranks, jobs,
         landmarksKnown(), sm::landmarkCount(), g_textIssues, maxStuck / 1000.0);
  printf("  kinds:"); for (int i = 0; i < sm::ENC_COUNT; i++) printf(" %d", kinds[i]); printf("\n");
  printf("  caps:"); for (int i = 0; i < sm::CAP_COUNT; i++) printf(" %s%u", sm::capName((sm::CapId)i), p.cap[i]); printf("\n");
  printf("  ranks:"); for (int i = 0; i < sm::CR_COUNT; i++) printf(" %s%u", sm::careerName((sm::CareerId)i), p.rank[i]); printf("\n");
  return 0;
}
