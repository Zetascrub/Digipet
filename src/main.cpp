#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <time.h>
#include <esp32-hal-psram.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include <Adafruit_XCA9554.h>
#include <Arduino_DriveBus_Library.h>
#include <Arduino_GFX_Library.h>

#include "pin_config.h"
#include "familiar_battle_service.h"
#include "ota_updater.h"
#include "pet_genome.h"

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

constexpr uint32_t PET_MAGIC_V1 = 0x44504731;
constexpr uint32_t PET_MAGIC_V2 = 0x44504732;
constexpr uint32_t PET_MAGIC = 0x44504733;
constexpr uint32_t ANIMATION_MS = 650;
constexpr uint32_t SETTINGS_MAGIC_V1 = 0x44505331;
constexpr uint32_t SETTINGS_MAGIC_V2 = 0x44505332;
constexpr uint32_t SETTINGS_MAGIC = 0x44505333;

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

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
Arduino_GFX *display = panel;
Arduino_Canvas pageCanvasA(LCD_WIDTH, LCD_HEIGHT, panel);
Arduino_Canvas pageCanvasB(LCD_WIDTH, LCD_HEIGHT, panel);
uint16_t *transitionFrame = nullptr;
bool transitionsReady = false;

std::shared_ptr<Arduino_IIC_DriveBus> i2cBus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void touchInterrupt();
void drawCentered(const char *text, int16_t y, uint8_t size, uint16_t color);
bool i2cPresent(uint8_t address);
void saveCopiedGenome(const PetGenome &genome);
std::unique_ptr<Arduino_IIC> touch(new Arduino_CST816x(
    i2cBus, CST816T_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, touchInterrupt));

Adafruit_XCA9554 expander;
Preferences preferences;
I2SClass audioI2S;
PetState pet;
FamiliarBattleService battle;

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

DeviceSettings settings;
const uint32_t SLEEP_TIMEOUTS[] = {15000, 30000, 60000, 120000};
const char *SLEEP_LABELS[] = {"15 SEC", "30 SEC", "1 MIN", "2 MIN"};
const char *VOLUME_LABELS[] = {"MUTE", "LOW", "MEDIUM", "HIGH", "MAX"};
const uint8_t CODEC_VOLUMES[] = {0x00, 0x58, 0x8B, 0xBF, 0xF2};
const char *WAKE_LABELS[] = {"TOUCH", "BOOT KEY"};
const char *THEME_LABELS[] = {"AUTO // PET", "CYBER MINT", "AMBER CORE",
                              "VIOLET LINK", "MONO SIGNAL"};
uint8_t settingsGridPage = 0;

uint8_t brightnessPercent() { return settings.brightnessIndex * 10; }
uint8_t brightnessLevel() {
  // Keep the 0% choice barely visible so it can always be changed again.
  return settings.brightnessIndex == 0 ? 8 :
      static_cast<uint8_t>(settings.brightnessIndex * 255 / 10);
}

enum SettingsView : uint8_t {
  SETTINGS_HOME,
  SETTINGS_BRIGHTNESS,
  SETTINGS_IDLE,
  SETTINGS_VOLUME,
  SETTINGS_WAKE,
  SETTINGS_THEME,
  SETTINGS_BOOT,
};
SettingsView settingsView = SETTINGS_HOME;

uint32_t lastMinute = 0;
uint32_t lastTouchRead = 0;
uint32_t lastInteraction = 0;
uint32_t lastAnimation = 0;
uint16_t creatureFrameInterval = 45;
bool touchWasDown = false;
bool sleeping = false;
bool screenOff = false;
bool animationFrame = false;
bool imuDetected = false;
bool rtcDetected = false;
bool pmuDetected = false;
bool codecDetected = false;
uint8_t i2cDeviceCount = 0;
bool audioReady = false;
bool sdDetected = false;
bool clockValid = false;
bool timeSynced = false;
uint32_t lastClockDraw = 0;
uint32_t sleepStarted = 0;
uint32_t lastImuRead = 0;
uint32_t lastBootRead = 0;
uint32_t nextBlink = 0;
uint32_t blinkUntil = 0;
bool bootWasDown = false;
bool imuReady = false;
bool imuBaselineReady = false;
float lastAccelX = 0;
float lastAccelY = 0;
float lastAccelZ = 0;
char clockText[6] = "--:--";
char message[40] = "Your companion is awake";
uint8_t playerId[32]{};
char playerIdHex[65]{};
uint64_t playerIdTimestamp = 0;
bool showingPlayerId = false;
bool showingUpdate = false;
bool showingGenomeProfile = false;
bool showingEvolutionDebug = false;
bool newEggConfirmation = false;
uint32_t newEggConfirmationUntil = 0;
uint8_t pendingHatchMode = 0;
uint32_t touchStartedAt = 0;
PetGenome copiedGenome{};
bool hasCopiedGenome = false;
bool battleGenomeCopied = false;
char genomeTransferStatus[32] = "NO COPIED GENOME";

struct NetworkConfig {
  char ssid[65];
  char password[65];
  char timezone[48];
  char ntp[65];
  bool syncOnBoot;
  bool valid;
};

NetworkConfig networkConfig{};

enum Page : uint8_t {
  PAGE_COMPANION,
  PAGE_STATUS,
  PAGE_BATTLE,
  PAGE_SETTINGS,
  PAGE_GENOME_LAB,
};
Page genomeProfileReturnPage = PAGE_COMPANION;
void presentCoherentPageFrame(Page page);
void transitionSettingsView(SettingsView target, bool forward);
void transitionSettingsGrid(uint8_t target);
Page currentPage = PAGE_COMPANION;
FamiliarBattleState lastBattleState = FamiliarBattleState::Idle;
uint16_t lastBattleTurn = 0;
uint16_t lastBattleMyHp = 0;
uint16_t lastBattleOpponentHp = 0;
int32_t touchStartX = -1;
int32_t touchStartY = -1;
int32_t touchLastX = -1;
int32_t touchLastY = -1;

void touchInterrupt() {
  touch->IIC_Interrupt_Flag = true;
}

bool codecWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t codecRead(uint8_t reg) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(0x18, 1) != 1) return 0;
  return Wire.read();
}

bool initAudio() {
  if (!codecDetected) return false;
  pinMode(AUDIO_PA, OUTPUT);
  digitalWrite(AUDIO_PA, HIGH);
  delay(20);
  audioI2S.setPins(I2S_BCLK, I2S_LRCLK, I2S_DOUT, I2S_DIN, I2S_MCLK);
  if (!audioI2S.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("Audio: I2S initialization failed");
    digitalWrite(AUDIO_PA, LOW);
    return false;
  }

  // ES8311 setup for 16 kHz / 4.096 MHz MCLK, following Waveshare's V2 example.
  const uint8_t init[][2] = {
      {0x00, 0x1F}, {0x00, 0x00}, {0x00, 0x80}, {0x01, 0x3F},
      {0x06, 0x00}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
      {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
      {0x09, 0x0C}, {0x0A, 0x0C}, {0x0D, 0x01}, {0x0E, 0x02},
      {0x12, 0x00}, {0x13, 0x10}, {0x1C, 0x6A}, {0x37, 0x08},
      {0x17, 0xC8}, {0x14, 0x1A}, {0x32, CODEC_VOLUMES[settings.volumeIndex]}};
  for (const auto &entry : init) {
    if (!codecWrite(entry[0], entry[1])) return false;
  }
  codecWrite(0x31, codecRead(0x31) & ~0x60); // unmute DAC
  Serial.println("Audio: ES8311 speaker ready");
  return true;
}

// Shared 32-sample sine table for every synthesized tone in the app.
static const int8_t kSineTable[] = {
    0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106,
    -90, -71, -49, -25};

// A soft attack into a curved decay across the rest of the note, rather
// than attack/flat-sustain/release: the flat plateau is what makes a tone
// read as a hard electronic "beep" instead of a struck/plucked chime.
float chimeEnvelope(uint32_t position, uint32_t attackSamples, uint32_t total) {
  if (total == 0) return 0.0f;
  if (attackSamples >= total) attackSamples = total - 1;
  if (position < attackSamples) {
    return attackSamples ? static_cast<float>(position) / attackSamples : 1.0f;
  }
  const float t = static_cast<float>(position - attackSamples) /
                  (total - attackSamples);
  return (1.0f - t) * (1.0f - t);
}

void playTone(uint16_t frequency, uint16_t durationMs, uint8_t level = 55) {
  if (!settings.soundEnabled || !audioReady || frequency == 0) return;
  constexpr uint32_t sampleRate = 16000;
  const uint32_t total = (sampleRate * durationMs) / 1000;
  const uint32_t attackSamples = min<uint32_t>(total / 6, sampleRate * 10 / 1000);
  uint32_t phase = 0;
  const uint32_t step = (static_cast<uint64_t>(frequency) << 32) / sampleRate;
  int16_t buffer[256]; // 128 stereo frames
  uint32_t produced = 0;
  while (produced < total) {
    const uint16_t frames = min<uint32_t>(128, total - produced);
    for (uint16_t i = 0; i < frames; i++) {
      const float envelope = chimeEnvelope(produced + i, attackSamples, total);
      // Peak is about 10,000 at level 100, safely inside signed 16-bit PCM.
      const int16_t sample = static_cast<int16_t>(
          kSineTable[phase >> 27] * static_cast<float>(level) * envelope *
          (10000.0f / (127.0f * 100.0f)));
      phase += step;
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }
    audioI2S.write(reinterpret_cast<uint8_t *>(buffer), frames * 4);
    produced += frames;
  }
}

// Mixes up to 4 simultaneous notes with the same chime envelope, for actual
// chords instead of a fast sequential arpeggio -- the single biggest lever
// for making a simple synth sound like a modern earcon instead of a 90s
// appliance beep.
void playChord(const uint16_t *frequencies, uint8_t voiceCount, uint16_t durationMs,
               uint8_t level = 55) {
  if (!settings.soundEnabled || !audioReady || voiceCount == 0) return;
  constexpr uint32_t sampleRate = 16000;
  const uint8_t voices = min<uint8_t>(voiceCount, 4);
  const uint32_t total = (sampleRate * durationMs) / 1000;
  const uint32_t attackSamples = min<uint32_t>(total / 6, sampleRate * 12 / 1000);
  uint32_t phase[4] = {0, 0, 0, 0};
  uint32_t step[4] = {0, 0, 0, 0};
  for (uint8_t v = 0; v < voices; v++)
    step[v] = (static_cast<uint64_t>(frequencies[v]) << 32) / sampleRate;
  const float scale = (10000.0f / 127.0f / 100.0f) / voices;
  int16_t buffer[256];
  uint32_t produced = 0;
  while (produced < total) {
    const uint16_t frames = min<uint32_t>(128, total - produced);
    for (uint16_t i = 0; i < frames; i++) {
      const float envelope = chimeEnvelope(produced + i, attackSamples, total);
      int32_t mixed = 0;
      for (uint8_t v = 0; v < voices; v++) {
        mixed += kSineTable[phase[v] >> 27];
        phase[v] += step[v];
      }
      const int16_t sample = static_cast<int16_t>(
          mixed * static_cast<float>(level) * envelope * scale);
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }
    audioI2S.write(reinterpret_cast<uint8_t *>(buffer), frames * 4);
    produced += frames;
  }
}

// The boot clip is embedded directly in flash (see platformio.ini's
// board_build.embed_files) as raw 16kHz/16-bit/stereo PCM, converted once
// from sd-card/Audio/Boot.mp3 -- there's no MP3 decoder in this firmware,
// so decoding happens offline and playback is just a straight PCM stream
// through the same I2S path playTone() already uses.
extern const uint8_t boot_clip_pcm_start[] asm("_binary_src_assets_boot_clip_pcm_start");
extern const uint8_t boot_clip_pcm_end[] asm("_binary_src_assets_boot_clip_pcm_end");

void bootClipTask(void *) {
  const uint8_t *data = boot_clip_pcm_start;
  const size_t total = static_cast<size_t>(boot_clip_pcm_end - boot_clip_pcm_start);
  size_t offset = 0;
  while (offset < total) {
    const size_t chunk = min<size_t>(512, total - offset);
    audioI2S.write(data + offset, chunk);
    offset += chunk;
  }
  vTaskDelete(nullptr);
}

// Streamed on its own FreeRTOS task, pinned off the core running setup(),
// so the ~5.2s clip plays in real time alongside the boot animation's
// render loop instead of blocking it outright (a single blocking call here
// would freeze the animation on its first frame until playback finished).
void playBootClipAsync() {
  if (!settings.soundEnabled || !audioReady) return;
  xTaskCreatePinnedToCore(bootClipTask, "bootClip", 3072, nullptr, 1, nullptr, 0);
}

void playActionSound(uint8_t action) {
  if (action == 0) {
    playTone(440, 55); playTone(523, 80);
  } else if (action == 1) {
    playTone(659, 55); playTone(880, 55); playTone(1047, 90);
  } else {
    playTone(330, 55); playTone(440, 55); playTone(659, 110);
  }
}

uint8_t bcdToDec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

uint8_t decToBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

bool imuWrite(uint8_t reg, uint8_t value) {
  const uint8_t address = i2cPresent(0x6B) ? 0x6B : 0x6A;
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool imuRead(uint8_t reg, uint8_t *data, uint8_t length) {
  const uint8_t address = i2cPresent(0x6B) ? 0x6B : 0x6A;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, length) != length) return false;
  for (uint8_t i = 0; i < length; i++) data[i] = Wire.read();
  return true;
}

bool initializeLiftSensor() {
  if (!imuDetected) return false;
  uint8_t ctrl5 = 0;
  uint8_t ctrl7 = 0;
  imuRead(0x06, &ctrl5, 1);
  imuRead(0x08, &ctrl7, 1);
  // ±4 g, 11 Hz low-power accelerometer, LPF enabled; gyroscope stays off.
  const bool configured = imuWrite(0x03, 0x1E) && imuWrite(0x06, (ctrl5 & 0xF0) | 0x01) &&
                          imuWrite(0x08, (ctrl7 & 0xFC) | 0x01);
  Serial.printf("Lift sensor: %s\n", configured ? "ready" : "initialization failed");
  return configured;
}

bool readAcceleration(float &x, float &y, float &z) {
  uint8_t raw[6];
  if (!imuReady || !imuRead(0x35, raw, sizeof(raw))) return false;
  const int16_t rx = static_cast<int16_t>((raw[1] << 8) | raw[0]);
  const int16_t ry = static_cast<int16_t>((raw[3] << 8) | raw[2]);
  const int16_t rz = static_cast<int16_t>((raw[5] << 8) | raw[4]);
  x = rx * (4.0f / 32768.0f);
  y = ry * (4.0f / 32768.0f);
  z = rz * (4.0f / 32768.0f);
  return true;
}

bool rtcRead(struct tm &value) {
  Wire.beginTransmission(0x51);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x51, 7) != 7) {
    return false;
  }
  uint8_t data[7];
  for (uint8_t &byte : data) byte = Wire.read();
  if (data[0] & 0x80) return false; // oscillator-stop flag: time is untrustworthy
  value = {};
  value.tm_sec = bcdToDec(data[0] & 0x7F);
  value.tm_min = bcdToDec(data[1] & 0x7F);
  value.tm_hour = bcdToDec(data[2] & 0x3F);
  value.tm_mday = bcdToDec(data[3] & 0x3F);
  value.tm_wday = bcdToDec(data[4] & 0x07);
  value.tm_mon = bcdToDec(data[5] & 0x1F) - 1;
  value.tm_year = bcdToDec(data[6]) + 100;
  return value.tm_year >= 124 && value.tm_mon >= 0 && value.tm_mon < 12 &&
         value.tm_mday >= 1 && value.tm_mday <= 31 && value.tm_hour <= 23 &&
         value.tm_min <= 59 && value.tm_sec <= 59;
}

bool rtcWrite(const struct tm &value) {
  const uint8_t data[] = {
      0x04, decToBcd(value.tm_sec), decToBcd(value.tm_min),
      decToBcd(value.tm_hour), decToBcd(value.tm_mday),
      decToBcd(value.tm_wday), decToBcd(value.tm_mon + 1),
      decToBcd((value.tm_year + 1900) % 100)};
  Wire.beginTransmission(0x51);
  Wire.write(data, sizeof(data));
  return Wire.endTransmission() == 0;
}

const char *posixTimezone(const char *timezone) {
  if (strcmp(timezone, "Europe/London") == 0) {
    return "GMT0BST,M3.5.0/1,M10.5.0";
  }
  // Advanced users may place a POSIX TZ expression directly in the file.
  return timezone[0] ? timezone : "UTC0";
}

void copyConfigValue(char *destination, size_t size, const String &value) {
  String clean = value;
  if (clean.endsWith("\r")) clean.remove(clean.length() - 1);
  clean.toCharArray(destination, size);
}

bool readNetworkConfig() {
  if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA) ||
      !SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD: card mount failed");
    return false;
  }
  sdDetected = SD_MMC.cardType() != CARD_NONE;
  if (!sdDetected) {
    Serial.println("SD: no card present");
    SD_MMC.end();
    return false;
  }
  File file = SD_MMC.open("/digipet/wifi.ini", FILE_READ);
  if (!file || file.size() == 0 || file.size() > 1024) {
    Serial.println("SD: /digipet/wifi.ini missing or invalid size");
    if (file) file.close();
    SD_MMC.end();
    return false;
  }
  networkConfig = {};
  networkConfig.syncOnBoot = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    String trimmed = line;
    trimmed.trim();
    if (!trimmed.length() || trimmed.startsWith("#") || trimmed.startsWith(";")) continue;
    const int separator = line.indexOf('=');
    if (separator <= 0) continue;
    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim(); key.toLowerCase();
    if (key == "ssid") copyConfigValue(networkConfig.ssid, sizeof(networkConfig.ssid), value);
    else if (key == "password") copyConfigValue(networkConfig.password, sizeof(networkConfig.password), value);
    else if (key == "timezone") copyConfigValue(networkConfig.timezone, sizeof(networkConfig.timezone), value);
    else if (key == "ntp") copyConfigValue(networkConfig.ntp, sizeof(networkConfig.ntp), value);
    else if (key == "sync_on_boot") {
      value.trim(); value.toLowerCase();
      networkConfig.syncOnBoot = value == "true" || value == "1" || value == "yes";
    }
  }
  file.close();
  SD_MMC.end();
  networkConfig.valid = networkConfig.ssid[0] && networkConfig.password[0] &&
                        networkConfig.timezone[0] && networkConfig.ntp[0];
  Serial.printf("SD: Wi-Fi configuration %s (credentials hidden)\n",
                networkConfig.valid ? "valid" : "invalid");
  return networkConfig.valid;
}

bool mountGenomeCard() {
  expander.pinMode(7, OUTPUT);
  expander.digitalWrite(7, HIGH);
  delay(100);
  if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA) ||
      !SD_MMC.begin("/sdcard", true)) {
    expander.digitalWrite(7, LOW);
    return false;
  }
  return SD_MMC.cardType() != CARD_NONE;
}

void closeGenomeCard() {
  SD_MMC.end();
  expander.digitalWrite(7, LOW);
}

bool exportActiveGenome() {
  if (!mountGenomeCard()) {
    strcpy(genomeTransferStatus, "SD CARD NOT READY");
    return false;
  }
  SD_MMC.mkdir("/digipet");
  File file = SD_MMC.open("/digipet/genome.txt", FILE_WRITE);
  char code[PET_GENOME_CODE_LENGTH + 1]{};
  const bool encoded = encodePetGenome(pet.genome, code, sizeof(code));
  const bool written = file && encoded && file.println(code) > 0;
  if (file) file.close();
  closeGenomeCard();
  strcpy(genomeTransferStatus, written ? "EXPORTED TO SD" : "EXPORT FAILED");
  return written;
}

bool importCopiedGenome() {
  if (!mountGenomeCard()) {
    strcpy(genomeTransferStatus, "SD CARD NOT READY");
    return false;
  }
  File file = SD_MMC.open("/digipet/genome.txt", FILE_READ);
  String code;
  while (file && file.available() && !code.length()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() && !line.startsWith("#") && !line.startsWith(";")) code = line;
  }
  if (file) file.close();
  closeGenomeCard();
  code.trim();
  PetGenome imported{};
  if (!decodePetGenome(code.c_str(), imported)) {
    strcpy(genomeTransferStatus, "INVALID GENOME CODE");
    return false;
  }
  saveCopiedGenome(imported);
  return hasCopiedGenome;
}

// A BIOS/POST-style scrolling status log, standing in for the old spinner
// screen. Runs on every boot now (see runStartupNetworkSync), not just as a
// rare manual action.
struct BootLogEntry {
  char text[26];
  uint16_t color;
};
constexpr uint8_t BOOT_LOG_MAX = 6;
constexpr int16_t BOOT_LOG_X = 34;
constexpr int16_t BOOT_LOG_TOP = 156;
constexpr int16_t BOOT_LOG_LINE_H = 32;
constexpr int16_t BOOT_LOG_SPIN_X = 310;
BootLogEntry bootLog[BOOT_LOG_MAX];
uint8_t bootLogCount = 0;

// Renders "LABEL...... STATUS", dot-padding the label to a fixed column so
// every line lines up like a real boot log.
void formatBiosLine(char *out, size_t outSize, const char *label, const char *status) {
  char padded[16];
  size_t len = 0;
  while (label[len] && len < sizeof(padded) - 1) { padded[len] = label[len]; ++len; }
  while (len < 14 && len < sizeof(padded) - 1) padded[len++] = '.';
  padded[len] = '\0';
  snprintf(out, outSize, "%s %s", padded, status);
}

// Full redraw through the canvas: only called on real state changes (a new
// line, or a line's final status), never in the per-frame wait loop.
void drawBootLogScreen() {
  Arduino_GFX *previousDisplay = display;
  if (transitionsReady) display = &pageCanvasA;
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("SYSTEM BOOT", 60, 3, COLOR_MINT);
  drawCentered("STARTUP DIAGNOSTICS", 96, 1, COLOR_CYAN);
  display->setTextSize(2);
  for (uint8_t i = 0; i < bootLogCount; ++i) {
    display->setTextColor(bootLog[i].color);
    display->setCursor(BOOT_LOG_X, BOOT_LOG_TOP + i * BOOT_LOG_LINE_H);
    display->print(bootLog[i].text);
  }
  drawCentered("OFFLINE PLAY REMAINS AVAILABLE", 404, 1, COLOR_MUTED);
  if (transitionsReady) {
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
  }
}

void bootLogPush(const char *label, const char *status, uint16_t color) {
  if (bootLogCount >= BOOT_LOG_MAX) {
    for (uint8_t i = 1; i < BOOT_LOG_MAX; ++i) bootLog[i - 1] = bootLog[i];
    bootLogCount = BOOT_LOG_MAX - 1;
  }
  formatBiosLine(bootLog[bootLogCount].text, sizeof(bootLog[bootLogCount].text), label, status);
  bootLog[bootLogCount].color = color;
  ++bootLogCount;
  drawBootLogScreen();
}

void bootLogUpdateLast(const char *label, const char *status, uint16_t color) {
  if (bootLogCount == 0) return;
  formatBiosLine(bootLog[bootLogCount - 1].text, sizeof(bootLog[bootLogCount - 1].text),
                label, status);
  bootLog[bootLogCount - 1].color = color;
  drawBootLogScreen();
}

// Ticks a small "|/-\" spinner glyph right after the current line's text
// (measured, not a fixed column, so it doesn't float in a gap after short
// status words), drawn straight to the live panel rather than through a
// full-frame canvas blit — this is the same lesson as the old circular
// spinner: a ~20x20px update every ~100ms is fine, but a 329KB blit at that
// rate stacks extra SPI traffic right on top of Wi-Fi's own bus contention
// and causes hitches.
void bootLogSpin(uint8_t frame) {
  if (bootLogCount == 0) return;
  static const char glyphs[] = "|/-\\";
  const int16_t y = BOOT_LOG_TOP + (bootLogCount - 1) * BOOT_LOG_LINE_H;
  display->setTextSize(2);
  int16_t boundsX, boundsY;
  uint16_t textW, textH;
  display->getTextBounds(bootLog[bootLogCount - 1].text, BOOT_LOG_X, y,
                         &boundsX, &boundsY, &textW, &textH);
  const int16_t glyphX = BOOT_LOG_X + textW + 12;
  display->fillRect(glyphX, y - 2, 22, 20, COLOR_BACKGROUND);
  display->setTextColor(bootLog[bootLogCount - 1].color);
  display->setCursor(glyphX, y);
  display->print(glyphs[frame % 4]);
}

bool synchronizeClock() {
  bootLogCount = 0;
  if (!networkConfig.valid) return false;
  bootLogPush("LINK", "CONNECTING", COLOR_CYAN);
  WiFi.mode(WIFI_STA);
  WiFi.begin(networkConfig.ssid, networkConfig.password);
  const uint32_t started = millis();
  uint8_t linkFrame = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    bootLogSpin(linkFrame++);
    delay(90);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi: connection timed out");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    bootLogUpdateLast("LINK", "OFFLINE", COLOR_WARNING);
    delay(650);
    return false;
  }
  Serial.println("Wi-Fi: connected; requesting NTP time");
  bootLogUpdateLast("LINK", "OK", COLOR_MINT);
  bootLogPush("TIME SYNC", "SYNCING", COLOR_CYAN);
  configTzTime(posixTimezone(networkConfig.timezone), networkConfig.ntp);
  struct tm local{};
  bool received = false;
  const uint32_t syncStarted = millis();
  while (!received && millis() - syncStarted < 10000) {
    received = getLocalTime(&local, 120);
    bootLogSpin(linkFrame++);
  }
  if (received && rtcWrite(local)) {
    clockValid = timeSynced = true;
    preferences.putULong64("lastSync", static_cast<uint64_t>(time(nullptr)));
    snprintf(clockText, sizeof(clockText), "%02d:%02d", local.tm_hour, local.tm_min);
    Serial.println("Time: NTP synchronized and RTC updated");
    bootLogUpdateLast("TIME SYNC", "OK", COLOR_MINT);
  } else {
    Serial.println("Time: NTP or RTC update failed");
    bootLogUpdateLast("TIME SYNC", "FAILED", COLOR_DANGER);
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wi-Fi: radio disabled");
  bootLogPush("SYSTEM", "READY", COLOR_MINT);
  delay(500);
  return received;
}

// Runs as its own visible phase after the boot animation finishes, instead
// of polled silently mid-frame (that used to cause visible stutter during
// the animation, since the WiFi/NTP checks weren't as free as they looked).
void runStartupNetworkSync() {
  struct tm rtcTime{};
  clockValid = rtcDetected && rtcRead(rtcTime);
  if (clockValid) {
    snprintf(clockText, sizeof(clockText), "%02d:%02d", rtcTime.tm_hour, rtcTime.tm_min);
    Serial.println("RTC: valid retained time found");
  } else {
    Serial.println("RTC: time is invalid or power was lost");
  }
  if (!readNetworkConfig() || (!networkConfig.syncOnBoot && clockValid)) return;

  expander.pinMode(7, OUTPUT);
  expander.digitalWrite(7, HIGH);
  delay(100);
  synchronizeClock();
  expander.digitalWrite(7, LOW);
}

void generatePlayerId() {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  playerIdTimestamp = static_cast<uint64_t>(time(nullptr));
  const uint32_t randomValue = esp_random();
  uint8_t input[15]{};
  memcpy(input, mac + 3, 3);
  for (uint8_t i = 0; i < 8; ++i) input[3 + i] = playerIdTimestamp >> (56 - i * 8);
  for (uint8_t i = 0; i < 4; ++i) input[11 + i] = randomValue >> (24 - i * 8);
  mbedtls_sha256(input, sizeof(input), playerId, 0);
  for (uint8_t i = 0; i < sizeof(playerId); ++i) {
    snprintf(playerIdHex + i * 2, 3, "%02X", playerId[i]);
  }
  Serial.printf("Player ID generated at UTC %llu from device suffix %02X%02X%02X\n",
                static_cast<unsigned long long>(playerIdTimestamp),
                mac[3], mac[4], mac[5]);
}

uint8_t addClamped(uint8_t value, uint8_t amount) {
  return value > 100 - amount ? 100 : value + amount;
}

uint8_t subtractClamped(uint8_t value, uint8_t amount) {
  return value > amount ? value - amount : 0;
}

uint16_t scaleRgb565(uint16_t color, uint8_t percent) {
  const uint16_t red = min<uint16_t>(31, ((color >> 11) & 0x1F) * percent / 100);
  const uint16_t green = min<uint16_t>(63, ((color >> 5) & 0x3F) * percent / 100);
  const uint16_t blue = min<uint16_t>(31, (color & 0x1F) * percent / 100);
  return (red << 11) | (green << 5) | blue;
}

// Blends two RGB565 colors channel-wise; t is clamped to [0, 1]. Used to fake
// smooth fades, gradients and bloom on hardware with no alpha compositing.
uint16_t lerpRgb565(uint16_t from, uint16_t to, float t) {
  t = constrain(t, 0.0f, 1.0f);
  const int fr = (from >> 11) & 0x1F, tr = (to >> 11) & 0x1F;
  const int fg = (from >> 5) & 0x3F, tg = (to >> 5) & 0x3F;
  const int fb = from & 0x1F, tb = to & 0x1F;
  const uint16_t red = static_cast<uint16_t>(lroundf(fr + (tr - fr) * t));
  const uint16_t green = static_cast<uint16_t>(lroundf(fg + (tg - fg) * t));
  const uint16_t blue = static_cast<uint16_t>(lroundf(fb + (tb - fb) * t));
  return (red << 11) | (green << 5) | blue;
}

void applyTheme() {
  struct ThemeColors {
    uint16_t background, card, primary, text, muted;
    uint16_t warning, danger, cyan, secondary;
  };
  static constexpr ThemeColors themes[] = {
      {0, 0, 0, 0, 0, 0, 0, 0, 0},  // AUTO is derived below.
      {0x0823, 0x18E8, 0x6718, 0xE73C, 0x8413, 0xFE48, 0xF2CB, 0x269F, 0xA81F},
      {0x1000, 0x28C2, 0xFD20, 0xFF9C, 0x9B48, 0xFFE0, 0xF260, 0xFBA0, 0xB940},
      {0x080F, 0x2019, 0xC35F, 0xF73F, 0x8C18, 0xFD86, 0xF1CB, 0x6DFF, 0x91FF},
      {0x0000, 0x18C3, 0xC618, 0xFFFF, 0x7BEF, 0xDEFB, 0xD69A, 0xBDF7, 0x8410},
  };

  if (settings.themeIndex == 0) {
    const PetPalette petPalette = paletteForGenome(pet.genome);
    COLOR_BACKGROUND = scaleRgb565(petPalette.primaryDark, 22);
    COLOR_CARD = scaleRgb565(petPalette.primaryDark, 55);
    COLOR_MINT = petPalette.primaryLight;
    COLOR_TEXT = 0xF7BE;
    COLOR_MUTED = scaleRgb565(petPalette.secondary, 82);
    COLOR_WARNING = petPalette.accent;
    COLOR_DANGER = 0xF2CB;
    COLOR_CYAN = petPalette.glow;
    COLOR_PURPLE = petPalette.secondary;
    return;
  }

  const ThemeColors &theme = themes[settings.themeIndex];
  COLOR_BACKGROUND = theme.background;
  COLOR_CARD = theme.card;
  COLOR_MINT = theme.primary;
  COLOR_TEXT = theme.text;
  COLOR_MUTED = theme.muted;
  COLOR_WARNING = theme.warning;
  COLOR_DANGER = theme.danger;
  COLOR_CYAN = theme.cyan;
  COLOR_PURPLE = theme.secondary;
}

void savePet() {
  preferences.putBytes("state", &pet, sizeof(pet));
}

void loadCopiedGenome() {
  hasCopiedGenome = preferences.getBytesLength("copiedGenome") == sizeof(copiedGenome);
  if (hasCopiedGenome) {
    preferences.getBytes("copiedGenome", &copiedGenome, sizeof(copiedGenome));
    strcpy(genomeTransferStatus, "COPIED GENOME READY");
  }
}

void saveCopiedGenome(const PetGenome &genome) {
  copiedGenome = genome;
  hasCopiedGenome = preferences.putBytes("copiedGenome", &copiedGenome,
                                         sizeof(copiedGenome)) == sizeof(copiedGenome);
  strcpy(genomeTransferStatus, hasCopiedGenome ? "COPIED GENOME READY" :
                                                "COPY SAVE FAILED");
}

void saveSettings() {
  preferences.putBytes("settings", &settings, sizeof(settings));
}

void loadSettings() {
  if (preferences.getBytesLength("settings") == sizeof(settings)) {
    preferences.getBytes("settings", &settings, sizeof(settings));
  }
  if (settings.magic == SETTINGS_MAGIC_V1) {
    settings.magic = SETTINGS_MAGIC_V2;
    settings.themeIndex = 0;
  }
  if (settings.magic == SETTINGS_MAGIC_V2) {
    static constexpr uint8_t oldBrightnessToPercent[] = {3, 5, 8, 10};
    settings.brightnessIndex = oldBrightnessToPercent[
        min<uint8_t>(settings.brightnessIndex, 3)];
    settings.magic = SETTINGS_MAGIC;
    saveSettings();
  }
  if (settings.magic != SETTINGS_MAGIC || settings.brightnessIndex > 10 ||
      settings.sleepIndex > 3 || settings.volumeIndex > 4 ||
      settings.wakeMode > 1 || settings.themeIndex > 4) {
    settings = {SETTINGS_MAGIC, 8, 1, true, true, 2, 0, 0};
    saveSettings();
  }
}

void loadPet() {
  const size_t storedSize = preferences.getBytesLength("state");
  if (storedSize == sizeof(pet)) {
    preferences.getBytes("state", &pet, sizeof(pet));
  } else if (storedSize > 0) {
    struct StateV2 {
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
    } previous{};
    struct OldState {
      uint32_t magic, ageMinutes, actions;
      uint8_t food, joy, energy, health;
    } old{};
    if (storedSize == sizeof(previous)) {
      preferences.getBytes("state", &previous, sizeof(previous));
    }
    if (previous.magic == PET_MAGIC_V2) {
      pet = {PET_MAGIC, previous.ageMinutes, previous.actions, previous.food,
             previous.joy, previous.energy, previous.health, previous.stage,
             previous.training, {previous.reserved[0], previous.reserved[1]},
             generatePetGenome()};
      Serial.println("Pet: existing companion received a persistent genome");
    } else if (storedSize >= sizeof(old)) {
      preferences.getBytes("state", &old, sizeof(old));
    }
    if (old.magic == PET_MAGIC_V1) {
      pet = {PET_MAGIC, old.ageMinutes, old.actions, old.food, old.joy,
             old.energy, old.health, 1, 0, {0, 0}, generatePetGenome()};
    }
  }
  if (pet.magic != PET_MAGIC) {
    pet = {PET_MAGIC, 0, 0, 82, 82, 82, 100, 0, 0, {0, 0},
           generatePetGenome()};
    Serial.printf("Pet: new %s selected (%s affinity)\n",
                  eggLineageName(pet.genome.lineage),
                  elementName(pet.genome.element));
  }
  // Battle-system bring-up profile. reserved[0] is the pet's level until
  // the persistent progression schema lands.
  pet.reserved[0] = 5;
  savePet();
}

void drawCentered(const char *text, int16_t y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  display->setTextSize(size);
  display->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display->setCursor((LCD_WIDTH - w) / 2, y);
  display->setTextColor(color);
  display->print(text);
}

// A soft vertical wash used as the base of every full-screen page instead of
// a flat fill, so the whole UI reads as one layered surface rather than flat
// panels pasted on a single color. Every caller already renders through the
// PSRAM canvas, so the extra per-row work costs nothing on the wire.
// The vertical gradient color paintPageBackdrop() paints at a given y, so
// anything drawn after it (glows, halos) can blend toward its own color
// relative to that instead of guessing a fixed tone that can end up darker
// than the page around it.
uint16_t backdropColorAt(int16_t y) {
  const uint16_t horizon = lerpRgb565(COLOR_BACKGROUND, COLOR_CARD, 0.4f);
  return lerpRgb565(COLOR_BACKGROUND, horizon,
                    static_cast<float>(y) / (LCD_HEIGHT - 1));
}

void paintPageBackdrop() {
  for (int16_t y = 0; y < LCD_HEIGHT; ++y) {
    display->drawFastHLine(0, y, LCD_WIDTH, backdropColorAt(y));
  }
}

// A soft halo escaping from behind a card's edges, faked with a couple of
// widening, dimming outlines drawn before the card itself. Makes the hero
// panel on each page read as raised instead of flat-pasted.
void drawPanelGlow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                   uint16_t color) {
  display->drawRoundRect(x - 6, y - 6, w + 12, h + 12, radius + 6, scaleRgb565(color, 12));
  display->drawRoundRect(x - 3, y - 3, w + 6, h + 6, radius + 3, scaleRgb565(color, 22));
}

// --- Boot sequence: "Genesis" -------------------------------------------
// Every frame is composed off-screen into pageCanvasA and blitted to the
// panel in one shot, the same double-buffering trick the page transitions
// use, so nothing ever tears or flickers mid-draw.

constexpr int16_t BOOT_CX = LCD_WIDTH / 2;
constexpr int16_t BOOT_TOP_Y = 122;
constexpr int16_t BOOT_BOTTOM_Y = 316;
constexpr int16_t BOOT_CORE_Y = (BOOT_TOP_Y + BOOT_BOTTOM_Y) / 2;
constexpr uint8_t BOOT_NODE_COUNT = 16;

float bootSmoothstep(float from, float to, float value) {
  const float t = constrain((value - from) / (to - from), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// A soft vertical wash plus a couple of glow blobs, painted once into the
// backdrop canvas so the per-frame loop can just memcpy it back in.
void paintBootBackdrop(const PetPalette &palette) {
  Arduino_GFX *previousDisplay = display;
  display = &pageCanvasB;
  const uint16_t horizon = scaleRgb565(palette.primaryDark, 55);
  for (int16_t y = 0; y < LCD_HEIGHT; ++y) {
    display->drawFastHLine(0, y, LCD_WIDTH,
                           lerpRgb565(COLOR_BACKGROUND, horizon,
                                     static_cast<float>(y) / (LCD_HEIGHT - 1)));
  }
  auto glowBlob = [&](int16_t x, int16_t y, int16_t radius, uint16_t color,
                      uint8_t maxPercent) {
    const uint16_t backdrop = lerpRgb565(COLOR_BACKGROUND, horizon,
                                         static_cast<float>(y) / (LCD_HEIGHT - 1));
    for (int16_t r = radius; r > 4; r -= 5) {
      const float t = 1.0f - static_cast<float>(r) / radius;
      display->fillCircle(x, y, r, lerpRgb565(backdrop, color, t * (maxPercent / 100.0f)));
    }
  };
  glowBlob(90, 130, 120, palette.secondary, 30);
  glowBlob(280, 360, 140, palette.accent, 26);
  display = previousDisplay;
}

void drawBootParticles(const PetPalette &palette, uint32_t frame) {
  constexpr uint8_t COUNT = 22;
  const float rise = frame * 0.6f;
  for (uint8_t i = 0; i < COUNT; ++i) {
    const uint32_t seedWord = pet.genome.seed[i % 4];
    const uint8_t place = (seedWord >> ((i * 5) % 24)) & 0xFF;
    const int16_t x = 26 + (place % (LCD_WIDTH - 52));
    const float speed = 0.35f + (i % 5) * 0.12f;
    const float travel = fmodf(rise * speed + i * 37.0f, 300.0f);
    const int16_t y = 344 - static_cast<int16_t>(travel);
    if (y < 96) continue;
    const float twinkle = (sinf(frame * 0.09f + i * 1.7f) + 1.0f) * 0.5f;
    display->fillCircle(x, y, (i & 3) == 0 ? 2 : 1,
                        lerpRgb565(palette.secondary, palette.glow, twinkle));
  }
}

void drawBootHelix(const PetPalette &palette, uint32_t frame, float progress) {
  const float buildT = bootSmoothstep(0.0f, 0.42f, progress);
  const float convergeT = bootSmoothstep(0.55f, 0.85f, progress);
  constexpr float ANGLE_STEP = 6.2832f / BOOT_NODE_COUNT;

  for (uint8_t node = 0; node < BOOT_NODE_COUNT; ++node) {
    const float normalizedPos = static_cast<float>(node) / (BOOT_NODE_COUNT - 1);
    if (normalizedPos > buildT) continue;

    const float baseY = BOOT_TOP_Y + normalizedPos * (BOOT_BOTTOM_Y - BOOT_TOP_Y);
    const int16_t y = lroundf(baseY + (BOOT_CORE_Y - baseY) * convergeT);
    const float orbit = 60.0f * (1.0f - convergeT);
    const float phase = frame * 0.10f + node * ANGLE_STEP * 2.0f;
    const int16_t offset = lroundf(sinf(phase) * orbit);
    const float depthT = (cosf(phase) + 1.0f) * 0.5f;

    const uint16_t strandA = lerpRgb565(palette.primaryDark, palette.primaryLight, normalizedPos);
    const uint16_t strandB = lerpRgb565(palette.secondary, palette.accent, normalizedPos);
    const int16_t xA = BOOT_CX - offset, xB = BOOT_CX + offset;

    if (orbit > 6.0f) {
      display->drawLine(xA, y, xB, y, scaleRgb565(palette.primaryDark, 65));
    }
    display->fillCircle(xA, y, lroundf(2 + depthT * 3), scaleRgb565(strandA, 55 + lroundf(depthT * 45)));
    display->fillCircle(xB, y, lroundf(2 + (1.0f - depthT) * 3),
                        scaleRgb565(strandB, 55 + lroundf((1.0f - depthT) * 45)));
  }
}

void drawBootCore(const PetPalette &palette, uint32_t frame, float progress) {
  const float convergeT = bootSmoothstep(0.55f, 0.85f, progress);
  const float pulse = (sinf(frame * 0.18f) + 1.0f) * 0.5f;
  const int16_t radius = 8 + lroundf(convergeT * 22) + lroundf(pulse * (2 + convergeT * 5));

  display->fillCircle(BOOT_CX, BOOT_CORE_Y, radius + 14, scaleRgb565(palette.glow, 16));
  display->fillCircle(BOOT_CX, BOOT_CORE_Y, radius + 7, scaleRgb565(palette.glow, 30));
  display->fillCircle(BOOT_CX, BOOT_CORE_Y, radius, lerpRgb565(palette.primary, palette.glow, pulse));
  display->fillCircle(BOOT_CX, BOOT_CORE_Y, max<int16_t>(3, radius - 8), palette.primaryLight);

  if (convergeT > 0.6f) {
    for (uint8_t ring = 0; ring < 2; ++ring) {
      const float ringPhase = fmodf(frame * 0.035f + ring * 0.5f, 1.0f);
      const int16_t ringRadius = radius + 10 + lroundf(ringPhase * 76);
      display->drawCircle(BOOT_CX, BOOT_CORE_Y, ringRadius,
                          lerpRgb565(palette.glow, COLOR_BACKGROUND, ringPhase));
    }
  }
}

void drawBootTitle(float progress) {
  const float fadeT = bootSmoothstep(0.0f, 0.14f, progress);
  drawCentered("DIGIPET", 30, 3, lerpRgb565(COLOR_BACKGROUND, COLOR_TEXT, fadeT));
  drawCentered("GENETIC LIFE INTERFACE", 66, 1, lerpRgb565(COLOR_BACKGROUND, COLOR_CYAN, fadeT));
}

void drawBootStatus(float progress) {
  constexpr int16_t barX = 41, barY = 372, barW = 286, barH = 8;
  display->fillRoundRect(barX, barY, barW, barH, 4, COLOR_CARD);
  const uint16_t filled = static_cast<uint16_t>(barW * progress);
  if (filled > 2) {
    display->fillRoundRect(barX, barY, filled, barH, 4, lerpRgb565(COLOR_PURPLE, COLOR_CYAN, progress));
    display->fillCircle(barX + filled - 1, barY + barH / 2, 6, COLOR_CYAN);
    display->fillCircle(barX + filled - 1, barY + barH / 2, 3, RGB565_WHITE);
  }

  // Network/time status is no longer reported here: sync now runs as its
  // own visible phase after the animation instead of polled mid-frame,
  // which used to cause visible stutter during the sequence.
  const char *phaseText = progress < 0.30f ? "READING GENOME" :
                          progress < 0.58f ? "ASSEMBLING FORM" :
                          progress < 0.90f ? "STABILISING SIGNAL" :
                                             "LINK READY";
  drawCentered(phaseText, 410, 1, progress > 0.89f ? COLOR_MINT : COLOR_MUTED);
}

// Eases the helix/core into the resting badge logo instead of hard-cutting
// to it, so the sequence resolves as one continuous motion.
void bootOutro(const PetPalette &palette) {
  constexpr uint8_t FRAMES = 22;
  const uint16_t startWash = scaleRgb565(palette.primaryDark, 55);
  for (uint8_t f = 1; f <= FRAMES; ++f) {
    const float eased = bootSmoothstep(0.0f, 1.0f, static_cast<float>(f) / FRAMES);
    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;

    display->fillScreen(lerpRgb565(startWash, COLOR_BACKGROUND, eased));
    const int16_t ringOuter = lroundf(78 + eased * 23);
    display->fillCircle(BOOT_CX, 202, lroundf(66 + eased * 22), COLOR_CARD);
    display->drawCircle(BOOT_CX, 202, ringOuter, COLOR_CYAN);
    display->drawCircle(BOOT_CX, 202, ringOuter + 10, COLOR_PURPLE);
    display->fillCircle(BOOT_CX, 202, lroundf(40 + eased * 14), lerpRgb565(palette.primary, palette.glow, eased));
    display->fillCircle(BOOT_CX, 202, lroundf(24 + eased * 10), COLOR_BACKGROUND);
    display->fillCircle(BOOT_CX, 202, lroundf(8 + eased * 7), COLOR_TEXT);

    drawCentered("DIGIPET", 315, 5, lerpRgb565(COLOR_BACKGROUND, COLOR_TEXT, eased));
    drawCentered("LIFE SIGNAL ONLINE", 377, 2, lerpRgb565(COLOR_BACKGROUND, COLOR_CYAN, eased));

    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
    delay(18);
  }
  // The boot clip (started in bootAnimation/bootAnimationFallback) already
  // covers this outro; nothing further to play here.
}

// Direct-to-panel fallback for the rare case PSRAM canvases aren't
// available; functionally the same beats as the smooth path, just drawn
// straight to the panel without a framebuffer behind it.
void bootAnimationFallback() {
  const uint32_t animationStarted = millis();
  constexpr uint32_t BOOT_DURATION_MS = 5000;
  panel->setBrightness(180);
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("DIGIPET", 23, 3, COLOR_TEXT);
  drawCentered("GENETIC LIFE INTERFACE", 57, 1, COLOR_CYAN);
  display->drawRoundRect(24, 88, 320, 260, 30, COLOR_CARD);
  display->drawRoundRect(31, 95, 306, 246, 25, COLOR_PURPLE);
  playBootClipAsync();

  for (uint8_t frame = 0; millis() - animationStarted < BOOT_DURATION_MS; ++frame) {
    const uint32_t elapsed = millis() - animationStarted;
    const float progress = min(1.0f, elapsed / static_cast<float>(BOOT_DURATION_MS));
    display->fillRoundRect(38, 102, 292, 232, 20, COLOR_BACKGROUND);

    const int16_t coreY = 210;
    const int16_t orbit = 58 - static_cast<int16_t>(progress * 18);
    for (uint8_t node = 0; node < 10; ++node) {
      const float phase = frame * 0.13f + node * 0.6283f;
      const int16_t y = 126 + node * 19;
      const int16_t offset = lroundf(sinf(phase) * orbit);
      const uint16_t nearColor = cosf(phase) > 0 ? COLOR_MINT : COLOR_PURPLE;
      display->drawLine(184 - offset, y, 184 + offset, y, COLOR_CARD);
      display->fillCircle(184 - offset, y, 5, nearColor);
      display->fillCircle(184 + offset, y, 5, COLOR_CYAN);
    }

    const int16_t ringRadius = 30 + static_cast<int16_t>(progress * 70);
    display->drawCircle(184, coreY, ringRadius, COLOR_CYAN);
    display->drawCircle(184, coreY, max<int16_t>(8, ringRadius - 7), COLOR_CARD);
    display->fillCircle(184, coreY, 12 + lroundf(sinf(frame * 0.22f) * 3), COLOR_MINT);

    const uint16_t progressWidth = static_cast<uint16_t>(286 * progress);
    display->fillRoundRect(41, 315, 286, 7, 3, COLOR_CARD);
    if (progressWidth) display->fillRoundRect(41, 315, progressWidth, 7, 3, COLOR_CYAN);
    const char *phaseText = progress < 0.28f ? "READING GENOME" :
                            progress < 0.62f ? "ASSEMBLING FORM" :
                            progress < 0.90f ? "STABILISING SIGNAL" :
                                               "LINK READY";
    display->fillRect(80, 361, 208, 18, COLOR_BACKGROUND);
    drawCentered(phaseText, 364, 1, progress > 0.89f ? COLOR_MINT : COLOR_MUTED);
    delay(35);
  }

  display->fillScreen(COLOR_BACKGROUND);
  display->fillCircle(184, 202, 88, COLOR_CARD);
  display->drawCircle(184, 202, 91, COLOR_CYAN);
  display->drawCircle(184, 202, 101, COLOR_PURPLE);
  display->fillCircle(184, 202, 54, COLOR_MINT);
  display->fillCircle(184, 202, 38, COLOR_BACKGROUND);
  display->fillCircle(184, 202, 15, COLOR_TEXT);
  drawCentered("DIGIPET", 315, 5, COLOR_TEXT);
  drawCentered("LIFE SIGNAL ONLINE", 377, 2, COLOR_CYAN);
  // The boot clip started above already covers this outro.
}

void bootAnimation() {
  if (!transitionsReady) {
    bootAnimationFallback();
    return;
  }

  const uint32_t animationStarted = millis();
  constexpr uint32_t BOOT_DURATION_MS = 5200;
  const PetPalette palette = paletteForGenome(pet.genome);
  panel->setBrightness(180);
  paintBootBackdrop(palette);
  pinMode(0, INPUT_PULLUP);  // lets a boot-key press fast-forward the sequence
  playBootClipAsync();

  const size_t frameBytes = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * sizeof(uint16_t);
  float progress = 0.0f;
  bool skipping = false;
  for (uint32_t frame = 0; ; ++frame) {
    if (!skipping) {
      const uint32_t elapsed = millis() - animationStarted;
      progress = min(1.0f, elapsed / static_cast<float>(BOOT_DURATION_MS));
      if (elapsed > 250 && digitalRead(0) == LOW) skipping = true;
    } else {
      progress = min(1.0f, progress + 0.05f);
    }

    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;
    memcpy(pageCanvasA.getFramebuffer(), pageCanvasB.getFramebuffer(), frameBytes);
    drawBootParticles(palette, frame);
    drawBootHelix(palette, frame, progress);
    drawBootCore(palette, frame, progress);
    drawBootTitle(progress);
    drawBootStatus(progress);
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);

    if (progress >= 1.0f) break;
    delay(18);
  }

  bootOutro(palette);
}

enum StatIcon : uint8_t { ICON_FOOD, ICON_JOY, ICON_ENERGY, ICON_HEALTH };

// Hand-pixelled 16x16 silhouettes, enlarged 2x for crisp 32x32 icons.
const uint16_t STAT_ICONS[4][16] PROGMEM = {
  {0x2C18, 0x2C18, 0x2C18, 0x2C18, 0x3C18, 0x1818, 0x1818, 0x1818,
   0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818, 0x1818}, // fork + knife
  {0x03C0, 0x0FF0, 0x1FF8, 0x381C, 0x6006, 0x6666, 0x6666, 0x6006,
   0x6186, 0x6306, 0x360C, 0x1FF8, 0x0FF0, 0x03C0, 0x0000, 0x0000}, // smile
  {0x0180, 0x0300, 0x0600, 0x0C00, 0x1FF0, 0x3FE0, 0x07C0, 0x0780,
   0x0F00, 0x1E00, 0x3C00, 0x7800, 0x3000, 0x2000, 0x0000, 0x0000}, // bolt
  {0x0000, 0x0C30, 0x1E78, 0x3FFC, 0x7FFE, 0x7FFE, 0x7FFE, 0x3FFC,
   0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180, 0x0000, 0x0000, 0x0000}  // heart
};

const char *STAT_LABELS[] = {"FOOD", "JOY", "ENERGY", "HEALTH"};

void drawStatIcon(StatIcon icon, int16_t x, int16_t y, uint16_t color) {
  for (uint8_t row = 0; row < 16; row++) {
    const uint16_t pixels = pgm_read_word(&STAT_ICONS[icon][row]);
    for (uint8_t column = 0; column < 16; column++) {
      if (pixels & (0x8000 >> column)) {
        display->fillRect(x + column * 2, y + row * 2, 2, 2, color);
      }
    }
  }
}

void drawStatRow(StatIcon icon, uint8_t value, int16_t y) {
  const uint16_t color = value < 25 ? COLOR_DANGER :
      (value < 50 ? COLOR_WARNING : COLOR_MINT);
  drawStatIcon(icon, 22, y - 17, color);
  display->setTextSize(1);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(59, y - 7);
  display->print(STAT_LABELS[icon]);
  display->drawRoundRect(112, y - 7, 168, 18, 8, COLOR_MUTED);
  if (value > 0) {
    const int16_t filled = (162 * value) / 100;
    display->fillRoundRect(115, y - 4, filled, 12, 6, color);
    if (filled > 6) display->fillCircle(115 + filled - 6, y + 2, 5, lerpRgb565(color, RGB565_WHITE, 0.35f));
  }
  display->setTextSize(2);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(294, y - 7);
  display->printf("%3u%%", value);
}

bool i2cPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// A single targeted probe for just the audio codec, so the boot jingle can
// start without waiting on the full 126-address scan below (deferred until
// after the boot animation, since nothing it finds is needed before then).
void detectAudioHardware() {
  codecDetected = i2cPresent(0x18);  // ES8311
}

void detectHardware() {
  i2cDeviceCount = 0;
  Serial.println("I2C hardware scan:");
  for (uint8_t address = 1; address < 127; address++) {
    if (i2cPresent(address)) {
      i2cDeviceCount++;
      Serial.printf("  found 0x%02X\n", address);
    }
  }
  imuDetected = i2cPresent(0x6A) || i2cPresent(0x6B);  // QMI8658
  rtcDetected = i2cPresent(0x51);                       // PCF85063
  pmuDetected = i2cPresent(0x34);                       // AXP2101
  codecDetected = i2cPresent(0x18);                     // ES8311
  Serial.printf("QMI8658:%s  RTC:%s  PMU:%s  CODEC:%s\n",
                imuDetected ? "yes" : "no", rtcDetected ? "yes" : "no",
                pmuDetected ? "yes" : "no", codecDetected ? "yes" : "no");
}

void drawButton(const char *label, int16_t x) {
  display->fillRoundRect(x, 375, 104, 54, 14, COLOR_CARD);
  display->drawRoundRect(x, 375, 104, 54, 14, COLOR_MINT);
  display->setTextSize(2);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(x + (52 - strlen(label) * 6), 393);
  display->print(label);
}

void drawEgg(bool frame, uint16_t bg) {
  const PetPalette palette = paletteForGenome(pet.genome);
  const uint8_t lineage = pet.genome.lineage % 10;
  const uint8_t phase = (millis() / 90) % 32;
  const int y = 218 + ((phase < 16 ? phase : 31 - phase) / 8);

  // A compact aura and contact shadow establish depth without redrawing the UI.
  for (uint8_t i = 0; i < 6; ++i) {
    const int angleIndex = (phase + i * 5) & 31;
    const int px = 184 + ((angleIndex < 16 ? angleIndex : 31 - angleIndex) - 8) * 8;
    const int py = y - 8 + ((phase * 7 + i * 19) % 126) - 63;
    display->fillCircle(px, py, i & 1 ? 2 : 3,
                        i & 1 ? palette.glow : palette.accent);
  }
  display->fillEllipse(184, y + 67, 72, 13, COLOR_CARD);
  display->fillEllipse(184, y + 65, 56, 7, palette.primaryDark);

  // Offset nested shells produce a shaded, rounded volume on the AMOLED.
  display->fillEllipse(184, y, 62, 79, COLOR_TEXT);
  display->fillEllipse(184, y, 58, 75, palette.primaryDark);
  display->fillEllipse(188, y - 3, 53, 70, palette.primary);
  display->fillEllipse(194, y - 11, 43, 57, palette.primary);
  display->fillEllipse(201, y - 22, 27, 37, palette.primaryLight);
  display->fillEllipse(169, y + 19, 18, 42, palette.secondary);
  display->fillEllipse(202, y - 31, 9, 17, palette.primaryLight);
  display->fillCircle(200, y - 40, 3, lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.7f));
  display->fillEllipse(205, y - 36, 3, 6, COLOR_TEXT);

  // Each lineage owns a visual language; genes vary placement and palette.
  switch (lineage) {
    case 0: {  // Ember: animated molten fractures.
      display->drawLine(171, y - 55, 183, y - 29, palette.glow);
      display->drawLine(183, y - 29, 173, y - 5, palette.glow);
      display->drawLine(173, y - 5, 188, y + 22, palette.accent);
      display->drawLine(188, y + 22, 180, y + 48, palette.glow);
      display->fillCircle(183, y - 29, frame ? 5 : 3, palette.accent);
      break;
    }
    case 1:  // Tidal: layered wave bands and bubbles.
      for (int row = -30; row <= 34; row += 18) {
        display->drawLine(151, y + row, 166, y + row - 5, palette.accent);
        display->drawLine(166, y + row - 5, 184, y + row + 2, palette.accent);
        display->drawLine(184, y + row + 2, 210, y + row - 4, palette.accent);
      }
      display->drawCircle(204, y + 30 - phase / 3, 5, palette.glow);
      display->drawCircle(164, y - 18 - phase / 4, 3, palette.glow);
      break;
    case 2:  // Verdant: branching leaf veins.
      display->drawLine(184, y + 53, 184, y - 48, palette.accent);
      for (int branch = -34; branch <= 34; branch += 17) {
        display->drawLine(184, y + branch, 158, y + branch - 14, palette.secondary);
        display->drawLine(184, y + branch - 7, 211, y + branch - 23, palette.accent);
      }
      display->fillTriangle(177, y - 65, 184, y - 82, 190, y - 62, palette.glow);
      break;
    case 3:  // Volt: angular circuit paths.
      display->drawLine(158, y - 42, 181, y - 42, palette.accent);
      display->drawLine(181, y - 42, 171, y - 15, palette.accent);
      display->drawLine(171, y - 15, 197, y - 15, palette.glow);
      display->drawLine(197, y - 15, 185, y + 17, palette.glow);
      display->drawLine(185, y + 17, 210, y + 17, palette.accent);
      for (int i = 0; i < 4; ++i)
        display->fillCircle(158 + i * 17, y + 42 - (i & 1) * 10, 3,
                            (phase + i) & 2 ? palette.glow : palette.accent);
      break;
    case 4:  // Umbral: iris-like sigil with an orbiting glint.
      display->fillEllipse(184, y + 1, 31, 19, palette.secondary);
      display->fillEllipse(184, y + 1, 20, 14, palette.accent);
      display->fillEllipse(184, y + 1, 8, 14, palette.primaryDark);
      display->fillCircle(188, y - 4, 3, palette.glow);
      display->drawCircle(184, y + 1, 37 + (frame ? 2 : 0), palette.glow);
      break;
    case 5:  // Digital: asymmetric data blocks and traces.
      for (uint8_t i = 0; i < 9; ++i) {
        const int bx = 151 + ((pet.genome.seed[0] >> (i * 3 % 24)) & 0x2F);
        const int by = y - 49 + ((pet.genome.seed[1] >> (i * 3 % 24)) & 0x5F);
        display->fillRect(bx, by, 5 + (i & 3) * 2, 5,
                          i & 1 ? palette.accent : palette.secondary);
      }
      display->drawRect(169, y - 25, 31, 46, palette.glow);
      display->fillRect(178, y - 16 + phase / 4, 13, 4, RGB565_WHITE);
      break;
    case 6:  // Crystal: luminous facets.
      display->fillTriangle(184, y - 59, 157, y - 4, 184, y + 9, palette.secondary);
      display->fillTriangle(184, y - 59, 214, y - 7, 184, y + 9, palette.accent);
      display->fillTriangle(157, y - 4, 184, y + 9, 166, y + 49, palette.primaryDark);
      display->fillTriangle(214, y - 7, 184, y + 9, 204, y + 46, palette.glow);
      display->drawLine(184, y - 59, 184, y + 55, RGB565_WHITE);
      break;
    case 7:  // Alloy: segmented armour shell.
      display->drawLine(145, y - 25, 223, y - 25, palette.accent);
      display->drawLine(135, y + 8, 232, y + 8, palette.secondary);
      display->drawLine(148, y + 40, 218, y + 40, palette.accent);
      display->drawLine(184, y - 71, 184, y + 62, palette.primaryDark);
      for (int sy : {-25, 8, 40}) {
        display->fillCircle(153, y + sy, 4, palette.glow);
        display->fillCircle(215, y + sy, 4, palette.glow);
      }
      break;
    case 8:  // Celestial: constellation joined across the shell.
      for (uint8_t i = 0; i < 6; ++i) {
        const int sx = 157 + ((pet.genome.seed[2] >> (i * 4)) & 0x37);
        const int sy = y - 47 + ((pet.genome.seed[3] >> (i * 4)) & 0x5F);
        display->fillCircle(sx, sy, i == (phase / 6) % 6 ? 4 : 2, palette.accent);
        if (i) {
          const int px = 157 + ((pet.genome.seed[2] >> ((i - 1) * 4)) & 0x37);
          const int py = y - 47 + ((pet.genome.seed[3] >> ((i - 1) * 4)) & 0x5F);
          display->drawLine(px, py, sx, sy, palette.glow);
        }
      }
      break;
    default:  // Primal: scale plates and a claw glyph.
      for (int row = -37; row <= 35; row += 18)
        for (int column = -24; column <= 24; column += 16)
          display->drawCircle(184 + column + ((row / 18) & 1) * 8,
                              y + row, 9, palette.secondary);
      display->drawLine(170, y - 38, 181, y + 32, palette.accent);
      display->drawLine(184, y - 42, 190, y + 31, palette.accent);
      display->drawLine(198, y - 35, 198, y + 28, palette.accent);
      break;
  }

  if (pet.genome.mutationGenes) {
    display->drawCircle(184, y, 66 + (frame ? 2 : 0), palette.glow);
  }
}

int geneScale(uint8_t gene, int minimum, int maximum) {
  return minimum + (static_cast<int>(gene) * (maximum - minimum)) / 255;
}

void drawElementAura(const PetPalette &palette, int cx, int cy, int radius,
                     uint8_t phase) {
  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t orbit = (phase + i * 4) & 31;
    const int wave = orbit < 16 ? orbit : 31 - orbit;
    const int x = cx + (wave - 8) * radius / 8;
    const int y = cy - radius + ((phase * 5 + i * 17) % (radius * 2));
    const uint16_t color = i & 1 ? palette.glow : palette.accent;
    switch (pet.genome.element % 6) {
      case 0:
        display->fillTriangle(x - 4, y + 6, x + 4, y + 6, x, y - 7, color);
        break;
      case 1:
        display->drawCircle(x, y, 3 + (i & 1), color);
        break;
      case 2:
        display->fillEllipse(x, y, 5, 2, color);
        break;
      case 3:
        display->drawLine(x - 4, y - 5, x + 1, y, color);
        display->drawLine(x + 1, y, x - 2, y + 6, color);
        break;
      case 4:
        display->fillCircle(x, y, 2 + (i & 1), color);
        display->drawCircle(x, y, 6, palette.secondary);
        break;
      default:
        display->fillRect(x - 3, y - 3, 6, 6, color);
        break;
    }
  }
}

void drawGenomeMarkings(const PetPalette &palette, int cx, int cy,
                        int width, int height, uint8_t stage, uint8_t markingGene) {
  switch (markingGene % 5) {
    case 0:  // stripes
      for (int y = cy - height / 4; y <= cy + height / 4; y += 13)
        display->fillRoundRect(cx - width / 3, y, width * 2 / 3, 4, 2,
                               palette.secondary);
      break;
    case 1:  // spots
      for (uint8_t i = 0; i < 5 + stage; ++i) {
        const int x = cx - width / 3 + ((pet.genome.seed[1] >> (i * 3 % 24)) %
                                        max(1, width * 2 / 3));
        const int y = cy - height / 4 + ((pet.genome.seed[2] >> (i * 3 % 24)) %
                                         max(1, height / 2));
        display->fillCircle(x, y, 3 + (i & 2), palette.secondary);
      }
      break;
    case 2:  // circuit
      display->drawLine(cx - width / 3, cy, cx - 8, cy, palette.accent);
      display->drawLine(cx - 8, cy, cx - 8, cy + height / 4, palette.accent);
      display->drawLine(cx - 8, cy + height / 4, cx + width / 3,
                        cy + height / 4, palette.accent);
      display->fillCircle(cx - width / 3, cy, 4, palette.glow);
      display->fillCircle(cx + width / 3, cy + height / 4, 4, palette.glow);
      break;
    case 3: {  // scales: overlapping two-tone shingles, sized to the body
      const int rows = max(2, height / 24);
      const int cols = max(3, width / 20);
      for (int row = -rows / 2; row <= rows / 2; ++row) {
        for (int column = -cols / 2; column <= cols / 2; ++column) {
          const int sx = cx + column * 11 + (row & 1) * 6;
          const int sy = cy + row * 10;
          display->fillCircle(sx, sy, 6, palette.primaryDark);
          display->fillCircle(sx, sy - 1, 4, palette.secondary);
        }
      }
      break;
    }
    default:  // elemental core
      display->drawCircle(cx, cy + 4, 14 + stage * 2, palette.accent);
      display->fillCircle(cx, cy + 4, 7 + stage, palette.glow);
      break;
  }
}

void drawGenomeFace(const PetPalette &palette, int cx, int cy, int headWidth,
                    bool blink, uint8_t stage, uint8_t faceGene) {
  const int spread = max(12, headWidth / 4);
  const int eyeWidth = 8 + (faceGene % 5);
  const int eyeHeight = 13 + ((faceGene >> 2) % 6);
  for (int direction : {-1, 1}) {
    const int x = cx + direction * spread;
    if (blink) {
      display->fillRoundRect(x - eyeWidth, cy, eyeWidth * 2, 4, 2,
                             palette.primaryDark);
    } else {
      display->fillEllipse(x, cy, eyeWidth, eyeHeight, COLOR_TEXT);
      display->fillEllipse(x + direction * 2, cy + 2, eyeWidth - 3,
                           eyeHeight - 4, palette.primaryDark);
      display->fillCircle(x + direction * 3, cy - 4, 3, palette.glow);
      display->fillCircle(x + direction * 4, cy - 6, 1, RGB565_WHITE);
    }
  }
  if ((faceGene & 1) == 0) {
    display->drawLine(cx - 10, cy + 24, cx, cy + 29, palette.primaryDark);
    display->drawLine(cx, cy + 29, cx + 10, cy + 24, palette.primaryDark);
  } else {
    display->fillTriangle(cx - 8, cy + 23, cx + 8, cy + 23,
                          cx, cy + 31, palette.primaryDark);
    if (stage >= 3) display->fillTriangle(cx - 5, cy + 24, cx, cy + 31,
                                          cx + 5, cy + 24, COLOR_TEXT);
  }
}

void drawProceduralCreature(bool asleep) {
  const PetPalette palette = paletteForGenome(pet.genome);
  const uint8_t stage = constrain(pet.stage, 1, 4);
  const uint8_t phase = (millis() / 45) & 31;
  const int wave = phase < 16 ? phase : 31 - phase;
  const int bob = asleep ? 5 : (wave - 8) / 4;
  const bool blink = asleep || (millis() % 3400) > 3260;
  const int cx = 184;
  const int baseY = 316 + bob;
  const int growth = (stage - 1) * 9;
  const uint8_t widthGene = evolvedGenomeGene(pet.genome, pet.genome.widthGene,
                                               stage, 0, 24);
  const uint8_t heightGene = evolvedGenomeGene(pet.genome, pet.genome.heightGene,
                                                stage, 1, 24);
  const uint8_t headGene = evolvedGenomeGene(pet.genome, pet.genome.headGene,
                                              stage, 2, 20);
  const uint8_t faceGene = evolvedGenomeGene(pet.genome, pet.genome.faceGene,
                                              stage, 3, 7);
  const uint8_t markingGene = evolvedGenomeGene(pet.genome, pet.genome.markingGene,
                                                 stage, 1, 6);
  const uint16_t featureGenes = evolvedGenomeFeatures(pet.genome, stage);
  const int bodyWidth = geneScale(widthGene, 82, 132) + growth;
  const int bodyHeight = geneScale(heightGene, 75, 126) + growth;
  const int headWidth = geneScale(headGene, 76, 118) + growth / 2;
  const int headHeight = headWidth * 4 / 5;
  const int headY = baseY - bodyHeight - headHeight / 2 + 24;
  const uint8_t bodyType = pet.genome.bodyType % 5;

  drawElementAura(palette, cx, baseY - 105, 90 + stage * 5, phase);

  // A soft life-signal glow bleeds out from behind the silhouette, echoing
  // the boot sequence's core so the creature reads as the same living thing.
  // Blended relative to the actual backdrop color so it always brightens,
  // even against pale palettes where a flat scaled tone can read as a smudge.
  const uint16_t glowBase = backdropColorAt(baseY - bodyHeight / 2);
  display->fillEllipse(cx, baseY - bodyHeight / 2, bodyWidth / 2 + 30,
                       bodyHeight / 2 + 26, lerpRgb565(glowBase, palette.glow, 0.22f));
  display->fillEllipse(cx, baseY - bodyHeight / 2, bodyWidth / 2 + 14,
                       bodyHeight / 2 + 12, lerpRgb565(glowBase, palette.glow, 0.4f));

  display->fillEllipse(cx, baseY + 6, 68 + growth, 10 + stage, COLOR_CARD);
  display->fillEllipse(cx + 7, baseY + 4, 48 + growth, 5 + stage / 2,
                       palette.primaryDark);

  // Rear silhouette: tails, wings and stage-dependent mutations.
  const int tailSwing = (wave - 8) * 3;
  if ((featureGenes & 0x20) || bodyType == 0 || bodyType == 4) {
    display->fillTriangle(cx + bodyWidth / 3, baseY - 64,
                          cx + bodyWidth / 2 + 54, baseY - 80 + tailSwing,
                          cx + bodyWidth / 2 - 3, baseY - 33,
                          COLOR_TEXT);
    display->fillTriangle(cx + bodyWidth / 3 + 3, baseY - 62,
                          cx + bodyWidth / 2 + 43, baseY - 77 + tailSwing,
                          cx + bodyWidth / 2 - 1, baseY - 39,
                          palette.secondary);
  }
  if ((featureGenes & 0x08) || bodyType == 2) {
    const int wingLift = asleep ? 8 : (wave - 8);
    display->fillTriangle(cx - bodyWidth / 3, baseY - 112,
                          cx - bodyWidth / 2 - 58, baseY - 144 - wingLift,
                          cx - bodyWidth / 2 + 5, baseY - 56, COLOR_TEXT);
    display->fillTriangle(cx + bodyWidth / 3, baseY - 112,
                          cx + bodyWidth / 2 + 58, baseY - 144 - wingLift,
                          cx + bodyWidth / 2 - 5, baseY - 56, COLOR_TEXT);
    display->fillTriangle(cx - bodyWidth / 3 - 3, baseY - 108,
                          cx - bodyWidth / 2 - 45, baseY - 137 - wingLift,
                          cx - bodyWidth / 2 + 8, baseY - 64, palette.secondary);
    display->fillTriangle(cx + bodyWidth / 3 + 3, baseY - 108,
                          cx + bodyWidth / 2 + 45, baseY - 137 - wingLift,
                          cx + bodyWidth / 2 - 8, baseY - 64, palette.secondary);
    // Feather seams: a couple of short strokes between tip and body so each
    // wing reads as layered feathers instead of one flat triangle.
    for (int direction : {-1, 1}) {
      const int tipX = cx + direction * (bodyWidth / 2 + 45);
      const int tipY = baseY - 137 - wingLift;
      const int rootX = cx + direction * (bodyWidth / 2 - 8);
      const int rootY = baseY - 64;
      for (int i = 1; i <= 2; ++i) {
        const int fx = tipX + (rootX - tipX) * i / 3;
        const int fy = tipY + (rootY - tipY) * i / 3;
        display->drawLine(fx, fy, fx + direction * 9, fy + 12, palette.primaryDark);
      }
    }
  }

  int bodyCx = cx;
  int bodyCy = baseY - bodyHeight / 2;
  if (bodyType == 4) {  // serpent: overlapping coils and raised torso
    display->fillEllipse(cx, baseY - 10, bodyWidth / 2 + 25, 25, COLOR_TEXT);
    display->fillEllipse(cx + 4, baseY - 12, bodyWidth / 2 + 17, 18,
                         palette.primaryDark);
    display->fillEllipse(cx - 25, baseY - 34, bodyWidth / 2, 31, COLOR_TEXT);
    display->fillEllipse(cx - 20, baseY - 37, bodyWidth / 2 - 6, 24,
                         palette.primary);
    bodyCy -= 8;
    display->fillRoundRect(cx - bodyWidth / 3, bodyCy - bodyHeight / 2,
                           bodyWidth * 2 / 3, bodyHeight, bodyWidth / 3, COLOR_TEXT);
    display->fillRoundRect(cx - bodyWidth / 3 + 6, bodyCy - bodyHeight / 2 + 5,
                           bodyWidth * 2 / 3 - 12, bodyHeight - 11,
                           bodyWidth / 3 - 5, palette.primary);
    display->fillEllipse(cx - bodyWidth / 10, bodyCy - bodyHeight / 4,
                         bodyWidth / 7, bodyHeight / 10, palette.primaryLight);
    display->fillCircle(cx - bodyWidth / 10 - 3, bodyCy - bodyHeight / 4 - 2, 2,
                        lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.6f));
  } else if (bodyType == 0) {  // quadruped: broad torso and four grounded legs
    bodyCy = baseY - 67;
    display->fillRoundRect(cx - bodyWidth / 2, bodyCy - bodyHeight / 3,
                           bodyWidth, bodyHeight * 2 / 3, bodyHeight / 3, COLOR_TEXT);
    display->fillRoundRect(cx - bodyWidth / 2 + 6, bodyCy - bodyHeight / 3 + 5,
                           bodyWidth - 12, bodyHeight * 2 / 3 - 10,
                           bodyHeight / 3 - 4, palette.primary);
    display->fillEllipse(cx - bodyWidth / 6, bodyCy - bodyHeight / 5,
                         bodyWidth / 5, bodyHeight / 9, palette.primaryLight);
    display->fillCircle(cx - bodyWidth / 6 - 3, bodyCy - bodyHeight / 5 - 2, 2,
                        lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.6f));
    for (int direction : {-1, 1}) {
      // The outer leg of each pair is drawn a touch smaller and higher than
      // the inner one, a cheap forced-perspective cue so four legs stamped
      // in a flat row still read as a body with some girth to it.
      for (int inner : {0, 1}) {
        const int legX = cx + direction * (bodyWidth / 4 + inner * 17);
        const int legW = inner ? 24 : 28;
        const int legH = inner ? 62 : 68;
        const int legTop = baseY - 67 + (inner ? 5 : 0);
        display->fillRoundRect(legX - legW / 2, legTop, legW, legH, 12, COLOR_TEXT);
        display->fillRoundRect(legX - legW / 2 + 5, legTop + 3, legW - 10, legH - 6,
                               8, inner ? palette.primaryDark : palette.primary);
        display->fillEllipse(legX + direction * 4, baseY - (inner ? 3 : 0), 18, 8,
                             COLOR_TEXT);
      }
    }
  } else if (bodyType == 2) {  // avian: tapered feather body and talons
    bodyCy = baseY - bodyHeight / 2;
    display->fillTriangle(cx, bodyCy - bodyHeight / 2, cx - bodyWidth / 2,
                          baseY - 29, cx, baseY - 2, COLOR_TEXT);
    display->fillTriangle(cx, bodyCy - bodyHeight / 2 + 7,
                          cx - bodyWidth / 2 + 8, baseY - 31,
                          cx, baseY - 10, palette.primary);
    display->fillTriangle(cx, bodyCy - bodyHeight / 2, cx + bodyWidth / 2,
                          baseY - 29, cx, baseY - 2, COLOR_TEXT);
    display->fillTriangle(cx, bodyCy - bodyHeight / 2 + 7,
                          cx + bodyWidth / 2 - 8, baseY - 31,
                          cx, baseY - 10, palette.primaryLight);
    for (int direction : {-1, 1}) {
      display->drawLine(cx + direction * 20, baseY - 25,
                        cx + direction * 24, baseY, COLOR_TEXT);
      display->drawLine(cx + direction * 24, baseY,
                        cx + direction * 35, baseY + 3, COLOR_TEXT);
    }
  } else if (bodyType == 3) {  // blob: teardrop body tapering toward the head
    bodyCy = baseY - bodyHeight / 2;
    display->fillEllipse(cx, bodyCy, bodyWidth / 2, bodyHeight / 2, COLOR_TEXT);
    display->fillEllipse(cx, bodyCy - bodyHeight / 3, bodyWidth / 3,
                         bodyHeight / 3, COLOR_TEXT);
    display->fillEllipse(cx + 5, bodyCy - 4, bodyWidth / 2 - 6,
                         bodyHeight / 2 - 6, palette.primary);
    display->fillEllipse(cx + 4, bodyCy - bodyHeight / 3 - 3, bodyWidth / 3 - 5,
                         bodyHeight / 3 - 5, palette.primary);
    display->fillEllipse(cx - bodyWidth / 6, bodyCy - bodyHeight / 5,
                         bodyWidth / 6, bodyHeight / 9, palette.primaryLight);
    display->fillCircle(cx - bodyWidth / 6 - 3, bodyCy - bodyHeight / 5 - 2, 2,
                        lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.6f));
    display->fillEllipse(cx - 24, baseY - 3, 25, 10, COLOR_TEXT);
    display->fillEllipse(cx + 24, baseY - 3, 25, 10, COLOR_TEXT);
  } else {  // humanoid: torso, articulated arms and legs
    bodyCy = baseY - bodyHeight / 2;
    display->fillRoundRect(cx - bodyWidth / 2, bodyCy - bodyHeight / 2,
                           bodyWidth, bodyHeight, bodyWidth / 4, COLOR_TEXT);
    display->fillRoundRect(cx - bodyWidth / 2 + 6, bodyCy - bodyHeight / 2 + 6,
                           bodyWidth - 12, bodyHeight - 12, bodyWidth / 4 - 3,
                           palette.primary);
    display->fillEllipse(cx - bodyWidth / 6, bodyCy - bodyHeight / 4,
                         bodyWidth / 6, bodyHeight / 10, palette.primaryLight);
    display->fillCircle(cx - bodyWidth / 6 - 3, bodyCy - bodyHeight / 4 - 2, 2,
                        lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.6f));
    for (int direction : {-1, 1}) {
      // Pushed out enough to leave a sliver of background between torso and
      // arm outlines, plus a joint accent, so they read as separate limbs
      // instead of fusing into one white silhouette.
      const int armX = cx + direction * (bodyWidth / 2 + 15);
      display->fillRoundRect(armX - 13, bodyCy - bodyHeight / 3,
                             26, bodyHeight * 2 / 3, 12, COLOR_TEXT);
      display->fillRoundRect(armX - 8, bodyCy - bodyHeight / 3 + 5,
                             16, bodyHeight * 2 / 3 - 10, 8, palette.secondary);
      display->fillCircle(cx + direction * (bodyWidth / 2 + 3),
                          bodyCy - bodyHeight / 3 + 8, 8, palette.accent);
      const int legX = cx + direction * bodyWidth / 4;
      display->fillRoundRect(legX - 17, baseY - 58, 34, 62, 13, COLOR_TEXT);
      display->fillRoundRect(legX - 11, baseY - 54, 22, 51, 9,
                             palette.primaryDark);
      display->fillEllipse(legX + direction * 5, baseY + 1, 22, 8, COLOR_TEXT);
    }
  }

  drawGenomeMarkings(palette, bodyCx, bodyCy, bodyWidth, bodyHeight, stage,
                     markingGene);

  // Evolved forms earn actual armor instead of just scaling up: a plate or
  // guard keyed to body type, with a glowing rivet once fully matured.
  if (stage >= 3) {
    if (bodyType == 0) {  // quadruped: shoulder guard over the front torso
      display->fillTriangle(bodyCx - bodyWidth / 2 - 4, bodyCy - bodyHeight / 3,
                            bodyCx, bodyCy - bodyHeight / 3 - 24,
                            bodyCx + bodyWidth / 2 + 4, bodyCy - bodyHeight / 3,
                            COLOR_TEXT);
      display->fillTriangle(bodyCx - bodyWidth / 2 + 3, bodyCy - bodyHeight / 3 - 3,
                            bodyCx, bodyCy - bodyHeight / 3 - 17,
                            bodyCx + bodyWidth / 2 - 3, bodyCy - bodyHeight / 3 - 3,
                            palette.accent);
    } else if (bodyType == 1) {  // humanoid: pauldrons over each shoulder
      for (int direction : {-1, 1}) {
        const int sx = cx + direction * (bodyWidth / 2 + 3);
        const int sy = bodyCy - bodyHeight / 3 + 8;
        display->fillCircle(sx, sy, 14, COLOR_TEXT);
        display->fillCircle(sx, sy, 10, palette.accent);
      }
    } else if (bodyType == 2) {  // avian: breastplate wedge
      display->fillTriangle(cx, bodyCy - bodyHeight / 2 - 6, cx - bodyWidth / 4,
                            bodyCy + 4, cx + bodyWidth / 4, bodyCy + 4, COLOR_TEXT);
      display->fillTriangle(cx, bodyCy - bodyHeight / 2, cx - bodyWidth / 5 + 2,
                            bodyCy - 2, cx + bodyWidth / 5 - 2, bodyCy - 2,
                            palette.accent);
    } else if (bodyType == 3) {  // blob: dorsal shell ridge
      display->fillEllipse(cx, bodyCy - bodyHeight / 3, bodyWidth / 3 + 4,
                           bodyHeight / 6 + 3, COLOR_TEXT);
      display->fillEllipse(cx, bodyCy - bodyHeight / 3 - 2, bodyWidth / 3 - 3,
                           bodyHeight / 6 - 2, palette.accent);
    } else {  // serpent: banded collar
      display->fillEllipse(cx, bodyCy - bodyHeight / 2 + 4, bodyWidth / 2 + 8, 11,
                           COLOR_TEXT);
      display->fillEllipse(cx, bodyCy - bodyHeight / 2 + 2, bodyWidth / 2 + 2, 7,
                           palette.accent);
    }
    if (stage >= 4) {
      const float platePulse = (sinf(phase * 0.2f) + 1.0f) * 0.5f;
      display->fillCircle(bodyCx, bodyCy - bodyHeight / 3, 3 + lroundf(platePulse * 2),
                          lerpRgb565(palette.glow, RGB565_WHITE, platePulse * 0.5f));
    }
  }

  // Head, ears/horns and foreground identity features.
  if ((featureGenes & 0x01) || stage >= 3) {
    // A tapered three-point crest, sized relative to the head so it scales
    // cleanly across stages instead of sprawling into competing shards.
    const int crownBase = headY - headHeight / 2 + 6;
    const int crownWidth = max(6, headWidth / 8);
    auto crestSpike = [&](int offsetX, int height) {
      const int bx = cx + offsetX;
      display->fillTriangle(bx - crownWidth, crownBase, bx + crownWidth, crownBase,
                            bx, crownBase - height, COLOR_TEXT);
      display->fillTriangle(bx - crownWidth + 3, crownBase - 2,
                            bx + crownWidth - 3, crownBase - 2,
                            bx, crownBase - height + 6, palette.accent);
    };
    crestSpike(0, headHeight / 2 + 10);
    crestSpike(-headWidth / 4, headHeight / 3 + 6);
    crestSpike(headWidth / 4, headHeight / 3 + 6);
  }
  display->fillEllipse(cx, headY, headWidth / 2 + 5, headHeight / 2 + 5, COLOR_TEXT);
  display->fillEllipse(cx + 4, headY - 3, headWidth / 2 - 2,
                       headHeight / 2 - 2, palette.primary);
  display->fillEllipse(cx + headWidth / 6, headY - headHeight / 6,
                       headWidth / 5, headHeight / 7, palette.primaryLight);
  display->fillCircle(cx + headWidth / 6 + 2, headY - headHeight / 6 - 3, 2,
                      lerpRgb565(palette.primaryLight, RGB565_WHITE, 0.6f));

  if ((featureGenes & 0x02) || bodyType == 2) {
    // Restrained, two-toned so they read as ears against pale palettes
    // instead of blending into the crown's shards.
    const int earSize = 15 + stage * 2;
    for (int direction : {-1, 1}) {
      const int bx = cx + direction * (headWidth / 2 - 4);
      const int by = headY - headHeight / 3;
      display->fillTriangle(bx, by + 10, bx + direction * earSize,
                            by - earSize * 3 / 4, bx + direction * 5, by - 4,
                            COLOR_TEXT);
      display->fillTriangle(bx + direction * 2, by + 5,
                            bx + direction * (earSize - 4), by - earSize * 3 / 4 + 4,
                            bx + direction * 4, by - 1, palette.secondary);
    }
  }
  if ((featureGenes & 0x80) && stage >= 2) {
    for (int direction : {-1, 1})
      display->fillTriangle(cx + direction * headWidth / 3, headY - headHeight / 3,
                            cx + direction * (headWidth / 2 + 15), headY - 8,
                            cx + direction * headWidth / 2, headY + 18,
                            palette.accent);
  }
  if (stage >= 3 || (featureGenes & 0x100)) {
    display->drawRoundRect(cx - headWidth / 2 + 3, headY - headHeight / 2 + 4,
                           headWidth - 6, headHeight - 8, headHeight / 3,
                           palette.accent);
  }
  drawGenomeFace(palette, cx, headY, headWidth, blink, stage, faceGene);

  if (bodyType == 2) {  // avian: a beak over the mouth position
    display->fillTriangle(cx - 9, headY + 20, cx + 9, headY + 20,
                          cx, headY + 32, COLOR_TEXT);
    display->fillTriangle(cx - 6, headY + 21, cx + 6, headY + 21,
                          cx, headY + 29, palette.accent);
  }

  if (pet.genome.mutationGenes) {
    display->drawCircle(cx, headY, headWidth / 2 + 12 + (wave > 7), palette.glow);
    if (pet.genome.mutationGenes & 0x02)
      display->fillCircle(cx, headY - headHeight / 2 - 15, 8, palette.glow);
  }
}

void drawCreature(bool frame, bool asleep) {
  const uint16_t bg = asleep ? RGB565_BLACK : COLOR_BACKGROUND;
  if (pet.stage == 0) {
    drawEgg(frame, bg);
    return;
  }
  drawProceduralCreature(asleep);
}

void drawPageDots(Page active) {
  const int16_t y = active == PAGE_STATUS ? 355 :
                    (active >= PAGE_SETTINGS ? 437 : 425);
  const Page pages[] = {PAGE_COMPANION, PAGE_STATUS, PAGE_BATTLE,
                        PAGE_SETTINGS, PAGE_GENOME_LAB};
  for (uint8_t i = 0; i < 5; ++i) {
    display->fillCircle(152 + i * 16, y, 4,
                        active == pages[i] ? COLOR_MINT : COLOR_CARD);
  }
}

void drawCompanionPage() {
  paintPageBackdrop();
  drawCentered("DIGIPET // 001", 15, 3, COLOR_MINT);
  display->setTextSize(1);
  display->setTextColor(COLOR_CYAN);
  display->setCursor(28, 49);
  display->print(pet.stage == 0 ? eggLineageName(pet.genome.lineage)
                                : STAGE_NAMES[pet.stage]);
  display->setTextSize(2);
  display->setTextColor(clockValid ? COLOR_TEXT : COLOR_MUTED);
  display->setCursor(288, 45);
  display->print(clockText);
  drawPanelGlow(20, 82, 328, 276, 32, COLOR_CYAN);
  display->drawRoundRect(20, 82, 328, 276, 32, COLOR_CARD);
  display->drawRoundRect(27, 89, 314, 262, 27, COLOR_CYAN);
  display->fillRect(14, 126, 18, 5, COLOR_PURPLE);
  display->fillRect(336, 309, 18, 5, COLOR_MINT);
  display->drawCircle(54, 111, 8, COLOR_MINT);
  display->drawCircle(332, 329, 8, COLOR_PURPLE);
  drawCreature(false, false);
  drawCentered("TAP PET FOR PROFILE", 374, 1, COLOR_MUTED);
  drawCentered("SWIPE FOR STATUS  >", 399, 1, COLOR_CYAN);
  drawPageDots(PAGE_COMPANION);
}

void drawStatusPage() {
  paintPageBackdrop();
  drawCentered("COMPANION STATUS", 18, 3, COLOR_MINT);
  char identity[40];
  if (pet.stage == 0) {
    snprintf(identity, sizeof(identity), "%s // %s",
             eggLineageName(pet.genome.lineage), elementName(pet.genome.element));
  } else {
    snprintf(identity, sizeof(identity), "%s // %s",
             STAGE_NAMES[pet.stage], elementName(pet.genome.element));
  }
  drawCentered(identity, 49, 1, COLOR_CYAN);
  drawStatRow(ICON_FOOD, pet.food, 82);
  drawStatRow(ICON_JOY, pet.joy, 128);
  drawStatRow(ICON_ENERGY, pet.energy, 174);
  drawStatRow(ICON_HEALTH, pet.health, 220);

  drawPanelGlow(25, 251, 318, 91, 18, COLOR_CYAN);
  display->fillRoundRect(25, 251, 318, 91, 18, COLOR_CARD);
  display->setTextSize(2);
  display->setCursor(45, 265);
  display->setTextColor(imuDetected ? COLOR_MINT : COLOR_DANGER);
  display->print(imuDetected ? "IMU OK" : "IMU --");
  display->setCursor(195, 265);
  display->setTextColor(rtcDetected ? COLOR_MINT : COLOR_DANGER);
  display->print(rtcDetected ? "RTC OK" : "RTC --");
  display->setCursor(45, 294);
  display->setTextColor(pmuDetected ? COLOR_MINT : COLOR_DANGER);
  display->print(pmuDetected ? "PWR OK" : "PWR --");
  display->setCursor(195, 294);
  display->setTextColor(codecDetected ? COLOR_MINT : COLOR_DANGER);
  display->print(codecDetected ? "SOUND OK" : "SOUND --");
  display->setTextSize(1);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(45, 324);
  display->printf("AGE %lumin  CARE %lu  TRAIN %u%%  //  %u DEV",
                  (unsigned long)pet.ageMinutes, (unsigned long)pet.actions,
                  pet.training, i2cDeviceCount);

  drawButton("FEED", 14);
  drawButton("PLAY", 132);
  drawButton("TRAIN", 250);
  drawPageDots(PAGE_STATUS);
}

void drawSettingsIcon(uint8_t item, int16_t cx, int16_t cy, uint16_t color) {
  switch (item) {
    case 0:  // brightness
      display->drawCircle(cx, cy, 15, color);
      display->fillCircle(cx, cy, 8, color);
      for (uint8_t i = 0; i < 8; ++i) {
        const float angle = i * 0.785398f;
        display->drawLine(cx + lroundf(cosf(angle) * 20), cy + lroundf(sinf(angle) * 20),
                          cx + lroundf(cosf(angle) * 27), cy + lroundf(sinf(angle) * 27), color);
      }
      break;
    case 1:  // idle timer
      display->drawCircle(cx, cy, 24, color);
      display->drawLine(cx, cy, cx, cy - 14, color);
      display->drawLine(cx, cy, cx + 12, cy + 7, color);
      break;
    case 2:  // speaker
      display->fillRect(cx - 25, cy - 9, 12, 18, color);
      display->fillTriangle(cx - 13, cy - 9, cx + 3, cy - 22, cx + 3, cy + 22, color);
      display->drawCircle(cx + 3, cy, 17, color);
      display->drawCircle(cx + 3, cy, 25, color);
      break;
    case 3:  // wake
      display->drawCircle(cx, cy, 24, color);
      display->fillCircle(cx, cy, 7, color);
      display->drawLine(cx, cy - 8, cx, cy - 27, color);
      display->drawLine(cx - 20, cy + 16, cx - 29, cy + 23, color);
      display->drawLine(cx + 20, cy + 16, cx + 29, cy + 23, color);
      break;
    case 4:  // theme palette
      display->drawCircle(cx, cy, 25, color);
      display->fillCircle(cx - 10, cy - 8, 5, COLOR_CYAN);
      display->fillCircle(cx + 8, cy - 11, 5, COLOR_PURPLE);
      display->fillCircle(cx + 13, cy + 7, 5, COLOR_WARNING);
      display->fillCircle(cx - 7, cy + 12, 5, COLOR_MINT);
      break;
    case 5:  // boot effect
      display->drawLine(cx, cy - 28, cx, cy + 28, color);
      display->drawLine(cx - 28, cy, cx + 28, cy, color);
      display->drawLine(cx - 19, cy - 19, cx + 19, cy + 19, color);
      display->drawLine(cx + 19, cy - 19, cx - 19, cy + 19, color);
      display->fillCircle(cx, cy, 8, color);
      break;
    case 6:  // identity card
      display->drawRoundRect(cx - 28, cy - 20, 56, 40, 6, color);
      display->fillCircle(cx - 14, cy - 5, 7, color);
      display->drawLine(cx - 23, cy + 12, cx - 5, cy + 12, color);
      display->drawLine(cx + 3, cy - 8, cx + 20, cy - 8, color);
      display->drawLine(cx + 3, cy + 2, cx + 20, cy + 2, color);
      break;
    default:  // update
      display->drawLine(cx, cy - 25, cx, cy + 13, color);
      display->fillTriangle(cx - 15, cy + 4, cx + 15, cy + 4, cx, cy + 23, color);
      display->drawLine(cx - 25, cy + 28, cx + 25, cy + 28, color);
      break;
  }
}

void drawSettingsTile(uint8_t item, int16_t x, int16_t y,
                      const char *label, const char *value) {
  const uint16_t accent = item & 1 ? COLOR_PURPLE : COLOR_CYAN;
  drawPanelGlow(x, y, 160, 137, 20, accent);
  display->fillRoundRect(x, y, 160, 137, 20, COLOR_CARD);
  display->drawRoundRect(x, y, 160, 137, 20, accent);
  drawSettingsIcon(item, x + 80, y + 43, COLOR_MINT);
  display->setTextSize(1);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(x + 16, y + 82);
  display->print(label);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(x + 16, y + 104);
  display->print(value);
}

void drawCenteredInRect(const char *text, int16_t x, int16_t y,
                        int16_t width, int16_t height, uint8_t size,
                        uint16_t color) {
  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  display->setTextSize(size);
  display->getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display->setTextColor(color);
  display->setCursor(x + (width - textWidth) / 2, y + (height - textHeight) / 2);
  display->print(text);
}

void drawSettingsBack() {
  drawPanelGlow(84, 385, 200, 43, 14, COLOR_CYAN);
  display->fillRoundRect(84, 385, 200, 43, 14, COLOR_CARD);
  display->drawRoundRect(84, 385, 200, 43, 14, COLOR_CYAN);
  drawCenteredInRect("<  BACK", 84, 385, 200, 43, 2, COLOR_TEXT);
}

void drawChoiceRow(const char *label, int16_t y, bool selected) {
  display->fillRoundRect(28, y, 312, 48, 14,
                         selected ? COLOR_PURPLE : COLOR_CARD);
  display->drawRoundRect(28, y, 312, 48, 14,
                         selected ? COLOR_MINT : COLOR_MUTED);
  display->setTextSize(2);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(47, y + 17);
  display->print(label);
  if (selected) {
    display->fillCircle(316, y + 24, 8, COLOR_MINT);
    display->fillCircle(316, y + 24, 3, COLOR_BACKGROUND);
  }
}

void drawSettingsControlPage() {
  paintPageBackdrop();
  const char *titles[] = {"SETTINGS", "BRIGHTNESS", "IDLE TIMER", "VOLUME",
                          "WAKE MODE", "THEME", "BOOT EFFECT"};
  drawCentered(titles[settingsView], 18, 3, COLOR_MINT);
  drawCentered("SELECT AN OPTION", 51, 1, COLOR_CYAN);

  if (settingsView == SETTINGS_BRIGHTNESS) {
    for (uint8_t value = 0; value <= 10; ++value) {
      const uint8_t column = value % 3;
      const uint8_t row = value / 3;
      const int16_t x = 22 + column * 113;
      const int16_t y = 83 + row * 68;
      const bool selected = settings.brightnessIndex == value;
      display->fillRoundRect(x, y, 98, 53, 13,
                             selected ? COLOR_PURPLE : COLOR_CARD);
      display->drawRoundRect(x, y, 98, 53, 13,
                             selected ? COLOR_MINT : COLOR_MUTED);
      char percent[8];
      snprintf(percent, sizeof(percent), "%u%%", value * 10);
      drawCenteredInRect(percent, x, y, 98, 53, 2, COLOR_TEXT);
    }
  } else if (settingsView == SETTINGS_IDLE) {
    for (uint8_t i = 0; i < 4; ++i)
      drawChoiceRow(SLEEP_LABELS[i], 89 + i * 62, settings.sleepIndex == i);
  } else if (settingsView == SETTINGS_VOLUME) {
    for (uint8_t i = 0; i < 5; ++i)
      drawChoiceRow(VOLUME_LABELS[i], 78 + i * 57, settings.volumeIndex == i);
  } else if (settingsView == SETTINGS_WAKE) {
    drawChoiceRow("TOUCH TO WAKE", 121, settings.wakeMode == 0);
    drawChoiceRow("BOOT KEY TO WAKE", 190, settings.wakeMode == 1);
  } else if (settingsView == SETTINGS_THEME) {
    for (uint8_t i = 0; i < 5; ++i)
      drawChoiceRow(THEME_LABELS[i], 78 + i * 57, settings.themeIndex == i);
  } else if (settingsView == SETTINGS_BOOT) {
    drawChoiceRow("BOOT EFFECT ON", 130, settings.bootAnimationEnabled);
    drawChoiceRow("BOOT EFFECT OFF", 199, !settings.bootAnimationEnabled);
  }
  drawSettingsBack();
}

void drawSettingsPage() {
  if (settingsView != SETTINGS_HOME) {
    drawSettingsControlPage();
    return;
  }
  paintPageBackdrop();
  drawCentered("DEVICE SETTINGS", 18, 3, COLOR_MINT);
  drawCentered(settingsGridPage == 0 ? "CORE CONTROLS // SWIPE UP" :
                                      "SYSTEM OPTIONS // SWIPE DOWN",
               50, 1, COLOR_CYAN);

  if (settingsGridPage == 0) {
    char brightness[12];
    snprintf(brightness, sizeof(brightness), "%u%%", brightnessPercent());
    drawSettingsTile(0, 18, 78, "BRIGHTNESS", brightness);
    drawSettingsTile(1, 190, 78, "IDLE TIMER", SLEEP_LABELS[settings.sleepIndex]);
    drawSettingsTile(2, 18, 227, "VOLUME", VOLUME_LABELS[settings.volumeIndex]);
    drawSettingsTile(3, 190, 227, "WAKE MODE", WAKE_LABELS[settings.wakeMode]);
  } else {
    drawSettingsTile(4, 18, 78, "THEME", THEME_LABELS[settings.themeIndex]);
    drawSettingsTile(5, 190, 78, "BOOT EFFECT", settings.bootAnimationEnabled ? "ENABLED" : "DISABLED");
    drawSettingsTile(6, 18, 227, "PLAYER ID", "VIEW IDENTITY");
    drawSettingsTile(7, 190, 227, "FIRMWARE", "CHECK UPDATE");
  }
  drawCentered(settingsGridPage == 0 ? "1  /  2" : "2  /  2", 382, 1, COLOR_MUTED);
  drawPageDots(PAGE_SETTINGS);
}

void drawGenomeLabPage() {
  static const char *bodyNames[] = {"QUADRUPED", "HUMANOID", "AVIAN", "BLOB", "SERPENT"};
  static const char *temperaments[] = {"CALM", "BOLD", "CURIOUS", "LOYAL", "WILD", "CLEVER"};
  paintPageBackdrop();
  drawCentered("GENOME LAB", 15, 3, COLOR_MINT);
  drawCentered(hasCopiedGenome ? "COPIED GENOME READY" :
                                "IMPORT A GENOME TO CLONE OR BLEND",
               48, 1, COLOR_CYAN);

  drawPanelGlow(20, 74, 328, 244, 28, COLOR_PURPLE);
  display->drawRoundRect(20, 74, 328, 244, 28, COLOR_CARD);
  display->drawRoundRect(27, 81, 314, 230, 23, COLOR_PURPLE);
  drawEgg(animationFrame, COLOR_BACKGROUND);

  char fingerprint[28];
  snprintf(fingerprint, sizeof(fingerprint), "DESIGN %016llX",
           static_cast<unsigned long long>(petGenomeDesignId(pet.genome)));
  display->fillRoundRect(22, 325, 324, 43, 13, COLOR_CARD);
  display->setTextSize(1);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(34, 337);
  display->printf("%s  //  %s", eggLineageName(pet.genome.lineage),
                  elementName(pet.genome.element));
  display->setTextColor(COLOR_MUTED);
  display->setCursor(34, 352);
  display->printf("%s  %s  %s", bodyNames[pet.genome.bodyType % 5],
                  temperaments[pet.genome.temperament % 6], fingerprint);

  const bool confirming = newEggConfirmation && millis() < newEggConfirmationUntil;
  const char *labels[] = {"RANDOM", "CLONE", "BLEND"};
  for (uint8_t i = 0; i < 3; ++i) {
    const uint8_t mode = i + 1;
    const int16_t x = 11 + i * 119;
    const bool available = mode == 1 || hasCopiedGenome;
    const bool selected = confirming && pendingHatchMode == mode;
    display->fillRoundRect(x, 378, 108, 46, 13,
                           !available ? COLOR_MUTED :
                           (selected ? COLOR_DANGER : COLOR_PURPLE));
    drawCenteredInRect(selected ? "CONFIRM" : labels[i], x, 378, 108, 46,
                       1, COLOR_TEXT);
  }
  drawPageDots(PAGE_GENOME_LAB);
}

void beginNewEgg(uint8_t mode) {
  PetGenome nextGenome = generatePetGenome();
  if (mode == 2 && hasCopiedGenome) nextGenome = copiedGenome;
  if (mode == 3 && hasCopiedGenome)
    nextGenome = blendPetGenomes(pet.genome, copiedGenome);
  pet = {PET_MAGIC, 0, 0, 82, 82, 82, 100, 0, 0, {5, 0},
         nextGenome};
  savePet();
  if (settings.themeIndex == 0) applyTheme();
  newEggConfirmation = false;
  pendingHatchMode = 0;
  newEggConfirmationUntil = 0;
  snprintf(message, sizeof(message), "%s signal acquired",
           eggLineageName(pet.genome.lineage));
  playTone(392, 70, 40);
  playTone(523, 70, 45);
  playTone(784, 130, 50);
  Serial.printf("Genome Lab: started %s, element=%s\n",
                eggLineageName(pet.genome.lineage),
                elementName(pet.genome.element));
}

void drawGenomeProfilePage() {
  static const char *bodyNames[] = {"QUADRUPED", "HUMANOID", "AVIAN", "BLOB", "SERPENT"};
  static const char *temperaments[] = {"CALM", "BOLD", "CURIOUS", "LOYAL", "WILD", "CLEVER"};
  char code[PET_GENOME_CODE_LENGTH + 1]{};
  encodePetGenome(pet.genome, code, sizeof(code));
  paintPageBackdrop();
  drawCentered("GENOME PROFILE", 18, 3, COLOR_MINT);
  drawCentered(genomeTransferStatus, 52, 1, COLOR_CYAN);

  drawPanelGlow(22, 78, 324, 143, 20, COLOR_CYAN);
  display->fillRoundRect(22, 78, 324, 143, 20, COLOR_CARD);
  display->setTextSize(1);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(39, 95); display->print("FORM / ELEMENT");
  display->setTextColor(COLOR_TEXT);
  display->setCursor(39, 111);
  display->printf("%s // %s", STAGE_NAMES[pet.stage], elementName(pet.genome.element));
  display->setTextColor(COLOR_MUTED);
  display->setCursor(39, 137); display->print("BODY / TEMPERAMENT");
  display->setTextColor(COLOR_TEXT);
  display->setCursor(39, 153);
  display->printf("%s // %s", bodyNames[pet.genome.bodyType % 5],
                  temperaments[pet.genome.temperament % 6]);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(39, 179); display->print("HERITABLE TRAITS");
  display->setTextColor(COLOR_TEXT);
  display->setCursor(39, 195);
  display->printf("%u FEATURES // %s", __builtin_popcount(pet.genome.featureGenes),
                  pet.genome.mutationGenes ? "RARE MUTATION" : "STABLE");

  drawPanelGlow(22, 234, 324, 94, 18, COLOR_PURPLE);
  display->fillRoundRect(22, 234, 324, 94, 18, COLOR_CARD);
  display->setTextSize(1);
  display->setTextColor(COLOR_CYAN);
  for (uint8_t row = 0; row < 3; ++row) {
    char segment[21]{};
    memcpy(segment, code + row * 20, 20);
    display->setCursor(64, 250 + row * 22);
    display->print(segment);
  }
  display->setTextColor(COLOR_MUTED);
  display->setCursor(48, 312);
  display->printf("DESIGN %016llX", static_cast<unsigned long long>(petGenomeDesignId(pet.genome)));

  display->fillRoundRect(18, 344, 160, 40, 12, COLOR_PURPLE);
  display->fillRoundRect(190, 344, 160, 40, 12, COLOR_CYAN);
  drawCenteredInRect("EXPORT", 18, 344, 160, 40, 2, COLOR_TEXT);
  drawCenteredInRect("IMPORT COPY", 190, 344, 160, 40, 1, COLOR_BACKGROUND);
  display->fillRoundRect(84, 398, 200, 36, 12, COLOR_CARD);
  drawCenteredInRect("BACK", 84, 398, 200, 36, 2, COLOR_TEXT);
}

void presentGenomeProfilePage() {
  if (transitionsReady) {
    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;
    drawGenomeProfilePage();
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawGenomeProfilePage();
  }
}

void drawPlayerIdPage() {
  paintPageBackdrop();
  drawCentered("PLAYER IDENTITY", 22, 3, COLOR_MINT);
  drawCentered("SHA-256 // BOOT SESSION", 58, 1, COLOR_CYAN);
  drawPanelGlow(20, 93, 328, 218, 24, COLOR_PURPLE);
  display->fillRoundRect(20, 93, 328, 218, 24, COLOR_CARD);
  display->drawRoundRect(27, 100, 314, 204, 19, COLOR_PURPLE);
  display->setTextSize(2);
  display->setTextColor(COLOR_TEXT);
  for (uint8_t row = 0; row < 4; ++row) {
    char segment[17]{};
    memcpy(segment, playerIdHex + row * 16, 16);
    display->setCursor(88, 127 + row * 42);
    display->print(segment);
  }
  display->setTextSize(1);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(45, 330);
  display->printf("UTC SOURCE: %llu", static_cast<unsigned long long>(playerIdTimestamp));
  display->fillRoundRect(84, 375, 200, 48, 15, COLOR_CYAN);
  drawCentered("BACK TO SETTINGS", 393, 2, COLOR_BACKGROUND);
}

void presentPlayerIdPage() {
  if (transitionsReady) {
    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;
    drawPlayerIdPage();
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawPlayerIdPage();
  }
}

void drawUpdatePage(const char *status, int progress) {
  paintPageBackdrop();
  drawCentered("SIGNED UPDATE", 30, 3, COLOR_MINT);
  drawCentered("DIGIPET OTA // ECDSA P-256", 68, 1, COLOR_CYAN);
  drawPanelGlow(24, 110, 320, 208, 26, COLOR_PURPLE);
  display->fillRoundRect(24, 110, 320, 208, 26, COLOR_CARD);
  display->drawRoundRect(32, 118, 304, 192, 20, COLOR_PURPLE);
  display->drawCircle(184, 184, 43, COLOR_CYAN);
  display->drawCircle(184, 184, 29, COLOR_MINT);
  display->fillCircle(184, 184, 10, progress >= 0 ? COLOR_MINT : COLOR_WARNING);
  drawCentered(status, 249, 1, COLOR_TEXT);
  if (progress >= 0) {
    display->drawRoundRect(52, 280, 264, 17, 7, COLOR_MUTED);
    if (progress) display->fillRoundRect(55, 283, 258 * progress / 100, 11, 5, COLOR_MINT);
    char percent[8];
    snprintf(percent, sizeof(percent), "%d%%", progress);
    drawCentered(percent, 332, 2, COLOR_CYAN);
  } else {
    drawCentered("TAP TO RETURN", 343, 2, COLOR_CYAN);
  }
  display->setTextSize(1);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(125, 410);
  display->printf("INSTALLED v%s", DIGIPET_VERSION);
}

void presentUpdatePage(const char *status, int progress) {
  if (transitionsReady) {
    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;
    drawUpdatePage(status, progress);
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawUpdatePage(status, progress);
  }
}

void drawEvolutionDebugPage() {
  paintPageBackdrop();
  drawCentered("EVOLUTION DEBUG", 24, 3, COLOR_WARNING);
  drawCentered("DEVELOPMENT CONTROL", 61, 1, COLOR_MUTED);
  drawPanelGlow(24, 96, 320, 174, 24, COLOR_PURPLE);
  display->fillRoundRect(24, 96, 320, 174, 24, COLOR_CARD);
  display->drawRoundRect(31, 103, 306, 160, 19, COLOR_PURPLE);
  drawCentered("CURRENT FORM", 122, 1, COLOR_CYAN);
  drawCentered(STAGE_NAMES[pet.stage], 148, 3, COLOR_TEXT);
  drawCentered(pet.stage < 4 ? "NEXT EXPRESSION" : "FINAL EXPRESSION", 198, 1,
               COLOR_CYAN);
  drawCentered(pet.stage < 4 ? STAGE_NAMES[pet.stage + 1] : "MAXIMUM STAGE",
               222, 2, pet.stage < 4 ? COLOR_MINT : COLOR_WARNING);

  display->fillRoundRect(48, 296, 272, 58, 16,
                         pet.stage < 4 ? COLOR_DANGER : COLOR_MUTED);
  drawCentered(pet.stage < 4 ? "ADVANCE STAGE" : "MAX STAGE", 316, 2,
               COLOR_TEXT);
  display->fillRoundRect(92, 375, 184, 47, 14, COLOR_CARD);
  display->drawRoundRect(92, 375, 184, 47, 14, COLOR_CYAN);
  drawCentered("CANCEL", 391, 2, COLOR_TEXT);
}

void presentEvolutionDebugPage() {
  if (transitionsReady) {
    Arduino_GFX *previousDisplay = display;
    display = &pageCanvasA;
    drawEvolutionDebugPage();
    display = previousDisplay;
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(),
                              LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawEvolutionDebugPage();
  }
}

void debugAdvanceEvolution() {
  if (pet.stage >= 4) return;
  pet.stage++;
  savePet();
  snprintf(message, sizeof(message), "Debug form: %s", STAGE_NAMES[pet.stage]);
  playTone(440, 60, 40);
  playTone(659, 70, 45);
  playTone(880, 110, 50);
  Serial.printf("Evolution Debug: advanced to %s\n", STAGE_NAMES[pet.stage]);
}

void drawBattleHp(int16_t x, int16_t y, uint16_t hp, uint16_t maxHp,
                  uint16_t color) {
  display->drawRoundRect(x, y, 144, 17, 7, COLOR_MUTED);
  if (hp && maxHp) {
    display->fillRoundRect(x + 3, y + 3, 138 * hp / maxHp, 11, 5, color);
  }
}

void drawBattlePage() {
  paintPageBackdrop();
  drawCentered("LINK BATTLE", 16, 3, COLOR_MINT);
  display->setTextSize(1);
  display->setTextColor(COLOR_CYAN);
  display->setCursor(24, 51);
  display->printf("LV 5  HP 35  ATK %u  DEF %u",
                  FamiliarBattleService::deriveAttack(5, pet.stage),
                  FamiliarBattleService::deriveDefense(5, pet.stage));

  const FamiliarBattleState state = battle.state();
  if (state == FamiliarBattleState::Idle) {
    drawPanelGlow(22, 82, 324, 212, 26, COLOR_CYAN);
    display->fillRoundRect(22, 82, 324, 212, 26, COLOR_CARD);
    drawCentered("DIRECT CHALLENGE", 106, 2, COLOR_TEXT);
    drawCentered("CROSS-DEVICE BLE PROTOCOL", 139, 1, COLOR_MUTED);
    display->fillRoundRect(43, 180, 126, 62, 16, COLOR_PURPLE);
    display->fillRoundRect(199, 180, 126, 62, 16, COLOR_CYAN);
    drawCentered("HOST        FIND", 201, 2, COLOR_TEXT);
    drawCentered(battle.status().c_str(), 269, 1, COLOR_WARNING);
    drawCentered("HOST WAITS // FIND SCANS", 326, 1, COLOR_MUTED);
  } else if (state == FamiliarBattleState::Hosting ||
             state == FamiliarBattleState::Connecting) {
    display->drawCircle(184, 184, 72, COLOR_CYAN);
    display->drawCircle(184, 184, 51, COLOR_PURPLE);
    display->fillCircle(184, 184, 15, COLOR_MINT);
    drawCentered(state == FamiliarBattleState::Hosting ? "SIGNAL OPEN" : "HANDSHAKE",
                 278, 2, COLOR_TEXT);
    drawCentered(battle.status().c_str(), 310, 1, COLOR_MUTED);
    drawCentered("TAP BELOW TO CANCEL", 367, 1, COLOR_WARNING);
  } else {
    display->setTextSize(2);
    display->setTextColor(COLOR_TEXT);
    display->setCursor(24, 79); display->print("YOU");
    display->setCursor(242, 79); display->printf("OPP LV%u", battle.opponent().level);
    drawBattleHp(24, 105, battle.myHp(), battle.myMaxHp(), COLOR_MINT);
    drawBattleHp(200, 105, battle.opponentHp(), battle.opponentMaxHp(), COLOR_DANGER);
    display->setTextSize(1);
    display->setCursor(24, 128); display->printf("%u / %u", battle.myHp(), battle.myMaxHp());
    display->setCursor(268, 128); display->printf("%u / %u", battle.opponentHp(), battle.opponentMaxHp());
    drawCentered(battle.negotiatedCapabilities() ? "ENHANCED LINK" : "CORE LINK",
                 145, 1, battle.negotiatedCapabilities() ? COLOR_MINT : COLOR_MUTED);
    drawPanelGlow(24, 158, 320, 105, 22, COLOR_PURPLE);
    display->fillRoundRect(24, 158, 320, 105, 22, COLOR_CARD);
    drawCentered(state == FamiliarBattleState::Result ? "BATTLE COMPLETE" : "VS", 184, 3,
                 state == FamiliarBattleState::Result ? COLOR_WARNING : COLOR_PURPLE);
    const auto &log = battle.log();
    const String battleLine = log.empty() ? String("CHOOSE YOUR MOVE")
                                          : log.back().substring(0, 42);
    drawCentered(battleLine.c_str(), 238, 1, COLOR_TEXT);
    if (state == FamiliarBattleState::Battling) {
      const char *labels[] = {"ATK", "DEF", "SPEC", "FLEE"};
      const uint16_t colors[] = {COLOR_DANGER, COLOR_CYAN, COLOR_PURPLE, COLOR_MUTED};
      for (uint8_t i = 0; i < 4; ++i) {
        display->fillRoundRect(10 + i * 90, 305, 78, 55, 13, colors[i]);
        display->setTextSize(1); display->setTextColor(COLOR_TEXT);
        display->setCursor(35 + i * 90, 329); display->print(labels[i]);
      }
      drawCentered(battle.myMoveSubmitted() ? "WAITING FOR OPPONENT" : "SELECT MOVE",
                   378, 1, battle.myMoveSubmitted() ? COLOR_WARNING : COLOR_MINT);
    } else {
      if (battle.opponentGenomeCodeAvailable()) {
        display->fillRoundRect(18, 305, 160, 54, 14, COLOR_PURPLE);
        display->fillRoundRect(190, 305, 160, 54, 14, COLOR_CYAN);
        drawCenteredInRect(battleGenomeCopied ? "COPIED" : "COPY GENOME",
                           18, 305, 160, 54, 1, COLOR_TEXT);
        drawCenteredInRect("RETURN", 190, 305, 160, 54, 2, COLOR_BACKGROUND);
      } else {
        drawCentered("TAP TO RETURN", 330, 2, COLOR_CYAN);
      }
    }
  }
  drawPageDots(PAGE_BATTLE);
}

void drawHome() {
  if (currentPage == PAGE_COMPANION) drawCompanionPage();
  else if (currentPage == PAGE_STATUS) drawStatusPage();
  else if (currentPage == PAGE_BATTLE) drawBattlePage();
  else if (currentPage == PAGE_SETTINGS) drawSettingsPage();
  else drawGenomeLabPage();
}

void renderPageToCanvas(Page page, Arduino_Canvas &canvas) {
  Arduino_GFX *previousDisplay = display;
  display = &canvas;
  if (page == PAGE_COMPANION) drawCompanionPage();
  else if (page == PAGE_STATUS) drawStatusPage();
  else if (page == PAGE_BATTLE) drawBattlePage();
  else if (page == PAGE_SETTINGS) drawSettingsPage();
  else drawGenomeLabPage();
  display = previousDisplay;
}

// Debug-only: lets a connected dev machine pull the actual rendered
// framebuffer back over serial (`DUMP [bodyType] [lineage] [stage]
// [markingGene] [faceGene] [featureGenes]`), so creature art can be tuned
// against a real screenshot instead of guesswork. Renders into the scratch
// canvas so the live display is never disturbed; every override is applied
// to a copy of the live genome and restored immediately after.
void serviceSerialDebug() {
  if (!transitionsReady || !Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "WHOAMI") {
    Serial.printf("WHOAMI stage=%u age=%lu actions=%lu bodyType=%u lineage=%u "
                  "design=%016llX\n",
                  pet.stage, (unsigned long)pet.ageMinutes,
                  (unsigned long)pet.actions, pet.genome.bodyType,
                  pet.genome.lineage,
                  static_cast<unsigned long long>(petGenomeDesignId(pet.genome)));
    Serial.flush();
    return;
  }
  if (line.startsWith("DUMPBIOS")) {
    int phase = 0;
    sscanf(line.c_str(), "DUMPBIOS %d", &phase);
    const uint8_t savedCount = bootLogCount;
    BootLogEntry savedLog[BOOT_LOG_MAX];
    memcpy(savedLog, bootLog, sizeof(bootLog));

    bootLogCount = 0;
    if (phase == 1) {
      bootLogPush("LINK", "OK", COLOR_MINT);
      bootLogPush("TIME SYNC", "OK", COLOR_MINT);
      bootLogPush("SYSTEM", "READY", COLOR_MINT);
    } else if (phase == 2) {
      bootLogPush("LINK", "OFFLINE", COLOR_WARNING);
    } else {
      bootLogPush("LINK", "OK", COLOR_MINT);
      bootLogPush("TIME SYNC", "SYNCING", COLOR_CYAN);
      // bootLogSpin() draws straight to the live panel by design; redirect
      // it into the canvas just for this preview so it's capturable too.
      Arduino_GFX *previousDisplay = display;
      display = &pageCanvasA;
      bootLogSpin(2);
      display = previousDisplay;
    }

    Serial.printf("FBDUMP %d %d\n", LCD_WIDTH, LCD_HEIGHT);
    Serial.write(reinterpret_cast<uint8_t *>(pageCanvasA.getFramebuffer()),
                 static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * sizeof(uint16_t));
    Serial.flush();

    memcpy(bootLog, savedLog, sizeof(bootLog));
    bootLogCount = savedCount;
    return;
  }
  if (!line.startsWith("DUMP")) return;

  int bodyType = -1, lineage = -1, stage = -1, markingGene = -1, faceGene = -1,
      featureGenes = -1;
  sscanf(line.c_str(), "DUMP %d %d %d %d %d %d", &bodyType, &lineage, &stage,
         &markingGene, &faceGene, &featureGenes);

  const PetGenome savedGenome = pet.genome;
  const uint8_t savedStage = pet.stage;
  if (bodyType >= 0) pet.genome.bodyType = static_cast<uint8_t>(bodyType);
  if (lineage >= 0) pet.genome.lineage = static_cast<uint8_t>(lineage);
  if (stage >= 0) pet.stage = static_cast<uint8_t>(stage);
  if (markingGene >= 0) pet.genome.markingGene = static_cast<uint8_t>(markingGene);
  if (faceGene >= 0) pet.genome.faceGene = static_cast<uint8_t>(faceGene);
  if (featureGenes >= 0) pet.genome.featureGenes = static_cast<uint16_t>(featureGenes);

  renderPageToCanvas(PAGE_COMPANION, pageCanvasB);

  pet.genome = savedGenome;
  pet.stage = savedStage;

  Serial.printf("FBDUMP %d %d\n", LCD_WIDTH, LCD_HEIGHT);
  Serial.write(reinterpret_cast<uint8_t *>(pageCanvasB.getFramebuffer()),
               static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * sizeof(uint16_t));
  Serial.flush();
}

void presentCoherentPageFrame(Page page) {
  if (!transitionsReady) {
    drawHome();
    return;
  }
  renderPageToCanvas(page, pageCanvasA);
  panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(),
                            LCD_WIDTH, LCD_HEIGHT);
}

void transitionSettingsView(SettingsView target, bool forward) {
  if (target == settingsView) return;
  if (!transitionsReady) {
    settingsView = target;
    drawSettingsPage();
    return;
  }

  renderPageToCanvas(PAGE_SETTINGS, pageCanvasA);
  settingsView = target;
  renderPageToCanvas(PAGE_SETTINGS, pageCanvasB);
  const uint16_t *from = pageCanvasA.getFramebuffer();
  const uint16_t *to = pageCanvasB.getFramebuffer();
  constexpr uint8_t FRAME_COUNT = 14;
  constexpr uint32_t FRAME_TIME_MS = 17;

  for (uint8_t frame = 1; frame <= FRAME_COUNT; ++frame) {
    const float linear = static_cast<float>(frame) / FRAME_COUNT;
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    const int16_t shift = min<int16_t>(LCD_WIDTH, lroundf(eased * LCD_WIDTH));
    const int16_t remaining = LCD_WIDTH - shift;
    for (int16_t y = 0; y < LCD_HEIGHT; ++y) {
      uint16_t *out = transitionFrame + static_cast<size_t>(y) * LCD_WIDTH;
      const uint16_t *oldRow = from + static_cast<size_t>(y) * LCD_WIDTH;
      const uint16_t *newRow = to + static_cast<size_t>(y) * LCD_WIDTH;
      if (forward) {
        if (remaining) memcpy(out, oldRow + shift, remaining * sizeof(uint16_t));
        if (shift) memcpy(out + remaining, newRow, shift * sizeof(uint16_t));
      } else {
        if (shift) memcpy(out, newRow + remaining, shift * sizeof(uint16_t));
        if (remaining) memcpy(out + shift, oldRow, remaining * sizeof(uint16_t));
      }
    }
    panel->draw16bitRGBBitmap(0, 0, transitionFrame, LCD_WIDTH, LCD_HEIGHT);
    delay(FRAME_TIME_MS);
  }
}

void transitionSettingsGrid(uint8_t target) {
  target = min<uint8_t>(target, 1);
  if (target == settingsGridPage) return;
  if (!transitionsReady) {
    settingsGridPage = target;
    drawSettingsPage();
    return;
  }

  const bool upward = target > settingsGridPage;
  renderPageToCanvas(PAGE_SETTINGS, pageCanvasA);
  settingsGridPage = target;
  renderPageToCanvas(PAGE_SETTINGS, pageCanvasB);
  const uint16_t *from = pageCanvasA.getFramebuffer();
  const uint16_t *to = pageCanvasB.getFramebuffer();
  constexpr uint8_t FRAME_COUNT = 14;

  for (uint8_t frame = 1; frame <= FRAME_COUNT; ++frame) {
    const float linear = static_cast<float>(frame) / FRAME_COUNT;
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    const int16_t shift = min<int16_t>(LCD_HEIGHT, lroundf(eased * LCD_HEIGHT));
    const int16_t remaining = LCD_HEIGHT - shift;
    if (upward) {
      if (remaining) memcpy(transitionFrame, from + static_cast<size_t>(shift) * LCD_WIDTH,
                            static_cast<size_t>(remaining) * LCD_WIDTH * sizeof(uint16_t));
      if (shift) memcpy(transitionFrame + static_cast<size_t>(remaining) * LCD_WIDTH, to,
                        static_cast<size_t>(shift) * LCD_WIDTH * sizeof(uint16_t));
    } else {
      if (shift) memcpy(transitionFrame, to + static_cast<size_t>(remaining) * LCD_WIDTH,
                        static_cast<size_t>(shift) * LCD_WIDTH * sizeof(uint16_t));
      if (remaining) memcpy(transitionFrame + static_cast<size_t>(shift) * LCD_WIDTH, from,
                            static_cast<size_t>(remaining) * LCD_WIDTH * sizeof(uint16_t));
    }
    panel->draw16bitRGBBitmap(0, 0, transitionFrame, LCD_WIDTH, LCD_HEIGHT);
    delay(17);
  }
}

void presentBattlePage() {
  if (transitionsReady) {
    renderPageToCanvas(PAGE_BATTLE, pageCanvasA);
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(),
                              LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawBattlePage();
  }
}

void transitionToPage(Page target, int8_t direction) {
  if (!transitionsReady || target == currentPage) {
    currentPage = target;
    drawHome();
    return;
  }

  renderPageToCanvas(currentPage, pageCanvasA);
  currentPage = target;
  renderPageToCanvas(currentPage, pageCanvasB);

  const uint16_t *from = pageCanvasA.getFramebuffer();
  const uint16_t *to = pageCanvasB.getFramebuffer();
  constexpr uint8_t FRAME_COUNT = 15;
  constexpr uint32_t FRAME_TIME_MS = 17;

  for (uint8_t frame = 1; frame <= FRAME_COUNT; ++frame) {
    const float linear = static_cast<float>(frame) / FRAME_COUNT;
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    const int16_t shift = min<int16_t>(LCD_WIDTH, lroundf(eased * LCD_WIDTH));
    const int16_t remaining = LCD_WIDTH - shift;

    for (int16_t y = 0; y < LCD_HEIGHT; ++y) {
      uint16_t *out = transitionFrame + static_cast<size_t>(y) * LCD_WIDTH;
      const uint16_t *oldRow = from + static_cast<size_t>(y) * LCD_WIDTH;
      const uint16_t *newRow = to + static_cast<size_t>(y) * LCD_WIDTH;
      if (direction < 0) {
        if (remaining) memcpy(out, oldRow + shift, remaining * sizeof(uint16_t));
        if (shift) memcpy(out + remaining, newRow, shift * sizeof(uint16_t));
      } else {
        if (shift) memcpy(out, newRow + remaining, shift * sizeof(uint16_t));
        if (remaining) memcpy(out + shift, oldRow, remaining * sizeof(uint16_t));
      }
    }
    const uint32_t frameStarted = millis();
    panel->draw16bitRGBBitmap(0, 0, transitionFrame, LCD_WIDTH, LCD_HEIGHT);
    const uint32_t elapsed = millis() - frameStarted;
    if (elapsed < FRAME_TIME_MS) delay(FRAME_TIME_MS - elapsed);
  }
}

void updateClockDisplay() {
  if (!clockValid) return;
  struct tm rtcTime{};
  if (!rtcRead(rtcTime)) {
    clockValid = false;
    strcpy(clockText, "--:--");
  } else {
    snprintf(clockText, sizeof(clockText), "%02d:%02d", rtcTime.tm_hour, rtcTime.tm_min);
  }
  if (!sleeping && currentPage == PAGE_COMPANION &&
      !showingEvolutionDebug && !showingGenomeProfile) {
    display->fillRect(284, 42, 68, 22, COLOR_BACKGROUND);
    display->setTextSize(2);
    display->setTextColor(clockValid ? COLOR_TEXT : COLOR_MUTED);
    display->setCursor(288, 45);
    display->print(clockText);
  }
}

void changeSetting(int16_t x, int16_t y) {
  if (y < 78 || y >= 364) return;
  const uint8_t column = x >= LCD_WIDTH / 2;
  const uint8_t row = y >= 227;
  const uint8_t item = settingsGridPage * 4 + row * 2 + column;

  if (item <= 5) {
    transitionSettingsView(static_cast<SettingsView>(item + 1), true);
  } else if (item == 6) {
    showingPlayerId = true;
    presentPlayerIdPage();
  } else if (item == 7) {
    showingUpdate = true;
    presentUpdatePage("STARTING SECURE CHECK", 0);
    const OtaResult otaResult = performSignedOta(
        networkConfig.ssid, networkConfig.password,
        [](const char *status, int progress) { presentUpdatePage(status, progress); });
    presentUpdatePage(otaResultMessage(otaResult), -1);
    lastInteraction = millis();
  }
}

void handleSettingsControlTap(int16_t x, int16_t y) {
  if (y >= 375) {
    transitionSettingsView(SETTINGS_HOME, false);
    return;
  }

  bool changed = false;
  if (settingsView == SETTINGS_BRIGHTNESS && y >= 83 && y < 355) {
    const int column = (x - 22) / 113;
    const int row = (y - 83) / 68;
    if (x >= 22 && column >= 0 && column < 3 && row >= 0 && row < 4 &&
        (x - 22) % 113 < 98 && (y - 83) % 68 < 53) {
      const int value = row * 3 + column;
      if (value <= 10) {
        settings.brightnessIndex = value;
        panel->setBrightness(brightnessLevel());
        changed = true;
      }
    }
  } else if (settingsView == SETTINGS_IDLE && y >= 89 && y < 337) {
    const uint8_t value = (y - 89) / 62;
    if ((y - 89) % 62 < 48 && value < 4) {
      settings.sleepIndex = value;
      changed = true;
    }
  } else if ((settingsView == SETTINGS_VOLUME || settingsView == SETTINGS_THEME) &&
             y >= 78 && y < 363) {
    const uint8_t value = (y - 78) / 57;
    if ((y - 78) % 57 < 48 && value < 5) {
      if (settingsView == SETTINGS_VOLUME) {
        settings.volumeIndex = value;
        settings.soundEnabled = value > 0;
        if (audioReady) codecWrite(0x32, CODEC_VOLUMES[value]);
        if (settings.soundEnabled) playTone(880, 100, 70);
      } else {
        settings.themeIndex = value;
        applyTheme();
      }
      changed = true;
    }
  } else if (settingsView == SETTINGS_WAKE) {
    if (y >= 121 && y < 169) { settings.wakeMode = 0; changed = true; }
    if (y >= 190 && y < 238) { settings.wakeMode = 1; changed = true; }
  } else if (settingsView == SETTINGS_BOOT) {
    if (y >= 130 && y < 178) { settings.bootAnimationEnabled = true; changed = true; }
    if (y >= 199 && y < 247) { settings.bootAnimationEnabled = false; changed = true; }
  }

  if (changed) {
    saveSettings();
    presentCoherentPageFrame(PAGE_SETTINGS);
  }
}

void drawSleep() {
  display->fillScreen(RGB565_BLACK);
  display->setTextColor(COLOR_CYAN);
  display->setTextSize(2);
  display->setCursor(animationFrame ? 170 : 174, 226);
  display->print("z");
  display->setTextSize(3);
  display->setCursor(animationFrame ? 193 : 197, 196);
  display->print("Z");
}

void updateCreatureAnimation() {
  if (sleeping) {
    // Keep idle visually quiet: only the two dream marks move.
    display->fillRect(158, 184, 72, 72, RGB565_BLACK);
    display->setTextColor(COLOR_CYAN);
    display->setTextSize(2);
    display->setCursor(animationFrame ? 170 : 174, 226);
    display->print("z");
    display->setTextSize(3);
    display->setCursor(animationFrame ? 193 : 197, 196);
    display->print("Z");
  } else if (currentPage == PAGE_COMPANION) {
    // Compose the entire frame in PSRAM. Directly streaming overlapping
    // primitives to the CO5300 causes visible scanline tearing.
    const uint32_t started = millis();
    presentCoherentPageFrame(PAGE_COMPANION);
    const uint16_t renderTime = millis() - started;
    const uint16_t nextInterval = constrain(renderTime + 6, 40, 90);
    creatureFrameInterval = (creatureFrameInterval * 3 + nextInterval) / 4;
  }
}

uint8_t calculateStage() {
  if (pet.ageMinutes < 2 || pet.actions < 2) return 0;
  if (pet.ageMinutes < 30 || pet.actions < 8) return 1;
  if (pet.ageMinutes < 360 || pet.actions < 40) return 2;
  if (pet.ageMinutes < 1440 || pet.actions < 120 || pet.training < 25) return 3;
  return 4;
}

void checkEvolution() {
  const uint8_t newStage = calculateStage();
  if (newStage > pet.stage) {
    pet.stage = newStage;
    savePet();
    display->fillScreen(RGB565_BLACK);
    for (int radius = 12; radius < 210; radius += 12) {
      display->drawCircle(184, 224, radius, radius % 24 ? COLOR_CYAN : COLOR_MINT);
      delay(45);
    }
    drawCentered("DATA EVOLUTION", 90, 3, COLOR_TEXT);
    drawCentered(STAGE_NAMES[pet.stage], 330, 3, COLOR_MINT);
    delay(1000);
    snprintf(message, sizeof(message), "Evolved into %s", STAGE_NAMES[pet.stage]);
  }
}

void performAction(int action) {
  if (action == 0) {
    pet.food = addClamped(pet.food, 18);
    pet.energy = addClamped(pet.energy, 2);
    strcpy(message, "Crunch! Data restored");
  } else if (action == 1) {
    if (pet.energy < 8) {
      strcpy(message, "Too tired to play");
      drawHome();
      return;
    }
    pet.joy = addClamped(pet.joy, 16);
    pet.energy = subtractClamped(pet.energy, 8);
    pet.food = subtractClamped(pet.food, 3);
    strcpy(message, "Connection strengthened");
  } else {
    if (pet.energy < 12) {
      strcpy(message, "Needs idle time first");
      drawHome();
      return;
    }
    pet.training = addClamped(pet.training, 4);
    pet.energy = subtractClamped(pet.energy, 12);
    pet.food = subtractClamped(pet.food, 4);
    pet.joy = addClamped(pet.joy, 3);
    strcpy(message, "Training complete!");
  }
  playActionSound(action);
  pet.actions++;
  checkEvolution();
  savePet();
  drawHome();
}

void agePet() {
  pet.ageMinutes++;
  if (pet.ageMinutes % 5 == 0) pet.food = subtractClamped(pet.food, 1);
  if (pet.ageMinutes % 8 == 0) pet.joy = subtractClamped(pet.joy, 1);
  if (sleeping) {
    pet.energy = addClamped(pet.energy, 5);
    if (pet.health < 100) pet.health = addClamped(pet.health, 1);
  } else if (pet.ageMinutes % 3 == 0) {
    pet.energy = subtractClamped(pet.energy, 1);
  }
  // Low stats lower general vitality; there is deliberately no illness state.
  if (pet.food < 15 || pet.joy < 15 || pet.energy < 5) {
    pet.health = subtractClamped(pet.health, 1);
  } else if (pet.health < 100 && pet.ageMinutes % 10 == 0) {
    pet.health = addClamped(pet.health, 1);
  }
  checkEvolution();
  savePet();
  if (!sleeping) drawHome();
  else if (!screenOff) drawSleep();
}

void enterSleep() {
  playTone(523, 80);
  playTone(392, 120);
  sleeping = true;
  showingEvolutionDebug = false;
  screenOff = false;
  sleepStarted = millis();
  animationFrame = false;
  panel->setBrightness(55);
  drawSleep();
}

void turnScreenOff() {
  if (!sleeping || screenOff) return;
  screenOff = true;
  imuBaselineReady = false;
  panel->setBrightness(0);
  panel->displayOff();
  Serial.println("Display: off; lift and selected wake control remain active");
}

void showIdleFromLift() {
  if (!screenOff) return;
  panel->displayOn();
  panel->setBrightness(55);
  screenOff = false;
  sleepStarted = millis();
  animationFrame = false;
  drawSleep();
  Serial.println("Wake: lift detected; showing idle screen");
}

void wakeUp() {
  if (screenOff) panel->displayOn();
  sleeping = false;
  screenOff = false;
  lastInteraction = millis();
  panel->setBrightness(brightnessLevel());
  strcpy(message, "Link restored");
  playTone(659, 60);
  playTone(880, 100);
  drawHome();
}

void pollLiftWake(uint32_t now) {
  if (!screenOff || !imuReady || now - lastImuRead < 90) return;
  lastImuRead = now;
  float x, y, z;
  if (!readAcceleration(x, y, z)) return;
  if (!imuBaselineReady) {
    lastAccelX = x; lastAccelY = y; lastAccelZ = z;
    imuBaselineReady = true;
    return;
  }
  const float movement = fabsf(x - lastAccelX) + fabsf(y - lastAccelY) + fabsf(z - lastAccelZ);
  lastAccelX = x; lastAccelY = y; lastAccelZ = z;
  if (movement > 0.42f) showIdleFromLift();
}

void pollBootWake(uint32_t now) {
  if (now - lastBootRead < 25) return;
  lastBootRead = now;
  const bool down = digitalRead(0) == LOW;
  if (down && !bootWasDown && sleeping && settings.wakeMode == 1) wakeUp();
  bootWasDown = down;
}

uint32_t battlePlayerId() {
  const uint64_t mac = ESP.getEfuseMac();
  return static_cast<uint32_t>(mac ^ (mac >> 32));
}

FamiliarBattleCapabilities battleCapabilities() {
  FamiliarBattleCapabilities capabilities;
  capabilities.flags = FamiliarCapBodyType | FamiliarCapElement |
      FamiliarCapSpeed | FamiliarCapSpecial | FamiliarCapMoveMatchups;
  capabilities.bodyType = pet.genome.bodyType % 5;
  capabilities.element = pet.genome.element % 6;
  const int bodySpeedBonus = capabilities.bodyType == 2 ? 12 :
                             (capabilities.bodyType == 3 ? -8 : 0);
  capabilities.speed = constrain(20 + pet.genome.limbGene * 55 / 255 +
                                 pet.stage * 6 + bodySpeedBonus, 1, 100);
  capabilities.special = constrain(25 + pet.genome.faceGene +
                                   pet.stage * 9 + pet.training / 4, 1, 100);
  return capabilities;
}

void handleBattleTap(int16_t x, int16_t y) {
  const FamiliarBattleState state = battle.state();
  if (state == FamiliarBattleState::Idle && y >= 165 && y <= 260) {
    const FamiliarBattleCapabilities capabilities = battleCapabilities();
    char genomeCode[PET_GENOME_CODE_LENGTH + 1]{};
    encodePetGenome(pet.genome, genomeCode, sizeof(genomeCode));
    battleGenomeCopied = false;
    if (x < LCD_WIDTH / 2) {
      battle.beginHost(battlePlayerId(), pet.stage, 5, capabilities, genomeCode);
    } else {
      display->fillScreen(COLOR_BACKGROUND);
      drawCentered("SCANNING BATTLE LINKS", 185, 2, COLOR_CYAN);
      drawCentered("4 SECOND SEARCH", 224, 1, COLOR_MUTED);
      battle.beginFind(battlePlayerId(), pet.stage, 5, capabilities, genomeCode);
      if (!battle.scanResults().empty()) battle.connectTo(0);
    }
    presentBattlePage();
  } else if (state == FamiliarBattleState::Battling && y >= 292 && y <= 375 &&
             !battle.myMoveSubmitted()) {
    const uint8_t move = min<uint8_t>(3, x / 92);
    battle.submitMove(static_cast<FamiliarBattleMove>(move));
    playTone(560 + move * 110, 55, 35);
    presentBattlePage();
  } else if (state == FamiliarBattleState::Result) {
    if (battle.opponentGenomeCodeAvailable() && y >= 292 && x < LCD_WIDTH / 2) {
      PetGenome received{};
      if (decodePetGenome(battle.opponentGenomeCode(), received)) {
        saveCopiedGenome(received);
        battleGenomeCopied = hasCopiedGenome;
        playTone(784, 70, 45);
        playTone(1047, 110, 55);
        presentBattlePage();
      }
    } else {
      battle.end();
      presentBattlePage();
    }
  } else if ((state == FamiliarBattleState::Hosting ||
              state == FamiliarBattleState::Connecting) && y >= 340) {
    battle.end();
    presentBattlePage();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("Digipet firmware v%s\n", DIGIPET_VERSION);
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Wire.begin(IIC_SDA, IIC_SCL);
  preferences.begin("digipet", false);
  loadSettings();
  loadPet();
  loadCopiedGenome();
  applyTheme();
  char genomeSelfTest[PET_GENOME_CODE_LENGTH + 1]{};
  PetGenome decodedSelfTest{};
  const bool genomeCodecReady = encodePetGenome(pet.genome, genomeSelfTest,
                                                 sizeof(genomeSelfTest)) &&
      decodePetGenome(genomeSelfTest, decodedSelfTest) &&
      petGenomeDesignId(decodedSelfTest) == petGenomeDesignId(pet.genome);
  Serial.printf("Genome codec: %s\n", genomeCodecReady ? "ready" : "FAILED");

  if (expander.begin(0x20)) {
    for (uint8_t pin : {0, 1, 2}) {
      expander.pinMode(pin, OUTPUT);
      expander.digitalWrite(pin, LOW);
    }
    delay(20);
    for (uint8_t pin : {0, 1, 2}) expander.digitalWrite(pin, HIGH);
  }

  // Only what the boot jingle needs is checked before the screen wakes up;
  // the full hardware scan and any network use are deferred until after the
  // animation has already played (see below) so nothing before the screen
  // turns on other than the minimum required to init audio.
  detectAudioHardware();
  audioReady = initAudio();

  panel->begin();

  // Stand up the PSRAM framebuffers before the boot animation so it can use
  // the same double-buffered blit path as the rest of the UI's transitions.
  if (psramFound()) {
    const bool firstCanvasReady = pageCanvasA.begin(GFX_SKIP_OUTPUT_BEGIN);
    const bool secondCanvasReady = pageCanvasB.begin(GFX_SKIP_OUTPUT_BEGIN);
    transitionFrame = static_cast<uint16_t *>(
        ps_malloc(static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * sizeof(uint16_t)));
    transitionsReady = firstCanvasReady && secondCanvasReady && transitionFrame;
  }
  Serial.printf("PSRAM: %u bytes; smooth transitions: %s\n",
                ESP.getPsramSize(), transitionsReady ? "ready" : "fallback");

  if (settings.bootAnimationEnabled) {
    bootAnimation();
  } else {
    panel->setBrightness(brightnessLevel());
    display->fillScreen(COLOR_BACKGROUND);
    drawCentered("DIGIPET", 196, 4, COLOR_MINT);
    delay(350);
  }
  panel->setBrightness(brightnessLevel());

  // The rest of hardware bring-up and any network/time sync now run as
  // their own visible phase, after the animation has already played
  // uninterrupted rather than polled mid-frame.
  detectHardware();
  runStartupNetworkSync();
  generatePlayerId();
  checkEvolution();

  if (transitionsReady) {
    renderPageToCanvas(currentPage, pageCanvasA);
    panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(),
                              LCD_WIDTH, LCD_HEIGHT);
  } else {
    drawHome();
  }

  if (touch->begin()) {
    touch->IIC_Write_Device_State(
        Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
  }
  pinMode(0, INPUT_PULLUP);
  imuReady = initializeLiftSensor();
  nextBlink = millis() + 2200;
  lastMinute = lastInteraction = lastAnimation = millis();
}

void loop() {
  const uint32_t now = millis();
  serviceSerialDebug();
  if (currentPage == PAGE_BATTLE && battle.state() != FamiliarBattleState::Idle) {
    battle.update();
    if (battle.state() != lastBattleState || battle.turnNumber() != lastBattleTurn ||
        battle.myHp() != lastBattleMyHp || battle.opponentHp() != lastBattleOpponentHp) {
      lastBattleState = battle.state();
      lastBattleTurn = battle.turnNumber();
      lastBattleMyHp = battle.myHp();
      lastBattleOpponentHp = battle.opponentHp();
      presentBattlePage();
    }
  }
  if (now - lastClockDraw >= 1000) {
    lastClockDraw = now;
    updateClockDisplay();
  }
  if (now - lastMinute >= 60000) {
    lastMinute += 60000;
    agePet();
  }

  if (!sleeping && now - lastInteraction >= SLEEP_TIMEOUTS[settings.sleepIndex]) {
    enterSleep();
  }

  if (sleeping && !screenOff && now - sleepStarted >= 10000) turnScreenOff();
  pollLiftWake(now);
  pollBootWake(now);

  if (sleeping && !screenOff && now - lastAnimation >= 900) {
    lastAnimation = now;
    animationFrame = !animationFrame;
    updateCreatureAnimation();
  } else if (!sleeping && !showingEvolutionDebug && !showingGenomeProfile &&
             currentPage == PAGE_COMPANION &&
             now - lastAnimation >= creatureFrameInterval) {
    lastAnimation = now;
    animationFrame = !animationFrame;
    updateCreatureAnimation();
  } else if (!sleeping && !showingGenomeProfile &&
             currentPage == PAGE_GENOME_LAB &&
             now - lastAnimation >= 50) {
    lastAnimation = now;
    animationFrame = !animationFrame;
    presentCoherentPageFrame(PAGE_GENOME_LAB);
  }

  if (newEggConfirmation && now >= newEggConfirmationUntil) {
    newEggConfirmation = false;
    pendingHatchMode = 0;
    if (currentPage == PAGE_GENOME_LAB) drawHome();
  }

  if (now - lastTouchRead < 25) return;
  lastTouchRead = now;
  const int32_t fingers = touch->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

  if (fingers > 0) {
    const int32_t x = touch->IIC_Read_Device_Value(
        Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    const int32_t y = touch->IIC_Read_Device_Value(
        Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

    if (!touchWasDown) {
      touchWasDown = true;
      touchStartedAt = now;
      if (sleeping) {
        touchStartX = touchStartY = -1;
        if (settings.wakeMode == 0) wakeUp();
        return;  // The wake touch never activates a button.
      }
      lastInteraction = now;
      touchStartX = touchLastX = x;
      touchStartY = touchLastY = y;
    } else {
      touchLastX = x;
      touchLastY = y;
    }
  } else if (touchWasDown) {
    touchWasDown = false;
    if (touchStartX >= 0) {
      const int32_t deltaX = touchLastX - touchStartX;
      const int32_t deltaY = touchLastY - touchStartY;
      const uint32_t touchDuration = now - touchStartedAt;
      if (showingEvolutionDebug) {
        if (touchLastY >= 285 && touchLastY < 365 && pet.stage < 4) {
          debugAdvanceEvolution();
          showingEvolutionDebug = false;
          presentCoherentPageFrame(PAGE_COMPANION);
        } else if (touchLastY >= 365) {
          showingEvolutionDebug = false;
          presentCoherentPageFrame(PAGE_COMPANION);
        }
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      if (currentPage == PAGE_COMPANION && touchDuration >= 900 &&
          abs(deltaX) < 30 && abs(deltaY) < 30 &&
          touchStartX >= 105 && touchStartX <= 263 &&
          touchStartY >= 125 && touchStartY <= 305) {
        showingEvolutionDebug = true;
        playTone(262, 80, 35);
        presentEvolutionDebugPage();
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      if (showingPlayerId || showingUpdate) {
        showingPlayerId = false;
        showingUpdate = false;
        if (transitionsReady) {
          renderPageToCanvas(PAGE_SETTINGS, pageCanvasA);
          panel->draw16bitRGBBitmap(0, 0, pageCanvasA.getFramebuffer(),
                                    LCD_WIDTH, LCD_HEIGHT);
        } else {
          drawSettingsPage();
        }
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      if (showingGenomeProfile) {
        if (touchLastY >= 338 && touchLastY < 393) {
          if (touchLastX < LCD_WIDTH / 2) exportActiveGenome();
          else importCopiedGenome();
          presentGenomeProfilePage();
        } else if (touchLastY >= 393 || deltaX > 55) {
          showingGenomeProfile = false;
          presentCoherentPageFrame(genomeProfileReturnPage);
        }
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      if (currentPage == PAGE_SETTINGS && settingsView != SETTINGS_HOME) {
        if (deltaX > 55 && abs(deltaX) > abs(deltaY)) {
          transitionSettingsView(SETTINGS_HOME, false);
        } else if (abs(deltaX) < 35 && abs(deltaY) < 35) {
          handleSettingsControlTap(touchLastX, touchLastY);
        }
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      if (currentPage == PAGE_SETTINGS && settingsView == SETTINGS_HOME &&
          abs(deltaY) > 55 &&
          abs(deltaY) > abs(deltaX)) {
        playTone(deltaY < 0 ? 988 : 784, 30, 30);
        transitionSettingsGrid(deltaY < 0 ? 1 : 0);
        touchStartX = touchStartY = touchLastX = touchLastY = -1;
        return;
      }
      const bool battleLinkActive = currentPage == PAGE_BATTLE &&
                                    battle.state() != FamiliarBattleState::Idle;
      if (!battleLinkActive && abs(deltaX) > 60 && abs(deltaX) > abs(deltaY)) {
        if (deltaX < 0 && currentPage < PAGE_GENOME_LAB) {
          playTone(988, 30, 35);
          const Page target = static_cast<Page>(currentPage + 1);
          strcpy(message, target == PAGE_STATUS ? "Care console ready" :
                                                 "Device setup ready");
          transitionToPage(target, -1);
        } else if (deltaX > 0 && currentPage > PAGE_COMPANION) {
          playTone(784, 30, 35);
          const Page target = static_cast<Page>(currentPage - 1);
          strcpy(message, target == PAGE_COMPANION ? "Companion link active" :
                                                    "Care console ready");
          transitionToPage(target, 1);
        }
      } else if (currentPage == PAGE_COMPANION && touchDuration < 900 &&
                 abs(deltaX) < 30 && abs(deltaY) < 30 &&
                 touchStartX >= 55 && touchStartX <= 313 &&
                 touchStartY >= 90 && touchStartY <= 358) {
        genomeProfileReturnPage = PAGE_COMPANION;
        showingGenomeProfile = true;
        presentGenomeProfilePage();
      } else if (currentPage == PAGE_STATUS && touchLastY >= 360 && touchLastY < 448) {
        if (touchLastX < 124) performAction(0);
        else if (touchLastX < 244) performAction(1);
        else performAction(2);
      } else if (currentPage == PAGE_SETTINGS) {
        changeSetting(touchLastX, touchLastY);
      } else if (currentPage == PAGE_GENOME_LAB && touchLastY >= 374) {
        const uint8_t mode = min<uint8_t>(3, touchLastX / 119 + 1);
        if (mode > 1 && !hasCopiedGenome) {
          strcpy(genomeTransferStatus, "IMPORT A GENOME FIRST");
          playTone(196, 100, 35);
        } else if (newEggConfirmation && pendingHatchMode == mode &&
                   millis() < newEggConfirmationUntil) {
          beginNewEgg(mode);
          transitionToPage(PAGE_COMPANION, 1);
        } else {
          newEggConfirmation = true;
          pendingHatchMode = mode;
          newEggConfirmationUntil = millis() + 6000;
          playTone(330, 90, 38);
        }
        drawHome();
      } else if (currentPage == PAGE_BATTLE) {
        handleBattleTap(touchLastX, touchLastY);
      }
    }
    touchStartX = touchStartY = touchLastX = touchLastY = -1;
  }
}
