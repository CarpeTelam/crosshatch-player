#pragma once

#include <cstdint>

namespace GameCore {

// Port: a monotonic millisecond clock (ch.time.ms, ch.timer). Its zero is
// arbitrary; callers use differences. Device and simulator providers live in
// src/games; host tests pass a fake they advance by hand.
class IClock {
 public:
  virtual ~IClock() = default;
  virtual uint64_t nowMs() const = 0;
};

}  // namespace GameCore
