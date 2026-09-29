#pragma once

// Helpers the harness suites share: a game folder on the fake SD card, converted
// image files, and the log and PSRAM state a test starts from.

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ConverterBmpLayout.h"
#include "HalMemoryStub.h"
#include "HalStorage.h"
#include "Logging.h"

namespace harness {

using Bytes = std::vector<uint8_t>;

// A converted 1-bit image file (the converter's layout, ConverterBmpLayout.h).
inline Bytes bmpFile(const int width, const int height, const std::function<bool(int x, int y)>& white) {
  return ConverterBmpLayout::converterBmp1bit(width, height, white);
}

// The pixel rows of a converted file: what follows its 62-byte header.
inline Bytes rowsOf(const Bytes& file) { return Bytes(file.begin() + 62, file.end()); }

// A pattern with no long runs and no symmetry, so an image read from the wrong
// offset, or drawn shifted, shows.
inline bool speckle(const int x, const int y, const int seed = 0) { return ((x * 7 + y * 13 + seed) % 5) < 2; }

inline bool logHas(const std::string& part) { return fakelog::any(part); }

// Every test starts with an empty card, an empty log, and no PSRAM blocks, and ends with
// every block freed and none freed with a damaged guard band, so a leak or a write past a
// block fails the test that caused it whether or not that test looks. A test that damages
// a block on purpose sets `expectCleanPsram` false. A fixture that owns blocks frees them
// in its own TearDown, then calls this one.
class HarnessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
    fakepsram::reset();
  }
  void TearDown() override {
    if (!expectCleanPsram) return;
    EXPECT_EQ(fakepsram::liveBlocks, 0u) << "a PSRAM block was never freed";
    EXPECT_EQ(fakepsram::overruns, 0u) << "a PSRAM block was written past its end";
  }

  bool expectCleanPsram = true;
};

}  // namespace harness
