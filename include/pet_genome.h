#pragma once

#include <Arduino.h>

enum class EggLineage : uint8_t {
  Ember,
  Tidal,
  Verdant,
  Volt,
  Umbral,
  Digital,
  Crystal,
  Alloy,
  Celestial,
  Primal,
  Count,
};

enum class PetBodyType : uint8_t { Quadruped, Humanoid, Avian, Blob, Serpent };
enum class PetElement : uint8_t { Fire, Water, Nature, Electric, Dark, Digital };

struct PetGenome {
  uint32_t seed[4];
  uint32_t evolutionSeed[4];
  uint16_t featureGenes;
  uint8_t lineage;
  uint8_t element;
  uint8_t bodyType;
  uint8_t widthGene;
  uint8_t heightGene;
  uint8_t headGene;
  uint8_t limbGene;
  uint8_t faceGene;
  uint8_t markingGene;
  uint8_t paletteGene;
  uint8_t temperament;
  uint8_t mutationGenes;
  uint8_t reserved[3];
};

struct PetPalette {
  uint16_t primaryDark;
  uint16_t primary;
  uint16_t primaryLight;
  uint16_t secondary;
  uint16_t accent;
  uint16_t glow;
};

PetGenome generatePetGenome();
PetPalette paletteForGenome(const PetGenome &genome);
const char *eggLineageName(uint8_t lineage);
const char *elementName(uint8_t element);

