// Unit tests for color_utils.cpp's readable-label-color logic -- added
// after a real bug report: the Battle page's HOST/FIND buttons (and, it
// turned out, several other buttons across the app) always drew their
// label in COLOR_TEXT regardless of the button's fill color, which reads
// fine on this app's dark page chrome but goes nearly unreadable against
// several themes' brighter accent fills. See docs/style-guide.md.
//
// The "theme" tests below hardcode each theme's actual color table from
// include/ui_pages.h's kThemes[] -- not because color_utils.cpp reads
// theme state (it doesn't; it's pure color math), but so a future edit to
// a theme's palette or to pickReadableColor()'s heuristic gets caught here
// against the concrete colors this app actually ships, not just synthetic
// extremes. Run with `pio test -e native`. If kThemes[] changes, these
// literals need updating to match -- there's no way for this file (which
// can't include ui_pages.h; that would pull in Arduino/GFX) to read them
// automatically.
//
// Every expected pick below was independently recomputed from
// rgb565Luma()'s own formula (see the Python cross-check this test file's
// author ran alongside it) rather than eyeballed -- pickReadableColor
// picks whichever of text/bg is the *farther* luma distance from the
// fill, which is not always the more intuitively "readable" one at a
// glance (e.g. a mid-luma purple fill ends up farther from a bright text
// color than from an near-black background, so it picks text).

#include <unity.h>

#include "color_utils.h"

void setUp(void) {}
void tearDown(void) {}

// --- rgb565Luma ----------------------------------------------------------------

void test_luma_of_black_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(0, rgb565Luma(0x0000));
}

void test_luma_of_white_is_max(void) {
  TEST_ASSERT_EQUAL_UINT16(255, rgb565Luma(0xFFFF));
}

// Pure green should read brighter than pure red or pure blue at equal
// channel saturation -- the whole reason rgb565Luma weights channels
// instead of just averaging them. TEST_ASSERT_GREATER_THAN_UINT16's
// arguments are (threshold, actual): asserts actual > threshold.
void test_luma_green_brighter_than_red(void) {
  TEST_ASSERT_GREATER_THAN_UINT16(rgb565Luma(0xF800), rgb565Luma(0x07E0));
}

void test_luma_green_brighter_than_blue(void) {
  TEST_ASSERT_GREATER_THAN_UINT16(rgb565Luma(0x001F), rgb565Luma(0x07E0));
}

// --- pickReadableColor: synthetic extremes ------------------------------------

void test_picks_dark_option_for_a_bright_fill(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0xFFFF, 0xFFFF, 0x0000));
}

void test_picks_light_option_for_a_dark_fill(void) {
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, pickReadableColor(0x0000, 0xFFFF, 0x0000));
}

// A perfect middle gray (0x8410: r=16/31, g=32/63, b=16/31, ~50% each) is
// exactly as far from white as from black -- pickReadableColor's >= means
// dark wins the tie, matching this app's own empirical finding that dark
// labels read better far more often than not (see the theme sweep below),
// so the tie-break isn't arbitrary.
void test_ties_favor_the_dark_option(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0x8410, 0xFFFF, 0x0000));
}

// --- pickReadableColor: this app's actual theme colors --------------------------
// One test per (theme, accent fill) pair actually used by a button
// (COLOR_CYAN/COLOR_PURPLE/COLOR_MINT/COLOR_DANGER/COLOR_WARNING/
// COLOR_MUTED). Expected picks computed from each theme's real text/
// background colors (include/ui_pages.h's kThemes[] table) via the same
// luma-distance rule pickReadableColor implements.

void test_cyber_mint_cyan_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0823, pickReadableColor(0x269F, 0xE73C, 0x0823));
}
void test_cyber_mint_purple_picks_text(void) {
  TEST_ASSERT_EQUAL_UINT16(0xE73C, pickReadableColor(0xA81F, 0xE73C, 0x0823));
}
void test_cyber_mint_mint_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0823, pickReadableColor(0x6718, 0xE73C, 0x0823));
}
void test_cyber_mint_danger_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0823, pickReadableColor(0xF2CB, 0xE73C, 0x0823));
}
void test_cyber_mint_warning_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0823, pickReadableColor(0xFE48, 0xE73C, 0x0823));
}
void test_cyber_mint_muted_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0823, pickReadableColor(0x8433, 0xE73C, 0x0823));
}

void test_amber_core_cyan_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x1000, pickReadableColor(0xFBA0, 0xFF9C, 0x1000));
}
void test_amber_core_purple_picks_text(void) {
  TEST_ASSERT_EQUAL_UINT16(0xFF9C, pickReadableColor(0xB940, 0xFF9C, 0x1000));
}
void test_amber_core_mint_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x1000, pickReadableColor(0xFD20, 0xFF9C, 0x1000));
}
void test_amber_core_danger_picks_text(void) {
  TEST_ASSERT_EQUAL_UINT16(0xFF9C, pickReadableColor(0xF260, 0xFF9C, 0x1000));
}
void test_amber_core_warning_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x1000, pickReadableColor(0xFFE0, 0xFF9C, 0x1000));
}
void test_amber_core_muted_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x1000, pickReadableColor(0xABCB, 0xFF9C, 0x1000));
}

void test_violet_link_cyan_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x080F, pickReadableColor(0x6DFF, 0xF73F, 0x080F));
}
void test_violet_link_purple_picks_text(void) {
  TEST_ASSERT_EQUAL_UINT16(0xF73F, pickReadableColor(0x91FF, 0xF73F, 0x080F));
}
void test_violet_link_mint_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x080F, pickReadableColor(0xC35F, 0xF73F, 0x080F));
}
void test_violet_link_danger_picks_text(void) {
  TEST_ASSERT_EQUAL_UINT16(0xF73F, pickReadableColor(0xF1CB, 0xF73F, 0x080F));
}
void test_violet_link_warning_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x080F, pickReadableColor(0xFD86, 0xF73F, 0x080F));
}
void test_violet_link_muted_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x080F, pickReadableColor(0xAD1A, 0xF73F, 0x080F));
}

void test_mono_signal_cyan_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0xBDF7, 0xFFFF, 0x0000));
}
void test_mono_signal_purple_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0x8410, 0xFFFF, 0x0000));
}
void test_mono_signal_mint_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0xC618, 0xFFFF, 0x0000));
}
void test_mono_signal_danger_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0xD69A, 0xFFFF, 0x0000));
}
void test_mono_signal_warning_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0xDEFB, 0xFFFF, 0x0000));
}
void test_mono_signal_muted_picks_background(void) {
  TEST_ASSERT_EQUAL_UINT16(0x0000, pickReadableColor(0x8410, 0xFFFF, 0x0000));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_luma_of_black_is_zero);
  RUN_TEST(test_luma_of_white_is_max);
  RUN_TEST(test_luma_green_brighter_than_red);
  RUN_TEST(test_luma_green_brighter_than_blue);
  RUN_TEST(test_picks_dark_option_for_a_bright_fill);
  RUN_TEST(test_picks_light_option_for_a_dark_fill);
  RUN_TEST(test_ties_favor_the_dark_option);
  RUN_TEST(test_cyber_mint_cyan_picks_background);
  RUN_TEST(test_cyber_mint_purple_picks_text);
  RUN_TEST(test_cyber_mint_mint_picks_background);
  RUN_TEST(test_cyber_mint_danger_picks_background);
  RUN_TEST(test_cyber_mint_warning_picks_background);
  RUN_TEST(test_cyber_mint_muted_picks_background);
  RUN_TEST(test_amber_core_cyan_picks_background);
  RUN_TEST(test_amber_core_purple_picks_text);
  RUN_TEST(test_amber_core_mint_picks_background);
  RUN_TEST(test_amber_core_danger_picks_text);
  RUN_TEST(test_amber_core_warning_picks_background);
  RUN_TEST(test_amber_core_muted_picks_background);
  RUN_TEST(test_violet_link_cyan_picks_background);
  RUN_TEST(test_violet_link_purple_picks_text);
  RUN_TEST(test_violet_link_mint_picks_background);
  RUN_TEST(test_violet_link_danger_picks_text);
  RUN_TEST(test_violet_link_warning_picks_background);
  RUN_TEST(test_violet_link_muted_picks_background);
  RUN_TEST(test_mono_signal_cyan_picks_background);
  RUN_TEST(test_mono_signal_purple_picks_background);
  RUN_TEST(test_mono_signal_mint_picks_background);
  RUN_TEST(test_mono_signal_danger_picks_background);
  RUN_TEST(test_mono_signal_warning_picks_background);
  RUN_TEST(test_mono_signal_muted_picks_background);
  return UNITY_END();
}
