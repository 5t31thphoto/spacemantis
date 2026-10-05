// Landmark hubs: routes past a hub, gates at a hub, and what survives the pod.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static int gateTo(const char *n) { for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && sm::sameName(objs[i].name,n)) return i; return -1; }
// fly the chain; at the hub (turn point), optionally take the hub gate to `pick`
static void fly(int g, const char *pick = nullptr) {
  threadGate(objs[g]);
  for (int guard = 0; guard < 60 && sm::trip().active; guard++) {
    if (pick && layer > 0) { int hg = gateTo(pick); if (hg >= 0 && objs[hg].uses == 1) { threadGate(objs[hg]); pick = nullptr; continue; } }
    int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; }
    if (n < 0) break; threadGate(objs[n]);
  }
}
int main() {
  setup(); bootOpen = false; sm::earnCap(sm::CAP_BULKHEADS, 10);
  sm::contractAbandon();
  const sm::Landmark *L = sm::landmarkAt(3);          // a hub
  char A[24]; strcpy(A, hereName);
  // 1) A -> B, passing the hub on the way
  int g = -1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && !(objs[i].gflags&GF_FIXED)) g = i;
  char B[24]; strcpy(B, objs[g].name);
  sm::sheet().fuel = 100; threadGate(objs[g]); asciiCopy(sm::trip().via, sizeof(sm::trip().via), L->name);
  for (int guard=0; guard<40 && sm::trip().active; guard++) { int n=navObj; if(n<0||objs[n].kind==K_NONE){spawnNextOnPath(); n=navObj;} threadGate(objs[n]); }
  check(sm::sameName(hereName, B), "arrived at B after passing the hub");
  int gA = gateTo(A), gL = gateTo(L->name);
  check(gA >= 0, "B has a gate back to A");
  check(gA >= 0 && objs[gA].lmId == (int)L->id && !(objs[gA].gflags & GF_FIXED), "that gate runs past the hub (VIA)");
  check(gL >= 0 && (objs[gL].gflags & GF_FIXED), "B has a gate to the hub itself");
  // 2) back at A: same two gates
  sm::sheet().fuel = 100; fly(gA);
  check(sm::sameName(hereName, A), "flew the via-gate back to A");
  int gB = gateTo(B); gL = gateTo(L->name);
  check(gB >= 0 && objs[gB].lmId == (int)L->id, "A has a gate to B past the hub");
  check(gL >= 0, "A has a gate to the hub");
  // 3) straight to the hub: it offers every known place routed through it; take B
  sm::sheet().fuel = 100; sm::sheet().hull = 100; fly(gL, B);
  check(sm::sameName(hereName, B), "at the hub, took its gate to B and surfaced at B");
  // 4) straight to the hub and just keep climbing: somewhere new, linked past the hub
  gL = gateTo(L->name); sm::sheet().fuel = 100; sm::sheet().hull = 100;
  char before[24]; strcpy(before, hereName); fly(gL);
  check(!sm::sameName(hereName, before) && !sm::sameName(hereName, A), "ignored the hub gates: surfaced somewhere new");
  int gBack = gateTo(before);
  check(gBack >= 0 && objs[gBack].lmId == (int)L->id, "the new place has a gate back past the hub");
  // 5) the map can route through a hub
  int path[40]; int pn = sm::atlasPath(sm::atlas().here, sm::atlasFind(A), path, 40);
  check(pn >= 2, "the map traces a way home");
  // 6) the pod: an uncharted hub is forgotten, a charted one stays
  const sm::Landmark *C = sm::landmarkAt(5);
  sm::discoverLandmark(C->id); sm::atlasFixedPoint(C->name, C->band);
  sm::damageHull(500); checkDestroy();
  check(sm::atlasFind(L->name) < 0, "uncharted hub forgotten after the pod");
  check(sm::atlasFind(C->name) >= 0, "charted hub survives the pod");
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
