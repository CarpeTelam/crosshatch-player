#pragma once

#include <cstdint>

// The one setting HomeActivity reads (src/CrossPointSettings.h): whether the library walk reads
// each book's own title and author.
class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }
  uint8_t libraryUseMetadata = 1;
};

#define SETTINGS CrossPointSettings::getInstance()
