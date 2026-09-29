#include "haptics.h"
#include <M5Unified.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace hx {
namespace {

struct HumState { float target, value, pulseHz, grit, phase; bool touched; };
HumState s_hum[HUM_COUNT];

enum EvKind : uint8_t { EV_POP, EV_THUD, EV_SWELL, EV_STUTTER, EV_HEART, EV_BOOM };
struct Event { uint8_t kind; float t, a, b, c; int n; };
static const int MAX_EV = 10;
Event s_ev[MAX_EV];
int s_evN = 0;

float s_silence = 0.f;   // seconds of enforced quiet remaining
float s_out = 0.f;
float s_noise = 0.f, s_noiseT = 0.f;
uint32_t s_rng = 0x9E3779B9u;
int s_lastMotor = -1;

float frand() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return (s_rng & 0xFFFF) / 65535.f; }

void push(Event e) {
  if (s_silence > 0.f) return;
  if (s_evN < MAX_EV) { s_ev[s_evN++] = e; return; }
  // replace the weakest/oldest
  int w = 0; for (int i = 1; i < MAX_EV; i++) if (s_ev[i].t > s_ev[w].t) w = i;
  s_ev[w] = e;
}

float evalEvent(Event &e, bool &alive) {
  float t = e.t;
  switch (e.kind) {
    case EV_POP:   alive = t < e.b * 6.f; return e.a * expf(-t / e.b);
    case EV_THUD:  alive = t < 0.6f; return e.a * expf(-t / 0.11f) * (0.75f + 0.25f * s_noise);
    case EV_BOOM:  alive = t < 1.4f; return e.a * (t < 0.03f ? t / 0.03f : expf(-(t - 0.03f) / 0.32f)) * (0.7f + 0.3f * s_noise);
    case EV_SWELL: {
      alive = t < e.b + e.c * 5.f;
      if (t < e.b) { float u = t / e.b; return e.a * u * u; }
      return e.a * expf(-(t - e.b) / e.c);
    }
    case EV_STUTTER: {
      alive = t < e.n * e.b;
      float ph = fmodf(t, e.b) / e.b;
      return ph < 0.45f ? e.a * (0.6f + 0.4f * s_noise) : 0.f;
    }
    case EV_HEART: {
      alive = t < 0.55f;
      float b1 = expf(-((t - 0.02f) * (t - 0.02f)) / 0.0012f);
      float b2 = 0.7f * expf(-((t - 0.21f) * (t - 0.21f)) / 0.0018f);
      return e.a * (b1 + b2);
    }
  }
  alive = false; return 0.f;
}

void motor(float v) {
  // An ERM motor does not spin below a threshold, so map 0..1 above its floor.
  int m = v < 0.03f ? 0 : (int)(72 + v * 183);
  if (m > 255) m = 255;
  if (m == s_lastMotor || (m > 0 && s_lastMotor > 0 && abs(m - s_lastMotor) < 7)) return;
  s_lastMotor = m;
  M5.Power.setVibration((uint8_t)m);
}

}  // namespace

void begin() {
  memset(s_hum, 0, sizeof(s_hum));
  s_evN = 0; s_silence = 0; s_out = 0; s_lastMotor = -1;
  M5.Power.setVibration(0);
}

void hum(Hum ch, float level, float pulseHz, float grit) {
  if (ch >= HUM_COUNT) return;
  HumState &h = s_hum[ch];
  if (level > h.target || !h.touched) h.target = level;
  h.pulseHz = pulseHz; h.grit = grit; h.touched = true;
}

void pop(float s, float d)                { push({EV_POP, 0, s, d, 0, 0}); }
void thud(float s)                        { push({EV_THUD, 0, s, 0, 0, 0}); }
void boom(float s)                        { push({EV_BOOM, 0, s, 0, 0, 0}); }
void swell(float p, float r, float f)     { push({EV_SWELL, 0, p, r > 0.01f ? r : 0.01f, f > 0.01f ? f : 0.01f, 0}); }
void stutter(float s, int n, float per)   { push({EV_STUTTER, 0, s, per > 0.02f ? per : 0.02f, 0, n}); }
void heartbeat(float s)                   { push({EV_HEART, 0, s, 0, 0, 0}); }

void cut(float holdSec) {
  s_silence = holdSec;
  s_evN = 0;
  for (auto &h : s_hum) { h.value = 0; h.target = 0; }
  s_out = 0;
  s_lastMotor = -1;
  motor(0);
}

void update(float dt) {
  s_noiseT += dt;
  if (s_noiseT > 0.033f) { s_noiseT = 0; s_noise = frand() * 2.f - 1.f; }

  if (s_silence > 0.f) {
    s_silence -= dt;
    for (auto &h : s_hum) { h.touched = false; h.target = 0; h.value = 0; }
    motor(0);
    return;
  }

  // continuous layers, combined as a soft sum so layers stack without clipping
  float quiet = 1.f;
  for (auto &h : s_hum) {
    float tgt = h.touched ? h.target : 0.f;
    float k = tgt > h.value ? 1.f - expf(-dt / 0.05f) : 1.f - expf(-dt / 0.18f);
    h.value += (tgt - h.value) * k;
    h.phase += h.pulseHz * dt;
    if (h.phase > 1000.f) h.phase -= 1000.f;
    float mod = 1.f;
    if (h.pulseHz > 0.f) {
      float s = 0.5f + 0.5f * sinf(h.phase * 6.2831853f);
      mod = 0.3f + 0.7f * s * s;
    }
    float v = h.value * mod * (1.f + h.grit * 0.5f * s_noise);
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    quiet *= 1.f - v;
    h.touched = false; h.target = 0;
  }
  for (int i = 0; i < s_evN; ) {
    bool alive = true;
    float v = evalEvent(s_ev[i], alive);
    s_ev[i].t += dt;
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    quiet *= 1.f - v;
    if (!alive) s_ev[i] = s_ev[--s_evN]; else i++;
  }
  s_out = 1.f - quiet;
  motor(s_out);
}

float level() { return s_out; }

}  // namespace hx
