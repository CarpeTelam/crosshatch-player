// The gray viewer's decode arithmetic (lib/GameDisplayLab/ImageRows, story dlab-g3): which files the Image row lists
// and their order, the converters' size limits and PNG checks, a PNG row to 8-bit gray as the converter converts one
// (alpha on white), JPEGDEC's coarse scale, and the fit to the logical screen (the fit and the I/O matrix's Fit row).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "ImageRows.h"

namespace {

using namespace GameDisplayLab;

TEST(ImageRows, KindsByExtensionAnyCase) {
  EXPECT_EQ(imageKindOf("ink-sphere.png"), ImageKind::Png);
  EXPECT_EQ(imageKindOf("TEXT.PNG"), ImageKind::Png);
  EXPECT_EQ(imageKindOf("photo.jpg"), ImageKind::Jpeg);
  EXPECT_EQ(imageKindOf("photo.JPEG"), ImageKind::Jpeg);
  EXPECT_EQ(imageKindOf("photo.JpG"), ImageKind::Jpeg);
}

TEST(ImageRows, OtherFilesAreIgnored) {
  EXPECT_EQ(imageKindOf(nullptr), ImageKind::None);
  EXPECT_EQ(imageKindOf(""), ImageKind::None);
  EXPECT_EQ(imageKindOf("notes.txt"), ImageKind::None);
  EXPECT_EQ(imageKindOf("cover.bmp"), ImageKind::None);
  EXPECT_EQ(imageKindOf("png"), ImageKind::None);
  EXPECT_EQ(imageKindOf("archive.png.zip"), ImageKind::None);
  // Hidden files, macOS's resource forks included.
  EXPECT_EQ(imageKindOf(".png"), ImageKind::None);
  EXPECT_EQ(imageKindOf("._ink-sphere.png"), ImageKind::None);
  EXPECT_EQ(imageKindOf(".hidden.jpg"), ImageKind::None);
}

TEST(ImageRows, NamesSortCaseInsensitively) {
  std::vector<const char*> names = {"text.png", "Gradients.png", "ink-sphere.png", "hatch-and-weights.png", "a.png"};
  std::sort(names.begin(), names.end(), nameLess);
  const std::vector<std::string> sorted(names.begin(), names.end());
  EXPECT_EQ(sorted, (std::vector<std::string>{"a.png", "Gradients.png", "hatch-and-weights.png", "ink-sphere.png",
                                              "text.png"}));
  EXPECT_FALSE(nameLess("A.png", "a.png"));
  EXPECT_FALSE(nameLess("a.png", "A.png"));
  EXPECT_TRUE(nameLess("a", "ab"));
  EXPECT_FALSE(nameLess("ab", "a"));
}

TEST(ImageList, NoFolderOrNoFilesIsTheChartOnly) {
  // The I/O matrix's first row: the Image row offers the chart only.
  const ImageList none;
  EXPECT_EQ(none.files(), 0u);
  EXPECT_EQ(none.entries(), 1u);
  EXPECT_EQ(none.file(0), nullptr);
  EXPECT_EQ(ImageList::nextEntry(0, none.entries()), 0u);
  std::array<ImageName, 4> names{};
  ImageList empty(names.data(), names.size());
  EXPECT_EQ(empty.add("notes.txt", false), ImageList::Add::Ignored);
  EXPECT_EQ(empty.add("pictures.png", true), ImageList::Add::Ignored);  // a folder named like a picture
  empty.sort();
  EXPECT_EQ(empty.entries(), 1u);
}

TEST(ImageList, FilesAreListedByNameAfterTheChart) {
  std::array<ImageName, 8> names{};
  ImageList list(names.data(), names.size());
  EXPECT_EQ(list.add("text.png", false), ImageList::Add::Listed);
  EXPECT_EQ(list.add("README.md", false), ImageList::Add::Ignored);
  EXPECT_EQ(list.add("._text.png", false), ImageList::Add::Ignored);
  EXPECT_EQ(list.add("Gradients.PNG", false), ImageList::Add::Listed);
  EXPECT_EQ(list.add("photo.jpeg", false), ImageList::Add::Listed);
  EXPECT_EQ(list.add("ink-sphere.png", false), ImageList::Add::Listed);
  list.sort();
  ASSERT_EQ(list.files(), 4u);
  EXPECT_EQ(list.entries(), 5u);
  EXPECT_STREQ(list.file(0), "Gradients.PNG");
  EXPECT_STREQ(list.file(1), "ink-sphere.png");
  EXPECT_STREQ(list.file(2), "photo.jpeg");
  EXPECT_STREQ(list.file(3), "text.png");
  EXPECT_EQ(list.file(4), nullptr);
  // Next steps through them and, after the last, back to the chart (I/O matrix: Next while holding).
  size_t entry = 0;
  std::vector<size_t> seen;
  for (int i = 0; i < 6; ++i) {
    entry = ImageList::nextEntry(entry, list.entries());
    seen.push_back(entry);
  }
  EXPECT_EQ(seen, (std::vector<size_t>{1, 2, 3, 4, 0, 1}));
}

TEST(ImageList, LongNamesAndAFullListAreRefused) {
  std::array<ImageName, 2> names{};
  ImageList list(names.data(), names.size());
  const std::string longest(IMAGE_NAME_BYTES - 1 - 4, 'a');
  EXPECT_EQ(list.add((longest + ".png").c_str(), false), ImageList::Add::Listed);
  EXPECT_EQ(list.add((longest + "a.png").c_str(), false), ImageList::Add::TooLong);
  EXPECT_EQ(list.add("b.png", false), ImageList::Add::Listed);
  EXPECT_EQ(list.add("c.png", false), ImageList::Add::Full);
  EXPECT_EQ(list.add("notes.txt", false), ImageList::Add::Ignored);
  EXPECT_EQ(list.files(), 2u);
  EXPECT_EQ(std::string(list.file(0)), longest + ".png");
  // No storage: nothing is listed.
  ImageList none(nullptr, 4);
  EXPECT_EQ(none.add("a.png", false), ImageList::Add::Full);
}

TEST(ImageList, AFullListKeepsTheFirstNamesByName) {
  // Over capacity the list keeps the first names in nameLess order, whatever order the directory gives them in.
  std::array<ImageName, 3> names{};
  ImageList list(names.data(), names.size());
  for (const char* name : {"m.png", "z.png", "k.png"}) EXPECT_EQ(list.add(name, false), ImageList::Add::Listed);
  EXPECT_EQ(list.add("a.png", false), ImageList::Add::Full);  // replaces z
  EXPECT_EQ(list.add("y.png", false), ImageList::Add::Full);  // after m, the greatest kept: dropped
  EXPECT_EQ(list.add("B.png", false), ImageList::Add::Full);  // replaces m
  list.sort();
  ASSERT_EQ(list.files(), 3u);
  EXPECT_STREQ(list.file(0), "a.png");
  EXPECT_STREQ(list.file(1), "B.png");
  EXPECT_STREQ(list.file(2), "k.png");
  // The same names in any order give the same list.
  std::array<ImageName, 3> other{};
  ImageList reversed(other.data(), other.size());
  for (const char* name : {"B.png", "y.png", "a.png", "k.png", "z.png", "m.png"}) (void)reversed.add(name, false);
  reversed.sort();
  for (size_t i = 0; i < 3; ++i) EXPECT_STREQ(reversed.file(i), list.file(i));
}

TEST(ImageRows, SizeLimitsAreTheConverters) {
  EXPECT_TRUE(imageSizeAccepted(480, 800));
  EXPECT_TRUE(imageSizeAccepted(1, 1));
  EXPECT_TRUE(imageSizeAccepted(2048, 4096));  // exactly 8 MP
  EXPECT_FALSE(imageSizeAccepted(2049, 4096));
  EXPECT_FALSE(imageSizeAccepted(0, 10));
  EXPECT_FALSE(imageSizeAccepted(10, -1));
  EXPECT_FALSE(imageSizeAccepted(32768, 1));
  EXPECT_TRUE(imageSizeAccepted(32767, 1));
}

TEST(ImageRows, PngDepthsAreTheConverters) {
  for (const PngPixel type : {PngPixel::Grayscale, PngPixel::Indexed}) {
    for (const int bits : {1, 2, 4, 8}) EXPECT_TRUE(pngDepthSupported(static_cast<int>(type), bits)) << bits;
    EXPECT_FALSE(pngDepthSupported(static_cast<int>(type), 16));
    EXPECT_FALSE(pngDepthSupported(static_cast<int>(type), 3));
  }
  for (const PngPixel type : {PngPixel::Truecolor, PngPixel::GrayAlpha, PngPixel::TruecolorAlpha}) {
    EXPECT_TRUE(pngDepthSupported(static_cast<int>(type), 8));
    EXPECT_FALSE(pngDepthSupported(static_cast<int>(type), 4));
    EXPECT_FALSE(pngDepthSupported(static_cast<int>(type), 16));
  }
}

TEST(ImageRows, PngBufferNeedIsTheConverters) {
  // ((pitch + 1) * 2) + 32.
  EXPECT_EQ(pngInternalBufferBytes(480, static_cast<int>(PngPixel::Grayscale), 8), (481 * 2) + 32);
  EXPECT_EQ(pngInternalBufferBytes(480, static_cast<int>(PngPixel::Truecolor), 8), (1441 * 2) + 32);
  EXPECT_EQ(pngInternalBufferBytes(480, static_cast<int>(PngPixel::GrayAlpha), 8), (961 * 2) + 32);
  EXPECT_EQ(pngInternalBufferBytes(480, static_cast<int>(PngPixel::TruecolorAlpha), 8), (1921 * 2) + 32);
  EXPECT_EQ(pngInternalBufferBytes(481, static_cast<int>(PngPixel::Indexed), 1), (61 + 1) * 2 + 32);
  EXPECT_EQ(pngInternalBufferBytes(10, static_cast<int>(PngPixel::Grayscale), 4), (5 + 1) * 2 + 32);
  // 800 px truecolour with alpha fits the firmware's 16416-byte PNG buffer; 2100 px does not.
  EXPECT_LE(pngInternalBufferBytes(800, static_cast<int>(PngPixel::TruecolorAlpha), 8), 16416);
  EXPECT_GT(pngInternalBufferBytes(2100, static_cast<int>(PngPixel::TruecolorAlpha), 8), 16416);
}

TEST(ImageRows, PngGrayRowsAtEachDepth) {
  std::array<uint8_t, 8> gray{};
  const uint8_t eight[] = {0, 17, 128, 255};
  pngRowToGray(eight, gray.data(), 4, static_cast<int>(PngPixel::Grayscale), 8, nullptr, false, 0);
  EXPECT_EQ((std::vector<uint8_t>(gray.begin(), gray.begin() + 4)), (std::vector<uint8_t>{0, 17, 128, 255}));
  // 1 bit, most significant sample first: 1, 0, 1, 1, 0, 0, 0, 1.
  const uint8_t one[] = {0xB1};
  pngRowToGray(one, gray.data(), 8, static_cast<int>(PngPixel::Grayscale), 1, nullptr, false, 0);
  EXPECT_EQ((std::vector<uint8_t>(gray.begin(), gray.end())), (std::vector<uint8_t>{255, 0, 255, 255, 0, 0, 0, 255}));
  // 2 bits: 0, 1, 2, 3 expanded by 255 / 3.
  const uint8_t two[] = {0x1B};
  pngRowToGray(two, gray.data(), 4, static_cast<int>(PngPixel::Grayscale), 2, nullptr, false, 0);
  EXPECT_EQ((std::vector<uint8_t>(gray.begin(), gray.begin() + 4)), (std::vector<uint8_t>{0, 85, 170, 255}));
  // 4 bits: 0x0 and 0xF, then 0x8 (8 * 255 / 15 = 136).
  const uint8_t four[] = {0x0F, 0x80};
  pngRowToGray(four, gray.data(), 3, static_cast<int>(PngPixel::Grayscale), 4, nullptr, false, 0);
  EXPECT_EQ((std::vector<uint8_t>(gray.begin(), gray.begin() + 3)), (std::vector<uint8_t>{0, 255, 136}));
}

TEST(ImageRows, PngGrayColourKeyIsWhite) {
  std::array<uint8_t, 3> gray{};
  const uint8_t row[] = {0, 40, 0};
  // tRNS: sample 40 is transparent, composited on white.
  pngRowToGray(row, gray.data(), 3, static_cast<int>(PngPixel::Grayscale), 8, nullptr, true, 40);
  EXPECT_EQ((std::vector<uint8_t>(gray.begin(), gray.end())), (std::vector<uint8_t>{0, 255, 0}));
}

TEST(ImageRows, PngTruecolourIsTheConvertersLuma) {
  std::array<uint8_t, 4> gray{};
  const uint8_t row[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30};
  pngRowToGray(row, gray.data(), 4, static_cast<int>(PngPixel::Truecolor), 8, nullptr, false, 0);
  EXPECT_EQ(gray[0], (255 * 77) >> 8);
  EXPECT_EQ(gray[1], (255 * 150) >> 8);
  EXPECT_EQ(gray[2], (255 * 29) >> 8);
  EXPECT_EQ(gray[3], (10 * 77 + 20 * 150 + 30 * 29) >> 8);
  // A colour key: that RGB is transparent.
  pngRowToGray(row, gray.data(), 4, static_cast<int>(PngPixel::Truecolor), 8, nullptr, true, 0x00FF00);
  EXPECT_EQ(gray[1], 255);
  EXPECT_EQ(gray[0], (255 * 77) >> 8);
}

TEST(ImageRows, PngIndexedUsesThePaletteAndItsAlpha) {
  std::vector<uint8_t> palette(1024, 255);
  // Entry 0 black, opaque; entry 1 mid-gray RGB (100, 100, 100), half transparent; entry 2 red, opaque.
  palette[0] = palette[1] = palette[2] = 0;
  palette[3] = palette[4] = palette[5] = 100;
  palette[6] = 255;
  palette[7] = palette[8] = 0;
  palette[768 + 1] = 128;
  const uint8_t row[] = {0, 1, 2};
  std::array<uint8_t, 3> gray{};
  pngRowToGray(row, gray.data(), 3, static_cast<int>(PngPixel::Indexed), 8, palette.data(), true, 0);
  const uint8_t mid = (100 * 77 + 100 * 150 + 100 * 29) >> 8;
  EXPECT_EQ(gray[0], 0);
  EXPECT_EQ(gray[1], (mid * 128 + 255 * 127) / 255);
  EXPECT_EQ(gray[2], (255 * 77) >> 8);
  // Without tRNS the palette's alpha is ignored.
  pngRowToGray(row, gray.data(), 3, static_cast<int>(PngPixel::Indexed), 8, palette.data(), false, 0);
  EXPECT_EQ(gray[1], mid);
  // 2-bit indices, packed: 0, 1, 2, 0.
  const uint8_t packed[] = {0x18};
  std::array<uint8_t, 4> four{};
  pngRowToGray(packed, four.data(), 4, static_cast<int>(PngPixel::Indexed), 2, palette.data(), false, 0);
  EXPECT_EQ((std::vector<uint8_t>(four.begin(), four.end())),
            (std::vector<uint8_t>{0, mid, static_cast<uint8_t>((255 * 77) >> 8), 0}));
  // No palette: the samples read as gray.
  pngRowToGray(packed, four.data(), 4, static_cast<int>(PngPixel::Indexed), 2, nullptr, false, 0);
  EXPECT_EQ((std::vector<uint8_t>(four.begin(), four.end())), (std::vector<uint8_t>{0, 85, 170, 0}));
}

TEST(ImageRows, PngAlphaIsCompositedOnWhite) {
  const uint8_t grayAlpha[] = {0, 255, 0, 0, 0, 128, 200, 51};
  std::array<uint8_t, 4> gray{};
  pngRowToGray(grayAlpha, gray.data(), 4, static_cast<int>(PngPixel::GrayAlpha), 8, nullptr, false, 0);
  EXPECT_EQ(gray[0], 0);    // opaque black
  EXPECT_EQ(gray[1], 255);  // transparent: white
  EXPECT_EQ(gray[2], (0 * 128 + 255 * 127) / 255);
  EXPECT_EQ(gray[3], (200 * 51 + 255 * 204) / 255);
  const uint8_t rgba[] = {0, 0, 0, 255, 0, 0, 0, 0, 100, 100, 100, 10};
  std::array<uint8_t, 3> g3{};
  pngRowToGray(rgba, g3.data(), 3, static_cast<int>(PngPixel::TruecolorAlpha), 8, nullptr, false, 0);
  EXPECT_EQ(g3[0], 0);
  EXPECT_EQ(g3[1], 255);
  EXPECT_EQ(g3[2], (100 * 10 + 255 * 245) / 255);  // a gray's luma is itself: (100 * 256) >> 8
}

TEST(ImageRows, PngUnknownTypeIsMidGray) {
  const uint8_t row[] = {1, 2};
  std::array<uint8_t, 2> gray{};
  pngRowToGray(row, gray.data(), 2, 5, 8, nullptr, false, 0);
  EXPECT_EQ(gray[0], 128);
  EXPECT_EQ(gray[1], 128);
}

TEST(ImageRows, JpegScaleIsTheConverters) {
  EXPECT_EQ(jpegScaleDenominator(100), 8);
  EXPECT_EQ(jpegScaleDenominator(125), 8);
  EXPECT_EQ(jpegScaleDenominator(126), 4);
  EXPECT_EQ(jpegScaleDenominator(250), 4);
  EXPECT_EQ(jpegScaleDenominator(500), 2);
  EXPECT_EQ(jpegScaleDenominator(501), 1);
  EXPECT_EQ(jpegScaleDenominator(3000), 1);
}

TEST(ImageRows, JpegScaleAndOptionForAFit) {
  // 1600 x 2560 shown at 480 x 768: 300 permille on both axes, over 250: a half.
  JpegScale s = jpegScaleFor(1600, 2560, 480, 768, false);
  EXPECT_EQ(s.denominator, 2);
  EXPECT_EQ(s.option, 2);
  EXPECT_EQ(s.scaledW, 800);
  EXPECT_EQ(s.scaledH, 1280);
  // The smaller axis' scale decides: 4000 x 3000 on 480 wide (120 permille) is an eighth.
  s = jpegScaleFor(4000, 3000, 480, 360, false);
  EXPECT_EQ(s.denominator, 8);
  EXPECT_EQ(s.option, 8);
  EXPECT_EQ(s.scaledW, 500);
  EXPECT_EQ(s.scaledH, 375);
  // Enlarging or 1:1: no scale, option 0; odd sizes round up.
  s = jpegScaleFor(101, 51, 480, 242, false);
  EXPECT_EQ(s.denominator, 1);
  EXPECT_EQ(s.option, 0);
  EXPECT_EQ(s.scaledW, 101);
  // Progressive: an eighth whatever the fit (JPEGDEC decodes it DC-only), rounded up.
  s = jpegScaleFor(101, 51, 480, 242, true);
  EXPECT_EQ(s.denominator, 8);
  EXPECT_EQ(s.option, 8);
  EXPECT_EQ(s.scaledW, 13);
  EXPECT_EQ(s.scaledH, 7);
  // A quarter.
  EXPECT_EQ(jpegScaleFor(2000, 2000, 480, 480, false).denominator, 4);
}

TEST(ImageRows, FitKeepsTheAspectAndCentres) {
  // The repo's images on a portrait screen: exactly the screen.
  FitRect f = fitToScreen(480, 800, 480, 800);
  EXPECT_EQ(f.x, 0);
  EXPECT_EQ(f.y, 0);
  EXPECT_EQ(f.w, 480);
  EXPECT_EQ(f.h, 800);
  // The same image on a landscape screen: the height limits, centred horizontally.
  f = fitToScreen(480, 800, 800, 480);
  EXPECT_EQ(f.h, 480);
  EXPECT_EQ(f.w, 288);
  EXPECT_EQ(f.x, (800 - 288) / 2);
  EXPECT_EQ(f.y, 0);
  // A small wide image scales up: the width limits.
  f = fitToScreen(100, 50, 480, 800);
  EXPECT_EQ(f.w, 480);
  EXPECT_EQ(f.h, 240);
  EXPECT_EQ(f.x, 0);
  EXPECT_EQ(f.y, (800 - 240) / 2);
  // A huge image scales down.
  f = fitToScreen(4000, 3000, 800, 480);
  EXPECT_EQ(f.h, 480);
  EXPECT_EQ(f.w, 640);
  // A one-pixel-high sliver keeps at least one row.
  f = fitToScreen(30000, 1, 480, 800);
  EXPECT_EQ(f.w, 480);
  EXPECT_EQ(f.h, 1);
  // Degenerate sizes.
  f = fitToScreen(0, 10, 480, 800);
  EXPECT_EQ(f.w, 0);
  EXPECT_EQ(f.h, 0);
}

TEST(ImageRows, FitAlwaysInsideTheScreen) {
  for (const int sw : {1, 7, 480, 800}) {
    for (const int sh : {1, 9, 480, 800}) {
      for (const int w : {1, 2, 3, 100, 479, 480, 801, 5000}) {
        for (const int h : {1, 2, 5, 333, 800, 4096}) {
          const FitRect f = fitToScreen(w, h, sw, sh);
          ASSERT_GE(f.x, 0);
          ASSERT_GE(f.y, 0);
          ASSERT_GE(f.w, 1);
          ASSERT_GE(f.h, 1);
          ASSERT_LE(f.x + f.w, sw);
          ASSERT_LE(f.y + f.h, sh);
          // One side fills the screen.
          ASSERT_TRUE(f.w == sw || f.h == sh) << w << "x" << h << " on " << sw << "x" << sh;
        }
      }
    }
  }
}

TEST(ImageRows, FitAxisSamplesPixelCentres) {
  // 1:1 is the identity.
  const FitAxis same{5, 5};
  for (int d = 0; d < 5; ++d) EXPECT_EQ(same.srcOf(d), d);
  // Halving takes the second of each pair (the centre of output pixel d is at source 2d + 1).
  const FitAxis half{10, 5};
  for (int d = 0; d < 5; ++d) EXPECT_EQ(half.srcOf(d), 2 * d + 1);
  // Doubling repeats each source pixel twice.
  const FitAxis twice{3, 6};
  EXPECT_EQ((std::vector<int>{twice.srcOf(0), twice.srcOf(1), twice.srcOf(2), twice.srcOf(3), twice.srcOf(4),
                              twice.srcOf(5)}),
            (std::vector<int>{0, 0, 1, 1, 2, 2}));
}

TEST(ImageRows, FitAxisRangesPartitionTheOutput) {
  for (const int src : {1, 2, 3, 7, 100, 480, 801}) {
    for (const int dst : {1, 2, 5, 288, 480, 800}) {
      const FitAxis axis{src, dst};
      ASSERT_EQ(axis.firstDst(0), 0);
      ASSERT_EQ(axis.firstDst(src), dst);
      int prev = -1;
      for (int d = 0; d < dst; ++d) {
        const int s = axis.srcOf(d);
        ASSERT_GE(s, prev);
        ASSERT_LT(s, src);
        // d lies in its source pixel's range.
        ASSERT_GE(d, axis.firstDst(s)) << src << "->" << dst << " d " << d;
        ASSERT_LT(d, axis.firstDst(s + 1)) << src << "->" << dst << " d " << d;
        prev = s;
      }
    }
  }
}

TEST(ImageRows, TapsAreNearestWhenShrinkingAndBlendWhenEnlarging) {
  const FitAxis half{10, 5};
  for (int d = 0; d < 5; ++d) {
    const FitAxis::Tap t = half.tap(d);
    EXPECT_EQ(t.s0, half.srcOf(d));
    EXPECT_EQ(t.s1, half.srcOf(d));
    EXPECT_EQ(t.weight, 0u);
  }
  // Doubling 3 -> 6: centres at source 0.25 - 0.5 (clamped), 0.25, 0.75, 1.25, 1.75, 2.25 (clamped): weights 1/4, 3/4.
  const FitAxis twice{3, 6};
  EXPECT_TRUE(twice.enlarges());
  const std::vector<std::array<uint32_t, 3>> want = {{0, 0, 0},     {0, 1, 16384}, {0, 1, 49152},
                                                     {1, 2, 16384}, {1, 2, 49152}, {2, 2, 0}};
  for (int d = 0; d < 6; ++d) {
    const FitAxis::Tap t = twice.tap(d);
    EXPECT_EQ((std::array<uint32_t, 3>{static_cast<uint32_t>(t.s0), static_cast<uint32_t>(t.s1), t.weight}),
              want[static_cast<size_t>(d)])
        << d;
  }
  // 1:1 is the identity, unblended.
  const FitAxis same{4, 4};
  for (int d = 0; d < 4; ++d) EXPECT_EQ(same.tap(d).s0, d);
  for (int d = 0; d < 4; ++d) EXPECT_EQ(same.tap(d).weight, 0u);
}

TEST(ImageRows, BlockSpansPartitionTheOutput) {
  // JPEG MCU blocks of 8 or 16 source pixels: the output pixels whose last needed source pixel is in a block tile the
  // output, each pixel once, empty when shrinking skips a block.
  for (const int src : {8, 50, 100, 480, 1000}) {
    for (const int dst : {7, 48, 100, 480, 800}) {
      const FitAxis axis{src, dst};
      for (const int block : {8, 16}) {
        int next = 0;
        for (int start = 0; start < src; start += block) {
          const int end = start + block < src ? start + block : src;
          const Span span = outputsEndingIn(axis, start, end);
          ASSERT_EQ(span.first, next) << src << "->" << dst;
          for (int d = span.first; d < span.end; ++d) {
            ASSERT_GE(axis.tap(d).s1, start);
            ASSERT_LT(axis.tap(d).s1, end);
            // Its first source pixel is in the block or the one just before it (kept as above or left).
            ASSERT_GE(axis.tap(d).s0, start - 1);
          }
          next = span.end;
        }
        ASSERT_EQ(next, dst);
      }
    }
  }
  // A block past the source's edge (an MCU's padding) adds nothing.
  EXPECT_TRUE(outputsEndingIn(FitAxis{20, 40}, 20, 24).empty());
}

TEST(ImageRows, RowsReadyAtPartitionTheOutputOnceTheirRowsHaveArrived) {
  for (const int src : {1, 3, 7, 100, 480, 801}) {
    for (const int dst : {1, 5, 288, 480, 800}) {
      const FitAxis axis{src, dst};
      int next = 0;
      for (int s = 0; s < src; ++s) {
        const Span rows = rowsReadyAt(axis, s);
        ASSERT_EQ(rows.first, next) << src << "->" << dst << " row " << s;
        for (int d = rows.first; d < rows.end; ++d) {
          // Its last needed row is this one, and its first this one or the one before.
          ASSERT_EQ(axis.tap(d).s1, s);
          ASSERT_GE(axis.tap(d).s0, s - 1);
        }
        next = rows.end;
      }
      ASSERT_EQ(next, dst) << src << "->" << dst;
    }
  }
}

TEST(ImageRows, SampleBlockIsNearestWhenShrinking) {
  const uint8_t row[] = {0, 10, 20, 30, 40, 50, 60, 70};
  const FitAxis half{8, 4};
  const FitAxis one{1, 1};
  const SourceBlock block{row, 8, 0, 8, 0, 1};
  uint8_t out[4] = {};
  sampleBlock(block, half, one.tap(0), 0, 4, out);
  EXPECT_EQ((std::vector<uint8_t>(out, out + 4)), (std::vector<uint8_t>{10, 30, 50, 70}));
  // A segment of it: output columns 1..2.
  uint8_t seg[2] = {};
  sampleBlock(block, half, one.tap(0), 1, 3, seg);
  EXPECT_EQ(seg[0], 30);
  EXPECT_EQ(seg[1], 50);
}

TEST(ImageRows, SampleBlockIsBilinearWhenEnlarging) {
  // Doubling 0, 100, 200 (centre-aligned taps): 0 (clamped), 25, 75, 125, 175, 200 (clamped), each step the 16.16 blend
  // (a * (65536 - w) + b * w) >> 16.
  const uint8_t row[] = {0, 100, 200};
  const FitAxis twice{3, 6};
  const FitAxis one{1, 1};
  uint8_t out[6] = {};
  sampleBlock(SourceBlock{row, 3, 0, 3, 0, 1}, twice, one.tap(0), 0, 6, out);
  EXPECT_EQ((std::vector<uint8_t>(out, out + 6)), (std::vector<uint8_t>{0, 25, 75, 125, 175, 200}));
  // Vertically: halfway between a black row and a white one (the white row is the block, the black one above it).
  const uint8_t black[] = {0, 0, 0};
  const uint8_t white[] = {255, 255, 255};
  SourceBlock below{white, 3, 0, 3, 1, 2};
  below.above = black;
  sampleBlock(below, twice, FitAxis::Tap{0, 1, 32768}, 0, 6, out);
  for (const uint8_t v : out) EXPECT_EQ(v, 127);
  // Without the row above, the blend repeats the block's edge.
  sampleBlock(SourceBlock{white, 3, 0, 3, 1, 2}, twice, FitAxis::Tap{0, 1, 32768}, 0, 6, out);
  for (const uint8_t v : out) EXPECT_EQ(v, 255);
}

// A source image as decoded strips and blocks, sampled block by block with the row above and the column left kept as
// the reader keeps them, against the same image sampled as one block: the seams leave no trace.
std::vector<uint8_t> sampledByBlocks(const std::vector<uint8_t>& image, const int w, const int h, const int dstW,
                                     const int dstH, const int blockW, const int blockH, const bool keepSeams = true) {
  const FitAxis ax{w, dstW};
  const FitAxis ay{h, dstH};
  std::vector<uint8_t> out(static_cast<size_t>(dstW * dstH), 0);
  std::vector<uint8_t> above(static_cast<size_t>(w));
  std::vector<uint8_t> left(static_cast<size_t>(blockH));
  std::vector<uint8_t> row(static_cast<size_t>(dstW));
  for (int by = 0; by < h; by += blockH) {
    const int bh = by + blockH < h ? blockH : h - by;
    for (int bx = 0; bx < w; bx += blockW) {
      const int bw = bx + blockW < w ? blockW : w - bx;
      // The block's own pixels, as JPEGDEC hands them (rows `bw` apart here).
      std::vector<uint8_t> pixels(static_cast<size_t>(bw * bh));
      for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x)
          pixels[static_cast<size_t>(y * bw + x)] = image[static_cast<size_t>((by + y) * w + bx + x)];
      }
      SourceBlock block{pixels.data(), bw, bx, bx + bw, by, by + bh};
      if (keepSeams && by > 0) block.above = above.data();
      if (keepSeams && bx > 0) block.left = left.data();
      const Span cols = outputsEndingIn(ax, bx, bx + bw);
      const Span rows = outputsEndingIn(ay, by, by + bh);
      for (int dy = rows.first; dy < rows.end && !cols.empty(); ++dy) {
        sampleBlock(block, ax, ay.tap(dy), cols.first, cols.end, row.data());
        for (int dx = cols.first; dx < cols.end; ++dx)
          out[static_cast<size_t>(dy * dstW + dx)] = row[static_cast<size_t>(dx - cols.first)];
      }
      for (int y = 0; y < bh; ++y) left[static_cast<size_t>(y)] = pixels[static_cast<size_t>(y * bw + bw - 1)];
    }
    // The strip is done: its last row is the row above the next one.
    for (int x = 0; x < w; ++x) above[static_cast<size_t>(x)] = image[static_cast<size_t>((by + bh - 1) * w + x)];
  }
  return out;
}

TEST(ImageRows, BlendsCrossStripAndBlockSeams) {
  // A 1/8-scale progressive JPEG enlarges by about 8: two strips of 2 rows, blocks of 3 columns, 6 x 4 up to 48 x 32.
  std::vector<uint8_t> image(6 * 4);
  for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>((i * 37) % 256);
  const std::vector<uint8_t> whole = sampledByBlocks(image, 6, 4, 48, 32, 6, 4);
  const std::vector<uint8_t> strips = sampledByBlocks(image, 6, 4, 48, 32, 3, 2);
  EXPECT_EQ(strips, whole);
  // Every output pixel was made exactly once (none left at the fill value where the image has no zero).
  std::vector<uint8_t> bright(6 * 4, 200);
  bright[0] = 10;
  const std::vector<uint8_t> once = sampledByBlocks(bright, 6, 4, 48, 32, 3, 2);
  for (const uint8_t v : once) EXPECT_GE(v, 10);
  // Without the kept row and column, the same blocks would clamp at the seams and differ from the whole.
  std::vector<uint8_t> ramp(6 * 4);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 6; ++x) ramp[static_cast<size_t>(y * 6 + x)] = static_cast<uint8_t>(40 * y + 5 * x);
  }
  EXPECT_EQ(sampledByBlocks(ramp, 6, 4, 48, 32, 3, 2), sampledByBlocks(ramp, 6, 4, 48, 32, 6, 4));
  EXPECT_NE(sampledByBlocks(ramp, 6, 4, 48, 32, 3, 2, false), sampledByBlocks(ramp, 6, 4, 48, 32, 6, 4));
  // Shrinking (nearest) through blocks equals shrinking the whole.
  std::vector<uint8_t> big(40 * 24);
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>((i * 13) % 251);
  EXPECT_EQ(sampledByBlocks(big, 40, 24, 15, 9, 16, 8), sampledByBlocks(big, 40, 24, 15, 9, 40, 24));
}

// ---- the PNG chunk walk -----------------------------------------------------------------------------------------

std::vector<uint8_t> pngChunk(const char* type, const uint32_t length) {
  std::vector<uint8_t> c = {static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
                            static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
  for (int i = 0; i < 4; ++i) c.push_back(static_cast<uint8_t>(type[i]));
  c.resize(c.size() + length + 4, 0);  // data and CRC
  return c;
}

std::vector<uint8_t> pngFile(const std::vector<std::vector<uint8_t>>& chunks) {
  std::vector<uint8_t> f = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  for (const auto& c : chunks) f.insert(f.end(), c.begin(), c.end());
  return f;
}

PngLayout walk(const std::vector<uint8_t>& file, size_t size = SIZE_MAX) {
  if (size == SIZE_MAX) size = file.size();
  int reads = 0;
  const PngLayout layout =
      walkPngChunks(static_cast<uint32_t>(size), [&](const uint32_t offset, uint8_t* out, const size_t n) {
        ++reads;
        if (offset + n > size) return false;
        std::copy(file.begin() + offset, file.begin() + offset + static_cast<long>(n), out);
        return true;
      });
  EXPECT_LE(reads, 64);
  return layout;
}

TEST(PngChunks, ACompleteFileWalksThroughIend) {
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 5000), pngChunk("IDAT", 300), pngChunk("IEND", 0)});
  EXPECT_EQ(walk(file), PngLayout::Complete);
  // Bytes after IEND are ignored.
  auto padded = file;
  padded.resize(file.size() + 10, 0);
  EXPECT_EQ(walk(padded), PngLayout::Complete);
}

TEST(PngChunks, ATruncatedFileIsRefused) {
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 5000), pngChunk("IEND", 0)});
  // Cut inside the IDAT data, inside its CRC, inside IEND, inside a chunk header, after the signature: all truncated.
  for (const size_t cut : {size_t{8}, size_t{8 + 25 + 4}, size_t{8 + 25 + 100}, file.size() - 12, file.size() - 1}) {
    EXPECT_EQ(walk(file, cut), PngLayout::Truncated) << cut;
  }
  // No IEND at all.
  EXPECT_EQ(walk(pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 10)})), PngLayout::Truncated);
  // Shorter than a signature.
  EXPECT_EQ(walk(file, 4), PngLayout::Truncated);
}

TEST(PngChunks, AMalformedFileIsRefused) {
  auto notPng = pngFile({pngChunk("IHDR", 13), pngChunk("IEND", 0)});
  notPng[1] = 'J';
  EXPECT_EQ(walk(notPng), PngLayout::Malformed);
  auto huge = pngFile({pngChunk("IHDR", 13), pngChunk("IEND", 0)});
  huge[8] = 0x80;  // IHDR's length over 2^31 - 1
  EXPECT_EQ(walk(huge), PngLayout::Malformed);
}

TEST(ImageRows, PngColourTypesThatDoNotExistAreRefused) {
  // The converter lets an 8-bit file of colour type 1, 5 or 7 through, to paint flat gray; the viewer refuses it.
  for (const int type : {1, 5, 7, 8}) {
    EXPECT_FALSE(pngDepthSupported(type, 8)) << type;
    EXPECT_FALSE(pngDepthSupported(type, 4)) << type;
  }
  for (const int type : {0, 2, 3, 4, 6}) EXPECT_TRUE(pngDepthSupported(type, 8)) << type;
}

}  // namespace
