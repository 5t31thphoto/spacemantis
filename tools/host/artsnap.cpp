// Phase 1 art review: stars, stations, ships, staged and rendered by the real draw code.
#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
static void snap(const char *n) { char p[64]; snprintf(p,sizeof p,"snaps/%s.log",n); bannerUntil = 0; g_log=fopen(p,"w"); draw(); fclose(g_log); g_log=nullptr; }
static void clean() { clearWorld(); target = -1; navObj = -1; crossFlash = 0; sm::tripEnd(); }
static void face(V3 dir, float yaw = 0, float pitch = 0) { shipB = Basis::facing(norm(dir), V3{0,1,0}); shipB.yaw(yaw); shipB.pitch(pitch); shipB.fix(); }
int main() {
  setup(); bootOpen = false; tNow = 10.f;
  // ---- 1. skybox suns, every type ----
  static const char *sn[] = {"a1_red_dwarf","a2_orange","a3_yellow","a4_white","a5_blue_giant","a6_red_giant","a7_white_dwarf"};
  for (int t = 0; t < 7; t++) {
    makeRealScene("SKY", false); clean(); nearStar = false;
    sunType = t; static const uint16_t c[7] = {rgb(255,120,70),rgb(255,172,100),rgb(255,232,175),rgb(248,248,255),rgb(165,195,255),rgb(255,105,60),rgb(200,220,255)};
    sunCol = c[t]; sunDir = norm(V3{0.25f, 0.12f, 1}); face(V3{0,0,1}); snap(sn[t]);
  }
  // ---- 2. stars as bodies you can fly toward, and the deep ones ----
  struct B { const char *n; uint8_t type; float dist, rad; uint16_t col; int lay; } bodies[] = {
    {"b1_star_body", BT_STAR, 900, 120, rgb(255,232,175), 0}, {"b2_red_giant_close", BT_REDGIANT, 700, 420, rgb(255,105,60), 0},
    {"b3_white_dwarf", BT_WHITEDWARF, 300, 6, rgb(200,220,255), 0}, {"b4_neutron_deep", BT_NEUTRON, 220, 3, rgb(210,190,255), 3},
    {"b5_impossible_star", BT_WRONGSTAR, 300, 40, 0, 3}, {"b6_collapsed_star", BT_HOLE, 160, 13, 0, 3}};
  for (auto &b : bodies) {
    makeRealScene("SKY", false); clean(); nearStar = b.lay == 0; layer = b.lay; if (b.lay) makeLayerScene(); clearWorld();
    face(V3{0,0,1}); spawnBody(b.type, b.dist, b.rad, norm(V3{0.18f, 0.05f, 1}));
    for (auto &o : objs) if (o.kind == K_BODY && b.col) o.col = b.col;
    snap(b.n); layer = 0;
  }
  // ---- 3. stations ----
  struct S { const char *n; uint8_t style, brand; float rad, dist; float yaw, pitch; } sts[] = {
    {"c1_liminar_hexcore", SS_HEXCORE, BR_LIMINAR, 24, 62, 0.45f, -0.25f}, {"c2_maltaplex_ring", SS_RING, BR_MALTAPLEX, 32, 125, 0.6f, -0.35f},
    {"c3_portex_spindle", SS_SPINDLE, BR_PORTEX, 26, 78, 1.1f, -0.2f}, {"c4_freehold_outpost", SS_OUTPOST, BR_FREEHOLD, 11, 32, 0.7f, -0.3f},
    {"c5_deseret_hexcore_preview", SS_HEXCORE, BR_DESERET, 32, 95, 0.05f, 0.f}, {"c6_maltaplex_ring_close", SS_RING, BR_MALTAPLEX, 32, 62, 0.25f, -0.15f}};
  for (auto &st : sts) {
    makeRealScene("SKY", false); clean(); nearStar = false; sunDir = norm(V3{-0.5f, 0.55f, -0.65f});
    face(V3{0,0,1});
    Obj *o = newObj(K_STATION); o->bodyType = st.style; o->uses = st.brand; o->radius = st.rad; o->mesh = M_STATION; o->col = rgb(150,160,175);
    o->p = shipPos + shipB.f * st.dist;
    Basis b = Basis::facing(V3{0,0,-1}, V3{0,1,0}); b.yaw(st.yaw); b.pitch(st.pitch); b.fix(); o->o = b;
    if (st.style == SS_RING) { Obj *g = newObj(K_DOCKGATE); g->p = o->p; g->o = o->o; g->radius = st.rad * 0.55f; snprintf(g->name, 24, "DOCK"); }
    snap(st.n);
  }
  // ---- 3b. a cool star you can scoop, and the status screen ----
  {
    int star = -1;
    for (int k = 0; k < 3000 && star < 0; k++) { char nm[24]; snprintf(nm, sizeof nm, "COOL %d", k); makeRealScene(nm, false);
      for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_BODY && isStarBody(objs[i].bodyType) && objs[i].uses) star = i; }
    target = -1; crossFlash = 0; for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_GATE || objs[i].kind == K_ROCK || objs[i].kind == K_SHIP) objs[i].kind = K_NONE;
    if (star >= 0) {
      Obj &S = objs[star]; shipPos = S.p + norm(V3{0.3f, 0.2f, -1}) * (S.radius * 1.9f); prevShipPos = shipPos;
      face(norm(S.p - shipPos), 0.35f, 0.05f); target = star; sm::sheet().fuel = 30; snap("e1_scoopable_star");
    }
    statusOpen = true; sm::earnCap(sm::CAP_FUELSYS, 2); snap("e2_status_nine_systems"); statusOpen = false;
  }
  // ---- 3c. the docking board header, and a ghostfleet hull up close ----
  {
    makeRealScene("SKY", false); clean(); sunDir = norm(V3{-0.5f, 0.55f, -0.65f}); face(V3{0,0,1});
    placeStation(shipPos + shipB.f * 150.f, -shipB.f, makeLook(SS_HEXCORE, BR_LIMINAR));
    for (int i = 0; i < MAX_OBJ; i++) if (objs[i].kind == K_STATION) stationIdx = i;
    openBoard(); snap("f1_board_liminar"); stationOpen = false;
    makeRealScene("SKY", false); clean(); sunDir = norm(V3{-0.4f, 0.6f, -0.7f}); face(V3{0,0,1});
    Obj *g = newObj(K_SHIP); g->enc = sm::ENC_SECURITY; g->mesh = M_GHOST; g->ghost = true; g->radius = 5.f; g->col = rgb(70,80,90);
    g->p = shipPos + shipB.f * 15.f + shipB.u * -1.f; g->o = Basis::facing(norm(V3{-1.f, 0.02f, -0.12f}), V3{0,1,0});
    snap("f2_ghostfleet_hull");
  }
  // ---- 4. ships: before and after the liveries ----
  for (int pass = 0; pass < 2; pass++) {
    makeRealScene("SKY", false); clean(); nearStar = false; sunDir = norm(V3{-0.45f, 0.6f, -0.65f}); face(V3{0,0,1});
    liveryOn = pass == 1;
    struct Sh { uint8_t enc, mesh; uint16_t col; float r; } ships[] = {
      {sm::ENC_TRAVELER, M_SHUTTLE, rgb(200,190,160), 3.2f}, {sm::ENC_MERCHANT, M_COBRA, rgb(140,175,165), 4.2f},
      {sm::ENC_SECURITY, M_VIPER, rgb(120,160,230), 3.2f}, {sm::ENC_PIRATE, M_SIDEWINDER, rgb(190,90,70), 3.2f},
      {sm::ENC_SUBPIRATE, M_KRAIT, rgb(150,90,200), 3.2f}, {sm::ENC_SECURITY, M_GHOST, rgb(70,80,90), 5.f}};
    for (int i = 0; i < 6; i++) {
      Obj *o = newObj(K_SHIP); o->enc = ships[i].enc; o->mesh = ships[i].mesh; o->col = ships[i].col; o->radius = ships[i].r; o->ghost = i == 5;
      o->p = shipPos + shipB.f * 21.f + shipB.r * ((i % 3) - 1) * 8.5f + shipB.u * (i < 3 ? -1.5f : -8.f);
      o->v = V3{0,0,0}; o->o = Basis::facing(norm(V3{-0.75f, 0.05f, -0.65f}), V3{0,1,0});
    }
    snap(pass ? "d2_ships_livery" : "d1_ships_before");
  }
}
