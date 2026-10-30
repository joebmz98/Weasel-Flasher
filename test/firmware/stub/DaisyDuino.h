#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "PersistentStorage.h"
struct FakeUsbSerial {
  std::vector<uint8_t> in, out; size_t pos = 0;
  void begin(unsigned long) {}
  int available() { return (int)(in.size() - pos); }
  int read() { return pos < in.size() ? in[pos++] : -1; }
  size_t write(const uint8_t *b, size_t n) { out.insert(out.end(), b, b + n); return n; }
  void flush() {}
};
extern FakeUsbSerial SerialUSB;
inline void delay(unsigned long) {}
