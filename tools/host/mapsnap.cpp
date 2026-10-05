#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void flyTrip(int g) { threadGate(objs[g]); for (int guard=0; guard<40 && sm::trip().active; guard++) { int n=navObj; if (n<0||objs[n].kind==K_NONE){spawnNextOnPath(); n=navObj;} if(n<0)break; threadGate(objs[n]); } }
static void snap(const char *n) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",n); g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
int main() {
  setup(); bootOpen = false; bannerUntil = 0; crossFlash = 0;
  sm::sheet().fuel = 100; sm::earnCap(sm::CAP_BULKHEADS, 10);
  for (int i = 0; i < 3; i++) sm::discoverLandmark(sm::landmarkAt(i)->id);
  // wander: ten trips, picking different gates, sometimes going back
  uint32_t r = 7;
  for (int k = 0; k < 10; k++) {
    int cand[8], n = 0;
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && (objs[i].gflags & GF_DEST) && !(objs[i].gflags & GF_FIXED)) cand[n++] = i;
    if (!n) break;
    r = r * 1103515245u + 12345u;
    sm::sheet().fuel = 100; sm::sheet().hull = 100; flyTrip(cand[(r >> 8) % n]);
    if (k == 4) { sm::atlasRumor("Hollow Tide", 2, false); }
    if (k == 6) { const sm::Landmark *lm = sm::landmarkAt(1); sm::atlasFixedPoint(lm->name, lm->band); }
  }
  sm::atlasRumor("Cinder Lock", 1, false);
  sm::contractOffer(sm::CK_HAUL); sm::contractAccept(sm::contractOfferPeek());
  mapOpen = true; mapPage = 0; snap("40_map");
  mapTrace = sm::atlasFind("Hollow Tide"); snap("41_map_trace");
  mapPage = 1; mapTrace = -1; snap("42_system_real");
  mapOpen = false;
  sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 3; tr.layer = 2; strcpy(tr.dest, "VEX ANCHOR");
  layer = 2; makeLayerScene(); spawnLandmarkObj(sm::landmarkAt(4), shipPos + shipB.f * 160); spawnNextOnPath();
  mapOpen = true; mapPage = 1; snap("43_system_deep");
  mapOpen = false; layer = 0;
  sm::journalAdd("THE LATTICE held me in the dark. the shields are stronger. no idea how.");
  sm::journalAdd("Charted Mantis Glass. It was where the stories said.");
  statusOpen = true; statusPage = 1; snap("44_journal");
}
