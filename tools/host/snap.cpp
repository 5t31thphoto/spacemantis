// Scripted states -> draw logs -> PNGs (render.py). A layout and look check.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void step(int n) { for (int i=0;i<n;i++){ uint32_t now=millis(); static uint32_t prev=now; dt=clampf((now-prev)/1000.f,0.008f,0.05f); prev=now; tNow+=dt; updateInput(); updateWorld(); hx::update(dt);} }
static void snap(const char *name) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",name); g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
static int findKind(uint8_t k){ for(int i=0;i<MAX_OBJ;i++) if(objs[i].kind==k) return i; return -1; }
static void faceTo(V3 p, float yawOff=0, float pitchOff=0){ shipB = Basis::facing(norm(p-shipPos), V3{0,1,0}); shipB.yaw(yawOff); shipB.pitch(pitchOff); shipB.fix(); }
int main() {
  setup(); bannerUntil = 0; crossFlash = 0;
  // 1: arrival — station, gates, giant
  int st = findKind(K_STATION);
  faceTo(objs[st].p, 0.25f, 0.05f); throttleT = 0.5f; shipSpeed = 13;
  setBanner("RESURFACED: DUSK CROWN. A WAY THROUGH DEPTH 2 IS WORTH MONEY | +135cr", 5000);
  snap("01_real_arrival");
  // 2: look at the gas giant with the sun
  int gi = findKind(K_BODY); faceTo(objs[gi].p, 0.3f, 0.1f); bannerUntil = 0;
  sunDir = norm(shipB.f*0.6f + shipB.r*0.75f + shipB.u*0.3f);
  snap("02_giant_and_sun");
  // 3: targeting a pirate with context verbs
  Obj *pir = newObj(K_SHIP); pir->enc = sm::ENC_PIRATE; pir->mesh = M_SIDEWINDER; pir->col = rgb(190,90,70); pir->hostile = true; pir->radius = 3.2f;
  snprintf(pir->name, sizeof pir->name, "RAIDER"); pir->p = shipPos + shipB.f*28 + shipB.r*4 + shipB.u*2; pir->v = shipB.r*-8.f; pir->o = Basis::facing(norm(shipB.r*-1 + shipB.f*-0.6f), V3{0,1,0});
  target = idxOf(pir); snap("03_target_ship");
  // 4: a station targeted -> DOCK
  pir->kind = K_NONE; faceTo(objs[st].p); shipPos = objs[st].p + objs[st].o.f*90 + objs[st].o.r*30; faceTo(objs[st].p, 0.1f); target = st; snap("04_target_station");
  // 5: asteroid mining beam
  target = -1; spawnRock(shipPos + shipB.f*22 + shipB.r*-3, 5.f); int rk = findKind(K_ROCK); target = rk; runVerb(VB_MINE); step(40); snap("05_mining");
  theater = TH_NONE;
  // 6: station board
  dockNow(st); step(30); snap("06a_docking"); step(300); stationChoice = 1; snap("06_station_board");
  launch(); step(200); bannerUntil = 0;
  // 7: dest gate labels + slider
  int dg = -1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST)) dg=i;
  shipPos = objs[dg].p + objs[dg].o.f*60 + objs[dg].o.r*10; faceTo(objs[dg].p, 0.05f); throttleT = 0.78f; snap("07_dest_gate");
  // thread it, then approach the portal in real space
  threadGate(objs[dg]); int ng = navObj; threadGate(objs[ng]);
  int po = navObj; shipPos = objs[po].p - norm(objs[po].p - shipPos)*55.f; faceTo(objs[po].p, 0.04f, 0.02f); bannerUntil = 0; throttleT = 0.5f;
  snap("08_portal_ahead");
  shipPos = objs[po].p - norm(objs[po].p - shipPos)*18.f; faceTo(objs[po].p); snap("09_portal_close");
  // layers 1..4
  const char *names[] = {"", "10_layer1_shallows", "11_layer2_roads", "12_layer3_below", "13_layer4_cove"};
  for (int L = 1; L <= 4; L++) {
    sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 4; tr.ascending = 0; tr.layer = (uint8_t)L; tr.step = 0;
    layer = L; makeLayerScene(); crossFlash = 0; bannerUntil = 0;
    spawnNextOnPath(); step(30);
    int n = navObj; if (n >= 0) { shipPos = objs[n].p - norm(objs[n].p - shipPos)*40.f; faceTo(objs[n].p, 0.12f, -0.05f); }
    tNow += 1.3f * L; deepFlash = 0;
    if (L == 2) { Obj *s2 = newObj(K_SHIP); s2->enc = sm::ENC_SUBPIRATE; s2->mesh = M_KRAIT; s2->col = rgb(150,90,200); s2->radius=3.2f; s2->p = shipPos + shipB.f*30 + shipB.r*-9; s2->v = shipB.r*6; s2->o = Basis::facing(shipB.r, V3{0,1,0}); snprintf(s2->name,24,"WAKE THIEF"); }
    if (L == 3) { spawnBody(BT_HOLE, 120, 12, norm(shipB.f + shipB.r*0.25f)); }
    if (L == 4) { const sm::Landmark *lm = pickLandmark(4, false); if (lm) spawnLandmarkObj(lm, shipPos + shipB.f*150 + shipB.r*-60); }
    snap(names[L]);
  }
  // deepest heartbeat inversion
  deepFlash = 0.8f; snap("14_cove_heartbeat"); deepFlash = 0;
  // portal from layer 3 looking into the cove
  { sm::Trip &tr = sm::trip(); tr.layer = 3; tr.step = 2; tr.ascending = 0; layer = 3; makeLayerScene(); spawnNextOnPath(); int p = navObj; shipPos = objs[p].p - norm(objs[p].p - shipPos)*22.f; faceTo(objs[p].p); bannerUntil=0; snap("15_portal_into_cove"); }
  // crossing flash
  crossFlash = 0.4f; snap("16_crossing");
  crossFlash = 0; endingOpen = true; snprintf(endingName, 24, "MANTIS GLASS"); layer = 4; snap("17_ending");
  return 0;
}
