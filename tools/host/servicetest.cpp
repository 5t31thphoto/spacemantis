// Station services: the hangar fits real equipment; the market lets a trucker speculate.
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
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet();
  p.credits = 50000; sm::earnCap(sm::CAP_TRAILER, 4);
  dockAt("MALTA YARD", SS_RING, BR_MALTAPLEX);
  check(hangarN == 3, "a full station racks three pieces of equipment");
  HangarOffer h = hangar[0]; uint8_t before = sm::capTier((sm::CapId)h.cap); int32_t cr = p.credits;
  hangarSel = 0; hangarBuy();
  check(sm::capTier((sm::CapId)h.cap) == h.mark && h.mark == before + 1 && p.credits == cr - h.price, "fitting equipment raises the mark and charges for it");
  dockAt("FAR CAMP", SS_OUTPOST, BR_FREEHOLD);
  check(hangarN == 2, "an outpost racks only two");
  int traded = 0; for (int g = 0; g < sm::G_COUNT; g++) traded += sm::marketTrades((sm::Good)g);
  check(traded < sm::G_COUNT, "an outpost carries a short list");
  // spread
  dockAt("LIM HUB", SS_HEXCORE, BR_LIMINAR);
  bool spread = true; for (int g = 0; g < sm::G_COUNT; g++) if (sm::marketBuy((sm::Good)g) <= sm::marketSell((sm::Good)g)) spread = false;
  check(spread, "every good buys dearer than it sells at one dock");
  // a speculative route: buy where it is made, sell where it is needed
  int bestProfit = 0; const char *bestGood = "";
  struct D { const char *n; uint8_t st, br; } docks[] = {{"A1", SS_HEXCORE, BR_LIMINAR}, {"B1", SS_SPINDLE, BR_PORTEX}, {"C1", SS_RING, BR_MALTAPLEX}, {"D1", SS_OUTPOST, BR_FREEHOLD}};
  for (auto &a : docks) for (auto &b : docks) {
    if (&a == &b) continue;
    for (int g = 0; g < sm::G_COUNT; g++) {
      dockAt(a.n, a.st, a.br); if (!sm::marketTrades((sm::Good)g)) continue; int buy = sm::marketBuy((sm::Good)g);
      dockAt(b.n, b.st, b.br); if (!sm::marketTrades((sm::Good)g)) continue; int sell = sm::marketSell((sm::Good)g);
      if (sell - buy > bestProfit) { bestProfit = sell - buy; bestGood = sm::goodName((sm::Good)g); }
    }
  }
  printf("    best route per unit: %s +%dcr  (x %u hold = +%dcr a run)\n", bestGood, bestProfit, p.holdCap, bestProfit * p.holdCap);
  check(bestProfit > 5, "buying where it is made and selling where it is needed pays");
  // trade by the unit
  dockAt("C1", SS_RING, BR_MALTAPLEX); sm::haulClear(); marketSel = sm::G_ALLOYS;
  int32_t c0 = p.credits; marketTrade(true, 5);
  check(sm::haulCount("alloys") == 5 && p.credits < c0, "buy five");
  c0 = p.credits; marketTrade(false, 999);
  check(sm::haulCount("alloys") == 0 && p.credits > c0, "sell them all");
  // the lead's cargo is protected
  sm::contractAbandon(); boardLanes(); if (sm::contractOffer(sm::CK_SUPPLY) && sm::contractOfferPeek().kind == sm::CK_SUPPLY) {
    sm::contractAccept(sm::contractOfferPeek()); marketSel = sm::G_PARTS; uint16_t had = sm::haulCount("machine parts");
    marketTrade(false, 999); check(sm::haulCount("machine parts") == had && had > 0, "you cannot sell your lead's cargo");
  }
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
