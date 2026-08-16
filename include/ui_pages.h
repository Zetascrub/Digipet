#pragma once

#include <vector>

#include <Arduino.h>
#include <Arduino_GFX.h>

#include "color_utils.h"
#include "familiar_battle_service.h"
#include "pet_genome.h"
#include "pin_config.h"

// The Companion/Status page renderers and their shared drawing helpers,
// factored out of main.cpp so they can be linked into something other than
// the firmware -- specifically tools/sim's native render harness, which
// renders the real page-drawing code to PNG for visual review without
// flashing a physical board. See tools/sim/README.md.
//
// This split follows the same "move code verbatim, declare what it reads
// as extern" pattern already used for familiar_battle_rules.h: main.cpp
// still owns every one of these globals (constructs them, mutates them,
// persists them) -- this header only declares their shape and existence so
// a second translation unit (either main.cpp's own, now living in
// src/ui_pages.cpp, or the sim harness with its own fake definitions) can
// link against them.
//
// PetState and Page moved here rather than staying main.cpp-local because
// the extern declarations below need PetState complete and Page defined.

struct PetState {
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t actions;
  uint8_t food;
  uint8_t joy;
  uint8_t energy;
  uint8_t health;
  uint8_t stage;
  uint8_t training;
  uint8_t reserved[2];
  PetGenome genome;
};

enum Page : uint8_t {
  PAGE_COMPANION,
  PAGE_STATUS,
  PAGE_BATTLE,
  PAGE_SETTINGS,
  PAGE_GENOME_LAB,
};

enum StatIcon : uint8_t { ICON_FOOD, ICON_JOY, ICON_ENERGY, ICON_HEALTH };

// --- State these renderers read ---------------------------------------------
// Definitions live in src/main.cpp for the real firmware; tools/sim's
// harness provides its own (see tools/sim/render_harness.cpp).

extern Arduino_GFX *display;
extern PetState pet;

extern uint16_t COLOR_BACKGROUND;
extern uint16_t COLOR_CARD;
extern uint16_t COLOR_MINT;
extern uint16_t COLOR_TEXT;
extern uint16_t COLOR_MUTED;
extern uint16_t COLOR_WARNING;
extern uint16_t COLOR_DANGER;
extern uint16_t COLOR_CYAN;
extern uint16_t COLOR_PURPLE;

extern const char *STAGE_NAMES[];

extern bool clockValid;
extern char clockText[6];

extern bool imuDetected;
extern bool rtcDetected;
extern bool pmuDetected;
extern bool codecDetected;
extern uint8_t i2cDeviceCount;

extern bool animationFrame;

// A per-device record, not a per-pet one -- persists across egg hatches/
// blends. Definition (magic-guarded, persisted through Preferences/NVS)
// stays in main.cpp; the type lives here because drawRivalsPage() reads it.
constexpr uint8_t kMaxBattleRivals = 8;

struct BattleRival {
  uint32_t playerId = 0;
  uint16_t wins = 0;
  uint16_t losses = 0;
};

struct BattleStats {
  uint32_t magic = 0;
  uint32_t wins = 0;
  uint32_t losses = 0;
  uint32_t fled = 0;
  uint32_t opponentFled = 0;
  uint32_t disconnected = 0;
  uint8_t rivalCount = 0;
  uint8_t reserved[3]{};
  BattleRival rivals[kMaxBattleRivals]{};
};
extern BattleStats battleStats;

struct DeviceSettings {
  uint32_t magic;
  uint8_t brightnessIndex;
  uint8_t sleepIndex;
  bool soundEnabled;
  bool bootAnimationEnabled;
  uint8_t volumeIndex;
  uint8_t wakeMode;
  uint8_t themeIndex;
};
extern DeviceSettings settings;

extern const char *SLEEP_LABELS[];
extern const char *VOLUME_LABELS[];
extern const char *WAKE_LABELS[];
extern const char *THEME_LABELS[];
extern uint8_t settingsGridPage;
uint8_t brightnessPercent();

// Canonical color tables for the app's 5 themes, index-aligned with
// THEME_LABELS/settings.themeIndex. Index 0 (AUTO) has no fixed palette --
// it's derived per-pet from paletteForGenome() instead (see applyTheme()
// and drawSettingsControlPage()'s own handling of it) -- so its entry here
// is a placeholder, not a color to draw with.
//
// This is the one place this data lives, specifically so it can't drift
// out of sync with itself: applyTheme() (src/main.cpp) reads it to actually
// switch themes, the Settings > Theme picker's preview swatches
// (drawSettingsControlPage(), ui_pages.cpp) read it to show what a theme
// looks like before it's selected, and tools/sim's render harness reads it
// for its --theme= flag. A `constexpr` array in a header gives every
// translation unit that includes this file its own copy, but they're all
// generated from this same literal, so there's no risk of the copies
// disagreeing the way three hand-maintained ones could.
struct ThemeColors {
  uint16_t background, card, primary, text, muted, warning, danger, cyan, secondary;
};
constexpr ThemeColors kThemes[] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0},  // AUTO -- see comment above.
    {0x0823, 0x18E8, 0x6718, 0xE73C, 0x8413, 0xFE48, 0xF2CB, 0x269F, 0xA81F},  // Cyber Mint
    {0x1000, 0x28C2, 0xFD20, 0xFF9C, 0x9B48, 0xFFE0, 0xF260, 0xFBA0, 0xB940},  // Amber Core
    {0x080F, 0x2019, 0xC35F, 0xF73F, 0x8C18, 0xFD86, 0xF1CB, 0x6DFF, 0x91FF},  // Violet Link
    {0x0000, 0x18C3, 0xC618, 0xFFFF, 0x7BEF, 0xDEFB, 0xD69A, 0xBDF7, 0x8410},  // Mono Signal
};
constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

enum SettingsView : uint8_t {
  SETTINGS_HOME,
  SETTINGS_BRIGHTNESS,
  SETTINGS_IDLE,
  SETTINGS_VOLUME,
  SETTINGS_WAKE,
  SETTINGS_THEME,
  SETTINGS_BOOT,
};
extern SettingsView settingsView;

extern bool hasCopiedGenome;
extern bool newEggConfirmation;
extern uint32_t newEggConfirmationUntil;
extern uint8_t pendingHatchMode;

// --- Color/easing helpers ------------------------------------------------------

uint16_t scaleRgb565(uint16_t color, uint8_t percent);
uint16_t lerpRgb565(uint16_t from, uint16_t to, float t);
// This app's own wrapper around color_utils.h's pickReadableColor(), fixed
// to this app's light/dark pair. See docs/style-guide.md's "Text and icons
// on a colored fill" rule: any label/icon drawn on a fill color that isn't
// COLOR_CARD/COLOR_BACKGROUND itself (a button, a selected-state highlight,
// a badge) must get its color from this, never a hardcoded COLOR_TEXT --
// COLOR_TEXT is tuned to read on this app's dark page chrome, and goes
// close to unreadable against several themes' brighter accent fills.
uint16_t readableTextColor(uint16_t fillColor);
// Smoothstep, remapped from an arbitrary [from, to] input range instead of a
// fixed [0, 1] one -- shared by the boot sequence, the toast slide, and the
// evolution animation, all in main.cpp, plus this header's own extracted
// callers, hence living here rather than next to any one of them.
float bootSmoothstep(float from, float to, float value);

// Same reasoning as bootSmoothstep: the boot sequence's on-screen "core"
// position, reused by playEvolutionAnimation() (main.cpp) for the evolved
// creature's portrait.
constexpr int16_t BOOT_CX = LCD_WIDTH / 2;
constexpr int16_t BOOT_TOP_Y = 122;
constexpr int16_t BOOT_BOTTOM_Y = 316;
constexpr int16_t BOOT_CORE_Y = (BOOT_TOP_Y + BOOT_BOTTOM_Y) / 2;
constexpr uint8_t BOOT_NODE_COUNT = 16;

// --- Generic drawing helpers ---------------------------------------------------

void drawCentered(const char *text, int16_t y, uint8_t size, uint16_t color);
void drawCenteredInRect(const char *text, int16_t x, int16_t y, int16_t width,
                        int16_t height, uint8_t size, uint16_t color);
uint16_t backdropColorAt(int16_t y);
void paintPageBackdrop();
void drawPanelGlow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                   uint16_t color);
void drawGlyph16(const uint16_t *rows, int16_t x, int16_t y, uint16_t color);
void drawStatIcon(StatIcon icon, int16_t x, int16_t y, uint16_t color);
void drawStatRow(StatIcon icon, uint8_t value, int16_t y);
void drawButton(const char *label, int16_t x);
void drawPageDots(Page active);

// --- Companion creature rendering -----------------------------------------------

void drawEgg(bool frame, uint16_t bg);
int geneScale(uint8_t gene, int minimum, int maximum);
void drawElementAura(const PetPalette &palette, int cx, int cy, int radius,
                     uint8_t phase);
void drawGenomeMarkings(const PetPalette &palette, int cx, int cy, int width,
                        int height, uint8_t stage, uint8_t markingGene);
void drawGenomeFace(const PetPalette &palette, int cx, int cy, int headWidth,
                    bool blink, uint8_t stage, uint8_t faceGene);
void drawCreaturePortrait(const PetGenome &genome, uint8_t stage, int cx, int cy,
                          int headWidth, uint16_t ringColor);
void drawProceduralCreature(bool asleep);
void drawCreature(bool frame, bool asleep);

// --- Battle --------------------------------------------------------------------
// drawBattlingLayout/drawBattleResultsPage/drawOpponentRow/drawBattleButton
// take every value as a parameter rather than reading the live
// FamiliarBattleService `battle` object directly (see each one's own
// comment in ui_pages.cpp) -- the same design that already let main.cpp's
// DUMPBATTLE/DUMPSCAN debug serial commands preview them with synthetic
// data, since a live BLE match needs two physical devices to test at all.
// That means they need no live-battle stand-in to render here either.
// drawBattlePage() itself, which dispatches on battle.state(), stays in
// main.cpp for exactly that reason.

constexpr uint8_t kBattleResultsPerPage = 3;

enum BattleMoveIcon : uint8_t { ICON_ATTACK, ICON_DEFEND, ICON_SPECIAL, ICON_FLEE };

void drawBattleMoveIcon(BattleMoveIcon icon, int16_t x, int16_t y, uint16_t color);
void drawOpponentRow(const FamiliarBattleOpponent &opponent, int16_t y);
void drawBattleResultsPage(const std::vector<FamiliarBattleOpponent> &results,
                           uint8_t page);
void drawBattleButton(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                      uint16_t color, BattleMoveIcon icon, const char *label);
void drawBattlingLayout(uint16_t myHp, uint16_t myMaxHp, uint16_t opponentHp,
                        uint16_t opponentMaxHp, uint8_t opponentLevel,
                        bool enhancedLink, const char *logLine1, const char *logLine2,
                        bool isResult, bool moveSubmitted, bool fleeArmed,
                        bool opponentGenomeAvailable, bool genomeCopied,
                        const PetGenome *opponentGenome);

// A full-screen overlay (tapped open from the Battle Idle screen's "RECORD"
// line, dismissed by tapping anywhere -- see main.cpp), not one of the 5
// swipeable pages, same as the Player ID/OTA update/evolution debug
// overlays already are.
void drawRivalsPage();

// --- Settings ------------------------------------------------------------------

void drawSettingsIcon(uint8_t item, int16_t cx, int16_t cy, uint16_t color);
void drawSettingsTile(uint8_t item, int16_t x, int16_t y, const char *label,
                      const char *value);
void drawSettingsBack();
void drawChoiceRow(const char *label, int16_t y, bool selected);
void drawThemeSwatch(int16_t cx, int16_t cy, uint8_t themeIndex);
void drawSettingsControlPage();

// --- Pages --------------------------------------------------------------------

void drawCompanionPage();
void drawStatusPage();
void drawSettingsPage();
void drawGenomeLabPage();
