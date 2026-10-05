#pragma once
// Host stand-in for the ESP32 SD library: files live under ./sdcard/
#include <stdio.h>
#include <string>
#include <stdint.h>
#include <stddef.h>
#include "SPI.h"
#define FILE_READ "rb"
#define FILE_WRITE "wb"
#define GPIO_NUM_4 4
class File {
 public:
  File(FILE *f = nullptr) : f_(f) {}
  explicit operator bool() const { return f_ != nullptr; }
  size_t write(const uint8_t *b, size_t n) { return f_ ? fwrite(b, 1, n, f_) : 0; }
  size_t read(uint8_t *b, size_t n) { return f_ ? fread(b, 1, n, f_) : 0; }
  void close() { if (f_) fclose(f_); f_ = nullptr; }
 private:
  FILE *f_;
};
class SDClass {
 public:
  bool present = true;
  bool begin(int, SPIClass &, int);
  bool exists(const char *p);
  bool mkdir(const char *p);
  File open(const char *p, const char *mode);
  bool remove(const char *p);
  bool rename(const char *a, const char *b);
  std::string root = "sdcard";
};
extern SDClass SD;
