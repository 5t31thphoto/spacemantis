#pragma once
// ============================================================
//  SpaceMantis — tiny 3D math for the flight engine
// ============================================================
#include <math.h>

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { return V3{x, y, z}; }
static inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
static inline V3 &operator+=(V3 &a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : V3{0, 0, 1}; }
static inline V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }

// Rodrigues rotation of v about unit axis k
static inline V3 rotAxis(V3 v, V3 k, float a) {
  float c = cosf(a), s = sinf(a);
  return v * c + cross(k, v) * s + k * (dot(k, v) * (1.f - c));
}

// Orientation as an orthonormal basis: right, up, forward (world space).
struct Basis {
  V3 r{1, 0, 0}, u{0, 1, 0}, f{0, 0, 1};
  void yaw(float a)   { r = rotAxis(r, u, a); f = rotAxis(f, u, a); }
  void pitch(float a) { u = rotAxis(u, r, a); f = rotAxis(f, r, a); }
  void roll(float a)  { r = rotAxis(r, f, a); u = rotAxis(u, f, a); }
  void fix() { f = norm(f); r = norm(cross(u, f)); u = cross(f, r); }
  V3 toWorld(V3 m) const { return r * m.x + u * m.y + f * m.z; }
  V3 toLocal(V3 w) const { return {dot(w, r), dot(w, u), dot(w, f)}; }
  static Basis facing(V3 fwd, V3 upHint) {
    Basis b; b.f = norm(fwd);
    b.r = cross(upHint, b.f);
    if (len(b.r) < 1e-4f) b.r = cross(V3{1, 0, 0}, b.f);
    b.r = norm(b.r); b.u = cross(b.f, b.r);
    return b;
  }
};
