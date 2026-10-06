// Delivery jobs: point at a gate already here, pay only at the destination's dock.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void flyTrip(int g) { threadGate(objs[g]); for (int guard = 0; guard < 40 && sm::trip().active; guard++) { int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; } if (n < 0) break; threadGate(objs[n]); } }
int main() {
  setup(); bootOpen = false; sm::earnCap(sm::CAP_BULKHEADS, 10);
  int total = 0, paid = 0, pointedAtExisting = 0, paidOnArrival = 0, strayGate = 0, noDock = 0, notGold = 0;
  sm::ContractKind kinds[] = {sm::CK_HAUL, sm::CK_SUPPLY, sm::CK_SMUGGLE, sm::CK_MARKET, sm::CK_DEPTHRUN};
  for (int k = 0; k < 60; k++) {
    sm::contractAbandon(); sm::haulClear();
    sm::sheet().credits = 5000; sm::sheet().fuel = 100; sm::sheet().hull = 100;
    char nm[24]; snprintf(nm, sizeof nm, "HARBOUR %d", k); makeRealScene(nm, true);
    int before = 0; for (auto &o : objs) if (o.kind == K_GATE && (o.gflags & GF_DEST)) before++;
    boardLanes();
    if (!sm::contractOffer(kinds[k % 5]) || sm::contractOfferPeek().kind != kinds[k % 5]) continue;   // e.g. no deep lane for a depth run
    if (!sm::contractAccept(sm::contractOfferPeek())) continue;
    lightJobLane();
    int after = 0, g = -1;
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && (objs[i].gflags & GF_DEST)) { after++; if (objs[i].gflags & GF_JOB) g = i; }
    total++;
    if (g >= 0 && after == before) pointedAtExisting++;
    if (g < 0) continue;
    int32_t cr0 = sm::sheet().credits;
    flyTrip(g);
    if (!sm::contract().live) paidOnArrival++;   // (route money for a deep dive is not job pay)
    (void)cr0;
    for (auto &o : objs) if (o.kind == K_GATE && (o.gflags & GF_JOB)) strayGate++;
    int dock = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_DOCKGATE) dock = i;
    if (dock < 0) { noDock++; continue; }
    if (gateColor(objs[dock]) != rgb(240, 200, 90)) notGold++;
    if (sm::contractDeliverHere(hereName) && !sm::contract().live) paid++;
  }
  printf("jobs %d | point at an existing gate %d | paid on arrival (wrong) %d | stray job gate at destination %d | no dock at destination %d | dock not gold %d | paid at the dock %d\n",
         total, pointedAtExisting, paidOnArrival, strayGate, noDock, notGold, paid);
  bool ok = total > 30 && pointedAtExisting == total && paidOnArrival == 0 && strayGate == 0 && noDock == 0 && notGold == 0 && paid == total;
  printf(ok ? "ALL OK\n" : "FAILURES\n");
  return ok ? 0 : 1;
}
