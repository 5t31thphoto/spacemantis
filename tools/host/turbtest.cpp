// Turbulence: a hint in real space; in subspace it bites past the rating.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
// sit in a layer near a portal for `secs`; report heading drift, worst shake, hull lost
static void sit(int L, int stab, int bulk, float secs, float &drift, float &shake, int &lost, bool &died) {
  sm::Pilot &p = sm::sheet();
  p.cap[sm::CAP_STABILIZER] = (uint8_t)stab; p.cap[sm::CAP_BULKHEADS] = (uint8_t)bulk; p.cap[sm::CAP_SHIELDS] = 1;
  p.hull = p.hullMax; uint32_t lives0 = p.lives; lostOpen = false; endingOpen = false;
  if (L == 0) makeRealScene("TEST", false); else { sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = (uint8_t)L; tr.ascending = 1; tr.layer = (uint8_t)L; strcpy(tr.dest, "X"); layer = L; makeLayerScene(); }
  for (auto &o : objs) if (o.kind == K_SHIP || o.kind == K_BODY || o.kind == K_LANDMARK) o.kind = K_NONE;
  alienPending = -1; throttleT = 0.f; shipSpeed = 0;
  Obj *po = newObj(K_PORTAL); po->p = shipPos + shipB.f * 120.f + shipB.r * 60.f; po->o = Basis::facing(V3{0,0,1}, V3{0,1,0}); po->radius = 13.f;   // off the nose: the assist won't pull
  V3 f0 = shipB.f; drift = 0; shake = 0; died = false;
  for (float t = 0; t < secs; t += dt) {
    frame(); spawnTimer = 99;
    po->p = shipPos + shipB.f * 120.f + shipB.r * 60.f;   // keep it near
    if (fabsf(shakeX) + fabsf(shakeY) > shake && getenv("WHY")) printf("      L%d T %.2f S %.2f rated %d stress %.1f rating %u maxBand %u layer %d\n", L, turbulence, turbSeverity, turbRated, hullStress, sm::depthRating(), sm::depthQuery((uint8_t)layer).maxBand, layer);
    shake = fmaxf(shake, fabsf(shakeX) + fabsf(shakeY));
    if (p.lives != lives0) { died = true; break; }
  }
  drift = acosf(clampf(dot(f0, shipB.f), -1.f, 1.f)) * 57.3f;
  lost = p.hullMax - p.hull;
}
int main() {
  setup(); bootOpen = false;
  float drift, shake; int lost; bool died;
  sit(0, 0, 1, 20.f, drift, shake, lost, died);
  printf("    real space, MK0:            drift %5.1f deg  shake %.2f px  hull -%d\n", drift, shake, lost);
  check(drift < 0.5f && shake <= 1.6f && lost == 0, "real space: only a faint tremble");
  sit(1, 1, 1, 20.f, drift, shake, lost, died);
  printf("    shallows, MK1 (rated):      drift %5.1f deg  shake %.2f px  hull -%d\n", drift, shake, lost);
  check(lost == 0 && shake < 2.5f, "rated for the layer: damped, no stress");
  sit(2, 0, 1, 30.f, drift, shake, lost, died);
  printf("    roads, MK0 (unrated):       drift %5.1f deg  shake %.2f px  hull -%d\n", drift, shake, lost);
  check(lost > 5 && !died, "one layer past the rating: it hurts, survivable");
  sit(4, 0, 1, 60.f, drift, shake, lost, died);
  printf("    deep cove, MK0:             drift %5.1f deg  shake %.2f px  hull -%d  %s\n", drift, shake, lost, died ? "DESTROYED" : "");
  check(died, "the deep cove at MK0 is regrettable");
  sit(4, 5, 4, 60.f, drift, shake, lost, died);
  printf("    deep cove, MK5 + bulkheads: drift %5.1f deg  shake %.2f px  hull -%d\n", drift, shake, lost);
  check(!died && lost == 0 && shake > 1.5f && drift < 5.f, "MK5 rated: visible shake and haptics, barely any kicks, no stress");
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
