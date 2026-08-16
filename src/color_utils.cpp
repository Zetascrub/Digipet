#include "color_utils.h"

#include <cstdlib>

uint16_t rgb565Luma(uint16_t color) {
  const uint16_t r = (color >> 11) & 0x1F;
  const uint16_t g = (color >> 5) & 0x3F;
  const uint16_t b = color & 0x1F;
  // Scale each channel to 0-255 before weighting -- R/B are 5-bit, G is
  // 6-bit, so without this a saturated green would be overweighted purely
  // from having more source bits, not because it's actually brighter.
  const uint16_t r8 = r * 255 / 31;
  const uint16_t g8 = g * 255 / 63;
  const uint16_t b8 = b * 255 / 31;
  return static_cast<uint16_t>((r8 * 299 + g8 * 587 + b8 * 114) / 1000);
}

uint16_t pickReadableColor(uint16_t fillColor, uint16_t lightOption, uint16_t darkOption) {
  const int fillLuma = rgb565Luma(fillColor);
  const int lightDiff = abs(rgb565Luma(lightOption) - fillLuma);
  const int darkDiff = abs(rgb565Luma(darkOption) - fillLuma);
  return darkDiff >= lightDiff ? darkOption : lightOption;
}
