#pragma once

// pet_genome.cpp's only ESP-IDF dependency: esp_random(), used by
// generatePetGenome()/blendPetGenomes(). The render harness always builds
// genomes via derivePetGenome() with fixed seeds instead (deterministic
// output is what you want from a visual-diff tool), so this never actually
// gets called -- it only needs to exist so the translation unit compiles.

#include <cstdint>

inline uint32_t esp_random() { return 0; }
