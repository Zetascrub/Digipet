#pragma once

#include <cstdint>

// Pure RGB565 color math with no Arduino/GFX/theme dependency of its own,
// so it can be unit-tested on the host (see test/test_color_utils) the
// same way pet_genome.cpp/familiar_battle_rules.cpp are, and reused by
// both the firmware (via ui_pages.cpp's readableTextColor() wrapper) and
// anything else that ends up needing contrast-aware color choices.

// Rec. 601 luma (integer approximation, not gamma-correct WCAG luminance --
// this is for picking a readable label color, not certifying accessibility
// compliance) of an RGB565 color, 0-255.
uint16_t rgb565Luma(uint16_t color);

// Returns whichever of `lightOption`/`darkOption` has greater luma
// *distance* from `fillColor` -- the practical proxy for "reads better on
// this background" that doesn't need gamma-correct contrast-ratio math.
// Callers pass their own light/dark choices (there's no assumption they're
// literally white/black) so this stays reusable outside this app's own
// COLOR_TEXT/COLOR_BACKGROUND theme pair.
uint16_t pickReadableColor(uint16_t fillColor, uint16_t lightOption, uint16_t darkOption);
