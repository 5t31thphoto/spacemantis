#include "harness_core.inc"
static bool qA=false,qC=false,qAdown=false,qCdown=false;
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false;
  BtnA.down=qAdown; BtnC.down=qCdown; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void frame() { uint32_t now=millis(); static uint32_t prev=now; dt=clampf((now-prev)/1000.f,0.008f,0.05f); prev=now; tNow+=dt; updateInput(); updateWorld(); hx::update(dt); }
static void flyTrip(int g) { threadGate(objs[g]); for (int guard=0; guard<40 && sm::trip().active; guard++) { int n=navObj; if (n<0||objs[n].kind==K_NONE){spawnNextOnPath(); n=navObj;} if(n<0)break; threadGate(objs[n]); } }
static void report(const char *tag) {
  printf("[%s] place=%s layer=%d trip=%d(%s L%u step%u) cr=%ld lives=%lu lead=%s atlasPlaces=%d journal=%d docked=%d\n", tag, hereName, layer,
    sm::trip().active, sm::trip().dest, sm::trip().layer, sm::trip().step, (long)sm::sheet().credits, (unsigned long)sm::sheet().lives,
    sm::contract().live ? sm::contract().title : "-", [](){int n=0; for (auto&p: sm::atlas().place) if (p.flags & sm::AP_USED) n++; return n;}(), sm::journal().count, stationOpen);
}
int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "boot";
  if (!strcmp(mode, "nosd")) SD.present = false;
  setup();
  report("boot");
  if (!strcmp(mode, "play")) {
    bootOpen = false;
    sm::sheet().credits = 1234;
    sm::contractOffer(sm::CK_HAUL); sm::contractAccept(sm::contractOfferPeek());
    int g=-1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && !(objs[i].gflags&GF_JOB)) g=i;
    flyTrip(g);                                   // arrive somewhere new
    // start another trip and stop mid-dive in layer 1
    int g2=-1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && !(objs[i].gflags&GF_JOB)) g2=i;
    threadGate(objs[g2]); threadGate(objs[navObj]); threadGate(objs[navObj]);   // gate, gate, portal -> layer 1
    saveAll(); serviceSD(true);
    report("saved mid-dive");
  }
  if (!strcmp(mode, "newgame")) {
    // splash is up; hold A + C for 2 seconds
    qAdown = qCdown = true;
    bool sawRed = false;
    for (int i = 0; i < 400 && newGameDone <= 0; i++) { frame(); if (newGameHold >= 1.5f) sawRed = true; }
    qAdown = qCdown = false;
    printf("progress turned red before clearing: %s, splash still open: %s\n", sawRed ? "yes" : "no", bootOpen ? "yes" : "no");
    report("after new game");
  }
  return 0;
}
