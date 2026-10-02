#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ConverterBmpLayout.h"
#include "GameImages.h"

using namespace GameCore;
using ConverterBmpLayout::converterBmp1bit;
using ConverterBmpLayout::converterHeader1bit;

namespace {

void set16(std::vector<uint8_t>& bytes, const size_t at, const uint16_t v) {
  bytes[at] = static_cast<uint8_t>(v & 0xFF);
  bytes[at + 1] = static_cast<uint8_t>(v >> 8);
}

void set32(std::vector<uint8_t>& bytes, const size_t at, const uint32_t v) {
  for (size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

size_t layoutBytes(const int width, const int height) {
  return IMAGE_HEADER_BYTES + static_cast<size_t>((width + 31) / 32 * 4) * height;
}

// Checks a header as a file of `fileBytes` bytes with the whole budget left.
ImageCheck check(const std::vector<uint8_t>& header, const size_t fileBytes, const size_t budget = IMAGES_BYTES) {
  ImageHeader out;
  return checkImageHeader(header.data(), header.size(), fileBytes, budget, out);
}

TEST(GameImagesTest, TheConstantsAreThePackageLimits) {
  EXPECT_EQ(IMAGES_BYTES, 131072u);
  EXPECT_EQ(MAX_IMAGES, 32u);
  EXPECT_EQ(IMAGE_HEADER_BYTES, 62u);
  EXPECT_EQ(IMAGE_NAME_BYTES, 32u);
  EXPECT_EQ(converterHeader1bit(1, 1).size(), IMAGE_HEADER_BYTES);
}

TEST(GameImagesTest, AConverterHeaderChecksOkWithItsPaddedRows) {
  struct Case {
    int width;
    int height;
    uint32_t rowBytes;
  };
  // 1 and 32 px fit one 4-byte row; 33 px needs a second word.
  const Case cases[] = {{1, 1, 4}, {32, 5, 4}, {33, 7, 8}, {100, 60, 16}, {37, 37, 8}};
  for (const Case& c : cases) {
    const std::vector<uint8_t> header = converterHeader1bit(c.width, c.height);
    ImageHeader out;
    const size_t file = layoutBytes(c.width, c.height);
    ASSERT_EQ(checkImageHeader(header.data(), header.size(), file, IMAGES_BYTES, out), ImageCheck::Ok) << c.width;
    EXPECT_EQ(out.width, static_cast<uint32_t>(c.width));
    EXPECT_EQ(out.height, static_cast<uint32_t>(c.height));
    EXPECT_EQ(out.rowBytes, c.rowBytes) << c.width;
    EXPECT_EQ(out.pixelBytes(), file - IMAGE_HEADER_BYTES) << c.width;
  }
  // A longer header buffer (the file's first bytes) is fine too.
  std::vector<uint8_t> longer = converterBmp1bit(33, 2, [](int, int) { return true; });
  EXPECT_EQ(check(longer, longer.size()), ImageCheck::Ok);
}

TEST(GameImagesTest, ShortFilesAreTruncated) {
  const std::vector<uint8_t> header = converterHeader1bit(33, 7);
  const size_t file = layoutBytes(33, 7);
  ImageHeader out;
  EXPECT_EQ(checkImageHeader(header.data(), 61, file, IMAGES_BYTES, out), ImageCheck::Truncated);
  EXPECT_EQ(checkImageHeader(header.data(), 0, file, IMAGES_BYTES, out), ImageCheck::Truncated);
  EXPECT_EQ(checkImageHeader(nullptr, 62, file, IMAGES_BYTES, out), ImageCheck::Truncated);
  EXPECT_EQ(check(header, 61), ImageCheck::Truncated);
  EXPECT_EQ(check(header, 62), ImageCheck::Truncated);
  EXPECT_EQ(check(header, file - 1), ImageCheck::Truncated);
  // Before the signature: a 10-byte file of anything is truncated.
  EXPECT_EQ(check(std::vector<uint8_t>(10, 'x'), 10), ImageCheck::Truncated);
  // Huge dimensions do not wrap: the file is simply too short for them.
  std::vector<uint8_t> huge = converterHeader1bit(1, 1);
  set32(huge, 18, 0x7FFFFFFF);
  set32(huge, 22, static_cast<uint32_t>(-1));
  set32(huge, 34, 0x10000000);  // (2^31 - 1 + 31) / 32 * 4 bytes
  set32(huge, 2, 62 + 0x10000000);
  EXPECT_EQ(check(huge, 4096), ImageCheck::Truncated);
  set32(huge, 22, 0x80000000);  // INT32_MIN rows: the size overflows 32 bits, so no field can match
  EXPECT_EQ(check(huge, 4096), ImageCheck::WrongLayout);
  EXPECT_EQ(out.width, 0u);  // untouched by every failure
}

TEST(GameImagesTest, OnlyBmIsABmp) {
  const size_t file = layoutBytes(8, 8);
  std::vector<uint8_t> header = converterHeader1bit(8, 8);
  header[0] = 'P';
  EXPECT_EQ(check(header, file), ImageCheck::NotBmp);
  header = converterHeader1bit(8, 8);
  header[1] = 'A';
  EXPECT_EQ(check(header, file), ImageCheck::NotBmp);
  header = std::vector<uint8_t>(62, 0);
  header[0] = 0x89;  // a PNG's first bytes
  header[1] = 'P';
  EXPECT_EQ(check(header, 1000), ImageCheck::NotBmp);
}

TEST(GameImagesTest, OtherDepthsAreWrongDepth) {
  for (const uint16_t bits : {0, 2, 4, 8, 16, 24, 32}) {
    std::vector<uint8_t> header = converterHeader1bit(8, 8);
    set16(header, 28, bits);
    EXPECT_EQ(check(header, layoutBytes(8, 8)), ImageCheck::WrongDepth) << bits;
  }
}

TEST(GameImagesTest, AnyOtherFieldIsWrongLayout) {
  const int w = 33;
  const int h = 7;
  const size_t file = layoutBytes(w, h);
  struct Case {
    const char* what;
    size_t at;
    uint32_t value;
    int bytes;
  };
  const Case cases[] = {
      {"bottom-up", 22, static_cast<uint32_t>(h), 4},
      {"zero height", 22, 0, 4},
      {"zero width", 18, 0, 4},
      {"negative width", 18, static_cast<uint32_t>(-w), 4},
      {"pixel offset", 10, 70, 4},
      {"DIB size", 14, 124, 4},
      {"planes", 26, 2, 2},
      {"compression", 30, 3, 4},
      {"image size", 34, 0, 4},
      {"file size field", 2, static_cast<uint32_t>(file + 1), 4},
      {"colours used", 46, 0, 4},
      {"swapped palette", 54, 0x00FFFFFF, 4},
      {"grey palette", 58, 0x00808080, 4},
  };
  for (const Case& c : cases) {
    std::vector<uint8_t> header = converterHeader1bit(w, h);
    if (c.bytes == 2) {
      set16(header, c.at, static_cast<uint16_t>(c.value));
    } else {
      set32(header, c.at, c.value);
    }
    EXPECT_EQ(check(header, file), ImageCheck::WrongLayout) << c.what;
  }
  // Longer than its layout.
  EXPECT_EQ(check(converterHeader1bit(w, h), file + 1), ImageCheck::WrongLayout);
  EXPECT_EQ(check(converterHeader1bit(w, h), file + 4096), ImageCheck::WrongLayout);
  // The resolution fields are not part of the check.
  std::vector<uint8_t> dpi = converterHeader1bit(w, h);
  set32(dpi, 38, 3780);
  set32(dpi, 42, 0);
  EXPECT_EQ(check(dpi, file), ImageCheck::Ok);
}

TEST(GameImagesTest, TheBudgetLeftIsInclusive) {
  const std::vector<uint8_t> header = converterHeader1bit(100, 60);
  const size_t file = layoutBytes(100, 60);
  EXPECT_EQ(check(header, file, file), ImageCheck::Ok);
  EXPECT_EQ(check(header, file, file - 1), ImageCheck::OverBudget);
  EXPECT_EQ(check(header, file, 0), ImageCheck::OverBudget);
  // One image of exactly the whole budget, and one row more.
  const int rows = static_cast<int>((IMAGES_BYTES - IMAGE_HEADER_BYTES) / 4);  // 1 px wide: 4 bytes a row
  EXPECT_EQ(layoutBytes(1, rows), IMAGES_BYTES - 2);
  EXPECT_EQ(check(converterHeader1bit(1, rows), layoutBytes(1, rows)), ImageCheck::Ok);
  EXPECT_EQ(check(converterHeader1bit(1, rows + 1), layoutBytes(1, rows + 1)), ImageCheck::OverBudget);
  // The layout is checked before the budget.
  EXPECT_EQ(check(header, file + 1, 0), ImageCheck::WrongLayout);
}

TEST(GameImagesTest, EveryCheckHasAName) {
  for (const ImageCheck c : {ImageCheck::Ok, ImageCheck::Truncated, ImageCheck::NotBmp, ImageCheck::WrongDepth,
                             ImageCheck::WrongLayout, ImageCheck::OverBudget, ImageCheck::TooMany}) {
    EXPECT_STRNE(imageCheckName(c), "?");
  }
}

// Adds a converter file of `rows` 1 px rows (4 bytes each) to `budget`.
ImageCheck addRows(ImageBudget& budget, const int rows) {
  const std::vector<uint8_t> header = converterHeader1bit(1, rows);
  ImageHeader out;
  return budget.add(header.data(), header.size(), layoutBytes(1, rows), out);
}

TEST(GameImagesTest, TheBudgetCountsEveryImageBeforeIt) {
  // Converter files are 62 + 4k bytes, so two sum to a multiple of 4: these two
  // fill IMAGES_BYTES exactly, and one more row in the second passes it by 4.
  const int first = 16000;
  const int second = static_cast<int>((IMAGES_BYTES - 2 * IMAGE_HEADER_BYTES) / 4) - first;
  ASSERT_EQ(layoutBytes(1, first) + layoutBytes(1, second), IMAGES_BYTES);

  ImageBudget exact;
  EXPECT_EQ(addRows(exact, first), ImageCheck::Ok);
  EXPECT_EQ(addRows(exact, second), ImageCheck::Ok);
  EXPECT_EQ(exact.count, 2u);
  EXPECT_EQ(exact.fileBytes, IMAGES_BYTES);
  EXPECT_EQ(exact.pixelBytes, IMAGES_BYTES - 2 * IMAGE_HEADER_BYTES);
  EXPECT_EQ(addRows(exact, 1), ImageCheck::OverBudget);  // nothing is left

  ImageBudget over;
  EXPECT_EQ(addRows(over, first), ImageCheck::Ok);
  EXPECT_EQ(addRows(over, second + 1), ImageCheck::OverBudget);
  EXPECT_EQ(over.count, 1u);  // the refused image is not counted
  EXPECT_EQ(over.fileBytes, layoutBytes(1, first));
  EXPECT_EQ(over.pixelBytes, layoutBytes(1, first) - IMAGE_HEADER_BYTES);
  EXPECT_EQ(addRows(over, second), ImageCheck::Ok);  // what still fits does
}

TEST(GameImagesTest, TheThirtyThirdImageIsRefused) {
  ImageBudget budget;
  for (size_t i = 0; i < MAX_IMAGES; ++i) EXPECT_EQ(addRows(budget, 2), ImageCheck::Ok) << i;
  EXPECT_EQ(budget.count, MAX_IMAGES);
  const size_t bytes = budget.fileBytes;
  EXPECT_EQ(addRows(budget, 2), ImageCheck::TooMany);
  EXPECT_EQ(budget.count, MAX_IMAGES);
  EXPECT_EQ(budget.fileBytes, bytes);
}

TEST(GameImagesTest, ABadImageInTheMiddleIsRefusedAlone) {
  ImageBudget budget;
  EXPECT_EQ(addRows(budget, 3), ImageCheck::Ok);
  std::vector<uint8_t> eightBit = converterHeader1bit(8, 8);
  set16(eightBit, 28, 8);
  ImageHeader out;
  EXPECT_EQ(budget.add(eightBit.data(), eightBit.size(), layoutBytes(8, 8), out), ImageCheck::WrongDepth);
  EXPECT_EQ(addRows(budget, 5), ImageCheck::Ok);
  EXPECT_EQ(budget.count, 2u);
  EXPECT_EQ(budget.fileBytes, layoutBytes(1, 3) + layoutBytes(1, 5));
  EXPECT_EQ(budget.pixelBytes, 4u * (3 + 5));
}

TEST(GameImagesTest, ImageNamesAreLowercaseStemsOtherThanTheReservedOnes) {
  char name[IMAGE_NAME_BYTES + 1];
  const auto nameOf = [&](const std::string& file) { return imageNameOf(file.data(), file.size(), name); };
  ASSERT_TRUE(nameOf("badge.bmp"));
  EXPECT_STREQ(name, "badge");
  ASSERT_TRUE(nameOf("a_1.bmp"));
  EXPECT_STREQ(name, "a_1");
  ASSERT_TRUE(nameOf("icons.bmp"));
  EXPECT_STREQ(name, "icons");
  ASSERT_TRUE(nameOf("icon_2.bmp"));
  ASSERT_TRUE(nameOf(std::string(32, 'z') + ".bmp"));
  EXPECT_EQ(std::string(name), std::string(32, 'z'));

  // Names beside the reserved ones are images.
  ASSERT_TRUE(nameOf("titles.bmp"));
  ASSERT_TRUE(nameOf("title_2.bmp"));
  ASSERT_TRUE(nameOf("handoffs.bmp"));
  ASSERT_TRUE(nameOf("hand_off.bmp"));
  EXPECT_STREQ(name, "hand_off");

  const std::string misses[] = {
      "icon.bmp",     // the launcher's icon, never an image
      "title.bmp",    // the title screen's splash, drawn by the runtime alone
      "handoff.bmp",  // the hidden hand-off page, drawn by the runtime alone
      std::string(33, 'z') + ".bmp",
      "Badge.bmp",
      "badge.BMP",
      "Icon.BMP",
      "bad-name.bmp",
      "bad name.bmp",
      ".bmp",
      "bmp",
      "",
      "badge.lua",
      "badge.bmpx",
      "badge.bm",
      std::string("bad\0e.bmp", 9),
  };
  for (const std::string& file : misses) {
    std::memset(name, 'q', sizeof(name));
    EXPECT_FALSE(nameOf(file)) << file;
  }
  // The length bounds the name: no NUL is needed after it.
  const char unterminated[] = {'d', 'o', 't', '.', 'b', 'm', 'p', 'X'};
  ASSERT_TRUE(imageNameOf(unterminated, 7, name));
  EXPECT_STREQ(name, "dot");
}

// The reserved images (AD-15, as amended 2026-10-02): icon, title, and handoff, whole names in any letter case, so
// GameAssets skips their .bmp files without calling them misnamed images.
TEST(GameImagesTest, ReservedImagesAreIconTitleAndHandoff) {
  const auto reserved = [](const std::string& stem) { return isReservedImage(stem.data(), stem.size()); };
  for (const char* stem : {"icon", "title", "handoff", "ICON", "Title", "HandOff"}) EXPECT_TRUE(reserved(stem)) << stem;
  for (const char* stem : {"", "ico", "icons", "titl", "titles", "hand", "handoff_", "hand_off", "badge", "x"}) {
    EXPECT_FALSE(reserved(stem)) << stem;
  }
  // The length bounds the name: "title" inside a longer buffer is reserved, and a prefix is not.
  EXPECT_TRUE(isReservedImage("titles", 5));
  EXPECT_FALSE(isReservedImage("title", 4));
  EXPECT_FALSE(isReservedImage(nullptr, 0));
  // The pages' limits are AD-15's.
  EXPECT_EQ(TITLE_IMAGE_WIDTH, 480u);
  EXPECT_EQ(TITLE_IMAGE_HEIGHT, 480u);
  EXPECT_EQ(HANDOFF_IMAGE_WIDTH, 480u);
  EXPECT_EQ(HANDOFF_IMAGE_HEIGHT, 800u);
}

TEST(GameImagesTest, LooksLikeImageIsAnyCaseBmp) {
  const auto looks = [](const std::string& file) { return looksLikeImage(file.data(), file.size()); };
  EXPECT_TRUE(looks("badge.bmp"));
  EXPECT_TRUE(looks("Icon.BMP"));
  EXPECT_TRUE(looks("bad-name.bmp"));
  EXPECT_TRUE(looks("x.Bmp"));
  EXPECT_TRUE(looks("icon.bmp"));
  EXPECT_FALSE(looks(".bmp"));
  EXPECT_FALSE(looks("bmp"));
  EXPECT_FALSE(looks(""));
  EXPECT_FALSE(looks("badge.lua"));
  EXPECT_FALSE(looks("badge.bmpx"));
  EXPECT_FALSE(looks("badge_bmp"));
}

TEST(GameImagesTest, FindMatchesWholeNamesOnly) {
  std::vector<ImageSpan> spans(2, ImageSpan{});
  std::strcpy(spans[0].name, "badge");
  spans[0].offset = 0;
  std::strcpy(spans[1].name, "dot");
  spans[1].offset = 960;
  const uint8_t pixels[1024] = {};
  const GameImages images{spans.data(), spans.size(), pixels};

  EXPECT_EQ(images.find("badge", 5), 0);
  EXPECT_EQ(images.find("dot", 3), 1);
  EXPECT_EQ(images.pixelsOf(spans[1]), pixels + 960);
  const std::string misses[] = {
      "no_such_image",
      "",
      "bad",
      "badges",
      "do",
      "dots",
      "BADGE",
      std::string("badge\0x", 7),
      std::string("badge\0", 6),
      std::string("\0badge", 6),
      std::string(40, 'b'),
  };
  for (const std::string& name : misses) {
    EXPECT_EQ(images.find(name.data(), name.size()), -1) << name;
  }
  // The length bounds the name.
  EXPECT_EQ(images.find("dotted", 3), 1);
  // No images: every name misses.
  EXPECT_EQ(NO_IMAGES.find("badge", 5), -1);
  EXPECT_EQ(NO_IMAGES.count, 0u);
  EXPECT_EQ(NO_IMAGES.spans, nullptr);
}

}  // namespace
