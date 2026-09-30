#pragma once

// The part of Arduino's Print that host code writes through: HalFile derives from it
// (lib/hal/HalStorage.h), so ZipFile::readFileToStream and the converters' `Print&` can
// target a file on the fake card.

#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t written = 0;
    while (written < size && write(buffer[written]) == 1) ++written;
    return written;
  }
  virtual void flush() {}
};
