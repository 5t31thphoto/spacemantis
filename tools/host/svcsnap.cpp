#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void snap(const char *n) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",n); bannerUntil = 0; g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
static void dockAt(const char *place, uint8_t style, uint8_t brand) {
  makeRealScene(place, false); for (auto &o : objs) if (o.kind == K_STATION || o.kind == K_DOCKGATE) o.kind = K_NONE;
  placeStation(shipPos + shipB.f * 150.f, -shipB.f, makeLook(style, brand));
  for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) stationIdx = i;
  openBoard(); crossFlash = 0;
}
int main() {
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet(); p.credits = 2400; sm::earnCap(sm::CAP_TRAILER, 4); sm::earnCap(sm::CAP_WEAPONS, 2);
  sm::haulAdd("ore", 12, true); sm::haulAdd("electronics", 20, true);
  dockAt("MALTA YARD", SS_RING, BR_MALTAPLEX);
  stationPage = 0; snap("v1_board"); stationPage = 1; hangarSel = 1; snap("v2_hangar"); stationPage = 2; marketSel = 6; snap("v3_market");
  dockAt("FAR CAMP", SS_OUTPOST, BR_FREEHOLD); stationPage = 1; snap("v4_hangar_outpost"); stationPage = 2; marketSel = 1; snap("v5_market_outpost");
  dockAt("LIM HUB", SS_HEXCORE, BR_LIMINAR); stationPage = 1; snap("v6_hangar_liminar");
}
