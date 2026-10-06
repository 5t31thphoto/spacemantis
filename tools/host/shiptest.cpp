// Owning ships: buy, swap (teleport), per-ship fits, loss and rebuild.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static void dockAt(const char *place, uint8_t style, uint8_t brand) {
  makeRealScene(place, false); for (auto &o : objs) if (o.kind == K_STATION || o.kind == K_DOCKGATE) o.kind = K_NONE;
  placeStation(shipPos + shipB.f * 150.f, -shipB.f, makeLook(style, brand));
  for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) stationIdx = i;
  openBoard();
}
int main() {
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet(); p.credits = 30000;
  check(p.activeShip == sm::SHIP_MANTIS && p.ships[0].owned, "every pilot starts in the license Mantis");
  sm::earnCap(sm::CAP_TRAILER, 3); sm::earnCap(sm::CAP_WEAPONS, 4);
  float mantisCruise = cruiseSpeed();
  dockAt("MALTA YARD", SS_RING, BR_MALTAPLEX);
  check(!shipSoldHere(sm::SHIP_FALCOR) && shipSoldHere(sm::SHIP_MALTESE), "each builder sells its own ship");
  dockAt("LIM HUB", SS_HEXCORE, BR_LIMINAR);
  shipAction(sm::SHIP_FALCOR);
  check(p.ships[sm::SHIP_FALCOR].owned, "bought the Falcor at a Liminar dock");
  sm::haulAdd("ore", 3, true); shipAction(sm::SHIP_FALCOR);
  check(p.activeShip == sm::SHIP_MANTIS, "no swap with cargo aboard (it would not survive the phase)");
  sm::haulClear(); shipAction(sm::SHIP_FALCOR);
  check(p.activeShip == sm::SHIP_FALCOR, "swapped into the Falcor");
  printf("    Falcor: hold %u, cruise %.1f vs Mantis %.1f, weapons MK%u\n", p.holdCap, cruiseSpeed(), mantisCruise, sm::capTier(sm::CAP_WEAPONS));
  check(p.holdCap < 20 && cruiseSpeed() > mantisCruise * 1.2f, "the Falcor is fast with a small hold");
  check(sm::capTier(sm::CAP_WEAPONS) == 1 && sm::capTier(sm::CAP_TRAILER) == 0, "a new ship has its own (basic) fit");
  sm::earnCap(sm::CAP_SHIELDS, 5);
  shipAction(sm::SHIP_MANTIS);
  check(p.activeShip == sm::SHIP_MANTIS && sm::capTier(sm::CAP_WEAPONS) == 4 && sm::capTier(sm::CAP_TRAILER) == 3, "back in the Mantis with its own fit");
  shipAction(sm::SHIP_FALCOR);
  check(sm::capTier(sm::CAP_SHIELDS) == 5, "the Falcor kept what was fitted to it");
  sm::earnCap(sm::CAP_SCANNERS, 3);   // fitted after the last teleport: not in the record
  // lose her
  sm::damageHull(999); checkDestroy();
  check(p.activeShip == sm::SHIP_MANTIS && sm::capTier(sm::CAP_WEAPONS) == 4, "the pod wakes in the Mantis, its fit intact");
  check(p.ships[sm::SHIP_FALCOR].lost, "the Falcor is lost, but on record");
  lostOpen = false; dockAt("LIM HUB 2", SS_HEXCORE, BR_LIMINAR); int32_t cr = p.credits;
  shipAction(sm::SHIP_FALCOR);
  check(!p.ships[sm::SHIP_FALCOR].lost && p.credits == cr - sm::shipRecoverFee(sm::SHIP_FALCOR), "rebuilt from the record for a fee");
  check(p.ships[sm::SHIP_FALCOR].cap[sm::CAP_SHIELDS] == 5 && p.ships[sm::SHIP_FALCOR].cap[sm::CAP_SCANNERS] == 0, "as she was when last teleported");
  // the flagship steadies the deep
  sm::shipGrant(sm::SHIP_GHOST); sm::haulClear(); shipAction(sm::SHIP_GHOST);
  p.cap[sm::CAP_STABILIZER] = 2;
  { sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 3; tr.ascending = 1; tr.layer = 3; strcpy(tr.dest, "X"); layer = 3; makeLayerScene(); }
  Obj *po = newObj(K_PORTAL); po->p = shipPos + shipB.f * 30.f; po->o = Basis::facing(V3{0,0,1}, V3{0,1,0}); po->radius = 13.f;
  applyTurbulence();
  check(turbRated, "the flagship's MK2 stabilizer holds below the roads (one layer deeper than usual)");
  sm::tripEnd(); layer = 0;
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
