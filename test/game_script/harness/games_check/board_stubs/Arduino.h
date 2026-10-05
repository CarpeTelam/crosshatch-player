#pragma once

// A stand-in for the Arduino core, only so that the SDK's BoardConfig.h compiles on the host for BoardInsetsTest.cpp
// (the one file that sees this folder: games_check.cmake gives it this directory first). It is as little as that
// header needs: `Serial`, `Serial0`, the pin calls it makes in holdPowerRails and releaseSdRail, and the constants
// they pass. Nothing here is behaviour: the test reads the board profiles' constants and calls none of those.

#include <cstddef>
#include <cstdint>
#include <initializer_list>

struct HardwareSerial {
  template <class... Args>
  void printf(const char*, Args...) {}
};
inline HardwareSerial Serial;
inline HardwareSerial Serial0;

constexpr int OUTPUT = 1;
constexpr int HIGH = 1;
constexpr int LOW = 0;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
