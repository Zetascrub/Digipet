#pragma once

// Minimal host-side stand-in for the Arduino core, scoped to exactly what
// the pure-logic modules under test (currently pet_genome.cpp/.h) need.
// This is NOT a general Arduino compatibility shim -- it exists only so
// those files can be compiled and unit-tested on the native platform
// without pulling in the real ESP32 Arduino framework. Extend it only as
// far as the next tested module actually requires.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

using std::max;
using std::min;

template <typename T>
inline T constrain(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}
