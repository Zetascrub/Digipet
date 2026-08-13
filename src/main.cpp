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
constexpr uint32_t SETTINGS_MAGIC = 0x44505331;

constexpr uint16_t COLOR_BACKGROUND = 0x0823;
constexpr uint16_t COLOR_CARD = 0x18E8;
constexpr uint16_t COLOR_MINT = 0x6718;
constexpr uint16_t COLOR_TEXT = 0xE73C;
constexpr uint16_t COLOR_MUTED = 0x8413;
constexpr uint16_t COLOR_WARNING = 0xFE48;
constexpr uint16_t COLOR_DANGER = 0xF2CB;
constexpr uint16_t COLOR_CYAN = 0x269F;
constexpr uint16_t COLOR_PURPLE = 0xA81F;

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
};

DeviceSettings settings;
const uint8_t BRIGHTNESS_LEVELS[] = {80, 140, 210, 255};
const uint32_t SLEEP_TIMEOUTS[] = {15000, 30000, 60000, 120000};
const char *SLEEP_LABELS[] = {"15 SEC", "30 SEC", "1 MIN", "2 MIN"};
const char *VOLUME_LABELS[] = {"MUTE", "LOW", "MEDIUM", "HIGH", "MAX"};
const uint8_t CODEC_VOLUMES[] = {0x00, 0x58, 0x8B, 0xBF, 0xF2};
const char *WAKE_LABELS[] = {"TOUCH", "BOOT KEY"};

uint32_t lastMinute = 0;
uint32_t lastTouchRead = 0;
uint32_t lastInteraction = 0;
uint32_t lastAnimation = 0;
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

struct NetworkConfig {
  char ssid[65];
  char password[65];
  char timezone[48];
  char ntp[65];
  bool syncOnBoot;
  bool valid;
};

NetworkConfig networkConfig{};

enum Page : uint8_t { PAGE_COMPANION, PAGE_STATUS, PAGE_BATTLE, PAGE_SETTINGS };
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

void playTone(uint16_t frequency, uint16_t durationMs, uint8_t level = 55) {
  if (!settings.soundEnabled || !audioReady || frequency == 0) return;
  static const int8_t sine32[] = {
      0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
      0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106,
      -90, -71, -49, -25};
  constexpr uint32_t sampleRate = 16000;
  const uint32_t total = (sampleRate * durationMs) / 1000;
  uint32_t phase = 0;
  const uint32_t step = (static_cast<uint64_t>(frequency) << 32) / sampleRate;
  int16_t buffer[256]; // 128 stereo frames
  uint32_t produced = 0;
  while (produced < total) {
    const uint16_t frames = min<uint32_t>(128, total - produced);
    for (uint16_t i = 0; i < frames; i++) {
      const uint32_t position = produced + i;
      uint16_t envelope = 255;
      if (position < 80) envelope = (position * 255) / 80;
      if (total - position < 120) envelope = ((total - position) * 255) / 120;
      // Peak is about 10,000 at level 100, safely inside signed 16-bit PCM.
      const int16_t sample = (static_cast<int32_t>(sine32[phase >> 27]) *
                              level * envelope * 10000) /
                             (127 * 100 * 255);
      phase += step;
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }
    audioI2S.write(reinterpret_cast<uint8_t *>(buffer), frames * 4);
    produced += frames;
  }
}

void playBootJingle() {
  playTone(523, 70); playTone(659, 70); playTone(784, 90); playTone(1047, 150);
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

void drawLinkStatus(const char *line, uint16_t color) {
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("DATA LINK", 145, 3, COLOR_MINT);
  drawCentered(line, 207, 2, color);
  drawCentered("OFFLINE PLAY REMAINS AVAILABLE", 255, 1, COLOR_MUTED);
}

void animateLinkStatus(uint8_t frame, uint16_t color) {
  constexpr int16_t cx = LCD_WIDTH / 2;
  constexpr int16_t cy = 304;
  display->fillRect(cx - 52, cy - 30, 104, 60, COLOR_BACKGROUND);
  display->drawCircle(cx, cy, 23, COLOR_CARD);
  for (uint8_t dot = 0; dot < 8; ++dot) {
    const float angle = (dot + frame) * 0.785398f;
    const int16_t x = cx + lroundf(cosf(angle) * 23.0f);
    const int16_t y = cy + lroundf(sinf(angle) * 23.0f);
    const uint16_t dotColor = dot < 3 ? color : COLOR_CARD;
    display->fillCircle(x, y, dot == 0 ? 5 : 3, dotColor);
  }
  display->fillCircle(cx, cy, 7, (frame & 1) ? COLOR_PURPLE : COLOR_MINT);
}

bool synchronizeClock() {
  if (!networkConfig.valid) return false;
  drawLinkStatus("CONNECTING...", COLOR_CYAN);
  WiFi.mode(WIFI_STA);
  WiFi.begin(networkConfig.ssid, networkConfig.password);
  const uint32_t started = millis();
  uint8_t linkFrame = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    animateLinkStatus(linkFrame++, COLOR_CYAN);
    delay(90);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi: connection timed out");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    drawLinkStatus("OFFLINE MODE", COLOR_WARNING);
    delay(650);
    return false;
  }
  Serial.println("Wi-Fi: connected; requesting NTP time");
  drawLinkStatus("SYNCING TIME...", COLOR_CYAN);
  configTzTime(posixTimezone(networkConfig.timezone), networkConfig.ntp);
  struct tm local{};
  bool received = false;
  const uint32_t syncStarted = millis();
  while (!received && millis() - syncStarted < 10000) {
    received = getLocalTime(&local, 120);
    animateLinkStatus(linkFrame++, COLOR_CYAN);
  }
  if (received && rtcWrite(local)) {
    clockValid = timeSynced = true;
    preferences.putULong64("lastSync", static_cast<uint64_t>(time(nullptr)));
    snprintf(clockText, sizeof(clockText), "%02d:%02d", local.tm_hour, local.tm_min);
    Serial.println("Time: NTP synchronized and RTC updated");
    drawLinkStatus("TIME LINKED", COLOR_MINT);
  } else {
    Serial.println("Time: NTP or RTC update failed");
    drawLinkStatus("SYNC FAILED", COLOR_DANGER);
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wi-Fi: radio disabled");
  delay(650);
  return received;
}

void initializeClockAndNetwork() {
  struct tm rtcTime{};
  clockValid = rtcDetected && rtcRead(rtcTime);
  if (clockValid) {
    snprintf(clockText, sizeof(clockText), "%02d:%02d", rtcTime.tm_hour, rtcTime.tm_min);
    Serial.println("RTC: valid retained time found");
  } else {
    Serial.println("RTC: time is invalid or power was lost");
  }
  expander.pinMode(7, OUTPUT);
  expander.digitalWrite(7, HIGH);
  delay(100);
  if (readNetworkConfig() && (networkConfig.syncOnBoot || !clockValid)) {
    synchronizeClock();
  }
  // The SD rail is only needed while reading configuration during this phase.
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

void savePet() {
  preferences.putBytes("state", &pet, sizeof(pet));
}

void saveSettings() {
  preferences.putBytes("settings", &settings, sizeof(settings));
}

void loadSettings() {
  if (preferences.getBytesLength("settings") == sizeof(settings)) {
    preferences.getBytes("settings", &settings, sizeof(settings));
  }
  if (settings.magic != SETTINGS_MAGIC || settings.brightnessIndex > 3 ||
      settings.sleepIndex > 3 || settings.volumeIndex > 4 || settings.wakeMode > 1) {
    settings = {SETTINGS_MAGIC, 2, 1, true, true, 2, 0};
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

void bootAnimation() {
  panel->setBrightness(180);
  display->fillScreen(RGB565_BLACK);
  playTone(196, 110, 45);

  // Scattered data artifacts converge into a digital life core.
  constexpr int SHARDS = 28;
  int16_t startX[SHARDS], startY[SHARDS], oldX[SHARDS], oldY[SHARDS];
  for (int i = 0; i < SHARDS; i++) {
    startX[i] = (i * 83 + 17) % LCD_WIDTH;
    startY[i] = (i * 137 + 29) % LCD_HEIGHT;
    oldX[i] = startX[i];
    oldY[i] = startY[i];
  }
  drawCentered("UNBOUND DATA", 24, 2, COLOR_MUTED);
  for (int step = 0; step <= 22; step++) {
    for (int i = 0; i < SHARDS; i++) {
      if (step > 0) display->fillRect(oldX[i] - 1, oldY[i] - 1, 8, 8, RGB565_BLACK);
      const float t = step / 22.0f;
      const int targetX = 184 + ((i % 7) - 3) * 9;
      const int targetY = 224 + ((i / 7) - 2) * 14;
      const int x = startX[i] + (targetX - startX[i]) * t;
      const int y = startY[i] + (targetY - startY[i]) * t;
      oldX[i] = x;
      oldY[i] = y;
      const uint16_t color = i % 3 == 0 ? COLOR_PURPLE : (i % 3 == 1 ? COLOR_CYAN : COLOR_MINT);
      display->fillRect(x, y, 6, 6, color);
    }
    if (step % 4 == 0) display->drawCircle(184, 224, 145 - step * 5, COLOR_CARD);
    if (step == 5) playTone(262, 45, 32);
    if (step == 12) playTone(330, 45, 36);
    if (step == 19) playTone(392, 55, 40);
    delay(68);
  }

  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("DATA CORE FORMED", 28, 2, COLOR_CYAN);
  // The collected core grows into an egg one layer at a time.
  for (int layer = 0; layer < 8; layer++) {
    const int radius = 15 + layer * 7;
    const uint16_t color = layer & 1 ? COLOR_MINT : COLOR_PURPLE;
    display->fillCircle(184, 211, radius, color);
    display->fillCircle(184, 204, max(4, radius - 10), COLOR_BACKGROUND);
    display->fillTriangle(184 - radius, 212, 184 + radius, 212,
                          184, 212 + radius + 18, color);
    display->drawCircle(184, 224, radius + 25, COLOR_CYAN);
    if ((layer & 1) == 1) playTone(440 + layer * 28, 45, 38);
    delay(118);
  }

  // Transformation energy builds, fractures the shell, then whites out.
  for (int pulse = 0; pulse < 10; pulse++) {
    display->drawCircle(184, 224, 70 + pulse * 13,
                        pulse & 1 ? COLOR_CYAN : COLOR_MINT);
    display->drawLine(184, 198, 160 - pulse * 4, 170 - pulse * 5, COLOR_TEXT);
    display->drawLine(184, 198, 208 + pulse * 4, 170 - pulse * 5, COLOR_TEXT);
    display->fillRect((pulse * 47) % LCD_WIDTH, (pulse * 73) % LCD_HEIGHT,
                      18, 4, pulse & 1 ? COLOR_PURPLE : COLOR_MINT);
    playTone(520 + pulse * 48, 36, 34 + pulse * 2);
    delay(96);
  }
  display->fillScreen(COLOR_TEXT);
  delay(180);
  display->fillScreen(COLOR_BACKGROUND);

  // A simple companion silhouette resolves out of the light.
  display->fillRoundRect(104, 142, 160, 155, 48, COLOR_MINT);
  display->fillTriangle(116, 166, 126, 119, 150, 151, COLOR_MINT);
  display->fillTriangle(252, 166, 242, 119, 218, 151, COLOR_MINT);
  display->fillCircle(148, 197, 11, COLOR_BACKGROUND);
  display->fillCircle(220, 197, 11, COLOR_BACKGROUND);
  display->drawLine(174, 246, 184, 253, COLOR_BACKGROUND);
  display->drawLine(184, 253, 194, 246, COLOR_BACKGROUND);
  drawCentered("DIGIPET", 323, 5, COLOR_TEXT);
  drawCentered("LIFE LINK ESTABLISHED", 385, 2, COLOR_CYAN);
  playBootJingle();
  delay(1000);
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
    display->fillRoundRect(115, y - 4, (162 * value) / 100, 12, 6, color);
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
  display->fillEllipse(194, y - 11, 43, 57, palette.primaryLight);
  display->fillEllipse(201, y - 22, 27, 37, palette.primary);
  display->fillEllipse(169, y + 19, 18, 42, palette.secondary);
  display->fillEllipse(203, y - 31, 12, 21, palette.primaryLight);
  display->fillEllipse(207, y - 37, 5, 9, RGB565_WHITE);

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

void drawCreature(bool frame, bool asleep) {
  const uint16_t bg = asleep ? RGB565_BLACK : COLOR_BACKGROUND;
  if (pet.stage == 0) {
    drawEgg(frame, bg);
    return;
  }

  const int bob = 0;
  const int cx = 184;
  const int top = 158 + bob;
  const uint16_t bodyColor = pet.health < 40 ? COLOR_DANGER :
      ((pet.food < 30 || pet.joy < 30) ? COLOR_WARNING : COLOR_MINT);
  display->fillEllipse(cx, 324, pet.stage >= 3 ? 92 : 72, 10, COLOR_CARD);

  if (pet.stage == 1) {
    // HATCHLING: big head, tiny paws, striped tail and oversized ears.
    display->fillTriangle(132, top + 32, 111, top - 24, 159, top + 4, COLOR_TEXT);
    display->fillTriangle(236, top + 32, 257, top - 24, 209, top + 4, COLOR_TEXT);
    display->fillTriangle(135, top + 27, 116, top - 16, 157, top + 8, bodyColor);
    display->fillTriangle(233, top + 27, 252, top - 16, 211, top + 8, bodyColor);
    display->fillTriangle(123, top - 8, 136, top + 18, 143, top + 3, COLOR_PURPLE);
    display->fillTriangle(245, top - 8, 232, top + 18, 225, top + 3, COLOR_PURPLE);
    display->fillRoundRect(119, top, 130, 119, 42, COLOR_TEXT);
    display->fillRoundRect(125, top + 6, 118, 107, 37, bodyColor);
    display->fillEllipse(184, top + 83, 35, 27, COLOR_CYAN);
    display->fillCircle(137, top + 110, 18, COLOR_TEXT);
    display->fillCircle(231, top + 110, 18, COLOR_TEXT);
    display->fillCircle(137, top + 108, 13, COLOR_MINT);
    display->fillCircle(231, top + 108, 13, COLOR_MINT);
    display->fillTriangle(242, top + 82, 276, top + 60, 257, top + 103, COLOR_TEXT);
    display->fillTriangle(246, top + 82, 269, top + 67, 257, top + 96, COLOR_PURPLE);
  } else if (pet.stage == 2) {
    // SCOUT: agile upright form with head crest, scarf and long tail.
    display->fillTriangle(166, top + 7, 184, top - 28, 193, top + 8, COLOR_TEXT);
    display->fillTriangle(173, top + 5, 184, top - 19, 188, top + 7, COLOR_CYAN);
    display->fillTriangle(129, top + 33, 116, top - 12, 159, top + 11, COLOR_TEXT);
    display->fillTriangle(239, top + 33, 252, top - 12, 209, top + 11, COLOR_TEXT);
    display->fillRoundRect(126, top + 1, 116, 90, 34, COLOR_TEXT);
    display->fillRoundRect(132, top + 7, 104, 78, 29, bodyColor);
    display->fillTriangle(126, top + 78, 242, top + 78, 215, top + 106, COLOR_PURPLE);
    display->fillRoundRect(145, top + 83, 78, 70, 24, COLOR_TEXT);
    display->fillRoundRect(151, top + 88, 66, 59, 19, bodyColor);
    display->fillRect(145, top + 88, 78, 13, COLOR_CYAN);
    display->fillTriangle(218, top + 94, 270, top + 77, 244, top + 119, COLOR_TEXT);
    display->fillTriangle(221, top + 96, 262, top + 84, 241, top + 113, COLOR_PURPLE);
    display->fillRoundRect(132, top + 141, 39, 16, 7, COLOR_TEXT);
    display->fillRoundRect(197, top + 141, 39, 16, 7, COLOR_TEXT);
  } else {
    // GUARDIAN/TITAN: broad armored silhouette with wings and gauntlets.
    const bool titan = pet.stage >= 4;
    display->fillTriangle(139, top + 62, 75, top + 35, 119, top + 91, COLOR_TEXT);
    display->fillTriangle(229, top + 62, 293, top + 35, 249, top + 91, COLOR_TEXT);
    display->fillTriangle(130, top + 62, 87, top + 44, 121, top + 82, COLOR_PURPLE);
    display->fillTriangle(238, top + 62, 281, top + 44, 247, top + 82, COLOR_PURPLE);
    display->fillTriangle(145, top + 23, 131, top - 24, 173, top + 9, COLOR_TEXT);
    display->fillTriangle(223, top + 23, 237, top - 24, 195, top + 9, COLOR_TEXT);
    display->fillTriangle(152, top + 14, 140, top - 12, 173, top + 14, COLOR_CYAN);
    display->fillTriangle(216, top + 14, 228, top - 12, 195, top + 14, COLOR_CYAN);
    if (titan) {
      display->fillTriangle(169, top + 5, 184, top - 35, 199, top + 5, COLOR_TEXT);
      display->fillTriangle(176, top + 3, 184, top - 23, 192, top + 3, COLOR_WARNING);
    }
    display->fillRoundRect(118, top + 2, 132, 105, 35, COLOR_TEXT);
    display->fillRoundRect(125, top + 9, 118, 91, 29, bodyColor);
    display->fillRoundRect(134, top + 88, 100, 65, 22, COLOR_TEXT);
    display->fillRoundRect(141, top + 94, 86, 52, 17, titan ? COLOR_PURPLE : bodyColor);
    display->fillCircle(184, top + 118, 20, COLOR_TEXT);
    display->fillCircle(184, top + 118, 14, COLOR_CYAN);
    display->fillTriangle(184, top + 106, 194, top + 122, 184, top + 136, COLOR_TEXT);
    display->fillTriangle(184, top + 106, 174, top + 122, 184, top + 136, COLOR_TEXT);
    display->fillCircle(121, top + 108, 20, COLOR_TEXT);
    display->fillCircle(247, top + 108, 20, COLOR_TEXT);
    display->fillCircle(121, top + 108, 13, COLOR_WARNING);
    display->fillCircle(247, top + 108, 13, COLOR_WARNING);
    display->fillRoundRect(137, top + 144, 42, 15, 6, COLOR_TEXT);
    display->fillRoundRect(189, top + 144, 42, 15, 6, COLOR_TEXT);
  }

  // Shared expressive face. The second frame is a blink, not a body redraw trick.
  const int eyeY = top + 43;
  const int eyeSpread = pet.stage >= 3 ? 27 : 30;
  if (asleep || frame) {
    display->fillRect(cx - eyeSpread - 10, eyeY, 20, 4, bg);
    display->fillRect(cx + eyeSpread - 10, eyeY, 20, 4, bg);
  } else {
    display->fillRoundRect(cx - eyeSpread - 11, eyeY - 12, 22, 27, 8, bg);
    display->fillRoundRect(cx + eyeSpread - 11, eyeY - 12, 22, 27, 8, bg);
    display->fillRect(cx - eyeSpread - 1, eyeY - 7, 6, 12, COLOR_TEXT);
    display->fillRect(cx + eyeSpread - 5, eyeY - 7, 6, 12, COLOR_TEXT);
    display->fillRect(cx - eyeSpread + 1, eyeY - 6, 3, 4, COLOR_CYAN);
    display->fillRect(cx + eyeSpread - 3, eyeY - 6, 3, 4, COLOR_CYAN);
  }
  display->fillTriangle(cx - 5, eyeY + 20, cx + 5, eyeY + 20,
                        cx, eyeY + 26, bg);
  display->drawLine(cx - 12, eyeY + 31, cx, eyeY + 36, bg);
  display->drawLine(cx, eyeY + 36, cx + 12, eyeY + 31, bg);
}

void drawFaceFrame(bool closed) {
  if (pet.stage == 0) return;
  const int cx = 184;
  const int eyeY = 201;
  const int eyeSpread = pet.stage >= 3 ? 27 : 30;
  const uint16_t bodyColor = pet.health < 40 ? COLOR_DANGER :
      ((pet.food < 30 || pet.joy < 30) ? COLOR_WARNING : COLOR_MINT);
  for (int direction : {-1, 1}) {
    const int center = cx + direction * eyeSpread;
    display->fillRoundRect(center - 13, eyeY - 14, 26, 31, 9, bodyColor);
    if (closed) {
      display->fillRect(center - 10, eyeY, 20, 4, COLOR_BACKGROUND);
    } else {
      display->fillRoundRect(center - 11, eyeY - 12, 22, 27, 8, COLOR_BACKGROUND);
      display->fillRect(center + (direction < 0 ? -1 : -5), eyeY - 7,
                        6, 12, COLOR_TEXT);
      display->fillRect(center + (direction < 0 ? 1 : -3), eyeY - 6,
                        3, 4, COLOR_CYAN);
    }
  }
}

void drawPageDots(Page active) {
  const int16_t y = active == PAGE_STATUS ? 355 : 425;
  display->fillCircle(160, y, 4, active == PAGE_COMPANION ? COLOR_MINT : COLOR_CARD);
  display->fillCircle(176, y, 4, active == PAGE_STATUS ? COLOR_MINT : COLOR_CARD);
  display->fillCircle(192, y, 4, active == PAGE_BATTLE ? COLOR_MINT : COLOR_CARD);
  display->fillCircle(208, y, 4, active == PAGE_SETTINGS ? COLOR_MINT : COLOR_CARD);
}

void drawCompanionPage() {
  display->fillScreen(COLOR_BACKGROUND);
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
  display->drawRoundRect(20, 82, 328, 276, 32, COLOR_CARD);
  display->drawRoundRect(27, 89, 314, 262, 27, COLOR_CYAN);
  display->fillRect(14, 126, 18, 5, COLOR_PURPLE);
  display->fillRect(336, 309, 18, 5, COLOR_MINT);
  display->drawCircle(54, 111, 8, COLOR_MINT);
  display->drawCircle(332, 329, 8, COLOR_PURPLE);
  drawCreature(false, false);
  drawCentered(message, 378, 1, COLOR_MUTED);
  drawCentered("SWIPE FOR STATUS  >", 399, 1, COLOR_CYAN);
  drawPageDots(PAGE_COMPANION);
}

void drawStatusPage() {
  display->fillScreen(COLOR_BACKGROUND);
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

void drawAdjuster(const char *label, const char *value, int16_t y) {
  display->setTextSize(1);
  display->setTextColor(COLOR_MUTED);
  display->setCursor(28, y);
  display->print(label);
  display->fillRoundRect(25, y + 18, 52, 43, 12, COLOR_CARD);
  display->fillRoundRect(291, y + 18, 52, 43, 12, COLOR_CARD);
  display->setTextSize(3);
  display->setTextColor(COLOR_MINT);
  display->setCursor(42, y + 27);
  display->print("-");
  display->setCursor(307, y + 27);
  display->print("+");
  display->drawRoundRect(89, y + 18, 190, 43, 12, COLOR_CYAN);
  int16_t x1, y1;
  uint16_t w, h;
  display->setTextSize(2);
  display->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  display->setCursor(184 - w / 2, y + 32);
  display->setTextColor(COLOR_TEXT);
  display->print(value);
}

void drawToggle(const char *label, bool enabled, int16_t y) {
  display->fillRoundRect(25, y, 318, 54, 14, COLOR_CARD);
  display->setTextSize(2);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(43, y + 19);
  display->print(label);
  display->fillRoundRect(257, y + 12, 68, 30, 15,
                         enabled ? COLOR_MINT : COLOR_MUTED);
  display->fillCircle(enabled ? 309 : 273, y + 27, 11, COLOR_TEXT);
}

void drawSettingsPage() {
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("DEVICE SETTINGS", 18, 3, COLOR_MINT);
  drawCentered("TAP CONTROLS TO CHANGE", 50, 1, COLOR_CYAN);

  char brightness[16];
  snprintf(brightness, sizeof(brightness), "%u%%",
           (BRIGHTNESS_LEVELS[settings.brightnessIndex] * 100) / 255);
  drawAdjuster("DISPLAY BRIGHTNESS", brightness, 69);
  drawAdjuster("IDLE SLEEP", SLEEP_LABELS[settings.sleepIndex], 131);
  drawAdjuster("SPEAKER VOLUME", VOLUME_LABELS[settings.volumeIndex], 193);
  drawAdjuster("WAKE CONTROL", WAKE_LABELS[settings.wakeMode], 255);
  drawToggle("BOOT FX", settings.bootAnimationEnabled, 328);
  display->fillRoundRect(18, 390, 160, 35, 12, COLOR_PURPLE);
  display->fillRoundRect(190, 390, 160, 35, 12, COLOR_CYAN);
  display->setTextSize(1);
  display->setTextColor(COLOR_TEXT);
  display->setCursor(66, 403); display->print("PLAYER ID");
  display->setCursor(233, 403); display->print("UPDATE");
  drawPageDots(PAGE_SETTINGS);
}

void drawPlayerIdPage() {
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("PLAYER IDENTITY", 22, 3, COLOR_MINT);
  drawCentered("SHA-256 // BOOT SESSION", 58, 1, COLOR_CYAN);
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
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("SIGNED UPDATE", 30, 3, COLOR_MINT);
  drawCentered("DIGIPET OTA // ECDSA P-256", 68, 1, COLOR_CYAN);
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

void drawBattleHp(int16_t x, int16_t y, uint16_t hp, uint16_t maxHp,
                  uint16_t color) {
  display->drawRoundRect(x, y, 144, 17, 7, COLOR_MUTED);
  if (hp && maxHp) {
    display->fillRoundRect(x + 3, y + 3, 138 * hp / maxHp, 11, 5, color);
  }
}

void drawBattlePage() {
  display->fillScreen(COLOR_BACKGROUND);
  drawCentered("LINK BATTLE", 16, 3, COLOR_MINT);
  display->setTextSize(1);
  display->setTextColor(COLOR_CYAN);
  display->setCursor(24, 51);
  display->printf("LV 5  HP 35  ATK %u  DEF %u",
                  FamiliarBattleService::deriveAttack(5, pet.stage),
                  FamiliarBattleService::deriveDefense(5, pet.stage));

  const FamiliarBattleState state = battle.state();
  if (state == FamiliarBattleState::Idle) {
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
      drawCentered("TAP TO RETURN", 330, 2, COLOR_CYAN);
    }
  }
  drawPageDots(PAGE_BATTLE);
}

void drawHome() {
  if (currentPage == PAGE_COMPANION) drawCompanionPage();
  else if (currentPage == PAGE_STATUS) drawStatusPage();
  else if (currentPage == PAGE_BATTLE) drawBattlePage();
  else drawSettingsPage();
}

void renderPageToCanvas(Page page, Arduino_Canvas &canvas) {
  Arduino_GFX *previousDisplay = display;
  display = &canvas;
  if (page == PAGE_COMPANION) drawCompanionPage();
  else if (page == PAGE_STATUS) drawStatusPage();
  else if (page == PAGE_BATTLE) drawBattlePage();
  else drawSettingsPage();
  display = previousDisplay;
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
  if (!sleeping && currentPage == PAGE_COMPANION) {
    display->fillRect(284, 42, 68, 22, COLOR_BACKGROUND);
    display->setTextSize(2);
    display->setTextColor(clockValid ? COLOR_TEXT : COLOR_MUTED);
    display->setCursor(288, 45);
    display->print(clockText);
  }
}

void changeSetting(int16_t x, int16_t y) {
  if (y >= 80 && y < 130) {
    if (x < 100 && settings.brightnessIndex > 0) settings.brightnessIndex--;
    if (x > 268 && settings.brightnessIndex < 3) settings.brightnessIndex++;
    panel->setBrightness(BRIGHTNESS_LEVELS[settings.brightnessIndex]);
  } else if (y >= 142 && y < 192) {
    if (x < 100 && settings.sleepIndex > 0) settings.sleepIndex--;
    if (x > 268 && settings.sleepIndex < 3) settings.sleepIndex++;
  } else if (y >= 204 && y < 254) {
    if (x < 100 && settings.volumeIndex > 0) settings.volumeIndex--;
    if (x > 268 && settings.volumeIndex < 4) settings.volumeIndex++;
    settings.soundEnabled = settings.volumeIndex > 0;
    if (audioReady) codecWrite(0x32, CODEC_VOLUMES[settings.volumeIndex]);
    if (settings.soundEnabled) playTone(880, 180, 80);
  } else if (y >= 266 && y < 316) {
    if (x < 100 && settings.wakeMode > 0) settings.wakeMode--;
    if (x > 268 && settings.wakeMode < 1) settings.wakeMode++;
  } else if (y >= 328 && y < 382) {
    settings.bootAnimationEnabled = !settings.bootAnimationEnabled;
  } else {
    return;
  }
  saveSettings();
  drawSettingsPage();
}

void drawSleep() {
  display->fillScreen(RGB565_BLACK);
  drawCentered("IDLE LINK", 35, 2, COLOR_MUTED);
  drawCreature(false, true);
  display->setTextColor(COLOR_CYAN);
  display->setTextSize(2);
  display->setCursor(animationFrame ? 267 : 275, 170);
  display->print("z");
  display->setTextSize(3);
  display->setCursor(animationFrame ? 288 : 296, 142);
  display->print("Z");
  drawCentered(settings.wakeMode == 0 ? "TOUCH TO WAKE" : "PRESS BOOT TO WAKE",
               382, settings.wakeMode == 0 ? 2 : 1, COLOR_MUTED);
}

void updateCreatureAnimation() {
  if (sleeping) {
    // The sleeping creature is completely static; only the tiny dream marks move.
    display->fillRect(255, 128, 80, 72, RGB565_BLACK);
    display->setTextColor(COLOR_CYAN);
    display->setTextSize(2);
    display->setCursor(animationFrame ? 267 : 275, 170);
    display->print("z");
    display->setTextSize(3);
    display->setCursor(animationFrame ? 288 : 296, 142);
    display->print("Z");
  } else if (currentPage == PAGE_COMPANION) {
    if (pet.stage == 0) {
      // Animate only the companion viewport; the surrounding interface remains
      // untouched and the canvas-sized screen never flashes.
      display->fillRect(105, 125, 158, 192, COLOR_BACKGROUND);
      drawEgg(animationFrame, COLOR_BACKGROUND);
    } else {
      // Redraw only 2 small eye patches. The body and surrounding UI never flashes.
      drawFaceFrame(animationFrame);
    }
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
  panel->setBrightness(BRIGHTNESS_LEVELS[settings.brightnessIndex]);
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

void handleBattleTap(int16_t x, int16_t y) {
  const FamiliarBattleState state = battle.state();
  if (state == FamiliarBattleState::Idle && y >= 165 && y <= 260) {
    if (x < LCD_WIDTH / 2) {
      battle.beginHost(battlePlayerId(), pet.stage, 5);
    } else {
      display->fillScreen(COLOR_BACKGROUND);
      drawCentered("SCANNING BATTLE LINKS", 185, 2, COLOR_CYAN);
      drawCentered("4 SECOND SEARCH", 224, 1, COLOR_MUTED);
      battle.beginFind(battlePlayerId(), pet.stage, 5);
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
    battle.end();
    presentBattlePage();
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

  if (expander.begin(0x20)) {
    for (uint8_t pin : {0, 1, 2}) {
      expander.pinMode(pin, OUTPUT);
      expander.digitalWrite(pin, LOW);
    }
    delay(20);
    for (uint8_t pin : {0, 1, 2}) expander.digitalWrite(pin, HIGH);
  }

  detectHardware();
  audioReady = initAudio();

  panel->begin();
  if (settings.bootAnimationEnabled) {
    bootAnimation();
  } else {
    panel->setBrightness(BRIGHTNESS_LEVELS[settings.brightnessIndex]);
    display->fillScreen(COLOR_BACKGROUND);
    drawCentered("DIGIPET", 196, 4, COLOR_MINT);
    delay(350);
  }
  panel->setBrightness(BRIGHTNESS_LEVELS[settings.brightnessIndex]);
  initializeClockAndNetwork();
  generatePlayerId();
  loadPet();
  checkEvolution();
  drawLinkStatus("FORMING INTERFACE...", COLOR_CYAN);
  animateLinkStatus(0, COLOR_CYAN);

  if (psramFound()) {
    const bool firstCanvasReady = pageCanvasA.begin(GFX_SKIP_OUTPUT_BEGIN);
    const bool secondCanvasReady = pageCanvasB.begin(GFX_SKIP_OUTPUT_BEGIN);
    transitionFrame = static_cast<uint16_t *>(
        ps_malloc(static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * sizeof(uint16_t)));
    transitionsReady = firstCanvasReady && secondCanvasReady && transitionFrame;
  }
  Serial.printf("PSRAM: %u bytes; smooth transitions: %s\n",
                ESP.getPsramSize(), transitionsReady ? "ready" : "fallback");
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
  } else if (!sleeping && currentPage == PAGE_COMPANION && pet.stage == 0 &&
             now - lastAnimation >= 90) {
    lastAnimation = now;
    animationFrame = !animationFrame;
    updateCreatureAnimation();
  } else if (!sleeping && currentPage == PAGE_COMPANION) {
    if (!animationFrame && now >= nextBlink) {
      animationFrame = true;
      blinkUntil = now + 105;
      updateCreatureAnimation();
    } else if (animationFrame && now >= blinkUntil) {
      animationFrame = false;
      nextBlink = now + 2600 + random(2200);
      updateCreatureAnimation();
    }
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
      const bool battleLinkActive = currentPage == PAGE_BATTLE &&
                                    battle.state() != FamiliarBattleState::Idle;
      if (!battleLinkActive && abs(deltaX) > 60 && abs(deltaX) > abs(deltaY)) {
        if (deltaX < 0 && currentPage < PAGE_SETTINGS) {
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
      } else if (currentPage == PAGE_STATUS && touchLastY >= 360 && touchLastY < 448) {
        if (touchLastX < 124) performAction(0);
        else if (touchLastX < 244) performAction(1);
        else performAction(2);
      } else if (currentPage == PAGE_SETTINGS) {
        if (touchLastY >= 382) {
          if (touchLastX < LCD_WIDTH / 2) {
            showingPlayerId = true;
            presentPlayerIdPage();
          } else {
            showingUpdate = true;
            presentUpdatePage("STARTING SECURE CHECK", 0);
            const OtaResult otaResult = performSignedOta(
                networkConfig.ssid, networkConfig.password,
                [](const char *status, int progress) {
                  presentUpdatePage(status, progress);
                });
            presentUpdatePage(otaResultMessage(otaResult), -1);
            lastInteraction = millis();
          }
        } else {
          changeSetting(touchLastX, touchLastY);
        }
      } else if (currentPage == PAGE_BATTLE) {
        handleBattleTap(touchLastX, touchLastY);
      }
    }
    touchStartX = touchStartY = touchLastX = touchLastY = -1;
  }
}
