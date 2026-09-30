// The link between PngToBmpConverter and test/game_core/ConverterBmpLayout.h (retro A2 of epic-icon-library, entry
// 13 of epic-install-and-launcher). The header rebuilds the converter's 1-bit output for the image tests, which
// could not build the converter; this suite does build it (installer.cmake), so the mirror is checked against the
// real thing here: a change to the converter's header or row layout fails this test instead of leaving the mirror,
// and the image tests built on it, describing a file the device no longer writes.

#include <PngToBmpConverter.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "ConverterBmpLayout.h"
#include "InstallerSupport.h"

using namespace installer_test;

namespace {

class CollectingPrint final : public Print {
 public:
  size_t write(const uint8_t byte) override {
    bytes.push_back(byte);
    return 1;
  }
  size_t write(const uint8_t* buffer, const size_t size) override {
    bytes.insert(bytes.end(), buffer, buffer + size);
    return size;
  }
  Bytes bytes;
};

// The converter's output for a width x height PNG of black and white pixels, `white(x, y)` deciding each; the
// image keeps its own size (target 0 x 0), as the installer asks for a package's images.
Bytes convert(const int width, const int height, const std::function<bool(int, int)>& white, bool& converted) {
  const std::string path = "/pngs/in.png";
  fakesd::addFile(path, makePng(width, height, [&](const int x, const int y) { return white(x, y) ? 255 : 0; }));
  HalFile png;
  EXPECT_TRUE(Storage.openFileForRead("TEST", path.c_str(), png));
  CollectingPrint out;
  converted = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, out, 0, 0);
  return out.bytes;
}

class ConverterLayoutTest : public ::testing::Test {
 protected:
  void SetUp() override { fakesd::reset(); }
};

TEST_F(ConverterLayoutTest, TheMirrorIsTheConvertersOutputByteForByte) {
  struct Size {
    int width;
    int height;
  };
  // Widths either side of a byte and of a 4-byte row group, so the padding differs; heights that are odd and even.
  const Size sizes[] = {{1, 1}, {7, 3}, {8, 2}, {9, 5}, {13, 4}, {31, 2}, {32, 3}, {33, 2}, {40, 6}, {64, 64}};
  const std::function<bool(int, int)> patterns[] = {
      [](int, int) { return false; },
      [](int, int) { return true; },
      [](int x, int y) { return (x + y) % 2 == 0; },
      [](int x, int y) { return (x * 3 + y * 5) % 7 < 3; },
      [](int x, int) { return x % 8 == 7; },  // the last bit of every byte
  };
  int checked = 0;
  for (const Size size : sizes) {
    for (const auto& pattern : patterns) {
      bool converted = false;
      const Bytes real = convert(size.width, size.height, pattern, converted);
      ASSERT_TRUE(converted) << size.width << "x" << size.height;
      EXPECT_EQ(real, ConverterBmpLayout::converterBmp1bit(size.width, size.height, pattern))
          << size.width << "x" << size.height << " pattern " << checked % 5;
      ++checked;
    }
  }
  EXPECT_EQ(checked, 50);
}

TEST_F(ConverterLayoutTest, TheHeaderMirrorIsThePartTheGameLoaderChecks) {
  bool converted = false;
  const Bytes real = convert(13, 4, [](int x, int y) { return x > y; }, converted);
  ASSERT_TRUE(converted);
  const Bytes header = ConverterBmpLayout::converterHeader1bit(13, 4);
  ASSERT_EQ(header.size(), 62u);
  ASSERT_GE(real.size(), header.size());
  EXPECT_EQ(Bytes(real.begin(), real.begin() + 62), header);
}

}  // namespace
