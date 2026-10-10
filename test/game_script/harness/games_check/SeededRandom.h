#pragma once

// The IRandom a round plays over: a splitmix32 stream from the round's own seed, so the same seed gives a game the same
// math.random draws and string-hash seed (LuaGame::load takes one value from it, openSandbox four more), and another
// seed gives other ones. No process-wide state: every round and every script VM owns its stream, so the order the
// check plays rounds in changes nothing.
//
// Stands in for the device's GameRandom (src/games/GameRandom.h, esp_random), whose draws no test can repeat; the
// check needs repeatable draws and nothing else of it, and GamesCheckEngineTest pins that the same seed repeats.

#include <IRandom.h>

#include <cstdint>

namespace games_check {

class SeededRandom final : public GameCore::IRandom {
 public:
  explicit SeededRandom(const uint32_t seed) : state(seed) {}

  uint32_t next32() override {
    state += 0x9E3779B9u;
    uint32_t word = state;
    word = (word ^ (word >> 16)) * 0x85EBCA6Bu;
    word = (word ^ (word >> 13)) * 0xC2B2AE35u;
    return word ^ (word >> 16);
  }

 private:
  uint32_t state;
};

}  // namespace games_check
