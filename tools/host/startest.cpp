// Stars: fly to them, scoop the rare cool ones (it burns), the fuel system helps.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
int main() {
  setup(); bootOpen = false;
  int near = 0, scoopable = 0, n = 400;
  for (int k = 0; k < n; k++) {
    char nm[24]; snprintf(nm, sizeof nm, "SYS %d", k); makeRealScene(nm, false);
    for (auto &o : objs) if (o.kind == K_BODY && isStarBody(o.bodyType)) { near++; if (o.uses) scoopable++; }
  }
  printf("near stars %d/%d systems, scoopable %d (rare)\n", near, n, scoopable);
  check(near > n / 4 && near < n / 2, "about a third of systems have a star you can fly to");
  check(scoopable > 0 && scoopable < near / 3, "scoopable stars are rare");
  // same place, same star
  makeRealScene("SYS 7", false); uint8_t t1 = sunType; bool n1 = nearStar;
  makeRealScene("SYS 8", false); makeRealScene("SYS 7", false);
  check(sunType == t1 && nearStar == n1, "a place keeps its star on revisit");
  // find a scoopable one and fly into range
  int star = -1;
  for (int k = 0; k < 2000 && star < 0; k++) {
    char nm[24]; snprintf(nm, sizeof nm, "COOL %d", k); makeRealScene(nm, false);
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_BODY && isStarBody(objs[i].bodyType) && objs[i].uses) star = i;
  }
  check(star >= 0, "found a scoopable star");
  if (star >= 0) {
    Obj &S = objs[star];
    shipPos = S.p + norm(shipPos - S.p) * (S.radius * 1.35f); prevShipPos = shipPos;
    shipB = Basis::facing(norm(S.p - shipPos), V3{0,1,0}); throttleT = 0.f; shipSpeed = 0;
    sm::Pilot &p = sm::sheet(); p.fuel = 20; p.hull = 100;
    target = star; Chip c[3]; int nc = verbsFor(S, c);
    check(nc && c[0].id == VB_SCOOP && c[0].enabled, "SCOOP offered in range");
    for (int i = 0; i < 400; i++) frame();
    check(p.hull < 100, "the hull heats while you sit in scooping range");
    uint16_t f0 = p.fuel; runVerb(VB_SCOOP);
    for (int i = 0; i < 300 && theater != TH_NONE; i++) frame();
    int plain = p.fuel - f0;
    check(plain > 0, "scooping the corona gives fuel");
    sm::earnCap(sm::CAP_FUELSYS, 4); p.fuel = 20; S.timer = 0;
    uint16_t f1 = p.fuel; runVerb(VB_SCOOP);
    int beats = 0; for (int i = 0; i < 300 && theater != TH_NONE; i++) { frame(); beats++; }
    check(p.fuel - f1 > plain, "a better fuel system scoops more");
    printf("    plain +%d, fuel system MK4 +%d in %d frames\n", plain, p.fuel - f1, beats);
  }
  // hot stars: no scoop
  int hot = -1;
  for (int k = 0; k < 2000 && hot < 0; k++) { char nm[24]; snprintf(nm, sizeof nm, "HOT %d", k); makeRealScene(nm, false);
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_BODY && isStarBody(objs[i].bodyType) && !objs[i].uses) hot = i; }
  if (hot >= 0) { Chip c[3]; verbsFor(objs[hot], c); check(!c[0].enabled && !strcmp(c[0].note, "TOO HOT"), "hot stars are TOO HOT to scoop"); }
  // the fuel system makes portals cheaper
  sm::tripBegin("X", 2, false, false); sm::Pilot &p = sm::sheet(); p.cap[sm::CAP_FUELSYS] = 0; int c0 = sm::tripPortalFuel(); p.cap[sm::CAP_FUELSYS] = 5; int c5 = sm::tripPortalFuel(); sm::tripEnd();
  check(c5 < c0, "a better fuel system crosses portals for less");
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
