// Headless soak test: the real firmware logic against tiny host stubs, flown by
// a bot pilot for N simulated hours. Fails on softlocks (a theater or trip that
// never progresses), broken invariants (NaN positions, sheet out of range), or
// non-ASCII text reaching the 6x8 display font.
//
//   ./run.sh            # 6 seeds x 2 simulated hours
//   ./harness 42 5      # seed 42, 5 simulated hours
#include "harness_core.inc"

static uint32_t botRng = 12345;
static uint32_t br() { botRng ^= botRng << 13; botRng ^= botRng >> 17; botRng ^= botRng << 5; return botRng; }
static int botGoal = -1;          // object the bot is flying at
static float botGoalT = 0;
static int stationActions = 0;

static bool finiteV(V3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

static void aimAt(V3 p) {
  V3 rel = p - shipPos;
  V3 loc = shipB.toLocal(norm(rel));
  // like a person: ease off when something close is well off the nose
  if (len(rel) < 35.f && loc.z < 0.75f) throttleT = 0.15f;
  else if (throttleT < 0.3f && loc.z > 0.9f) throttleT = 0.5f;
  float yawErr = atan2f(loc.x, loc.z), pitchUp = atan2f(loc.y, loc.z);
  // the current build's mapping: accel X is the yaw stick, accel Y the pitch stick
  M5.Imu.data.accel.x = clampf(-yawErr * 1.2f, -1.4f, 1.4f) + ((int)(br() % 100) - 50) * 0.002f;
  M5.Imu.data.accel.y = clampf(pitchUp * 1.2f, -1.4f, 1.4f) + ((int)(br() % 100) - 50) * 0.002f;
}

void M5Class::update() {
  g_ms += 8;
  BtnA.p = BtnB.p = BtnC.p = false; BtnA.click = BtnA.hold = BtnB.click = BtnB.hold = BtnC.click = BtnC.hold = false;
  Touch.d = TouchDetail{};
  Imu.data.accel = {0, 0, 1};
  if (endingOpen || bootOpen || lostOpen) { BtnA.p = true; BtnA.click = true; return; }
  if (dockAnim > 0) return;
  if (stationOpen) {
    if (++stationActions > 10 || br() % 50 == 0) { BtnA.p = true; stationActions = 0; return; }
    if (br() % 8 == 0) { stationChoice = (int)(br() % STATION_ROWS); BtnB.p = true; }
    return;
  }
  sm::Trip &tr = sm::trip();
  if (tr.active && navObj >= 0 && objs[navObj].kind != K_NONE) {
    if (throttleT > 0.65f) throttleT = 0.5f;
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_LANDMARK && theater == TH_NONE && !objs[i].done) {
      target = i; Chip c[3]; int n = verbsFor(objs[i], c);
      if (n && c[0].enabled) { runVerb(c[0].id); objs[i].done = true; return; }
      if (distTo(objs[i]) < 400) { aimAt(objs[i].p); return; }
    }
    aimAt(objs[navObj].p); return;
  }
  // pick something to do
  botGoalT -= 0.016f;
  if (botGoal < 0 || objs[botGoal].kind == K_NONE || botGoalT <= 0) {
    botGoal = -1; botGoalT = 25;
    int r = br() % 100;
    for (int tries = 0; tries < 40 && botGoal < 0; tries++) {
      int i = br() % MAX_OBJ;
      Obj &o = objs[i];
      if (o.kind == K_NONE) continue;
      if (r < 55 && o.kind == K_GATE && (o.gflags & GF_DEST)) botGoal = i;
      else if (r >= 55 && r < 70 && o.kind == K_STATION) { target = i; runVerb(VB_DOCK); botGoal = i; }
      else if (r >= 70 && (o.kind == K_SHIP || o.kind == K_ROCK || o.kind == K_POD || o.kind == K_WRECK || o.kind == K_ARTIFACT || o.kind == K_LANDMARK || o.kind == K_ANOMALY || (o.kind == K_BODY && o.bodyType == BT_GIANT))) botGoal = i;
    }
  }
  if (botGoal >= 0) {
    Obj &o = objs[botGoal];
    if (dockTarget < 0) aimAt(o.p);
    if (o.kind != K_GATE && o.kind != K_STATION && theater == TH_NONE && br() % 20 == 0) {
      target = botGoal;
      Chip c[3]; int n = verbsFor(o, c);
      for (int k = 0; k < n; k++) if (c[k].enabled && (c[k].id != VB_ATTACK || br() % 3 == 0)) { runVerb(c[k].id); botGoal = -1; break; }
    }
    // throttle slider now and then
    if (br() % 400 == 0) throttleT = (br() % 100) / 100.f;
    if (throttleT < 0.3f && br() % 50 == 0) throttleT = 0.5f;
  }
}

int main(int argc, char **argv) {
  uint32_t seed = argc > 1 ? (uint32_t)atoi(argv[1]) : 1;
  double hours = argc > 2 ? atof(argv[2]) : 2.0;
  botRng = seed * 2654435761u + 7;
  setup();
  rngState ^= seed * 747796405u;
  uint32_t end = g_ms + (uint32_t)(hours * 3600e3);
  long frames = 0; int jobsDone = 0, trips = 0, arrivals = 0, portals = 0, docks = 0, theaters = 0, maxLayer = 0, endings = 0;
  int32_t minCr = 1 << 30, maxCr = 0; uint32_t livesPrev = sm::sheet().lives; char lastCause[112] = ""; std::map<std::string,int> causes;
  bool wasTrip = false, wasStation = false, wasTheater = false; int lastLayer = 0;
  uint32_t progressAt = g_ms; int lastLegs = -1, lastStep = -1;
  float maxTheater = 0, theaterStart = 0;
  while (g_ms < end) {
    uint32_t now = millis(); static uint32_t prev = now;
    dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt;
    updateInput(); updateWorld(); hx::update(dt);
    if (frames % 25 == 0) draw();
    frames++;
    sm::Trip &tr = sm::trip();
    if (tr.active && !wasTrip) trips++;
    if (!tr.active && wasTrip && layer == 0) arrivals++;
    if (layer != lastLayer) portals++;
    if (stationOpen && !wasStation) docks++;
    if (theater != TH_NONE && !wasTheater) { theaters++; theaterStart = tNow; }
    if (theater != TH_NONE && tNow - theaterStart > maxTheater) maxTheater = tNow - theaterStart;
    if (endingOpen) endings++;
    if (getenv("TRACE")) {
      static char lastB[112] = "";
      if (strcmp(lastB, banner) && bannerUntil > millis()) { fprintf(stderr, "[%6.1fs L%d H%u F%u $%ld dock%d] %s\n", g_ms/1000.0, layer, sm::sheet().hull, sm::sheet().fuel, (long)sm::sheet().credits, dockTarget, banner); strcpy(lastB, banner); }
    }
    wasTrip = tr.active; wasStation = stationOpen; wasTheater = theater != TH_NONE; lastLayer = layer;
    if (layer > maxLayer) maxLayer = layer;
    { static char pb[112]=""; if (strcmp(pb,banner)) { if (!strncmp(banner,"JOB DONE",8)) jobsDone++; strcpy(pb,banner);} }
    if (sm::sheet().lives != livesPrev) { livesPrev = sm::sheet().lives; char k[40]; snprintf(k, sizeof k, "L%d:%.28s", lastLayer, lastCause); causes[k]++; }
    else if (bannerUntil > millis()) strncpy(lastCause, banner, sizeof lastCause - 1);
    sm::Pilot &p = sm::sheet();
    if (p.credits < minCr) minCr = p.credits; if (p.credits > maxCr) maxCr = p.credits;
    // invariants
    bool bad = !finiteV(shipPos) || !finiteV(shipB.f) || fabsf(len(shipB.f) - 1) > 0.01f || fabsf(dot(shipB.f, shipB.u)) > 0.01f ||
               p.hull > p.hullMax || p.fuel > p.fuelCap || layer < 0 || layer > 4 || p.credits < 0;
    for (auto &o : objs) if (o.kind != K_NONE && !finiteV(o.p)) bad = true;
    if (bad) { fprintf(stderr, "INVARIANT t=%u layer=%d hull=%u fuel=%u cr=%ld\n", g_ms, layer, p.hull, p.fuel, (long)p.credits); return 1; }
    if (tr.active != (lastLegs >= 0) || tr.legs != lastLegs || tr.step != lastStep) { progressAt = g_ms; lastLegs = tr.active ? tr.legs : -1; lastStep = tr.step; }
    if (theater != TH_NONE && tNow - theaterStart > 15) { fprintf(stderr, "STUCK THEATER\n"); return 2; }
    if (tr.active && g_ms - progressAt > 60000 && getenv("TRACE") && frames % 120 == 0 && navObj >= 0) {
      Obj &n = objs[navObj]; V3 l = n.o.toLocal(shipPos - n.p); V3 c = shipB.toLocal(n.p - shipPos);
      fprintf(stderr, "  nav kind=%d r=%.1f side=%.1f lat=%.1f  cam=(%.1f,%.1f,%.1f) speed=%.1f thr=%.2f theater=%d target=%d\n", n.kind, n.radius, l.z, sqrtf(l.x*l.x+l.y*l.y), c.x, c.y, c.z, shipSpeed, throttleT, theater, target);
    }
    if (tr.active && g_ms - progressAt > 240000) { fprintf(stderr, "STUCK TRIP layer=%d step=%u nav=%d\n", layer, tr.step, navObj); return 3; }
  }
  sm::Pilot &p = sm::sheet();
  int charted = 0; for (int i = 0; i < sm::landmarkCount(); i++) if (sm::landmarkDiscovered(sm::landmarkAt(i)->id)) charted++;
  int ranks = 0; for (int i = 0; i < sm::CR_COUNT; i++) ranks += p.rank[i];
  printf("seed %u: trips %d arrivals %d portals %d docks %d theaters %d maxLayer %d lives %lu cr %ld (max %ld) ranks %d charted %d/%d jobs %d ending %s text %d motorChanges %ld maxTheater %.1fs\n",
         seed, trips, arrivals, portals, docks, theaters, maxLayer, (unsigned long)p.lives, (long)p.credits, (long)maxCr, ranks, charted,
         sm::landmarkCount(), jobsDone, endings ? "yes" : "no", g_textIssues, M5.Power.changes, maxTheater);
  if (getenv("CAUSES")) for (auto &kv : causes) printf("   death x%d after: %s\n", kv.second, kv.first.c_str());
  return 0;
}
