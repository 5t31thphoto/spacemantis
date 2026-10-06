// The docking computer, from random approaches, for every kind of station.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
int main() {
  setup(); bootOpen = false;
  static const char *names[] = {"HEXCORE", "RING", "SPINDLE", "OUTPOST", "", "HABITAT"};
  static const int styles[] = {0, 1, 2, 3, 5};
  int allOk = 1;
  for (int si = 0; si < 5; si++) { int style = styles[si];
    int ok = 0, n = 30, scrapes = 0; float worst = 0;
    for (int k = 0; k < n; k++) {
      stationOpen = false; dockAnim = 0; theater = TH_NONE;
      makeRealScene("TEST", false);
      for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
      placeStation(shipPos + shipB.f * 160.f, norm(shipB.r * 0.3f - shipB.f), makeLook((uint8_t)style, (uint8_t)style));
      int st = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) st = i;
      V3 d = randDir();
      shipPos = objs[st].p + d * rf(70, 260); prevShipPos = shipPos;
      shipB = Basis::facing(randDir(), V3{0,1,0});
      for (auto &o : objs) o.prevSide = dot(shipPos - o.p, o.o.f);
      target = st; runVerb(VB_DOCK);
      uint16_t hull0 = sm::sheet().hull; float t0 = tNow; bool done = false;
      for (int f = 0; f < 9000; f++) {
        uint32_t now = millis(); static uint32_t prev = now;
        dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt;
        updateInput(); updateWorld(); spawnTimer = 99;
        if (dockAnim > 0 || stationOpen) { done = true; break; }
      }
      if (done) { ok++; if (tNow - t0 > worst) worst = tNow - t0; }
      else if (getenv("WHY")) { Obj &S2 = objs[st]; V3 rel = shipPos - S2.p; float ax = dot(rel, S2.o.f); printf("   miss: ax=%.1f lat=%.1f speed=%.1f dock=%d\n", ax, len(rel - S2.o.f * ax), shipSpeed, dockTarget); }
      if (sm::sheet().hull < hull0) scrapes++;
      sm::repairHull(100);
    }
    printf("%-8s docked %d/%d  worst %.1fs  hull contact %d\n", names[style], ok, n, worst, scrapes);
    if (ok < n) allOk = 0;
  }
  return allOk ? 0 : 1;
}
