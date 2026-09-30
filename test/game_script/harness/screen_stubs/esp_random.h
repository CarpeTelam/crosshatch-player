#pragma once

// A fixed-seed generator for GameRandom::next32 (a game's ch.random is not under test).

#include <cstdint>

inline uint32_t esp_random() {
  static uint32_t state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
