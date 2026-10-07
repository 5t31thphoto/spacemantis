// Same realistic sensor stream into a build: the case tips up 35 deg and holds.
#include "harness_core.inc"
static float devPitch = 0.6f;   // case angle (rad): held at ~35 deg toward the face, like a handheld
static uint32_t n = 0;
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; BtnA.click=BtnA.hold=BtnB.click=BtnB.hold=BtnC.click=BtnC.hold=false; Touch.d = TouchDetail{};
  n++; float noise = ((n * 1103515245u + 12345u) >> 16 & 1023) / 1023.f - 0.5f;
  Imu.data.accel = {0.01f * noise, -sinf(devPitch) + 0.01f * noise, cosf(devPitch)};
  Imu.data.gyro = {0.f, 0.f, 1.2f + noise};   // a little bias on z, like a real MPU6886
}
static void frame() { uint32_t now = millis(); static uint32_t prev = now; dt = clampf((now - prev) / 1000.f, 0.008f, 0.05f); prev = now; tNow += dt; updateInput(); updateWorld(); hx::update(dt); }
int main(int argc, char **argv) {
  float tip = argc > 1 ? atof(argv[1]) : -0.3f;   // how far the case tips from the held angle
  setup(); bootOpen = false;
  for (int i = 0; i < 40; i++) frame();          // neutral at the held angle
  for (int s = 0; s < 8; s++) {
    devPitch = 0.6f + tip;
    V3 u0 = shipB.u, f0 = shipB.f;
    for (int i = 0; i < 250; i++) { frame();
      if (s >= 3 && s <= 4 && i % 25 == 0) printf("   t=%.2f tiltY=%+.2f ratePitch=%+.2f rateRoll=%+.2f u.y=%+.2f f.y=%+.2f neutAy=%+.2f dock=%d auto=%d orbit=%d theater=%d layer=%d target=%d stat=%d map=%d chat=%d\n", tNow, tiltY, ratePitch, rateRoll, shipB.u.y, shipB.f.y, neutralAy, dockTarget, autoNav, orbitObj, theater, layer, target, statusOpen, mapOpen, chatOpen); }
    printf("t=%2ds nose %+6.1f deg toward old up\n", (s + 1) * 2, asinf(clampf(dot(shipB.f, u0), -1, 1)) * 57.3f);
    (void)f0;
  }
}
