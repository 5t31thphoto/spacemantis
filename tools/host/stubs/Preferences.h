#pragma once
#include <stddef.h>
#include <stdint.h>
class Preferences {
 public:
  bool begin(const char *ns, bool ro);
  void end() {}
  size_t putBytes(const char *k, const void *v, size_t n);
  size_t getBytesLength(const char *k);
  size_t getBytes(const char *k, void *v, size_t n);
 private:
  const char *ns_ = "";
};
