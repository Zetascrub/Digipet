// Native render harness: links the real ui_pages.cpp (extracted verbatim
// from main.cpp -- see include/ui_pages.h's own comment) against
// Arduino_GFX/Arduino_Canvas compiled for the host, feeds it a fixed test
// PetState, and dumps whichever page was asked for as a PPM (converted to
// PNG by tools/sim/render.sh). Not part of the firmware or the pio test
// suite -- see tools/sim/README.md for how and when to use this.

#include <Arduino_GFX.h>
#include <canvas/Arduino_Canvas.h>

#include <cstdio>
#include <cstring>

#include "pet_genome.h"
#include "ui_pages.h"

// --- Definitions for the globals ui_pages.h declares extern -------------------
// The real firmware (src/main.cpp) defines these backed by actual hardware
// state (theme colors, sensor detection, RTC, etc.); this harness defines
// its own fixed/fake versions purely for rendering.

Arduino_GFX *display = nullptr;
PetState pet{};

uint16_t COLOR_BACKGROUND = 0x0823;
uint16_t COLOR_CARD = 0x18E8;
uint16_t COLOR_MINT = 0x6718;
uint16_t COLOR_TEXT = 0xE73C;
uint16_t COLOR_MUTED = 0x8413;
uint16_t COLOR_WARNING = 0xFE48;
uint16_t COLOR_DANGER = 0xF2CB;
uint16_t COLOR_CYAN = 0x269F;
uint16_t COLOR_PURPLE = 0xA81F;

const char *STAGE_NAMES[] = {"BIT EGG", "HATCHLING", "SCOUT", "GUARDIAN", "TITAN"};

bool clockValid = true;
char clockText[6] = "12:34";

bool imuDetected = true;
bool rtcDetected = true;
bool pmuDetected = true;
bool codecDetected = true;
uint8_t i2cDeviceCount = 7;

namespace {

void seedTestPet(uint8_t stage) {
  // Fixed, arbitrary seeds -- deterministic output is the point of a
  // visual-diff tool. Swap these (or expose more CLI flags) to preview a
  // different genome/lineage/element.
  const uint32_t seed[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
  const uint32_t evolutionSeed[4] = {0x55555555, 0x66666666, 0x77777777, 0x88888888};
  pet = PetState{};
  pet.ageMinutes = 1440;
  pet.actions = 50;
  pet.food = 80;
  pet.joy = 65;
  pet.energy = 90;
  pet.health = 100;
  pet.stage = stage;
  pet.training = 40;
  pet.genome = derivePetGenome(seed, evolutionSeed);
}

bool writePpm(Arduino_Canvas &canvas, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
  const uint16_t *fb = canvas.getFramebuffer();
  for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; ++i) {
    const uint16_t px = fb[i];
    fputc(((px >> 11) & 0x1F) * 255 / 31, f);
    fputc(((px >> 5) & 0x3F) * 255 / 63, f);
    fputc((px & 0x1F) * 255 / 31, f);
  }
  fclose(f);
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
           "usage: %s <companion|status|egg> <output.ppm> [stage]\n"
           "  stage defaults to 2 (companion/status) or is forced to 0 (egg)\n",
           argv[0]);
    return 1;
  }
  const char *page = argv[1];
  const char *outPath = argv[2];
  const uint8_t stage = argc > 3 ? static_cast<uint8_t>(atoi(argv[3])) : 2;

  Arduino_Canvas canvas(LCD_WIDTH, LCD_HEIGHT, nullptr);
  canvas.begin();
  display = &canvas;

  if (strcmp(page, "companion") == 0) {
    seedTestPet(stage);
    drawCompanionPage();
  } else if (strcmp(page, "status") == 0) {
    seedTestPet(stage);
    drawStatusPage();
  } else if (strcmp(page, "egg") == 0) {
    seedTestPet(0);
    drawCompanionPage();
  } else {
    fprintf(stderr, "unknown page '%s' (want companion|status|egg)\n", page);
    return 1;
  }

  if (!writePpm(canvas, outPath)) {
    fprintf(stderr, "failed to write %s\n", outPath);
    return 1;
  }
  printf("wrote %s\n", outPath);
  return 0;
}
