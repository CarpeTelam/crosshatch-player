#pragma once

#include <IClock.h>

// The IClock provider: esp_timer's microseconds on the device, the steady clock in
// the simulator, both as milliseconds since boot.
class GameClock final : public GameCore::IClock {
 public:
  uint64_t nowMs() const override;
};
