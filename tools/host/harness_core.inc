// Shared host runtime: clock, NVS, draw-call log, then the real firmware main.
#include <map>
#include <string>
#include <vector>
#include <cstdlib>
#include <cmath>
#include "M5Unified.h"
#include "Preferences.h"
#include "SD.h"
#include <sys/stat.h>
SPIClass SPI;
SDClass SD;
bool SDClass::begin(int, SPIClass &, int) { if (!present) return false; ::mkdir(root.c_str(), 0755); return true; }
bool SDClass::exists(const char *p) { struct stat st; return stat((root + p).c_str(), &st) == 0; }
bool SDClass::mkdir(const char *p) { return ::mkdir((root + p).c_str(), 0755) == 0; }
File SDClass::open(const char *p, const char *mode) { return File(fopen((root + p).c_str(), mode)); }
bool SDClass::remove(const char *p) { return ::remove((root + p).c_str()) == 0; }
bool SDClass::rename(const char *a, const char *b) { return ::rename((root + a).c_str(), (root + b).c_str()) == 0; }

static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
void delay(uint32_t ms) { g_ms += ms; }
EspClass ESP;
M5Class M5;

static std::map<std::string, std::vector<uint8_t>> g_nvs;
bool Preferences::begin(const char *ns, bool) { ns_ = ns; return true; }
size_t Preferences::putBytes(const char *k, const void *v, size_t n) {
  auto &b = g_nvs[std::string(ns_) + "/" + k]; b.assign((const uint8_t *)v, (const uint8_t *)v + n); return n; }
size_t Preferences::getBytesLength(const char *k) {
  auto it = g_nvs.find(std::string(ns_) + "/" + k); return it == g_nvs.end() ? 0 : it->second.size(); }
size_t Preferences::getBytes(const char *k, void *v, size_t n) {
  auto it = g_nvs.find(std::string(ns_) + "/" + k); if (it == g_nvs.end()) return 0;
  size_t m = n < it->second.size() ? n : it->second.size(); memcpy(v, it->second.data(), m); return m; }

static FILE *g_log = nullptr;
static int g_textIssues = 0, g_offscreenText = 0;
void hostLog(const char *fmt, ...) {
  va_list ap;
  if (strncmp(fmt, "tx", 2) == 0) {
    va_start(ap, fmt);
    int x = va_arg(ap, int); int y = va_arg(ap, int); int sz = va_arg(ap, int); (void)va_arg(ap, unsigned);
    const char *s = va_arg(ap, const char *);
    va_end(ap);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
      if (*p >= 0x80) { if (g_textIssues++ < 5) fprintf(stderr, "NONASCII: %s\n", s); break; }
    (void)x; (void)y; (void)sz;
  }
  if (!g_log) return;
  va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
}

#include "../../src/main.cpp"
