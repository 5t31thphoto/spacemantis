#pragma once
#include "Arduino.h"
#include <stdarg.h>
void hostLog(const char *fmt, ...);
struct TouchDetail {
  int x = 0, y = 0; bool pressed = false, press = false, released = false;
  bool wasPressed() const { return pressed; }
  bool isPressed() const { return press; }
  bool wasReleased() const { return released; }
};
struct HostBtn { bool p = false, click = false, hold = false, down = false; bool wasPressed() const { return p; } bool isPressed() const { return down; } bool wasClicked() const { return click; } bool wasHold() const { return hold; } void setHoldThresh(int) {} };
struct Vec3 { float x, y, z; };
struct ImuData { Vec3 accel; Vec3 gyro; };
struct HostDisplay { void setRotation(int) {} void setBrightness(int) {} };
class M5Canvas {
 public:
  M5Canvas(HostDisplay *) {}
  void setColorDepth(int) {}
  void createSprite(int, int) {}
  void setPsram(bool) {}
  void fillSprite(uint16_t c) { hostLog("clear %u\n", c); }
  void pushSprite(int, int) { hostLog("push\n"); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { hostLog("fr %d %d %d %d %u\n", x, y, w, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { hostLog("dr %d %d %d %d %u\n", x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) { fillRect(x, y, w, h, c); }
  void drawRoundRect(int x, int y, int w, int h, int, uint16_t c) { drawRect(x, y, w, h, c); }
  void fillCircle(int x, int y, int r, uint16_t c) { hostLog("fc %d %d %d %u\n", x, y, r, c); }
  void fillEllipse(int x, int y, int rx, int ry, uint16_t c) { hostLog("fe %d %d %d %d %u\n", x, y, rx, ry, c); }
  void drawEllipse(int x, int y, int rx, int ry, uint16_t c) { hostLog("de %d %d %d %d %u\n", x, y, rx, ry, c); }
  void drawCircle(int x, int y, int r, uint16_t c) { hostLog("dc %d %d %d %u\n", x, y, r, c); }
  void drawLine(int a, int b, int c2, int d, uint16_t c) { hostLog("dl %d %d %d %d %u\n", a, b, c2, d, c); }
  void drawPixel(int x, int y, uint16_t c) { hostLog("dp %d %d %u\n", x, y, c); }
  void fillTriangle(int a, int b, int c2, int d, int e, int f, uint16_t c) { hostLog("ft %d %d %d %d %d %d %u\n", a, b, c2, d, e, f, c); }
  void setTextColor(uint16_t c) { col_ = c; }
  void setTextSize(int s) { size_ = s; }
  void setCursor(int x, int y) { cx_ = x; cy_ = y; }
  void print(const char *s) { hostLog("tx %d %d %d %u %s\n", cx_, cy_, size_, col_, s); cx_ += (int)strlen(s) * 6 * size_; }
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); print(b);
  }
 private:
  int cx_ = 0, cy_ = 0, size_ = 1; uint16_t col_ = 0xFFFF;
};
struct HostTouch { TouchDetail d; TouchDetail getDetail() { return d; } };
struct HostImu { ImuData data{{0, 0, 1}, {0, 0, 0}}; bool update() { return true; } ImuData getImuData() { return data; } };
struct HostPower { int last = 0; long changes = 0; void setVibration(int v) { if (v != last) changes++; last = v; } };
struct HostCfg { bool output_power, internal_imu; };
struct M5Class {
  HostDisplay Display; HostTouch Touch; HostBtn BtnA, BtnB, BtnC; HostImu Imu; HostPower Power;
  HostCfg config() { return {}; }
  void begin(HostCfg) {}
  void update();
};
extern M5Class M5;
