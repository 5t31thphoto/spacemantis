#include "harness_core.inc"
void M5Class::update() { g_ms += 8; BtnA.p=BtnB.p=BtnC.p=false; Touch.d = TouchDetail{}; Imu.data.accel={0,0,1}; }
int main() {
  setup(); bootOpen = false;
  int same = 0, rolls = 0;
  for (int k = 0; k < 300; k++) {
    // make the current place a known, rumored name so the hand is likely to offer it
    makeRealScene(sm::placeName(sm::urand(), 0, false), true);
    sm::knownGateAdd(hereName, 1); sm::rumorAdd(hereName, 1, 10);
    for (int r = 0; r < 20; r++) { sm::contractOffer(); rolls++; if (sm::contractOfferPeek().dest[0] && sm::sameName(sm::contractOfferPeek().dest, hereName)) same++; }
  }
  printf("offers pointing at the current place: %d / %d\n", same, rolls);
}
