#pragma once
// ============================================================
//  SpaceMantis — haptic "sound design"
//  The Core2 has an ERM vibration motor and no speaker worth using, so the
//  motor is the soundtrack: layered hums, shaped envelopes, pops, and silence.
// ============================================================
#include <stdint.h>

namespace hx {

enum Hum : uint8_t {
  HUM_BODY = 0,   // tidal throb near massive bodies
  HUM_PORTAL,     // spacetime tearing: builds as a portal approaches
  HUM_DEEP,       // the ambient pressure of subspace
  HUM_BEAM,       // mining / scoop / tractor texture
  HUM_ENGINE,     // faint engine purr, felt on boost and autopilot
  HUM_COUNT
};

void begin();
// Continuous layers: call every frame you want the layer alive. Unset layers release.
void hum(Hum ch, float level, float pulseHz = 0.f, float grit = 0.f);
// One-shot gestures.
void pop(float strength, float decaySec = 0.035f);            // weapon fire, clicks
void thud(float strength);                                     // incoming hit, collision
void swell(float peak, float riseSec, float fallSec);          // thread, dock, arrival
void stutter(float strength, int beats, float periodSec);      // glitch
void heartbeat(float strength);                                // deep-layer pulse
void boom(float strength);                                     // explosion
void cut(float holdSec);                                       // instant, total silence
void update(float dt);                                         // drives the motor
float level();                                                 // current output 0..1 (for visuals)

}  // namespace hx
