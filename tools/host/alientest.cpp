#include "harness_core.inc"
static bool qDrag=false;
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false;
  Touch.d = TouchDetail{}; if (qDrag) { Touch.d.press = true; Touch.d.x = 160 + (int)(g_ms / 8 % 40); Touch.d.y = 120; } Imu.data.accel={0.6f,0.4f,1}; }
static void frame() { uint32_t now=millis(); static uint32_t prev=now; dt=clampf((now-prev)/1000.f,0.008f,0.05f); prev=now; tNow+=dt; updateInput(); updateWorld(); hx::update(dt); }
static void snap(const char *n) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",n); g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
int main() {
  setup(); bootOpen = false; bannerUntil = 0; crossFlash = 0;
  int fails = 0;
  for (int cls = 0; cls < 3; cls++) {
    sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 4; tr.layer = 3; tr.step = 0; strcpy(tr.dest, "TEST");
    layer = 3; makeLayerScene(); for (auto &o : objs) if (o.kind == K_LANDMARK) o.kind = K_NONE;
    spawnNextOnPath(); crossFlash = 0; throttleT = 0.5f; shipSpeed = 20;
    sm::Pilot &p = sm::sheet(); uint16_t hull0 = p.hull = 60; p.fuel = 40;
    int j0 = sm::journal().count;
    alienCooldown = 0; alienBegin(); alien.cls = (uint8_t)cls; alien.outcome = (uint8_t)(cls == 0 ? 0 : cls == 1 ? 1 : 3);
    Basis b0; bool moved = false; float speedAt10 = -1; int snaps = 0; bool sawCut = false;
    qDrag = true;
    while (alien.active) {
      frame();
      float t = alien.t;
      if (t > 5.f && t < 21.f) { if (fabsf(dot(shipB.f, b0.f)) < 0.9999f && b0.f.z != 0) {} }
      if (t > 4.2f && t < 4.3f) b0 = shipB;
      if (t > 4.3f && t < 21.f && fabsf(dot(shipB.f, b0.f) - 1.f) > 1e-4f) moved = true;
      if (speedAt10 < 0 && t > 10.f) speedAt10 = shipSpeed;
      char name[48];
      if (cls == 0 && snaps == 0 && t > 2.5f) { snap("30_alien_approach"); snaps++; }
      if (snaps <= 1 && t > 9.f && snaps == (cls == 0 ? 1 : 0)) { snprintf(name, sizeof name, "3%d_alien_scan_%s", cls + 1, alienName(cls) + 4); snap(name); snaps = 2; }
      if (cls == 0 && snaps == 2 && t > 24.f) { snap("34_alien_aftermath"); snaps = 3; }
      if (cls == 0 && snaps == 3 && t > 29.3f) { snap("35_alien_toast"); snaps = 4; }
      if (sm::sheet().hull < hull0) { printf("FAIL damage during encounter\n"); fails++; break; }
    }
    qDrag = false;
    for (int i = 0; i < 200; i++) frame();
    printf("%s: controls frozen=%s  speed@10s=%.2f  hull %u->%u  fuel %u  shields MK%u  journal +%d: \"%s\"\n",
      alienName(cls), moved ? "NO" : "yes", speedAt10, hull0, p.hull, p.fuel, sm::capTier(sm::CAP_SHIELDS), sm::journal().count - j0,
      sm::journalNewest(0) ? sm::journalNewest(0)->text : "");
    if (moved) fails++;
    if (sm::journal().count - j0 != 1) fails++;
    printf("   flight resumes: speed now %.1f, alien cooldown %.0fs\n", shipSpeed, alienCooldown);
  }
  // never in shallow layers, never with a landmark present
  int scheduled = 0;
  for (int i = 0; i < 2000; i++) { layer = (i % 3); alienCooldown = 0; makeLayerScene(); if (alienPending > 0) scheduled++; }
  printf("scheduled in layers 0-2: %d (must be 0)\n", scheduled); if (scheduled) fails++;
  int deep = 0, withLm = 0;
  for (int i = 0; i < 4000; i++) { layer = 3 + (i & 1); alienCooldown = 0; makeLayerScene(); if (alienPending > 0) { deep++; if (countKind(K_LANDMARK)) withLm++; } }
  printf("scheduled in layers 3-4: %d of 4000 (rare), with a landmark present: %d (must be 0)\n", deep, withLm); if (withLm) fails++;
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
