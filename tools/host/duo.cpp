// Two devices, one sky: run two copies with SM_DEVICE=1 and SM_DEVICE=2 at the same time.
#include "harness_core.inc"
#include <unistd.h>
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0, dev = 1;
static void check(bool ok, const char *w) { printf("[dev%d] %s %s\n", dev, ok ? "ok  " : "FAIL", w); fflush(stdout); if (!ok) fails++; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); usleep(1500); }
template <typename F> static bool waitFor(F cond, int frames) { for (int i = 0; i < frames; i++) { frame(); if (cond()) return true; } return false; }
int main() {
  const char *e = getenv("SM_DEVICE"); dev = e ? atoi(e) : 1;
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet(); p.credits = 5000; p.fuel = p.fuelCap;
  sm::flagSet("signals", 1, true);
  check(signalsOn() && svcCount() == 4, "signals on: the dock grows a SIGNAL button");
  makeRealScene(dev == 1 ? "NORTH HARBOR" : "SOUTH REACH", true);
  for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) { stationIdx = i; break; }
  openBoard(); stationPage = 3;
  handleTap(60, 60);   // COMMISSION A PORTAL
  check(net::phase() == net::PH_SEEKING, "portal commissioned, seeking");
  check(waitFor([] { return net::phase() == net::PH_READY; }, 4000), "paired with the other pilot: the gate turns green");
  printf("[dev%d]    role %s, peer %s, seed %08x\n", dev, net::role() == net::ROLE_ANCHOR ? "ANCHOR" : "GUEST", net::peerName(), net::seed());
  handleTap(60, 88);   // OPEN GATE
  check(!stationOpen && target >= 0 && objs[target].uses == 2, "launched toward the magenta gate");
  // fly it: the gate, the chain, one layer down (the experimental wake), and back up
  threadGate(objs[target]);
  bool sawWake = false;
  for (int guard = 0; guard < 40 && sm::trip().active; guard++) {
    for (auto &o : objs) if (o.kind == K_ANOMALY && !strcmp(o.name, "EXPERIMENTAL WAKE")) sawWake = true;
    int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; } if (n < 0) break; threadGate(objs[n]);
  }
  check(sawWake, "the experimental wake one layer down");
  check(inMeeting && glitchT > 0.f, "arrived in the shared sky (haywire)");
  char sky[24]; asciiCopy(sky, sizeof(sky), hereName);
  check(waitFor([] { return remoteIdx >= 0; }, 3000), "the other pilot appears as a ship");
  printf("[dev%d]    shared sky %s; other pilot %.0f units away\n", dev, sky, remoteIdx >= 0 ? distTo(objs[remoteIdx]) : -1.f);
  // chat
  if (dev == 1) chatSend("NICE PARKING");
  waitFor([] { return chatLogN > (dev == 1 ? 1 : 0); }, 1500);
  if (dev == 2) check(chatLogN > 0 && strstr(chatLog[0], "NICE PARKING") != nullptr, "chat arrives on the other screen");
  // tag: device 1 lines up and fires
  if (dev == 1) {
    for (int k = 0; k < 400 && remoteIdx >= 0; k++) { shipB = Basis::facing(norm(objs[remoteIdx].p - shipPos), V3{0, 1, 0}); throttleT = 0; shipSpeed = 0; frame(); if (k == 5) { target = remoteIdx; runVerb(VB_ATTACK); } if (k > 5 && theater == TH_NONE) break; }
    check(tagsGiven > 0, "tagged them");
  } else check(waitFor([] { return itMe; }, 2500), "got tagged: you're it");
  // shared spawns: the anchor's pirate shows up on the guest; its death removes it there
  if (net::role() == net::ROLE_ANCHOR) {
    for (int k = 0; k < 400; k++) { frame(); }
    spawnTimer = 0;
    int before = 0; for (auto &o : objs) if (o.kind == K_SHIP && !o.remote) before++;
    for (int k = 0; k < 20; k++) { spawnContact(); }
    int sharedId = 0; for (auto &o : objs) if (o.kind == K_SHIP && !o.remote) { sharedId = 1; }
    for (int k = 0; k < 300; k++) frame();
    int ships = 0; for (auto &o : objs) if (o.kind == K_SHIP && !o.remote && o.net) ships++;
    check(sharedId && ships > 0, "the anchor's contacts carry shared ids");
    printf("[dev%d]    anchor has %d shared ships (was %d local)\n", dev, ships, before);
    for (int k = 0; k < 600; k++) frame();
    int killed = 0; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_SHIP && !objs[i].remote && objs[i].net && objs[i].net < 1000) { killObj(i); killed++; }
    printf("[dev%d]    anchor removed %d shared ships\n", dev, killed);
    for (int k = 0; k < 600; k++) frame();
  } else {
    check(waitFor([] { for (auto &o : objs) if (o.kind == K_SHIP && !o.remote && o.net && o.net < 1000) return true; return false; }, 4000), "the guest sees the anchor's contacts");
    check(waitFor([] { for (auto &o : objs) if (o.kind == K_SHIP && !o.remote && o.net && o.net < 1000) return false; return true; }, 4000), "and loses them when the anchor does");
  }
  // teardown: device 2 flies home
  if (dev == 2) {
    for (int k = 0; k < 300; k++) frame();
    int home = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE && sm::sameName(objs[i].name, "SOUTH REACH")) home = i;
    check(home >= 0, "a gate home");
    threadGate(objs[home]);
    check(!inMeeting && net::phase() == net::PH_OFF, "leaving the sky ends the session");
    for (int guard = 0; guard < 40 && sm::trip().active; guard++) { int n = navObj; if (n < 0 || objs[n].kind == K_NONE) { spawnNextOnPath(); n = navObj; } if (n < 0) break; threadGate(objs[n]); }
    check(sm::sameName(hereName, "SOUTH REACH"), "and the gate takes you home");
  } else {
    check(waitFor([] { return remoteIdx < 0; }, 6000), "the other pilot leaves the sky");
  }
  // the regular game was never touched: the meeting sky is nowhere in the atlas or the save
  check(sm::atlasFind(sky) < 0, "the shared sky never enters the atlas");
  Session ss; captureSession(ss);
  check(!sm::sameName(ss.here, sky), "or the session save");
  printf("[dev%d] %s\n", dev, fails ? "FAILURES" : "ALL OK");
  return fails;
}
