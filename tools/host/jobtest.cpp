#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
// Teleport through a whole trip using the real thread/cross code paths.
static void flyTrip(Obj &dest) {
  threadGate(dest);
  for (int guard = 0; guard < 40 && sm::trip().active; guard++) {
    int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; }
    if (n < 0) break;
    threadGate(objs[n]);
  }
}
int main() {
  setup(); bootOpen = false;
  int ok = 0, total = 0;
  for (int k = 0; k < 60; k++) {
    sm::contractAbandon();
    sm::ContractKind kinds[] = {sm::CK_HAUL, sm::CK_SUPPLY, sm::CK_SMUGGLE, sm::CK_MARKET, sm::CK_DEPTHRUN};
    sm::ContractKind kd = kinds[k % 5];
    sm::sheet().credits = 5000; sm::haulClear(); sm::sheet().fuel = 100; sm::sheet().hull = 100;
    sm::contractOffer(kd);
    if (!sm::contractAccept(sm::contractOfferPeek())) continue;
    makeRealScene("TEST HARBOUR", true);
    int g = -1; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags & GF_JOB)) g = i;
    total++;
    if (g < 0) { printf("no job gate for kind %d dest %s\n", kd, sm::contract().dest); continue; }
    flyTrip(objs[g]);
    if (!sm::contract().live) ok++;
    else if (k < 10) printf("NOT COMPLETED kind %d dest '%s' arrived at '%s'\n", kd, sm::contract().dest, hereName);
  }
  printf("completed %d / %d destination jobs\n", ok, total);
}
