// Deseret in real space; every ship's specialty; the Ghost Fleet flagship unlock.
#include "harness_core.inc"
static bool qHoldB = false;
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnC.click=BtnC.hold=false; BtnB.hold=qHoldB; qHoldB=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
static void fly(uint8_t t) { stationOpen = false; sm::Pilot &p = sm::sheet(); if (!p.ships[t].owned) { if (t == sm::SHIP_GHOST) sm::shipGrant(t); else { p.credits += 20000; sm::shipBuy(t); } } sm::haulClear(); sm::shipSwap(t); }
static void dockAt(const char *place, uint8_t style, uint8_t brand) {
  makeRealScene(place, false); for (auto &o : objs) if (o.kind == K_STATION || o.kind == K_DOCKGATE) o.kind = K_NONE;
  placeStation(shipPos + shipB.f * 150.f, -shipB.f, makeLook(style, brand));
  for (int i = MAX_OBJ - 1; i >= 0; i--) if (objs[i].kind == K_STATION) { stationIdx = i; break; }
  openBoard();
}
int main() {
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet();
  // ---- Deseret is everywhere a major company should be ----
  int habitats = 0, dOutposts = 0, facilities = 0, docks = 0;
  for (int k = 0; k < 600; k++) {
    char nm[24]; snprintf(nm, sizeof nm, "SKY %d", k); makeRealScene(nm, true);
    for (auto &o : objs) if (o.kind == K_STATION) { docks++;
      if (o.uses == BR_DESERET && o.bodyType == SS_HABITAT) habitats++;
      if (o.uses == BR_DESERET && o.bodyType == SS_OUTPOST) { if (!strcmp(o.name, "DESERET FACILITY")) facilities++; else dOutposts++; } }
  }
  printf("    600 docked skies: %d Deseret habitats, %d Deseret outposts, %d Deseret facilities beside others (%d docks)\n", habitats, dOutposts, facilities, docks);
  check(habitats > 60 && facilities > 60, "Deseret has its own stations and facilities beside the others");
  dockAt("D1", SS_HABITAT, BR_DESERET); int dfuel = (p.fuel = 0, refuelCost()); int dsell = (sm::haulClear(), sm::haulAdd("ore", 10, true), sellableValue(false));
  dockAt("L1", SS_HEXCORE, BR_LIMINAR); int lfuel = (p.fuel = 0, refuelCost()); int lsell = sellableValue(false);
  check(dfuel < lfuel, "Deseret undercuts on fuel");
  dockAt("D1", SS_HABITAT, BR_DESERET); sm::haulClear(); sm::haulAdd("ore", 10, true);
  int fair = 10 * sm::marketSellLine("ore", true); int dumped = sellableValue(false);
  printf("    Deseret: 10 ore on their market %d, dumped from the board %d\n", fair, dumped);
  check(dumped < fair, "and skimps on a dumped hold (their market stays fair)"); (void)dsell; (void)lsell;
  sm::haulClear(); p.fuel = p.fuelCap;
  dockAt("D2", SS_HABITAT, BR_DESERET);
  check(shipSoldHere(sm::SHIP_HONEYBEE), "the HoneyBee is sold at Deseret");
  dockAt("F1", SS_OUTPOST, BR_FREEHOLD);
  check(!shipSoldHere(sm::SHIP_HONEYBEE), "and only at Deseret");
  dockAt("D3", SS_HABITAT, BR_DESERET); sm::contractAbandon();
  bool titled = false; int8_t st0 = sm::flagGet("deseret");
  for (int i = 0; i < 20 && !titled; i++) { boardLanes(); sm::contractOffer(sm::CK_SURVEY); titled = sm::contractOfferPeek().issuer == BR_DESERET && !strcmp(sm::contractOfferPeek().title, "FRONTIER SURVEY"); }
  sm::contractAccept(sm::contractOfferPeek()); for (int i = 0; i < 4 && sm::contract().live; i++) sm::contractOnGate("SOMEWHERE NEW", 0, true);
  sm::Contract done; sm::contractTakeCompleted(done);
  check(titled && sm::flagGet("deseret") == st0 + 1, "Deseret work is the colony's, and builds standing");
  stationOpen = false;
  // ---- Maltese: autonav threads a whole trip ----
  fly(sm::SHIP_MALTESE); makeRealScene("PORT", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  int g = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && (objs[i].gflags & GF_DEST) && objs[i].depth == 1) g = i;
  char dest[24]; strcpy(dest, objs[g].name); p.fuel = p.fuelCap; p.hull = p.hullMax;
  target = g; runVerb(VB_AUTO);
  bool arrived = false, began = false;
  for (int i = 0; i < 40000 && !arrived; i++) { frame(); spawnTimer = 99; alienPending = -1; if (sm::trip().active) began = true; if (began && !sm::trip().active && layer == 0) arrived = true;
    if (getenv("WHY") && i % 2500 == 0) { int t2 = autoNavObj; printf("      t=%5.0f autoNav %d obj %d kind %d trip %d L%d step %u nav %d dist %.0f here %s\n", tNow, autoNav, t2, t2 >= 0 ? objs[t2].kind : -1, sm::trip().active, layer, sm::trip().step, navObj, t2 >= 0 ? distTo(objs[t2]) : -1.f, hereName); } }
  check(arrived && sm::trip().legs >= 0, "Maltese autonav flies the whole chain to arrival, hands off"); (void)dest;
  // orbit
  makeRealScene("GIANT", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  int gi = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_BODY && objs[i].bodyType == BT_GIANT) gi = i;
  if (gi < 0) { spawnBody(BT_GIANT, 600, 200, shipB.f); for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_BODY && objs[i].bodyType == BT_GIANT) gi = i; }
  Obj &G = objs[gi]; shipPos = G.p + norm(shipPos - G.p) * (G.radius * 1.3f); prevShipPos = shipPos; p.fuel = 10;
  target = gi; runVerb(VB_ORBIT);
  float minS = 1e9, maxS = 0; uint16_t f0 = p.fuel;
  for (int i = 0; i < 6000; i++) { frame(); spawnTimer = 99; float sd = surfaceDist(G); minS = fminf(minS, sd); maxS = fmaxf(maxS, sd); }
  printf("    orbit: surface distance %.0f..%.0f (radius %.0f), fuel %u -> %u\n", minS, maxS, G.radius, f0, p.fuel);
  check(minS > 0 && maxS < G.radius * 0.6f && p.fuel > f0, "Maltese orbit holds scooping range and scoops for you");
  orbitObj = -1;
  // ---- Falcor: more beams ----
  auto boltsPerVolley = [&](uint8_t t) {
    fly(t); makeRealScene("RANGE", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
    Obj *e = newObj(K_SHIP); e->enc = sm::ENC_PIRATE; e->mesh = M_SIDEWINDER; e->radius = 3.2f; e->p = shipPos + shipB.f * 40.f; snprintf(e->name, 24, "RAIDER");
    target = idxOf(e); runVerb(VB_ATTACK); int most = 0;
    for (int i = 0; i < 200 && theater != TH_NONE; i++) { frame(); if (boltN > most) most = boltN; }
    return most; };
  int bm = boltsPerVolley(sm::SHIP_MANTIS), bf = boltsPerVolley(sm::SHIP_FALCOR);
  printf("    peak bolts on screen: Mantis %d, Falcor %d\n", bm, bf);
  check(bf > bm, "the Falcor fires more beams");
  // ---- HoneyBee: shatters at any mark, bigger yield ----
  fly(sm::SHIP_HONEYBEE); makeRealScene("BELT", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  p.cap[sm::CAP_MINING] = 0; spawnRock(shipPos + shipB.f * 18.f, 5.f); int rock = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_ROCK) rock = i;
  target = rock; runVerb(VB_MINE); for (int i = 0; i < 300 && theater != TH_NONE; i++) frame();
  check(objs[rock].kind != K_ROCK && p.holdUsed >= 4, "the HoneyBee breaks the rock at MK0 and hauls a big load");
  // ---- Ghost flagship: endless cloak ----
  fly(sm::SHIP_GHOST); makeRealScene("DARK", false);
  qHoldB = true; frame(); for (int i = 0; i < 6000; i++) frame();
  check(cloakT > 1000.f, "the flagship's cloak does not run out");
  qHoldB = true; frame(); check(cloakT == 0.f && cloakCD == 0.f, "and toggles off with no cooldown");
  // ---- the unlock ----
  fly(sm::SHIP_MANTIS); p.ships[sm::SHIP_GHOST].owned = 0; sm::flagSet("gf_scans", 0, true);
  p.rank[sm::CR_DEPTHRUNNER] = 4; for (int i = 0; i < 3; i++) sm::discoverLandmark(sm::landmarkAt(i)->id);
  Obj gfo{}; gfo.kind = K_SHIP; gfo.ghost = true;
  sm::haulAdd("fixed point scan", 3, true); ghostFleetHail(gfo);
  check(!p.ships[sm::SHIP_GHOST].owned && sm::flagGet("gf_scans") == 3, "three scans sold: not yet");
  sm::haulAdd("fixed point scan", 2, true); gfo.done = false; ghostFleetHail(gfo);
  check(p.ships[sm::SHIP_GHOST].owned, "five scans: the Ghost Fleet gives you a flagship");
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
