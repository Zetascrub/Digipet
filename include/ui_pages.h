#pragma once

#include <Arduino.h>
#include <Arduino_GFX.h>

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

// --- Color/easing helpers ------------------------------------------------------

uint16_t scaleRgb565(uint16_t color, uint8_t percent);
uint16_t lerpRgb565(uint16_t from, uint16_t to, float t);
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

// --- Pages --------------------------------------------------------------------

void drawCompanionPage();
void drawStatusPage();
