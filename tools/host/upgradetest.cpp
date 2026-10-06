// Every upgrade does something you can measure.
#include "harness_core.inc"
static bool qHoldB = false;
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnC.click=BtnC.hold=false; BtnB.hold=qHoldB; qHoldB=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static int fails = 0;
static void check(bool ok, const char *w) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
int main() {
  setup(); bootOpen = false;
  sm::Pilot &p = sm::sheet();
  // trailer
  uint16_t h0 = p.holdCap; sm::earnCap(sm::CAP_TRAILER, 6);
  printf("    hold %u -> %u with trailer MK6\n", h0, p.holdCap); check(p.holdCap >= h0 + 80, "the trailer grows the hold");
  // shields
  p.hull = 100; damage(20); int unshielded = 100 - p.hull; p.hull = 100; sm::earnCap(sm::CAP_SHIELDS, 6); damage(20); int shielded = 100 - p.hull;
  printf("    collision 20 -> %d (MK1) vs %d (MK6)\n", unshielded, shielded); check(shielded < unshielded && shieldFx > 0, "shields soften hits and flare");
  // weapons: missiles at MK4+
  makeRealScene("RANGE", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  Obj *e = newObj(K_SHIP); e->enc = sm::ENC_PIRATE; e->mesh = M_SIDEWINDER; e->radius = 3.2f; e->p = shipPos + shipB.f * 40.f; snprintf(e->name, 24, "RAIDER");
  sm::earnCap(sm::CAP_WEAPONS, 5); target = idxOf(e); runVerb(VB_ATTACK);
  int sawMissiles = 0; for (int i = 0; i < 200 && theater != TH_NONE; i++) { frame(); for (auto &m : missiles) if (m.alive) sawMissiles++; }
  check(sawMissiles > 0, "weapons MK5 fire particle missiles");
  // mining: MK4 clears the rock and tractors the pieces
  makeRealScene("BELT", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  spawnRock(shipPos + shipB.f * 20.f, 5.f); int rock = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_ROCK) rock = i;
  sm::earnCap(sm::CAP_MINING, 4); sm::haulClear(); target = rock; runVerb(VB_MINE);
  for (int i = 0; i < 300 && theater != TH_NONE; i++) frame();
  int fl = 0; for (auto &f : frags) if (f.alive) fl++;
  check(objs[rock].kind != K_ROCK && fl > 0, "mining MK4 blows the rock apart and tractors the pieces");
  printf("    hold after one rock: %u\n", p.holdUsed);
  check(p.holdUsed >= 6, "and the yield is real");
  // cloak
  sm::earnCap(sm::CAP_CLOAK, 2); makeRealScene("DARK", false); for (auto &o : objs) if (o.kind == K_SHIP) o.kind = K_NONE;
  Obj *pir = newObj(K_SHIP); pir->enc = sm::ENC_PIRATE; pir->hostile = true; pir->mesh = M_SIDEWINDER; pir->radius = 3.2f; pir->p = shipPos + shipB.f * 80.f;
  qHoldB = true; frame();
  printf("    cloak MK2: %.0fs\n", cloakT); check(cloakT > 15.f, "hold B cloaks");
  for (int i = 0; i < 300; i++) frame();
  check(theater == TH_NONE, "a pirate cannot find a cloaked ship");
  while (cloakT > 0.f) frame();
  check(cloakCD > 0.f, "then the cloak recharges");
  // scanners: reach
  Obj probe{}; probe.kind = K_SHIP; probe.radius = 3; probe.p = shipPos + shipB.f * 250.f; Chip c[3];
  verbsFor(probe, c); bool far0 = c[0].enabled;
  sm::earnCap(sm::CAP_SCANNERS, 5); verbsFor(probe, c); bool far5 = c[0].enabled;
  check(!far0 && far5, "scanners MK5 hail from much further out");
  // stabilizer, below the roads at a portal: unrated it kicks and shakes; rated, the kicks are gone
  { sm::Trip &tr = sm::trip(); tr.active = 1; tr.destDepth = 3; tr.ascending = 1; tr.layer = 3; strcpy(tr.dest, "X"); layer = 3; makeLayerScene(); }
  Obj *po = newObj(K_PORTAL); po->p = shipPos + shipB.f * 30.f; po->o = Basis::facing(V3{0,0,1}, V3{0,1,0}); po->radius = 13.f;
  p.cap[sm::CAP_STABILIZER] = 0; applyTurbulence(); float shake0 = fabsf(shakeX) + fabsf(shakeY); uint8_t m0 = stabMode(); bool r0 = turbRated;
  p.cap[sm::CAP_STABILIZER] = 7; applyTurbulence(); uint8_t m7 = stabMode(); bool r7 = turbRated;
  printf("    below the roads at a portal: MK0 shake %.1f (rated %d) vs MK7 (rated %d)\n", shake0, r0, r7);
  check(!r0 && r7 && shake0 > 2.f && m0 != m7, "stabilizer marks decide whether the deep is survivable");
  sm::tripEnd(); layer = 0;
  layer = 3; float slow = (p.cap[sm::CAP_STABILIZER] = 0, cruiseSpeed()); p.cap[sm::CAP_STABILIZER] = 7; float fast = cruiseSpeed(); layer = 0;
  check(fast > slow * 1.2f, "and the ship is faster below");
  // cargo work scales with the trailer, and market runs pay back their load
  makeRealScene("PORT", true); boardLanes(); sm::contractAbandon(); sm::haulClear(); p.credits = 20000;
  if (sm::contractOffer(sm::CK_MARKET) && sm::contractOfferPeek().kind == sm::CK_MARKET) {
    int pay = sm::contractOfferPeek().pay; int32_t cr0 = p.credits; sm::contractAccept(sm::contractOfferPeek()); int spent = cr0 - p.credits;
    printf("    market run with trailer MK6: buys %u units for %d, pays %d\n", p.holdUsed, spent, pay);
    check(pay > spent * 1.3f && p.holdUsed > 15, "a big trailer makes a market run a big, profitable load");
  }
  printf(fails ? "FAILURES %d\n" : "ALL OK\n", fails);
  return fails;
}
