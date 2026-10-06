#include "harness_core.inc"
static bool qHold=false,qHoldC=false,qClick=false,qA=false;
void M5Class::update() { g_ms += 8; BtnA.p=qA; BtnB.p=BtnC.p=false; BtnB.click=qClick; BtnB.hold=qHold; BtnC.hold=qHoldC; BtnC.click=false; qA=qClick=qHold=qHoldC=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
int main() {
  setup(); bootOpen = false;
  sm::Pilot &p = sm::sheet();
  p.lives = 3; p.credits = 4388; p.hull = 22; p.fuel = 52; p.holdUsed = 6;
  p.rank[sm::CR_DEPTHRUNNER] = 9; p.rank[sm::CR_WANDERER] = 5; p.rank[sm::CR_TRADER] = 3; p.rank[sm::CR_GUNHAND] = 2; p.rank[sm::CR_HAULER] = 1;
  p.cap[sm::CAP_WEAPONS] = 2; p.cap[sm::CAP_SCANNERS] = 2; p.cap[sm::CAP_BULKHEADS] = 3; p.cap[sm::CAP_STABILIZER] = 1; p.cap[sm::CAP_MINING] = 1;
  for (int i = 0; i < 5; i++) sm::discoverLandmark(sm::landmarkAt(i)->id);
  // hold B in flight -> status opens
  qHoldC = true; updateInput();
  printf("statusOpen after hold: %d\n", statusOpen);
  g_log = fopen("snaps/20_status.log", "w"); draw(); fclose(g_log); g_log = nullptr;
  // world paused while open
  V3 before = shipPos; for (int i = 0; i < 50; i++) { updateInput(); updateWorld(); }
  printf("ship moved while open: %.3f\n", len(shipPos - before));
  // any button closes; short-press B (click) still recenters in flight
  qA = true; updateInput();
  printf("statusOpen after A: %d\n", statusOpen);
  bannerUntil = 0; qClick = true; updateInput();
  printf("banner after B click: %s\n", banner);
}
