#pragma once

#include <cstdint>

namespace GameCore {

// Port: a source of unpredictable 32-bit values for seeds (the Lua string-hash
// seed, and later math.random and session ids). Device and simulator providers
// live in src/games; host tests pass a fixed fake.
class IRandom {
 public:
  virtual ~IRandom() = default;
  virtual uint32_t next32() = 0;
};

}  // namespace GameCore
