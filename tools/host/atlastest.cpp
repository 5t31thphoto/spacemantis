#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void flyTrip(int g) { threadGate(objs[g]); for (int guard = 0; guard < 40 && sm::trip().active; guard++) { int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; } if (n < 0) break; threadGate(objs[n]); } }
static int gateTo(const char *name) { for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && sm::sameName(objs[i].name,name)) return i; return -1; }
static void gateNames(char out[8][24], int &n) { n=0; for (int i=0;i<MAX_OBJ;i++) if (objs[i].kind==K_GATE && (objs[i].gflags&GF_DEST) && !(objs[i].gflags&GF_JOB)) strcpy(out[n++], objs[i].name); }
static bool has(char a[8][24], int n, const char *s) { for (int i=0;i<n;i++) if (sm::sameName(a[i],s)) return true; return false; }
int main() {
  setup(); bootOpen = false; sm::contractAbandon(); sm::sheet().fuel = 100; sm::sheet().hull = 100;
  int fails = 0;
  char home[24]; strcpy(home, hereName);
  char g0[8][24]; int n0; gateNames(g0, n0);
  printf("home %s: %d gates\n", home, n0);
  // fly to the first gate's place, then come back through the gate that must now exist there
  char dest[24]; strcpy(dest, g0[0]);
  flyTrip(gateTo(dest));
  printf("arrived %s (expected %s)\n", hereName, dest);
  if (!sm::sameName(hereName, dest)) { printf("FAIL arrival name\n"); fails++; }
  int back = gateTo(home);
  if (back < 0) { printf("FAIL no gate back to %s\n", home); fails++; } else printf("gate back to %s: ok\n", home);
  char gd[8][24]; int nd; gateNames(gd, nd);
  // leave and come back: same gates at this place, and the same dock (or none)
  int st1 = countKind(K_STATION);
  makeRealScene(dest, st1 == 0);   // ask for the opposite: memory must win
  if (countKind(K_STATION) != st1) { printf("FAIL station changed on revisit\n"); fails++; } else printf("dock remembered on revisit: ok\n");
  char gd2[8][24]; int nd2; gateNames(gd2, nd2);
  bool same = nd == nd2; for (int i=0;i<nd;i++) if (!has(gd2,nd2,gd[i])) same = false;
  printf("revisit %s: %d gates, same set: %s\n", dest, nd2, same ? "yes" : "NO"); if (!same) fails++;
  // home again via the lane back: its gates unchanged
  flyTrip(gateTo(home));
  char g1[8][24]; int n1; gateNames(g1, n1);
  bool sameHome = true; for (int i=0;i<n0;i++) if (!has(g1,n1,g0[i])) sameHome = false;
  printf("home again: %d gates, all original lanes present: %s\n", n1, sameHome ? "yes" : "NO"); if (!sameHome) fails++;
  // a rumor opens a lane here, right now
  sm::atlasRumor("Velvet Sound", 2, false);
  makeRealScene(hereName, false);
  printf("rumor gate here: %s\n", gateTo("Velvet Sound") >= 0 ? "yes" : "NO"); if (gateTo("Velvet Sound") < 0) fails++;
  // path from here to the rumor
  int path[40]; int pn = sm::atlasPath(sm::atlas().here, sm::atlasFind("Velvet Sound"), path, 40);
  printf("path len to rumor: %d\n", pn);
  // chart a fixed point, then die: surface wiped, fixed point remains
  const sm::Landmark *lm = sm::landmarkAt(0); sm::discoverLandmark(lm->id); sm::atlasFixedPoint(lm->name, lm->band);
  int before = 0; for (auto &p : sm::atlas().place) if (p.flags & sm::AP_USED) before++;
  sm::damageHull(500); checkDestroy();
  int after = 0, fixedLeft = 0; for (auto &p : sm::atlas().place) if (p.flags & sm::AP_USED) { after++; if (p.flags & sm::AP_FIXED) fixedLeft++; }
  printf("after pod: places %d -> %d, fixed kept %d, here=%s\n", before, after, fixedLeft, hereName);
  if (fixedLeft != 1) { printf("FAIL fixed point lost\n"); fails++; }
  // save/load round trip
  saveAll(); sm::Atlas copy = sm::atlas(); sm::atlasClear(); sm::atlasLoad();
  bool rt = memcmp(copy.place, sm::atlas().place, sizeof(copy.place)) == 0;
  printf("atlas save/load round trip: %s\n", rt ? "ok" : "FAIL"); if (!rt) fails++;
  printf("journal entries: %d, newest: %s\n", sm::journal().count, sm::journalNewest(0) ? sm::journalNewest(0)->text : "-");
  printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
  return fails;
}
