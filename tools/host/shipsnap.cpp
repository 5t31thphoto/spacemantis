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
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet(); p.credits = 30000;
  for (int t = 1; t < sm::SHIP_COUNT; t++) { if (t == sm::SHIP_GHOST) sm::shipGrant((uint8_t)t); else sm::shipBuy((uint8_t)t); }
  for (int t = 0; t < sm::SHIP_COUNT; t++) { sm::haulClear(); if (t) sm::shipSwap((uint8_t)t); statusOpen = true; char n[32]; snprintf(n, sizeof n, "x%d_status", t); snap(n); statusOpen = false; }
  sm::shipSwap(sm::SHIP_MANTIS); p.ships[sm::SHIP_HONEYBEE].lost = 1; p.ships[sm::SHIP_GHOST].owned = 0;
  dockAt("LIM HUB", SS_HEXCORE, BR_LIMINAR); stationPage = 1; hangarTab = 1; shipSel = 1; snap("y1_hangar_ships");
  stationPage = 1; hangarTab = 0; snap("y2_hangar_equipment");
  sm::haulAdd("electronics", 6, true); sm::haulAdd("ore", 9, true); stationPage = 2; marketSel = 6; snap("y3_market_icons");
}
