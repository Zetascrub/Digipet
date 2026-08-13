#include "pet_genome.h"

#include <esp_system.h>

namespace {
uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

uint8_t clampChannel(int value) {
  return static_cast<uint8_t>(constrain(value, 0, 255));
}

uint16_t tint(uint8_t red, uint8_t green, uint8_t blue, int amount) {
  return rgb565(clampChannel(red + amount), clampChannel(green + amount),
                clampChannel(blue + amount));
}

struct RgbPalette {
  uint8_t red, green, blue;
  uint8_t secondaryRed, secondaryGreen, secondaryBlue;
  uint8_t accentRed, accentGreen, accentBlue;
};

constexpr RgbPalette kLineagePalettes[] = {
    {205, 55, 32, 74, 18, 25, 255, 174, 42},   // Ember
    {28, 132, 210, 18, 52, 115, 101, 244, 255},// Tidal
    {43, 153, 83, 18, 74, 48, 170, 246, 83},   // Verdant
    {230, 194, 28, 43, 47, 69, 108, 244, 255}, // Volt
    {92, 45, 150, 31, 16, 55, 235, 80, 210},   // Umbral
    {31, 190, 177, 28, 54, 80, 226, 89, 255},  // Digital
    {108, 101, 220, 43, 37, 98, 225, 222, 255},// Crystal
    {102, 122, 139, 39, 49, 62, 246, 139, 48}, // Alloy
    {35, 61, 151, 18, 25, 67, 245, 211, 107},  // Celestial
    {143, 91, 53, 66, 47, 35, 242, 181, 94},   // Primal
};

uint32_t nextGene(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
}  // namespace

PetGenome generatePetGenome() {
  PetGenome genome{};
  for (uint8_t i = 0; i < 4; ++i) {
    genome.seed[i] = esp_random();
    genome.evolutionSeed[i] = esp_random();
  }
  uint32_t state = genome.seed[0] ^ genome.seed[1] ^ genome.seed[2] ^ genome.seed[3];
  if (!state) state = 0xA53C9E17;
  genome.lineage = nextGene(state) % static_cast<uint8_t>(EggLineage::Count);
  // Lineages suggest an element without completely dictating later forms.
  constexpr uint8_t preferredElements[] = {0, 1, 2, 3, 4, 5, 5, 3, 4, 2};
  genome.element = (nextGene(state) % 5 == 0)
      ? nextGene(state) % 6 : preferredElements[genome.lineage];
  genome.bodyType = nextGene(state) % 5;
  genome.widthGene = 72 + nextGene(state) % 113;
  genome.heightGene = 72 + nextGene(state) % 113;
  genome.headGene = 72 + nextGene(state) % 113;
  genome.limbGene = 72 + nextGene(state) % 113;
  genome.featureGenes = nextGene(state) & 0x0FFF;
  genome.faceGene = nextGene(state) & 0x1F;
  genome.markingGene = nextGene(state) & 0x1F;
  genome.paletteGene = nextGene(state) & 0x3F;
  genome.temperament = nextGene(state) % 6;
  // Roughly one pet in sixteen begins with a visible rare mutation.
  genome.mutationGenes = (nextGene(state) & 0x0F) == 0
      ? 1u << (nextGene(state) % 5) : 0;
  return genome;
}

PetPalette paletteForGenome(const PetGenome &genome) {
  const RgbPalette &base = kLineagePalettes[
      genome.lineage % static_cast<uint8_t>(EggLineage::Count)];
  const int variation = static_cast<int>(genome.paletteGene % 17) - 8;
  return {
      tint(base.red, base.green, base.blue, -72 + variation),
      tint(base.red, base.green, base.blue, variation),
      tint(base.red, base.green, base.blue, 58 + variation),
      tint(base.secondaryRed, base.secondaryGreen, base.secondaryBlue, variation),
      tint(base.accentRed, base.accentGreen, base.accentBlue, variation),
      tint(base.accentRed, base.accentGreen, base.accentBlue, 35 + variation),
  };
}

const char *eggLineageName(uint8_t lineage) {
  static const char *names[] = {"EMBER EGG", "TIDAL EGG", "VERDANT EGG",
      "VOLT EGG", "UMBRAL EGG", "DIGITAL EGG", "CRYSTAL EGG",
      "ALLOY EGG", "CELESTIAL EGG", "PRIMAL EGG"};
  return names[lineage % static_cast<uint8_t>(EggLineage::Count)];
}

const char *elementName(uint8_t element) {
  static const char *names[] = {"FIRE", "WATER", "NATURE", "ELECTRIC", "DARK", "DIGITAL"};
  return names[element % 6];
}
