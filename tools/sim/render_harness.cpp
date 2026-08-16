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

bool animationFrame = false;

DeviceSettings settings{};
const char *SLEEP_LABELS[] = {"15 SEC", "30 SEC", "1 MIN", "2 MIN"};
const char *VOLUME_LABELS[] = {"MUTE", "LOW", "MEDIUM", "HIGH", "MAX"};
const char *WAKE_LABELS[] = {"TOUCH", "BOOT KEY"};
const char *THEME_LABELS[] = {"AUTO // PET", "CYBER MINT", "AMBER CORE",
                              "VIOLET LINK", "MONO SIGNAL"};
uint8_t settingsGridPage = 0;
SettingsView settingsView = SETTINGS_HOME;

bool hasCopiedGenome = false;
bool newEggConfirmation = false;
uint32_t newEggConfirmationUntil = 0;
uint8_t pendingHatchMode = 0;

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

namespace {

const char *kPageNames =
    "companion|status|egg|settings|settings2|"
    "settings-brightness|settings-idle|settings-volume|settings-wake|"
    "settings-theme|settings-boot|genomelab";

void seedTestSettings() {
  settings = DeviceSettings{};
  settings.brightnessIndex = 7;
  settings.sleepIndex = 1;
  settings.soundEnabled = true;
  settings.bootAnimationEnabled = true;
  settings.volumeIndex = 3;
  settings.wakeMode = 0;
  settings.themeIndex = 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <%s> <output.ppm> [stage]\n", argv[0], kPageNames);
    return 1;
  }
  const char *page = argv[1];
  const char *outPath = argv[2];
  const uint8_t stage = argc > 3 ? static_cast<uint8_t>(atoi(argv[3])) : 2;

  Arduino_Canvas canvas(LCD_WIDTH, LCD_HEIGHT, nullptr);
  canvas.begin();
  display = &canvas;
  seedTestSettings();

  if (strcmp(page, "companion") == 0) {
    seedTestPet(stage);
    drawCompanionPage();
  } else if (strcmp(page, "status") == 0) {
    seedTestPet(stage);
    drawStatusPage();
  } else if (strcmp(page, "egg") == 0) {
    seedTestPet(0);
    drawCompanionPage();
  } else if (strcmp(page, "settings") == 0) {
    seedTestPet(stage);
    settingsGridPage = 0;
    settingsView = SETTINGS_HOME;
    drawSettingsPage();
  } else if (strcmp(page, "settings2") == 0) {
    seedTestPet(stage);
    settingsGridPage = 1;
    settingsView = SETTINGS_HOME;
    drawSettingsPage();
  } else if (strncmp(page, "settings-", 9) == 0) {
    seedTestPet(stage);
    const char *sub = page + 9;
    if (strcmp(sub, "brightness") == 0) settingsView = SETTINGS_BRIGHTNESS;
    else if (strcmp(sub, "idle") == 0) settingsView = SETTINGS_IDLE;
    else if (strcmp(sub, "volume") == 0) settingsView = SETTINGS_VOLUME;
    else if (strcmp(sub, "wake") == 0) settingsView = SETTINGS_WAKE;
    else if (strcmp(sub, "theme") == 0) settingsView = SETTINGS_THEME;
    else if (strcmp(sub, "boot") == 0) settingsView = SETTINGS_BOOT;
    else {
      fprintf(stderr, "unknown settings sub-view '%s'\n", sub);
      return 1;
    }
    drawSettingsPage();
  } else if (strcmp(page, "genomelab") == 0) {
    seedTestPet(stage);
    drawGenomeLabPage();
  } else {
    fprintf(stderr, "unknown page '%s' (want %s)\n", page, kPageNames);
    return 1;
  }

  if (!writePpm(canvas, outPath)) {
    fprintf(stderr, "failed to write %s\n", outPath);
    return 1;
  }
  printf("wrote %s\n", outPath);
  return 0;
}
