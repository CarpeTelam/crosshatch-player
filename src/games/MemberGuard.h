#pragma once

#include <MinizConfig.h>

#include <cstddef>
#include <cstdint>

// What GamePackageInstaller checks of a member's bytes as they stream out of the zip (spine AD-15): never more
// than the directory declared (a zip bomb ends here, at the declared size), no Lua bytecode chunk, and a running
// CRC-32 to compare with the directory's. Its own header so that the host tests can drive it chunk by chunk:
// lib/ZipFile already stops a deflate stream at the declared size and reads a stored member for exactly that
// size, so through ZipFile the cap below never fires.
struct MemberGuard {
  MemberGuard(const uint32_t declared, const bool isLua) : declared(declared), isLua(isLua) {}

  // True when `count` more bytes may go on.
  bool admit(const uint8_t* bytes, const size_t count) {
    // Nothing to check, and mz_crc32 answers its initial value for a null pointer, which would reset the CRC.
    if (count == 0) return true;
    if (count > declared - total) {
      overrun = true;
    } else if (isLua && total == 0 && bytes[0] == LUA_BYTECODE_SIGNATURE) {
      binary = true;
    } else {
      crc = static_cast<uint32_t>(mz_crc32(crc, bytes, count));
      total += static_cast<uint32_t>(count);
      return true;
    }
    return false;
  }

  // The first byte of Lua's precompiled chunks (ESC); source text never starts with it.
  static constexpr uint8_t LUA_BYTECODE_SIGNATURE = 0x1B;

  const uint32_t declared;
  const bool isLua;
  uint32_t crc = MZ_CRC32_INIT;
  uint32_t total = 0;
  bool overrun = false;
  bool binary = false;
};
