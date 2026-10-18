// The gray viewer's decode arithmetic (lib/GameDisplayLab/ImageRows, story dlab-g3): which files the Image row lists
// and their order, the converters' size limits and PNG checks, a PNG row to 8-bit gray as the converter converts one
// (alpha on white), JPEGDEC's coarse scale, the fit to the logical screen (the fit and the I/O matrix's Fit row), the
// seams between decoder blocks, the PNG and JPEG layout walks, and the answers the reader gives a decoder's reads.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "ImageRows.h"
#include "image_fixtures.h"

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

// A source image as JPEGDEC hands it to the draw callback, sampled block by block through the readers' SeamKeeper,
// against the same image sampled as one block: the seams leave no trace. Strips of `blockH` rows, the last one trimmed
// (jpeg.inl 5335: iHeight cut to the image); blocks `blockW` wide, rows `blockW` apart, the right-edge one with only
// `iWidthUsed` valid columns (jpeg.inl 5329) and the rest of its pitch garbage. blockH 1 and blockW w are PNGdec's
// rows.
std::vector<uint8_t> sampledByBlocks(const std::vector<uint8_t>& image, const int w, const int h, const int dstW,
                                     const int dstH, const int blockW, const int blockH, const bool keepSeams = true) {
  const FitAxis ax{w, dstW};
  const FitAxis ay{h, dstH};
  std::vector<uint8_t> out(static_cast<size_t>(dstW * dstH), 0);
  std::vector<uint8_t> rowA(static_cast<size_t>(w));
  std::vector<uint8_t> rowB(static_cast<size_t>(w));
  std::vector<uint8_t> row(static_cast<size_t>(dstW));
  SeamKeeper seams;
  // As the readers: strip rows only when a blend reaches the row above, a left column only when one reaches left.
  const bool rows = keepSeams && ay.enlarges();
  seams.reset(w, rows ? rowA.data() : nullptr, rows ? rowB.data() : nullptr, keepSeams && ax.enlarges());
  for (int by = 0; by < h; by += blockH) {
    const int bh = by + blockH < h ? blockH : h - by;
    for (int bx = 0; bx < w; bx += blockW) {
      const int used = bx + blockW < w ? blockW : w - bx;
      // The block's pixels, `blockW` apart; the columns past `used` are garbage, as past JPEGDEC's iWidthUsed.
      std::vector<uint8_t> pixels(static_cast<size_t>(blockW * bh), 0xA5);
      for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < used; ++x)
          pixels[static_cast<size_t>(y * blockW + x)] = image[static_cast<size_t>((by + y) * w + bx + x)];
      }
      const SourceBlock block = seams.begin(pixels.data(), bx, by, used, bh, blockW);
      const Span cols = outputsEndingIn(ax, block.x0, block.x1);
      const Span outRows = outputsEndingIn(ay, block.y0, block.y1);
      for (int dy = outRows.first; dy < outRows.end && !cols.empty(); ++dy) {
        sampleBlock(block, ax, ay.tap(dy), cols.first, cols.end, row.data());
        for (int dx = cols.first; dx < cols.end; ++dx)
          out[static_cast<size_t>(dy * dstW + dx)] = row[static_cast<size_t>(dx - cols.first)];
      }
      seams.end(block);
    }
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

TEST(ImageRows, SeamsHoldForJpegdecShapedBlocks) {
  // 7 x 5 in strips of 2 (the last trimmed to 1) and blocks pitched 4 (the right-edge one 3 used of 4), enlarged on
  // both axes, on one, and shrunk: the same as the whole.
  std::vector<uint8_t> image(7 * 5);
  for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>((i * 53 + 17) % 256);
  for (const auto& [dstW, dstH] : {std::pair{50, 37}, std::pair{50, 4}, std::pair{5, 37}, std::pair{4, 3}}) {
    EXPECT_EQ(sampledByBlocks(image, 7, 5, dstW, dstH, 4, 2), sampledByBlocks(image, 7, 5, dstW, dstH, 7, 5))
        << dstW << "x" << dstH;
  }
  // PNGdec's shape: one-row strips of one block.
  EXPECT_EQ(sampledByBlocks(image, 7, 5, 50, 37, 7, 1), sampledByBlocks(image, 7, 5, 50, 37, 7, 5));
}

TEST(ImageRows, SeamKeeperHandsOutTheRowAboveAndTheColumnLeft) {
  std::array<uint8_t, 4> rowA{};
  std::array<uint8_t, 4> rowB{};
  SeamKeeper seams;
  seams.reset(4, rowA.data(), rowB.data(), true);
  // Strip 0: blocks [0, 2) and [2, 4), pitch 2, two rows each.
  const uint8_t a[] = {1, 2, 5, 6};
  const uint8_t b[] = {3, 4, 7, 8};
  SourceBlock first = seams.begin(a, 0, 0, 2, 2, 2);
  EXPECT_EQ(first.above, nullptr);
  EXPECT_EQ(first.left, nullptr);
  seams.end(first);
  SourceBlock second = seams.begin(b, 2, 0, 2, 2, 2);
  EXPECT_EQ(second.above, nullptr);
  ASSERT_NE(second.left, nullptr);
  EXPECT_EQ(second.left[0], 2);
  EXPECT_EQ(second.left[1], 6);
  EXPECT_EQ(second.at(1, 1), 6);  // the column left, through at()
  seams.end(second);
  // Strip 1: the row above is strip 0's last row, whole; a new strip has no column left yet.
  const uint8_t c[] = {9, 9};
  SourceBlock third = seams.begin(c, 0, 2, 2, 1, 2);
  ASSERT_NE(third.above, nullptr);
  EXPECT_EQ(third.left, nullptr);
  for (int x = 0; x < 4; ++x) EXPECT_EQ(third.above[x], (std::array<uint8_t, 4>{5, 6, 7, 8})[static_cast<size_t>(x)]);
  seams.end(third);
  // A block that does not start where the last one ended gets no column left.
  SourceBlock gap = seams.begin(c, 3, 2, 1, 1, 2);
  EXPECT_EQ(gap.left, nullptr);
  // Without strip rows (no vertical blend) there is never a row above.
  seams.reset(4, nullptr, nullptr, false);
  seams.end(seams.begin(a, 0, 0, 2, 2, 2));
  const SourceBlock below = seams.begin(c, 0, 2, 2, 1, 2);
  EXPECT_EQ(below.above, nullptr);
  EXPECT_EQ(below.left, nullptr);
}

// ---- the layout walks ---------------------------------------------------------------------------------------------

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

// How a walk is fed: the first `size` bytes of the file (all by default), read `failOnRead` failing (1-based; 0
// never), the stop asked after `stopAfter` questions (-1 never), and the walk's reads counted against `maxReads`.
struct Feed {
  size_t size = SIZE_MAX;
  int failOnRead = 0;
  int stopAfter = -1;
  int maxReads = 64;
};

template <typename Walk>
FileLayout walkWith(Walk&& walkFile, const std::vector<uint8_t>& file, Feed feed) {
  if (feed.size == SIZE_MAX) feed.size = file.size();
  int reads = 0;
  int asked = 0;
  const auto readAt = [&](const uint32_t offset, uint8_t* out, const size_t n) {
    ++reads;
    if (reads == feed.failOnRead) return false;
    if (offset + n > feed.size) return false;
    std::copy(file.begin() + offset, file.begin() + offset + static_cast<long>(n), out);
    return true;
  };
  const auto stopped = [&] { return feed.stopAfter >= 0 && asked++ >= feed.stopAfter; };
  const FileLayout layout = walkFile(static_cast<uint32_t>(feed.size), readAt, stopped);
  EXPECT_LE(reads, feed.maxReads);
  return layout;
}

FileLayout walkPng(const std::vector<uint8_t>& file, const Feed& feed = {}) {
  return walkWith([](uint32_t size, auto& readAt, auto& stopped) { return walkPngChunks(size, readAt, stopped); }, file,
                  feed);
}

FileLayout walkJpeg(const std::vector<uint8_t>& file, const Feed& feed = {}) {
  return walkWith([](uint32_t size, auto& readAt, auto& stopped) { return walkJpegSegments(size, readAt, stopped); },
                  file, feed);
}

template <size_t N>
std::vector<uint8_t> bytes(const uint8_t (&a)[N]) {
  return std::vector<uint8_t>(a, a + N);
}

TEST(PngChunks, ACompleteFileWalksThroughIend) {
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 5000), pngChunk("IDAT", 300), pngChunk("IEND", 0)});
  EXPECT_EQ(walkPng(file), FileLayout::Complete);
  EXPECT_EQ(walkPng(bytes(PNG_GRAY)), FileLayout::Complete);
  EXPECT_EQ(walkPng(bytes(PNG_PALETTE)), FileLayout::Complete);
  EXPECT_EQ(walkPng(bytes(PNG_NOISE)), FileLayout::Complete);
  // A zero-length IDAT is a whole chunk.
  EXPECT_EQ(walkPng(pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 0), pngChunk("IEND", 0)})), FileLayout::Complete);
  // An IDAT holding a short zlib stream with bytes after it is whole as a layout (what the bytes mean is PNGdec's).
  auto shortStream = pngChunk("IDAT", 12);
  const uint8_t stream[] = {0x78, 0xDA, 0x63, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xEE, 0xEE, 0xEE};
  std::copy(std::begin(stream), std::end(stream), shortStream.begin() + 8);
  EXPECT_EQ(walkPng(pngFile({pngChunk("IHDR", 13), shortStream, pngChunk("IEND", 0)})), FileLayout::Complete);
}

TEST(PngChunks, WhatFollowsIendIsNotWalked) {
  // PNGdec stops at IEND, and its reads past the walked size are filler: anything after IEND, a whole chunk, a cut
  // one, an oversized tRNS or a few stray bytes, leaves the file Complete, and is not read.
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 50), pngChunk("IEND", 0)});
  const auto tail = [&file](const std::vector<uint8_t>& after) {
    auto f = file;
    f.insert(f.end(), after.begin(), after.end());
    return walkPng(f, {.maxReads = 4});
  };
  EXPECT_EQ(tail(pngChunk("tEXt", 20)), FileLayout::Complete);
  const auto cut = pngChunk("tEXt", 20);
  EXPECT_EQ(tail(std::vector<uint8_t>(cut.begin(), cut.begin() + 10)), FileLayout::Complete);
  EXPECT_EQ(tail(pngChunk("tRNS", 300)), FileLayout::Complete);
  EXPECT_EQ(tail(std::vector<uint8_t>(7, 0)), FileLayout::Complete);
  EXPECT_EQ(tail(std::vector<uint8_t>(10, 0)), FileLayout::Complete);
}

TEST(PngChunks, ATruncatedFileIsRefused) {
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 5000), pngChunk("IEND", 0)});
  // IHDR is bytes [8, 33), IDAT [33, 5045) (its CRC [5041, 5045)), IEND [5045, 5057). Cut inside IDAT's header, its
  // data and its CRC, and inside IEND's header and its CRC: each leaves a chunk running past the end.
  for (const size_t cut : {size_t{33 + 4}, size_t{33 + 100}, size_t{5041 + 2}, size_t{5045 + 4}, file.size() - 1}) {
    EXPECT_EQ(walkPng(file, {.size = cut}), FileLayout::Truncated) << cut;
  }
  // Every cut of the sample files is refused one way or another.
  for (const auto& sample : {bytes(PNG_GRAY), bytes(PNG_PALETTE), bytes(PNG_NOISE)}) {
    for (size_t cut = 0; cut < sample.size(); ++cut) {
      EXPECT_NE(walkPng(sample, {.size = cut}), FileLayout::Complete) << cut;
    }
  }
}

TEST(PngChunks, AShortFileOrNoIendIsSaidSo) {
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IEND", 0)});
  for (const size_t cut : {size_t{0}, size_t{4}, size_t{7}}) {
    EXPECT_EQ(walkPng(file, {.size = cut}), FileLayout::TooShort) << cut;
  }
  // Whole chunks that end without IEND: the signature alone, or a cut between chunks.
  EXPECT_EQ(walkPng(file, {.size = 8}), FileLayout::NoEnd);
  EXPECT_EQ(walkPng(file, {.size = 33}), FileLayout::NoEnd);
  EXPECT_EQ(walkPng(pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 10)})), FileLayout::NoEnd);
}

TEST(PngChunks, AMalformedFileIsRefused) {
  auto notPng = pngFile({pngChunk("IHDR", 13), pngChunk("IEND", 0)});
  notPng[1] = 'J';
  EXPECT_EQ(walkPng(notPng), FileLayout::Malformed);
  auto huge = pngFile({pngChunk("IHDR", 13), pngChunk("IEND", 0)});
  huge[8] = 0x80;  // IHDR's length over 2^31 - 1
  EXPECT_EQ(walkPng(huge), FileLayout::Malformed);
}

TEST(PngChunks, PaletteAndTransparencyAreBounded) {
  // PNGdec copies PLTE and tRNS into its palette without a bound (png.inl 850, 868): 768 and 256 bytes at most.
  const auto with = [](const char* type, const uint32_t length) {
    return walkPng(pngFile({pngChunk("IHDR", 13), pngChunk(type, length), pngChunk("IEND", 0)}));
  };
  EXPECT_EQ(with("PLTE", 768), FileLayout::Complete);
  EXPECT_EQ(with("PLTE", 771), FileLayout::Malformed);
  EXPECT_EQ(with("PLTE", 10), FileLayout::Malformed);  // not whole RGB triples
  EXPECT_EQ(with("PLTE", 0), FileLayout::Complete);
  EXPECT_EQ(with("tRNS", 256), FileLayout::Complete);
  EXPECT_EQ(with("tRNS", 257), FileLayout::Malformed);
}

TEST(PngChunks, AFailedReadIsSaidSo) {
  const auto file = bytes(PNG_GRAY);
  EXPECT_EQ(walkPng(file, {.failOnRead = 1}), FileLayout::ReadFailed);  // the signature
  EXPECT_EQ(walkPng(file, {.failOnRead = 2}), FileLayout::ReadFailed);  // the first chunk header
}

TEST(PngChunks, TheChunkCountIsCapped) {
  // MAX_LAYOUT_CHUNKS chunks, IEND the last, pass; one more is refused.
  std::vector<std::vector<uint8_t>> chunks(MAX_LAYOUT_CHUNKS - 1, pngChunk("tEXt", 0));
  chunks.push_back(pngChunk("IEND", 0));
  const auto atCap = pngFile(chunks);
  const Feed many{.maxReads = static_cast<int>(MAX_LAYOUT_CHUNKS) + 2};
  EXPECT_EQ(walkPng(atCap, many), FileLayout::Complete);
  chunks.insert(chunks.begin(), pngChunk("tEXt", 0));
  EXPECT_EQ(walkPng(pngFile(chunks), many), FileLayout::Malformed);
}

TEST(PngChunks, TheWalkStopsWhenAsked) {
  const auto file = bytes(PNG_PALETTE);
  EXPECT_EQ(walkPng(file, {.stopAfter = 0}), FileLayout::Stopped);
  EXPECT_EQ(walkPng(file, {.stopAfter = 2}), FileLayout::Stopped);
  EXPECT_EQ(walkPng(file, {.stopAfter = 100}), FileLayout::Complete);
}

// A JPEG of SOI, `body`, then EOI.
std::vector<uint8_t> jpegOf(std::initializer_list<uint8_t> body) {
  std::vector<uint8_t> file = {0xFF, 0xD8};
  file.insert(file.end(), body.begin(), body.end());
  file.insert(file.end(), {0xFF, 0xD9});
  return file;
}

TEST(JpegSegments, ACompleteHeaderWalksToSosAndTheFileEndsInEoi) {
  EXPECT_EQ(walkJpeg(bytes(JPEG_BASELINE)), FileLayout::Complete);
  EXPECT_EQ(walkJpeg(bytes(JPEG_PROGRESSIVE)), FileLayout::Complete);
  const std::vector<uint8_t> big = bigExifJpeg();
  EXPECT_EQ(big.size(), 66713u);
  EXPECT_EQ(walkJpeg(big), FileLayout::Complete);
}

TEST(JpegSegments, AScanCutShortHasNoEnd) {
  // The scan data is the decoder's, but a file that does not end in EOI was cut (or never closed): JPEGDEC would
  // decode past the data it has.
  const auto file = bytes(JPEG_BASELINE);
  for (size_t cut = 330; cut < file.size(); ++cut) EXPECT_EQ(walkJpeg(file, {.size = cut}), FileLayout::NoEnd) << cut;
  // EOI must be the last two bytes: trailing bytes after it are a file that does not end in EOI.
  auto trailing = file;
  trailing.push_back(0);
  EXPECT_EQ(walkJpeg(trailing), FileLayout::NoEnd);
  // An SOS segment that ends the file leaves no room for EOI.
  EXPECT_EQ(walkJpeg({0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02}), FileLayout::NoEnd);
}

TEST(JpegSegments, FillBytesAndStandaloneMarkersAreWalkedAsJpegdecWalksThem) {
  // JPEGParseInfo steps over a fill byte (0xFFFF) or anything under 0xFFC0 three bytes at a time (jpeg.inl 1642), so
  // the walk does: a fill byte and the byte after it, then APP0, then SOS.
  EXPECT_EQ(walkJpeg(jpegOf({0xFF, 0xFF, 0x00, 0xFF, 0xE0, 0x00, 0x02, 0xFF, 0xDA, 0x00, 0x02})), FileLayout::Complete);
  // TEM (0xFF01) is under 0xFFC0: three bytes.
  EXPECT_EQ(walkJpeg(jpegOf({0xFF, 0x01, 0x00, 0xFF, 0xDA, 0x00, 0x02})), FileLayout::Complete);
  // A fill byte straight before a marker takes the marker's first byte with it, as JPEGDEC's parse does: the walk
  // lands inside APP0's length and goes on from there.
  EXPECT_NE(walkJpeg(jpegOf({0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x30, 0xFF, 0xDA, 0x00, 0x02})), FileLayout::Truncated);
  // RSTn and a second SOI are read with a length, as JPEGDEC reads one (its default case, jpeg.inl 1762).
  EXPECT_EQ(walkJpeg(jpegOf({0xFF, 0xD0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x02})), FileLayout::Complete);
  EXPECT_EQ(walkJpeg({0xFF, 0xD8, 0xFF, 0xD8, 0x00, 0x40, 0xFF, 0xDA, 0x00, 0x02, 0xFF, 0xD9}), FileLayout::Truncated);
}

// A JPEG of SOI, `segments`, an empty SOS and EOI.
FileLayout withSegments(const std::vector<std::vector<uint8_t>>& segments) {
  std::vector<uint8_t> file = {0xFF, 0xD8};
  for (const auto& segment : segments) file.insert(file.end(), segment.begin(), segment.end());
  file.insert(file.end(), {0xFF, 0xDA, 0x00, 0x02, 0xFF, 0xD9});
  return walkJpeg(file);
}

// A marker segment: `marker`, its length, `body`.
std::vector<uint8_t> segmentOf(const uint8_t marker, const std::vector<uint8_t>& body) {
  const size_t length = body.size() + 2;
  std::vector<uint8_t> seg = {0xFF, marker, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
  seg.insert(seg.end(), body.begin(), body.end());
  return seg;
}

TEST(JpegSegments, FramesAndQuantisationTablesAreBounded) {
  // SOF0 / SOF2: 1 to 4 components, length 8 + 3 each.
  const auto sof = [](const uint8_t type, const uint8_t count, const uint16_t length) {
    std::vector<uint8_t> seg = {0xFF, type, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length), 8, 0, 8, 0,
                                8,    count};
    seg.resize(2 + static_cast<size_t>(length < 8 ? 8 : length), 0x11);
    return seg;
  };
  EXPECT_EQ(withSegments({sof(0xC0, 1, 11)}), FileLayout::Complete);
  EXPECT_EQ(withSegments({sof(0xC2, 3, 17)}), FileLayout::Complete);
  EXPECT_EQ(withSegments({sof(0xC0, 5, 23)}), FileLayout::Malformed);  // past JPEGDEC's 4-entry table
  EXPECT_EQ(withSegments({sof(0xC0, 0, 8)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({sof(0xC0, 3, 11)}), FileLayout::Malformed);  // shorter than its components
  EXPECT_EQ(withSegments({sof(0xC2, 1, 14)}), FileLayout::Malformed);  // longer than them
  // DQT: whole tables of 1 + 64 (8-bit) or 1 + 128 (16-bit) bytes.
  const auto dqt = [](const std::vector<uint8_t>& precisions, const int extra) {
    std::vector<uint8_t> body;
    for (const uint8_t p : precisions) {
      body.push_back(p);
      body.resize(body.size() + (p != 0 ? 128 : 64), 1);
    }
    body.resize(static_cast<size_t>(static_cast<int>(body.size()) + extra), 1);
    return segmentOf(0xDB, body);
  };
  EXPECT_EQ(withSegments({dqt({0x00}, 0)}), FileLayout::Complete);
  EXPECT_EQ(withSegments({dqt({0x00, 0x11}, 0)}), FileLayout::Complete);
  EXPECT_EQ(withSegments({dqt({0x00}, 1)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dqt({0x00}, -1)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dqt({}, 0)}), FileLayout::Complete);  // an empty DQT copies nothing
}

// A DHT table: class and index byte `id`, sixteen counts summing to `codes` (all in the first count when it fits),
// and `codes` values.
std::vector<uint8_t> huffTable(const uint8_t id, const uint32_t codes) {
  std::vector<uint8_t> table = {id};
  uint32_t left = codes;
  for (int i = 0; i < 16; ++i) {
    const uint32_t count = left > 255 ? 255 : left;
    table.push_back(static_cast<uint8_t>(count));
    left -= count;
  }
  table.resize(table.size() + codes, 0x33);
  return table;
}

TEST(JpegSegments, HuffmanTablesAreBounded) {
  const auto dht = [](const std::vector<std::vector<uint8_t>>& tables, const int extra = 0) {
    std::vector<uint8_t> body;
    for (const auto& t : tables) body.insert(body.end(), t.begin(), t.end());
    body.resize(static_cast<size_t>(static_cast<int>(body.size()) + extra), 0x44);
    return segmentOf(0xC4, body);
  };
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 12)})}), FileLayout::Complete);
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 12), huffTable(0x10, 162), huffTable(0x01, 12), huffTable(0x13, 162)})}),
            FileLayout::Complete);
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 256)})}), FileLayout::Complete);
  // A class past 1 or an index past 3: JPEGDEC's loop never shortens its count for it (jpeg.inl 844-870).
  EXPECT_EQ(withSegments({dht({huffTable(0x20, 12)})}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dht({huffTable(0x04, 12)})}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dht({huffTable(0x14, 12)})}), FileLayout::Malformed);
  // Over 256 codes, or totals that are not the segment's length.
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 257)})}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 12)}, 1)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({dht({huffTable(0x00, 12)}, 17)}), FileLayout::Malformed);
  auto shortTable = huffTable(0x00, 12);
  shortTable.resize(10);  // fewer than its 17 header bytes
  EXPECT_EQ(withSegments({dht({shortTable})}), FileLayout::Malformed);
}

TEST(JpegSegments, TablesMustLieInsideJpegdecsBuffer) {
  // JPEGParseInfo reads a table segment out of its 2 KiB buffer, which starts at most 1 KiB before the segment: a DHT
  // or DQT that runs past it is refused, wherever it starts.
  std::vector<std::vector<uint8_t>> tables;
  for (int i = 0; i < 8; ++i) tables.push_back(huffTable(static_cast<uint8_t>((i % 2) << 4 | (i / 2)), 250));
  std::vector<uint8_t> body;
  for (const auto& t : tables) body.insert(body.end(), t.begin(), t.end());  // 8 x 267 = 2,136 bytes
  EXPECT_EQ(withSegments({segmentOf(0xC4, body)}), FileLayout::Malformed);
  // The same 4 tables (1,068 bytes) fit at the start of the file, but not 1,000 bytes into the buffer.
  body.resize(4 * 267);
  EXPECT_EQ(withSegments({segmentOf(0xC4, body)}), FileLayout::Complete);
  EXPECT_EQ(withSegments({segmentOf(0xE0, std::vector<uint8_t>(996, 0)), segmentOf(0xC4, body)}),
            FileLayout::Malformed);
  // Past 1 KiB into the buffer, JPEGDEC moves it on: the same tables after a 1,100-byte APP0 fit again.
  EXPECT_EQ(withSegments({segmentOf(0xE0, std::vector<uint8_t>(1100, 0)), segmentOf(0xC4, body)}),
            FileLayout::Complete);
}

// An APP1 Exif segment: the Exif header, a TIFF header in `order` ('M' or 'I') with IFD0 at `ifd`, then `tiffBody`
// from TIFF offset 8, padded to `length` bytes of body.
std::vector<uint8_t> exifSegment(const char order, const uint32_t ifd, const std::vector<uint8_t>& tiffBody,
                                 const size_t length) {
  const bool motorola = order == 'M';
  std::vector<uint8_t> body = {'E', 'x', 'i', 'f', 0, 0, static_cast<uint8_t>(order), static_cast<uint8_t>(order)};
  const auto u16 = [&body, motorola](const uint32_t v) {
    if (motorola) {
      body.insert(body.end(), {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)});
    } else {
      body.insert(body.end(), {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)});
    }
  };
  u16(42);
  if (motorola) {
    body.insert(body.end(), {static_cast<uint8_t>(ifd >> 24), static_cast<uint8_t>(ifd >> 16),
                             static_cast<uint8_t>(ifd >> 8), static_cast<uint8_t>(ifd)});
  } else {
    body.insert(body.end(), {static_cast<uint8_t>(ifd), static_cast<uint8_t>(ifd >> 8), static_cast<uint8_t>(ifd >> 16),
                             static_cast<uint8_t>(ifd >> 24)});
  }
  body.insert(body.end(), tiffBody.begin(), tiffBody.end());
  if (body.size() < length) body.resize(length, 0);
  return segmentOf(0xE1, body);
}

// A big-endian IFD of `count` entries (each 12 zero bytes but the tag) and the next IFD's offset.
std::vector<uint8_t> ifdOf(const uint32_t count, const uint32_t next) {
  static constexpr uint8_t ENTRY[12] = {0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 1, 0, 0};  // orientation, a short, 1
  std::vector<uint8_t> ifd;
  ifd.reserve(2 + 12 * static_cast<size_t>(count) + 4);
  ifd.push_back(static_cast<uint8_t>(count >> 8));
  ifd.push_back(static_cast<uint8_t>(count));
  for (uint32_t i = 0; i < count; ++i) {
    for (const uint8_t b : ENTRY) ifd.push_back(b);
  }
  for (const int shift : {24, 16, 8, 0}) ifd.push_back(static_cast<uint8_t>(next >> shift));
  return ifd;
}

TEST(JpegSegments, ExifIfdsMustLieInsideTheSegmentAndTheBuffer) {
  // A whole IFD0 of 3 entries, no next IFD; and a second IFD after it.
  EXPECT_EQ(withSegments({exifSegment('M', 8, ifdOf(3, 0), 200)}), FileLayout::Complete);
  std::vector<uint8_t> two = ifdOf(2, 8 + 2 + 24 + 4);
  const auto second = ifdOf(1, 0);
  two.insert(two.end(), second.begin(), second.end());
  EXPECT_EQ(withSegments({exifSegment('M', 8, two, 200)}), FileLayout::Complete);
  // Little-endian too.
  EXPECT_EQ(withSegments({exifSegment('I', 8, {1, 0, 0x12, 1, 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 100)}),
            FileLayout::Complete);
  // IFD0's entries past the segment's end (a count of 10, the segment over after 46 bytes of TIFF).
  EXPECT_EQ(withSegments({exifSegment('M', 8, {0, 10}, 60)}), FileLayout::Malformed);
  // IFD0 placed past the segment, or at an offset that is negative as JPEGDEC's int.
  EXPECT_EQ(withSegments({exifSegment('M', 4000, ifdOf(1, 0), 200)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({exifSegment('M', 0x80000000u, ifdOf(1, 0), 200)}), FileLayout::Malformed);
  // A second IFD JPEGDEC follows (its offset under the buffer's end) that runs past the segment, or sits before the
  // TIFF header (a negative offset).
  EXPECT_EQ(withSegments({exifSegment('M', 8, ifdOf(1, 155), 160)}), FileLayout::Malformed);
  EXPECT_EQ(withSegments({exifSegment('M', 8, ifdOf(1, 0xFFFFFFF0u), 160)}), FileLayout::Malformed);
  // A second IFD at or past the buffer's end is not followed by JPEGDEC, so not checked.
  EXPECT_EQ(withSegments({exifSegment('M', 8, ifdOf(1, 3000), 160)}), FileLayout::Complete);
  // A big APP1 with its IFDs inside JPEGDEC's buffer passes even though the segment runs past it.
  EXPECT_EQ(withSegments({exifSegment('M', 8, ifdOf(3, 0), 5000)}), FileLayout::Complete);
  // An IFD whose entries run past the buffer (IFD0 at 1,990 in a 3,000-byte segment, 20 entries) is refused.
  EXPECT_EQ(withSegments({exifSegment('M', 1990, {}, 3000)}), FileLayout::Complete);  // a count of 0 reads nothing more
  std::vector<uint8_t> far(1990 - 8, 0);
  const auto lateIfd = ifdOf(20, 0);
  far.insert(far.end(), lateIfd.begin(), lateIfd.end());
  EXPECT_EQ(withSegments({exifSegment('M', 1990, far, 3000)}), FileLayout::Malformed);
  // Not Exif by JPEGDEC's test (no byte order where it looks): not checked.
  auto xmp = exifSegment('M', 4000, {}, 60);
  xmp[10] = 'X';  // the byte order JPEGDEC looks for, 8 bytes on from the length field
  EXPECT_EQ(withSegments({xmp}), FileLayout::Complete);
}

TEST(JpegSegments, ACutHeaderIsRefused) {
  const auto file = bytes(JPEG_BASELINE);
  // APP0 is bytes [2, 20), SOF0 [89, 102), SOS's length at [320, 322).
  for (const size_t cut : {size_t{3}, size_t{5}, size_t{12}, size_t{19}}) {
    EXPECT_EQ(walkJpeg(file, {.size = cut}), cut < 4 ? FileLayout::TooShort : FileLayout::Truncated) << cut;
  }
  for (const size_t cut : {size_t{90}, size_t{92}, size_t{95}, size_t{101}, size_t{321}}) {
    EXPECT_EQ(walkJpeg(file, {.size = cut}), FileLayout::Truncated) << cut;
  }
  // Every cut of the samples is refused.
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE)}) {
    for (size_t cut = 0; cut < sample.size(); ++cut) {
      EXPECT_NE(walkJpeg(sample, {.size = cut}), FileLayout::Complete) << cut;
    }
  }
}

TEST(JpegSegments, NoSosIsSaidSo) {
  // A cut between segments, or an EOI, before any SOS.
  EXPECT_EQ(walkJpeg(bytes(JPEG_BASELINE), {.size = 89}), FileLayout::NoEnd);
  EXPECT_EQ(walkJpeg({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x02, 0xFF, 0xD9}), FileLayout::NoEnd);
  EXPECT_EQ(walkJpeg({0xFF, 0xD8, 0xFF}), FileLayout::TooShort);
}

TEST(JpegSegments, AMalformedHeaderIsRefused) {
  EXPECT_EQ(walkJpeg({0xFF, 0xD9, 0xFF, 0xDA, 0x00, 0x02}), FileLayout::Malformed);              // not SOI
  EXPECT_EQ(walkJpeg({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0xFF, 0xDA}), FileLayout::Malformed);  // length under 2
  EXPECT_EQ(walkJpeg(bytes(JPEG_BASELINE), {.failOnRead = 1}), FileLayout::ReadFailed);
  EXPECT_EQ(walkJpeg(bytes(JPEG_BASELINE), {.failOnRead = 3}), FileLayout::ReadFailed);
  EXPECT_EQ(walkJpeg(bytes(JPEG_BASELINE), {.stopAfter = 1}), FileLayout::Stopped);
  // The segment count is capped as the chunk count is.
  std::vector<uint8_t> many = {0xFF, 0xD8};
  for (uint32_t i = 0; i < MAX_LAYOUT_CHUNKS; ++i) many.insert(many.end(), {0xFF, 0xE2, 0x00, 0x02});
  many.insert(many.end(), {0xFF, 0xDA, 0x00, 0x02, 0xFF, 0xD9});
  EXPECT_EQ(walkJpeg(many, {.maxReads = 3 * static_cast<int>(MAX_LAYOUT_CHUNKS)}), FileLayout::Malformed);
}

// ---- the reads' answers -------------------------------------------------------------------------------------------

TEST(DecoderReads, TheFileGivesNothingPastTheWalkedSizeOrOnceStopped) {
  EXPECT_EQ(readAllowance(0, 100, 1000, false), 100);
  EXPECT_EQ(readAllowance(950, 100, 1000, false), 50);
  EXPECT_EQ(readAllowance(1000, 100, 1000, false), 0);
  EXPECT_EQ(readAllowance(5000, 100, 1000, false), 0);
  EXPECT_EQ(readAllowance(0, 100, 1000, true), 0);
  EXPECT_EQ(readAllowance(0, 0, 1000, false), 0);
  EXPECT_EQ(readAllowance(0, -5, 1000, false), 0);
}

TEST(DecoderReads, PngdecAlwaysGetsWhatItAskedFor) {
  std::array<uint8_t, 8> buf{1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_EQ(fillPngRead(buf.data(), 8, 3), 8);
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  buf.fill(7);
  EXPECT_EQ(fillPngRead(buf.data(), 8, -1), 8);  // a read error: all filler
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  buf.fill(7);
  EXPECT_EQ(fillPngRead(buf.data(), 8, 0), 8);  // nothing from the file (a stop, the end): all filler
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
  buf.fill(7);
  EXPECT_EQ(fillPngRead(buf.data(), 8, 8), 8);
  EXPECT_EQ(buf[7], 7);
  EXPECT_EQ(fillPngRead(buf.data(), 0, 0), 0);
}

TEST(DecoderReads, JpegdecGetsZerosNeverAStaleBuffer) {
  std::array<uint8_t, 8> buf{};
  // The header parse is told what the file gave; the rest of the span is zeros, not what the buffer held.
  buf.fill(0xD8);
  EXPECT_EQ(fillJpegRead(buf.data(), 8, 3, false), 3);
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{0xD8, 0xD8, 0xD8, 0, 0, 0, 0, 0}));
  buf.fill(0xD8);
  EXPECT_EQ(fillJpegRead(buf.data(), 8, 0, false), 0);  // nothing from the file
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{}));
  buf.fill(0xD8);
  EXPECT_EQ(fillJpegRead(buf.data(), 8, -1, false), 0);  // a read error
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{}));
  // The decode is told the whole span, so its buffer is always topped up.
  buf.fill(0xD8);
  EXPECT_EQ(fillJpegRead(buf.data(), 8, 0, true), 8);
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{}));
  buf.fill(0xD8);
  EXPECT_EQ(fillJpegRead(buf.data(), 8, 5, true), 8);
  EXPECT_EQ(buf, (std::array<uint8_t, 8>{0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0, 0, 0}));
  EXPECT_EQ(fillJpegRead(buf.data(), 0, 0, true), 0);
}

TEST(DecoderReads, JpegdecsPositionStaysUnderItsSize) {
  EXPECT_EQ(jpegPositionAfterRead(0, 1000), 0);
  EXPECT_EQ(jpegPositionAfterRead(500, 1000), 500);
  EXPECT_EQ(jpegPositionAfterRead(999, 1000), 999);
  EXPECT_EQ(jpegPositionAfterRead(1000, 1000), 999);
  EXPECT_EQ(jpegPositionAfterRead(5000, 1000), 999);
}

TEST(DecoderReads, TheReadCapCoversAWholeParseOrDecode) {
  EXPECT_EQ(decoderReadCap(0), 64u);
  EXPECT_EQ(decoderReadCap(2047), 64u);
  EXPECT_EQ(decoderReadCap(2048), 66u);
  EXPECT_EQ(decoderReadCap(66713), 2u * 32 + 64);
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
