#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
struct SerialStub {
  int read() { return -1; }
  size_t write(const uint8_t* b, size_t n) { size_t o = 0; while (o < n) { ssize_t w = ::write(1, b + o, n - o); if (w <= 0) break; o += w; } return n; }
  void flush() {}
};
extern SerialStub Serial;
