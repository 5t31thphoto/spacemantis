#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
static void snap(const char *n) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",n); bannerUntil = 0; g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
static void scene(const char *n) { makeRealScene(n, false); for (auto &o : objs) if (o.kind == K_SHIP || o.kind == K_GATE || o.kind == K_ROCK) o.kind = K_NONE; crossFlash = 0; sunDir = norm(V3{-0.5f, 0.6f, -0.6f}); }
int main() {
  setup(); bootOpen = false; sm::Pilot &p = sm::sheet();
  int marks[4] = {1, 3, 5, 7};
  for (int i = 0; i < 4; i++) {
    scene("RANGE"); p.cap[sm::CAP_WEAPONS] = (uint8_t)marks[i];
    Obj *e = newObj(K_SHIP); e->enc = sm::ENC_PIRATE; e->mesh = M_SIDEWINDER; e->col = rgb(190,90,70); e->radius = 3.2f; e->p = shipPos + shipB.f * 36.f + shipB.u * 4.f; e->o = Basis::facing(V3{-1,0,-0.3f}, V3{0,1,0}); snprintf(e->name, 24, "RAIDER");
    target = idxOf(e); runVerb(VB_ATTACK);
    for (int k = 0; k < 42; k++) { frame(); e->p = shipPos + shipB.f * 36.f + shipB.u * 4.f; }
    char n[32]; snprintf(n, sizeof n, "w%d_weapons_mk%d", i, marks[i]); snap(n);
    while (theater != TH_NONE) frame();
  }
  // a shield deflection
  scene("RANGE"); p.cap[sm::CAP_SHIELDS] = 5; addBolt(150, 60, 170, H - 30, rgb(255, 90, 70)); addBolt(170, H - 30, 60, 120, rgb(255, 160, 120));
  shieldFx = 0.3f; shieldFxX = 170; shieldFxY = H - 30; snap("s1_shield_deflect");
  // mining MK4: cutting, then the tractor
  scene("BELT"); p.cap[sm::CAP_MINING] = 4; spawnRock(shipPos + shipB.f * 22.f + shipB.r * 3.f, 5.f);
  int rock = -1; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_ROCK) rock = i;
  target = rock; runVerb(VB_MINE); for (int k = 0; k < 40; k++) frame(); snap("m1_mining_cut");
  while (theater != TH_NONE) frame(); for (auto &b : booms) b.alive = false; for (int k = 0; k < 120; k++) frame(); snap("m2_mining_tractor");
  // cloak
  scene("DARK"); p.cap[sm::CAP_CLOAK] = 3; cloakCD = 0; cloakT = 14.f; snap("c1_cloaked");
  cloakT = 0;
  // stabilizer characters at a portal
  for (int i = 0; i < 2; i++) {
    scene("GATE"); layer = 0; p.cap[sm::CAP_STABILIZER] = i ? 7 : 0;
    Obj *po = newObj(K_PORTAL); po->p = shipPos + shipB.f * 40.f; po->o = Basis::facing(V3{0,0,1}, V3{0,1,0}); po->radius = 13.f;
    sm::Trip &tr = sm::trip(); tr.active = 1; tr.step = 2; tr.destDepth = 2; strcpy(tr.dest, "X");
    for (int k = 0; k < 6; k++) { frame(); shipPos = po->p - shipB.f * 40.f; }
    applyTurbulence(); snap(i ? "t2_phase_locked" : "t1_undamped"); sm::tripEnd();
  }
}
