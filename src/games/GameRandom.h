#pragma once

#include <IRandom.h>

// The IRandom provider: the hardware RNG on the device, the OS entropy source in
// the simulator.
class GameRandom final : public GameCore::IRandom {
 public:
  uint32_t next32() override;
};
