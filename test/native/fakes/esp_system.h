#pragma once

// Host-side stand-in for esp_random(), the only ESP-IDF entropy source
// pet_genome.cpp calls. Deterministic and test-seedable (via
// esp_random_reseed) rather than truly random, so genome generation and
// blending are reproducible in unit tests.

#include <cstdint>

namespace pet_genome_test_fakes {
inline uint32_t &esp_random_state() {
  static uint32_t state = 0x9E3779B9u;
  return state;
}
}  // namespace pet_genome_test_fakes

inline void esp_random_reseed(uint32_t seed) {
  pet_genome_test_fakes::esp_random_state() = seed ? seed : 1;
}

inline uint32_t esp_random() {
  uint32_t &state = pet_genome_test_fakes::esp_random_state();
  // xorshift32 -- same shape as pet_genome.cpp's own PRNG, good enough for
  // deterministic test entropy.
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
