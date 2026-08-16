#pragma once

// Host-side stand-in for the Arduino core, scoped to what Arduino_GFX/
// Arduino_Canvas and this project's extracted rendering code actually
// need to compile and run on a native build -- not a general Arduino
// compatibility shim. See tools/sim/README.md for what this harness is
// and isn't for.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include "pgmspace.h"
#include "WString.h"
#include "Print.h"
#include "Printable.h"

using std::max;
using std::min;

// A macro, not a template, matching the real Arduino core exactly: callers
// routinely mix types (e.g. constrain(someUint8_t, 1, 4), an int literal
// range), which a same-T template rejects but a macro's plain ternary
// accepts via ordinary implicit conversion.
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// Real Arduino millis()/delay() are wall-clock and blocking; the render
// harness only ever renders single static frames (see render_harness.cpp),
// so nothing here actually needs to elapse time. A monotonically
// increasing fake is enough to satisfy call sites without hanging.
inline uint32_t millis() {
  static uint32_t fakeNow = 0;
  return fakeNow += 16;
}
inline void delay(uint32_t) {}

class HardwareSerialStub : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *buffer, size_t size) override { return size; }
};
inline HardwareSerialStub Serial;

#ifndef RGB565_BLACK
// Arduino_GFX_Library.h itself defines the RGB565_* palette and GFX_RGB565
// helper; nothing Arduino-core-specific about them, so no fake needed here.
#endif
