#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
int main() {
  setup();
  int ok = 0, n = 40; float worst = 0; int scrapes = 0;
  for (int k = 0; k < n; k++) {
    stationOpen = false; dockAnim = 0; theater = TH_NONE;
    makeRealScene("TEST", true);
    for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
    int st=-1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_STATION) st=i;
    V3 d = randDir();
    shipPos = objs[st].p + d * rf(60, 260); prevShipPos = shipPos;
    shipB = Basis::facing(randDir(), V3{0,1,0});
    for (auto &o: objs) o.prevSide = dot(shipPos - o.p, o.o.f);
    target = st; runVerb(VB_DOCK);
    uint16_t hull0 = sm::sheet().hull;
    float t0 = tNow; bool done = false;
    for (int f=0; f<9000; f++) {
      uint32_t now = millis(); static uint32_t prev = now;
      dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt;
      updateInput(); updateWorld();
      if (dockAnim > 0 || stationOpen) { done = true; break; }
      spawnTimer = 99;
    }
    if (done) { ok++; if (tNow - t0 > worst) worst = tNow - t0; }
    if (sm::sheet().hull < hull0) scrapes++;
    sm::repairHull(100);
  }
  printf("docked %d/%d  worst %.1fs  runs with hull contact %d\n", ok, n, worst, scrapes);
}
