// Deep outposts: premium for rock and ore, a market for deep scans, deep jobs.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
int main() {
  setup(); bootOpen = false;
  sm::haulClear(); sm::haulAdd("ore", 4, true); sm::haulAdd("anomaly scan", 2, true);
  layer = 0; int surface = sellableValue(false);
  layer = 1; int deep = sellableValue(false);
  check(deep > surface * 2, "a deep outpost pays far more for ore and buys deep scans");
  sm::haulClear(); sm::haulAdd("anomaly scan", 2, true); layer = 0;
  check(sellableValue(false) == 0, "nobody on the surface buys deep scans");
  // the deep board posts deep work
  sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 2; strcpy(tr.dest, "X"); layer = 1; makeLayerScene();
  placeStation(shipPos + shipB.f * 120.f, -shipB.f, makeLook(SS_OUTPOST, BR_FREEHOLD));
  for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) stationIdx = i;
  sm::contractAbandon(); openBoard();
  sm::ContractKind k = sm::contractOfferPeek().kind;
  check(k == sm::CK_DEEPRESCUE || k == sm::CK_DEEPSCAN, "the outpost posts deep work");
  stationChoice = 1; stationCommit();
  check(sm::contract().live, "deep job taken");
  stationOpen = false;
  // complete it the way the pilot would
  for (int i = 0; i < 3 && sm::contract().live; i++) {
    sm::ResolveIn in{sm::VERB_HAIL, k == sm::CK_DEEPRESCUE ? sm::ENC_ESCAPE_POD : sm::ENC_ANOMALY, 1, 2};
    sm::resolve(in);
  }
  sm::Contract done; bool fin = sm::contractTakeCompleted(done);
  check(fin && done.kind == k, "the deep job completes below");
  // and does not complete on the surface
  sm::contractAbandon(); sm::contractOfferDeep(); sm::contractAccept(sm::contractOfferPeek()); layer = 0; sm::contractSetBand(0);
  sm::ResolveIn in2{sm::VERB_HAIL, sm::ENC_ESCAPE_POD, 0, 2}; sm::resolve(in2); sm::ResolveIn in3{sm::VERB_HAIL, sm::ENC_ANOMALY, 0, 2}; sm::resolve(in3);
  check(sm::contract().live, "deep work does not count on the surface");
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
