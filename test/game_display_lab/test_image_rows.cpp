// The gray viewer's decode arithmetic (lib/GameDisplayLab/ImageRows, story dlab-g3): which files the Image row lists
// and their order, the converters' size limits and PNG checks, a PNG row to 8-bit gray as the converter converts one
// (alpha on white), JPEGDEC's coarse scale, the fit to the logical screen (the fit and the I/O matrix's Fit row), the
// seams between decoder blocks, the PNG and JPEG layout walks, the answers the reader gives a decoder's reads, and the
// bookkeeping around them (DecoderReads.h: which answer a read gets, the caps, the latch, the watchdog's quiet time).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "DecoderReads.h"
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

using fixtures::pngChunk;
using fixtures::pngFile;

// How a walk is fed: the first `size` bytes of the file (all by default), read `failOnRead` failing (1-based; 0
// never), the stop asked after `stopAfter` questions (-1 never), and the walk's reads counted against `maxReads`.
struct Feed {
  size_t size = SIZE_MAX;
  int failOnRead = 0;
  int stopAfter = -1;
  int maxReads = 64;
};

template <typename Walk>
FileLayout walkWith(Walk&& walkFile, const std::vector<uint8_t>& file, Feed feed, WalkExtent* extent) {
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
  const FileLayout layout = walkFile(static_cast<uint32_t>(feed.size), readAt, stopped, extent);
  EXPECT_LE(reads, feed.maxReads);
  return layout;
}

FileLayout walkPng(const std::vector<uint8_t>& file, const Feed& feed = {}, WalkExtent* extent = nullptr) {
  return walkWith(
      [](uint32_t size, auto& readAt, auto& stopped, WalkExtent* e) { return walkPngChunks(size, readAt, stopped, e); },
      file, feed, extent);
}

// The JPEG walk with the reader's EOI search buffer (JPEG_EOI_SEARCH_BYTES), or `searchBytes` of one.
FileLayout walkJpeg(const std::vector<uint8_t>& file, const Feed& feed = {}, WalkExtent* extent = nullptr,
                    const size_t searchBytes = JPEG_EOI_SEARCH_BYTES) {
  std::vector<uint8_t> search(searchBytes);
  return walkWith(
      [&search](uint32_t size, auto& readAt, auto& stopped, WalkExtent* e) {
        return walkJpegSegments(size, readAt, stopped, e, search.empty() ? nullptr : search.data(), search.size());
      },
      file, feed, extent);
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

TEST(PngChunks, TheWalkedSizeEndsAtIend) {
  // PNGdec has no IEND case: chunks after IEND would be parsed (a PLTE among them, copied unbounded). The walk stops at
  // IEND and its end is the walked size, so nothing after it reaches PNGdec (its reads there are filler).
  const auto file = pngFile({pngChunk("IHDR", 13), pngChunk("IDAT", 50), pngChunk("IEND", 0)});
  const auto tail = [&file](const std::vector<uint8_t>& after) {
    auto f = file;
    f.insert(f.end(), after.begin(), after.end());
    WalkExtent extent;
    const FileLayout layout = walkPng(f, {.maxReads = 4}, &extent);
    EXPECT_EQ(extent.end, file.size());
    return layout;
  };
  EXPECT_EQ(tail({}), FileLayout::Complete);
  EXPECT_EQ(tail(pngChunk("tEXt", 20)), FileLayout::Complete);
  const auto cut = pngChunk("tEXt", 20);
  EXPECT_EQ(tail(std::vector<uint8_t>(cut.begin(), cut.begin() + 10)), FileLayout::Complete);
  EXPECT_EQ(tail(pngChunk("PLTE", 3000)), FileLayout::Complete);
  EXPECT_EQ(tail(std::vector<uint8_t>(7, 0)), FileLayout::Complete);
  // The review's file: a short zlib stream, IEND, then a PLTE of 3,000 bytes.
  const auto review = fixtures::pngPaletteAfterIend(3000);
  WalkExtent extent;
  ASSERT_EQ(walkPng(review, {}, &extent), FileLayout::Complete);
  EXPECT_EQ(extent.end, 33u + 24u + 12u);
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

TEST(JpegSegments, EverySyntheticCase) {
  // The shared cases (image_fixtures.h), each with the walk's verdict; ImageDecoders opens and decodes the Complete
  // ones.
  for (const fixtures::JpegCase& c : fixtures::jpegSegmentCases()) {
    EXPECT_EQ(walkJpeg(c.file, {.maxReads = 256}), c.expected) << c.name;
  }
}

TEST(JpegSegments, ACompleteHeaderWalksToSosAndItsEoi) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_NOISE_BASELINE), bigExifJpeg()}) {
    WalkExtent extent;
    EXPECT_EQ(walkJpeg(sample, {.maxReads = 256}, &extent), FileLayout::Complete);
    EXPECT_EQ(extent.end, sample.size());  // EOI is the last two bytes
  }
  // A progressive file ends, for the decoder, at the marker after its first scan (the DC scan, all JPEGDEC decodes),
  // or its first 2 KiB when that is further (JPEGParseInfo refuses a first read under 256 bytes): JPEG_PROGRESSIVE's
  // DC scan ends at 151, where the next scan's DHT begins, so all 528 bytes; JPEG_NOISE_PROGRESSIVE's at 275, so 2 KiB
  // of its 7,438. JPEG_PROGRESSIVE_512's DC scan of about 3 KB ends past them.
  for (const auto& [sample, end] :
       {std::pair{bytes(JPEG_PROGRESSIVE), 528u}, std::pair{bytes(JPEG_NOISE_PROGRESSIVE), 2048u}}) {
    WalkExtent extent;
    EXPECT_EQ(walkJpeg(sample, {.maxReads = 256}, &extent), FileLayout::Complete);
    EXPECT_EQ(extent.end, end);
  }
  const auto progressive512 = bytes(JPEG_PROGRESSIVE_512);
  WalkExtent extent512;
  EXPECT_EQ(walkJpeg(progressive512, {.maxReads = 256}, &extent512), FileLayout::Complete);
  EXPECT_GT(extent512.end, 2048u);
  EXPECT_LT(extent512.end, progressive512.size());
  EXPECT_EQ(progressive512[extent512.end - 2], 0xFF);
  EXPECT_GE(progressive512[extent512.end - 1], 0xC0);
  const std::vector<uint8_t> big = bigExifJpeg();
  EXPECT_EQ(big.size(), 66713u);
  WalkExtent extent;
  ASSERT_EQ(walkJpeg(big, {}, &extent), FileLayout::Complete);
  // The header ends with JPEG_BASELINE's SOS (bytes [318, 328) of it, 2 + 4,100 + 62,008 - 2 bytes on); the parse's
  // buffer moves: past the APP1, past each COM, and to the baseline's APP0.
  EXPECT_EQ(extent.headerEnd, 2u + 4100u + 62008u + 328u - 2u);
  EXPECT_EQ(extent.windowMoves, 3u);
  WalkExtent small;
  ASSERT_EQ(walkJpeg(bytes(JPEG_BASELINE), {}, &small), FileLayout::Complete);
  EXPECT_EQ(small.headerEnd, 328u);
  EXPECT_EQ(small.windowMoves, 0u);
}

TEST(JpegSegments, TheWalkedSizeEndsAtTheFirstEoiAfterSos) {
  // A motion photo's video, or padding after the EOI: the walk finds the EOI and stops there.
  const auto file = bytes(JPEG_BASELINE);
  for (const auto& tail :
       {std::vector<uint8_t>(1, 0), std::vector<uint8_t>(1000, 0x5A), std::vector<uint8_t>{0xFF, 0xD9, 0x00, 0x11}}) {
    auto longer = file;
    longer.insert(longer.end(), tail.begin(), tail.end());
    WalkExtent extent;
    EXPECT_EQ(walkJpeg(longer, {.maxReads = 256}, &extent), FileLayout::Complete);
    EXPECT_EQ(extent.end, file.size());
  }
  // A file ending in FF D9 ends there, found by one read of its tail, unless an MPF APP2 says more pictures follow
  // (a phone's preview appended): then the first picture alone is the decoder's.
  auto twice = file;
  twice.insert(twice.end(), JPEG_PROGRESSIVE, JPEG_PROGRESSIVE + sizeof(JPEG_PROGRESSIVE));
  WalkExtent whole;
  EXPECT_EQ(walkJpeg(twice, {.maxReads = 21}, &whole), FileLayout::Complete);  // the header's 20 reads and the tail
  EXPECT_EQ(whole.end, twice.size());
  std::vector<uint8_t> mpfBody = {'M', 'P', 'F', 0};
  mpfBody.resize(40, 0);
  auto mpf = fixtures::jpegWithSegments({fixtures::segmentOf(0xE2, mpfBody)});
  const size_t firstPicture = mpf.size();
  mpf.insert(mpf.end(), JPEG_PROGRESSIVE, JPEG_PROGRESSIVE + sizeof(JPEG_PROGRESSIVE));
  WalkExtent first;
  EXPECT_EQ(walkJpeg(mpf, {}, &first), FileLayout::Complete);
  EXPECT_EQ(first.end, firstPicture);
  // An APP2 that is not MPF (an ICC profile) leaves the tail as the end.
  std::vector<uint8_t> iccBody = {'I', 'C', 'C', '_'};
  iccBody.resize(40, 0);
  auto icc = fixtures::jpegWithSegments({fixtures::segmentOf(0xE2, iccBody)});
  icc.insert(icc.end(), JPEG_PROGRESSIVE, JPEG_PROGRESSIVE + sizeof(JPEG_PROGRESSIVE));
  WalkExtent tail;
  EXPECT_EQ(walkJpeg(icc, {}, &tail), FileLayout::Complete);
  EXPECT_EQ(tail.end, icc.size());
  // Otherwise the search reads forward in reads of the caller's buffer and stops at the EOI: a big tail is not read
  // (the header takes 20 reads, the tail one, the scan's 7.6 KB two more).
  auto big = bytes(JPEG_NOISE_BASELINE);
  big.insert(big.end(), 100000, 0x00);
  WalkExtent extent;
  EXPECT_EQ(walkJpeg(big, {.maxReads = 23}, &extent), FileLayout::Complete);
  EXPECT_EQ(extent.end, sizeof(JPEG_NOISE_BASELINE));
  // Without an EOI the whole rest is read once, JPEG_EOI_SEARCH_BYTES a read.
  big.erase(big.begin() + static_cast<long>(sizeof(JPEG_NOISE_BASELINE)) - 2,
            big.begin() + static_cast<long>(sizeof(JPEG_NOISE_BASELINE)));
  const int rest = static_cast<int>(big.size()) - 330;
  const int searchReads =
      (rest + static_cast<int>(JPEG_EOI_SEARCH_BYTES) - 1) / static_cast<int>(JPEG_EOI_SEARCH_BYTES);
  EXPECT_EQ(walkJpeg(big, {.maxReads = 21 + searchReads}), FileLayout::NoEnd);
}

TEST(JpegSegments, TheEoiSearchCarriesAnFfAcrossItsReads) {
  // FF at the end of one read and D9 at the start of the next: found, at every buffer size and every split.
  const auto file = bytes(JPEG_BASELINE);
  const size_t sosEnd = 328;
  for (const size_t searchBytes : {size_t{17}, size_t{64}, size_t{256}, JPEG_EOI_SEARCH_BYTES}) {
    for (size_t split = 1; split <= searchBytes; ++split) {
      // Scan bytes (no FF) up to the split, then FF D9.
      std::vector<uint8_t> f(file.begin(), file.begin() + static_cast<long>(sosEnd));
      f.resize(sosEnd + split - 1, 0x5A);
      f.insert(f.end(), {0xFF, 0xD9, 0x00, 0x00});
      WalkExtent extent;
      ASSERT_EQ(walkJpeg(f, {.maxReads = 64}, &extent, searchBytes), FileLayout::Complete)
          << searchBytes << " split " << split;
      EXPECT_EQ(extent.end, sosEnd + split + 1) << searchBytes << " split " << split;
    }
  }
  // An FF ending a read followed by anything else is no EOI; D9 alone at a read's start after no FF is none either.
  std::vector<uint8_t> f(file.begin(), file.begin() + static_cast<long>(sosEnd));
  f.resize(sosEnd + 16, 0x5A);
  f.push_back(0xFF);
  f.push_back(0x00);
  f.resize(sosEnd + 33, 0x5A);
  f.push_back(0xD9);
  EXPECT_EQ(walkJpeg(f, {.maxReads = 64}, nullptr, 17), FileLayout::NoEnd);
  // A buffer smaller than the walk's own (or none) uses its own 17 bytes.
  f.insert(f.end(), {0xFF, 0xD9});
  EXPECT_EQ(walkJpeg(f, {.maxReads = 64}, nullptr, 0), FileLayout::Complete);
  EXPECT_EQ(walkJpeg(f, {.maxReads = 64}, nullptr, 4), FileLayout::Complete);
}

TEST(JpegSegments, AScanCutShortHasNoEnd) {
  // A scan with no EOI after it was cut (or never closed): JPEGDEC would decode past the data it has.
  const auto file = bytes(JPEG_BASELINE);
  for (size_t cut = 330; cut < file.size(); ++cut) {
    EXPECT_EQ(walkJpeg(file, {.size = cut, .maxReads = 64}), FileLayout::NoEnd) << cut;
  }
  EXPECT_EQ(walkJpeg(std::vector<uint8_t>(file.begin(), file.begin() + 328)), FileLayout::NoEnd);  // the header alone
  // The forward search stops when asked, and says so when a read fails.
  auto big = bytes(JPEG_NOISE_BASELINE);
  big.resize(big.size() - 2);
  big.insert(big.end(), 50000, 0x00);
  EXPECT_EQ(walkJpeg(big, {.stopAfter = 10, .maxReads = 64}), FileLayout::Stopped);
  EXPECT_EQ(walkJpeg(big, {.failOnRead = 12, .maxReads = 64}), FileLayout::ReadFailed);
}

TEST(JpegSegments, TooManyBufferMovesAreRefused) {
  // Each COM segment of 1,100 bytes moves JPEGParseInfo's buffer once: MAX_JPEG_WINDOW_MOVES of them pass.
  const auto withComs = [](const uint32_t count) {
    std::vector<std::vector<uint8_t>> coms(count, fixtures::segmentOf(0xFE, std::vector<uint8_t>(1100, 'c')));
    return fixtures::jpegWithSegments(coms);
  };
  WalkExtent extent;
  // Each COM after the first moves it, and the baseline's own APP0 after them once more.
  EXPECT_EQ(walkJpeg(withComs(MAX_JPEG_WINDOW_MOVES), {.maxReads = 2000}, &extent), FileLayout::Complete);
  EXPECT_EQ(extent.windowMoves, MAX_JPEG_WINDOW_MOVES);
  EXPECT_EQ(walkJpeg(withComs(MAX_JPEG_WINDOW_MOVES + 1), {.maxReads = 2000}), FileLayout::Malformed);
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
  // Every cut of the baseline sample is refused; of the progressive one, every cut before the end of its first scan
  // (the marker after it, at 153), and every one after it is whole to the decoder (under 256 bytes JPEGDEC's open
  // refuses it: "could not open").
  const auto baseline = bytes(JPEG_BASELINE);
  for (size_t cut = 0; cut < baseline.size(); ++cut) {
    EXPECT_NE(walkJpeg(baseline, {.size = cut}), FileLayout::Complete) << cut;
  }
  const auto progressive = bytes(JPEG_PROGRESSIVE);
  for (size_t cut = 0; cut < progressive.size(); ++cut) {
    EXPECT_EQ(walkJpeg(progressive, {.size = cut}) == FileLayout::Complete, cut >= 153) << cut;
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

TEST(DecoderReads, JpegdecsHeaderParseKeepsAReadWholeOrZeroesItsBuffer) {
  // A 2 KiB parse buffer whose last 8 bytes are the read's span.
  std::array<uint8_t, JPEG_PARSE_BUFFER> buffer{};
  uint8_t* const span = buffer.data() + JPEG_PARSE_BUFFER - 8;
  buffer.fill(0xD8);
  // Kept: told what the file gave, the rest of the span zeroed, the bytes before it untouched.
  EXPECT_EQ(answerJpegHeaderRead(span, 8, 3, false), 3);
  EXPECT_EQ(span[2], 0xD8);
  for (int i = 3; i < 8; ++i) EXPECT_EQ(span[i], 0) << i;
  EXPECT_EQ(buffer[0], 0xD8);
  // Dropped: the whole buffer zeroed, the bytes already in it too, and nothing told.
  buffer.fill(0xD8);
  EXPECT_EQ(answerJpegHeaderRead(span, 8, 3, true), 0);
  for (const uint8_t b : buffer) EXPECT_EQ(b, 0);
  // A read of the whole buffer (the parse's first).
  buffer.fill(0xD8);
  EXPECT_EQ(answerJpegHeaderRead(buffer.data(), JPEG_PARSE_BUFFER, 0, true), 0);
  for (const uint8_t b : buffer) EXPECT_EQ(b, 0);
  buffer.fill(0xD8);
  EXPECT_EQ(answerJpegHeaderRead(span, 8, 0, false), 0);  // nothing from the file, not a failure (the walked end)
  EXPECT_EQ(answerJpegHeaderRead(span, 8, -1, false), 0);
  EXPECT_EQ(answerJpegHeaderRead(span, 0, 0, true), 0);
}

TEST(DecoderReads, JpegdecsDecodeGetsTheWholeSpan) {
  std::array<uint8_t, JPEG_PARSE_BUFFER> buffer{};
  // A refill of 1,800 bytes (248 already in the buffer), the file giving 1,000 bytes of scan data.
  uint8_t* const span = buffer.data() + 248;
  for (int i = 0; i < 1800; ++i) span[i] = static_cast<uint8_t>(i % 200);
  EXPECT_EQ(answerJpegDecodeRead(span, 1800, 1000), 1800);
  EXPECT_EQ(span[999], 999 % 200);
  for (int i = 1000; i < 1800; ++i) EXPECT_EQ(span[i], 0) << i;
  // Nothing from the file (a stop, the end, a failed read): all zeros, the whole span told.
  EXPECT_EQ(answerJpegDecodeRead(span, 1800, 0), 1800);
  EXPECT_EQ(answerJpegDecodeRead(span, 1800, -1), 1800);
  EXPECT_EQ(answerJpegDecodeRead(span, 0, 0), 0);
}

TEST(DecoderReads, JpegFilteringIsCountedAsJpegdecFilters) {
  const auto filtered = [](std::initializer_list<uint8_t> bytes) {
    const std::vector<uint8_t> v(bytes);
    return jpegFilteredLength(v.data(), static_cast<int32_t>(v.size()));
  };
  EXPECT_EQ(filtered({1, 2, 3}), 3);
  EXPECT_EQ(filtered({1, 0xFF, 0x00, 2}), 3);        // a stuffed 0xFF stays, its 0 goes
  EXPECT_EQ(filtered({1, 0xFF, 0xD0, 2}), 2);        // a restart marker goes whole
  EXPECT_EQ(filtered({0xFF, 0xFF, 0xFF, 0xFF}), 0);  // a run of 0xFF: markers, all gone
  EXPECT_EQ(filtered({1, 2, 0xFF}), 2);              // a last 0xFF waits for the next read
  EXPECT_EQ(filtered({1, 0xFF, 0x00}), 2);
  EXPECT_EQ(filtered({}), 0);
}

TEST(DecoderReads, RestartMarkersCountWholeTowardStarvation) {
  const auto starvation = [](std::initializer_list<uint8_t> bytes) {
    const std::vector<uint8_t> v(bytes);
    return jpegStarvationLength(v.data(), static_cast<int32_t>(v.size()));
  };
  EXPECT_EQ(starvation({1, 2, 3}), 3);
  EXPECT_EQ(starvation({1, 0xFF, 0x00, 2}), 3);  // a stuffed 0xFF: as filtered
  for (uint8_t rst = 0xD0; rst <= 0xD7; ++rst) EXPECT_EQ(starvation({1, 0xFF, rst, 2}), 4) << int{rst};
  EXPECT_EQ(starvation({1, 0xFF, 0xCF, 2}), 2);  // other markers: as filtered
  EXPECT_EQ(starvation({1, 0xFF, 0xD8, 2}), 2);
  EXPECT_EQ(starvation({0xFF, 0xFF, 0xFF, 0xFF}), 0);
  EXPECT_EQ(starvation({1, 2, 0xFF}), 2);
}

TEST(DecoderReads, ScanDataThatWouldStarveTheDecoderIsTamed) {
  std::vector<uint8_t> span(1800);
  // Real scan data, a few stuffed 0xFF and a restart marker: untouched.
  for (size_t i = 0; i < span.size(); ++i) span[i] = static_cast<uint8_t>(i % 251);
  span[100] = 0xFF;
  span[101] = 0x00;
  span[500] = 0xFF;
  span[501] = 0xD3;
  const std::vector<uint8_t> real = span;
  answerJpegDecodeRead(span.data(), 1800, 1800);
  EXPECT_EQ(span, real);
  // An erased sector (all 0xFF) would filter to nothing: each 0xFF before another becomes 0, the last waits.
  std::fill(span.begin(), span.end(), 0xFF);
  answerJpegDecodeRead(span.data(), 1800, 1800);
  EXPECT_EQ(jpegFilteredLength(span.data(), 1800), 1799);
  EXPECT_EQ(span[1799], 0xFF);
  // A run of 0xFF ending in a stuffed 0xFF 00, a restart marker or other data keeps its last 0xFF and what follows.
  for (const uint8_t after : {uint8_t{0x00}, uint8_t{0xD5}, uint8_t{0x12}}) {
    std::fill(span.begin(), span.end(), 0x5A);
    std::fill(span.begin() + 10, span.begin() + 1200, 0xFF);
    span[1200] = after;
    answerJpegDecodeRead(span.data(), 1800, 1800);
    for (size_t i = 10; i < 1199; ++i) ASSERT_EQ(span[i], 0) << i;
    EXPECT_EQ(span[1199], 0xFF);
    EXPECT_EQ(span[1200], after);
    EXPECT_EQ(span[1201], 0x5A);
  }
  // Pairs of 0xFF 00 filter to half but are never touched (stuffed bytes are data; 0004 ends a decode they starve).
  for (size_t i = 0; i < span.size(); ++i) span[i] = i % 2 == 0 ? 0xFF : 0x00;
  const std::vector<uint8_t> stuffed = span;
  answerJpegDecodeRead(span.data(), 1800, 1800);
  EXPECT_EQ(span, stuffed);
  // Flat line art with a restart marker after every few bytes: under the mark as filtered, but the markers count
  // whole, so it is never tamed, even with runs of 0xFF 0xFF between them.
  for (size_t i = 0; i < span.size(); ++i) {
    const size_t at = i % 4;
    span[i] = at == 2 ? 0xFF : at == 3 ? static_cast<uint8_t>(0xD0 + (i / 4) % 8) : 0x00;
  }
  span[0] = 0xFF;
  span[1] = 0xFF;
  const std::vector<uint8_t> restarts = span;
  answerJpegDecodeRead(span.data(), 1800, 1800);
  EXPECT_EQ(span, restarts);
  // An erased run topping up a buffer that already holds 1,948 bytes leaves it over the mark and margin: untouched.
  std::vector<uint8_t> top(100, 0xFF);
  const std::vector<uint8_t> erased = top;
  answerJpegDecodeRead(top.data(), 100, 100);
  EXPECT_EQ(top, erased);
}

TEST(DecoderReads, JpegdecsPositionStaysUnderItsSize) {
  EXPECT_EQ(jpegPositionAfterRead(0, 1000), 0);
  EXPECT_EQ(jpegPositionAfterRead(500, 1000), 500);
  EXPECT_EQ(jpegPositionAfterRead(999, 1000), 999);
  EXPECT_EQ(jpegPositionAfterRead(1000, 1000), 999);
  EXPECT_EQ(jpegPositionAfterRead(5000, 1000), 999);
}

TEST(DecoderReads, TheReadCapsCoverAWholeParseOrDecode) {
  EXPECT_EQ(decoderReadCap(0), 64u);
  EXPECT_EQ(decoderReadCap(2047), 64u);
  EXPECT_EQ(decoderReadCap(2048), 66u);
  EXPECT_EQ(decoderReadCap(66713), 2u * 32 + 64);
  EXPECT_EQ(jpegHeaderReadCap(0), 4u);
  EXPECT_EQ(jpegHeaderReadCap(MAX_JPEG_WINDOW_MOVES), MAX_JPEG_WINDOW_MOVES + 4);
}

// A file for DecoderReads.h's callbacks: `bytes`, the position, a read that fails or gives `shortBy` fewer bytes than
// asked, a refused seek past the end, the stop, and the progress counted.
struct MemFile {
  std::vector<uint8_t> bytes;
  uint32_t pos = 0;
  bool open = true;
  bool fail = false;
  int32_t shortBy = 0;
  bool stop = false;
  int progressed = 0;

  int calls = 0;

  bool isOpen() const { return open; }
  int32_t read(uint8_t* buf, const int32_t n) {
    ++calls;
    if (fail) return -1;
    int32_t got = 0;
    while (got < n - shortBy && pos < bytes.size()) buf[got++] = bytes[pos++];
    return got;
  }
  bool seek(const uint32_t at) {
    if (at > bytes.size()) return false;
    pos = at;
    return true;
  }
  bool stopped() const { return stop; }
  void progress() { ++progressed; }
};

MemFile memFile(const size_t size) {
  MemFile f;
  f.bytes.resize(size);
  for (size_t i = 0; i < size; ++i) f.bytes[i] = static_cast<uint8_t>(1 + i % 200);
  return f;
}

DecoderReads readsOf(const uint32_t walked, const uint32_t headerEnd, const uint32_t moves = 0) {
  DecoderReads reads;
  reads.start(WalkExtent{walked, headerEnd, moves});
  return reads;
}

TEST(DecoderReads, PngReadsAreTheFilesBytesThenFiller) {
  MemFile f = memFile(100);
  DecoderReads reads = readsOf(90, 0);
  std::array<uint8_t, 64> buf{};
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 64), 64);
  EXPECT_EQ(buf[63], 64);
  EXPECT_EQ(f.progressed, 1);
  // 26 bytes left of the walked 90, then filler; at the walked size, filler only and no progress.
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 64), 64);
  EXPECT_EQ(buf[25], 90);
  EXPECT_EQ(buf[26], 0xFF);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 64), 64);
  EXPECT_EQ(buf[0], 0xFF);
  EXPECT_EQ(f.progressed, 2);
  // A refused seek gives nothing until a seek succeeds; a failed read, a stop or no handle give filler.
  EXPECT_EQ(answerSeek(f, reads, 500), -1);
  EXPECT_TRUE(reads.seekFailed);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 8), 8);
  EXPECT_EQ(buf[0], 0xFF);
  EXPECT_EQ(answerSeek(f, reads, 10), 10);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 8), 8);
  EXPECT_EQ(buf[0], 11);
  EXPECT_EQ(answerSeek(f, reads, -1), -1);
  f.pos = reads.position = 0;
  reads.seekFailed = false;
  f.stop = true;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 8), 8);
  EXPECT_EQ(buf[0], 0xFF);
  f.stop = false;
  f.fail = true;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 8), 8);
  EXPECT_EQ(buf[0], 0xFF);
  f.open = false;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 8), 8);
  EXPECT_EQ(answerSeek(f, reads, 0), -1);
}

TEST(DecoderReads, OnlyOpensReadsBeforeTheHeadersEndAreLatched) {
  std::vector<uint8_t> parse(JPEG_PARSE_BUFFER, 0xD8);
  int32_t iPos = 0;
  // A header read the file gives short: dropped, the parse buffer zeroed, latched.
  MemFile f = memFile(5000);
  DecoderReads reads = readsOf(5000, 3000);
  reads.startJpegOpen();
  f.shortBy = 1;
  EXPECT_EQ(answerJpegRead(f, reads, parse.data(), JPEG_PARSE_BUFFER, 5000, iPos), 0);
  EXPECT_TRUE(reads.headerFailed);
  for (const uint8_t b : parse) ASSERT_EQ(b, 0);
  // Every later header read is dropped too, even one the file would give whole.
  f.shortBy = 0;
  f.pos = reads.position = 2048;
  EXPECT_EQ(answerJpegRead(f, reads, parse.data() + 1024, 1024, 5000, iPos), 0);
  // open()'s top-up at the header's end is the decode's: never latched, the file's bytes, the whole span told.
  f.pos = reads.position = 3000;
  std::fill(parse.begin(), parse.end(), 0xD8);
  EXPECT_EQ(answerJpegRead(f, reads, parse.data() + 1024, 1024, 5000, iPos), 1024);
  EXPECT_EQ(parse[1024], f.bytes[3000]);
  EXPECT_EQ(iPos, 4024);
  // In decode() every read is the decode's, one before the header's end too.
  reads.startDecode();
  f.pos = reads.position = 100;
  EXPECT_EQ(answerJpegRead(f, reads, parse.data(), 64, 5000, iPos), 64);
  EXPECT_EQ(parse[0], f.bytes[100]);
  EXPECT_EQ(reads.decodeReads, 1u);
}

TEST(DecoderReads, JpegHeaderReadsAreDroppedAtAStopAFailureOrTheirCap) {
  // Reads into the parse buffer's last 64 bytes (a dropped read zeroes the whole buffer before them).
  std::vector<uint8_t> parse(JPEG_PARSE_BUFFER, 0xD8);
  uint8_t* const tail = parse.data() + JPEG_PARSE_BUFFER - 64;
  int32_t iPos = 0;
  for (int why = 0; why < 3; ++why) {
    MemFile f = memFile(5000);
    DecoderReads reads = readsOf(5000, 3000, 2);
    reads.startJpegOpen();
    if (why == 0) f.stop = true;
    if (why == 1) f.fail = true;
    if (why == 2) reads.seekFailed = true;
    EXPECT_EQ(answerJpegRead(f, reads, tail, 64, 5000, iPos), 0) << why;
    EXPECT_TRUE(reads.headerFailed) << why;
  }
  {
    // No handle (the open callback failed): taken as at the walked size, zeros.
    MemFile f = memFile(5000);
    f.open = false;
    DecoderReads reads = readsOf(5000, 3000, 2);
    reads.startJpegOpen();
    EXPECT_EQ(answerJpegRead(f, reads, tail, 64, 5000, iPos), 64);
    EXPECT_EQ(tail[0], 0);
    EXPECT_EQ(iPos, 4999);
  }
  // The cap: the walk's buffer moves and four header reads are the file's, the next is dropped.
  MemFile f = memFile(5000);
  DecoderReads reads = readsOf(5000, 3000, 2);
  reads.startJpegOpen();
  for (uint32_t i = 0; i < jpegHeaderReadCap(2); ++i) {
    f.pos = reads.position = 0;
    EXPECT_EQ(answerJpegRead(f, reads, tail, 64, 5000, iPos), 64) << i;
  }
  EXPECT_FALSE(reads.headerFailed);
  f.pos = reads.position = 0;
  EXPECT_EQ(answerJpegRead(f, reads, tail, 64, 5000, iPos), 0);
  EXPECT_TRUE(reads.headerFailed);
}

TEST(DecoderReads, JpegDecodeReadsAreTheFilesForTheirCapThenZeros) {
  MemFile f = memFile(4096);
  DecoderReads reads = readsOf(4096, 10);
  reads.startDecode();
  std::vector<uint8_t> buf(16);
  int32_t iPos = 0;
  for (uint32_t i = 0; i < decoderReadCap(4096); ++i) {
    f.pos = reads.position = 20;
    ASSERT_EQ(answerJpegRead(f, reads, buf.data(), 16, 4096, iPos), 16);
    ASSERT_EQ(buf[0], f.bytes[20]) << i;
  }
  f.pos = reads.position = 20;
  EXPECT_EQ(answerJpegRead(f, reads, buf.data(), 16, 4096, iPos), 16);
  EXPECT_EQ(buf[0], 0);
  // A stop gives zeros; the seek keeps JPEGFILE's position under the size.
  reads.startDecode();
  f.stop = true;
  f.pos = reads.position = 20;
  EXPECT_EQ(answerJpegRead(f, reads, buf.data(), 16, 4096, iPos), 16);
  EXPECT_EQ(buf[0], 0);
  EXPECT_EQ(answerJpegSeek(f, reads, 4096, 4096, iPos), 4096);
  EXPECT_EQ(iPos, 4095);
  iPos = 7;
  EXPECT_EQ(answerJpegSeek(f, reads, 5000, 4096, iPos), -1);
  EXPECT_EQ(iPos, 7);
}

TEST(DecoderReads, TheWatchdogIsLeftForFileCallsOnlyWhileTheReadMakesProgress) {
  WatchdogQuiet quiet(5000, 0);
  // Time in file calls is not quiet time: a 3 s call.
  EXPECT_TRUE(quiet.mayLeave(1000));
  quiet.back(4000);
  EXPECT_TRUE(quiet.mayLeave(6000));
  EXPECT_EQ(quiet.quietMs(), 3000u);
  quiet.back(6000);
  // Reads without progress: the quiet time adds up to the timeout, and then the task stays watched.
  EXPECT_TRUE(quiet.mayLeave(7999));
  quiet.back(7999);
  EXPECT_FALSE(quiet.mayLeave(8000));
  quiet.back(8000);
  EXPECT_FALSE(quiet.mayLeave(8001));
  // Progress starts it again.
  quiet.progress(9000);
  EXPECT_EQ(quiet.quietMs(), 0u);
  EXPECT_TRUE(quiet.mayLeave(13999));
  // Long file calls with little between them (slow failing reads): watched once twice the limit has passed since the
  // last progress, whatever the quiet time.
  quiet.progress(20000);
  EXPECT_TRUE(quiet.mayLeave(20001));
  quiet.back(29000);
  EXPECT_TRUE(quiet.mayLeave(29001));
  quiet.back(30000);
  EXPECT_FALSE(quiet.mayLeave(30001));
  EXPECT_LT(quiet.quietMs(), 10u);
  // millis() wrapping is counted as elapsed time.
  WatchdogQuiet wrap(10000, 0xFFFFF000u);
  EXPECT_TRUE(wrap.mayLeave(0x00000F00u));
  EXPECT_EQ(wrap.quietMs(), 0x1F00u);
}

TEST(DecoderReads, ReadsThatCanGiveNothingMakeNoFileCall) {
  MemFile f = memFile(1000);
  DecoderReads reads = readsOf(600, 0);
  std::array<uint8_t, 256> buf{};
  // The position is the reads' own: 0 at the open, a seek's target, plus each read's bytes.
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_EQ(reads.position, 256u);
  EXPECT_EQ(answerSeek(f, reads, 500), 500);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);  // 100 bytes to the walked size
  EXPECT_EQ(reads.position, 600u);
  EXPECT_EQ(f.calls, 2);
  // At the walked size, after a refused seek, once stopped: no call.
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_EQ(answerSeek(f, reads, 5000), -1);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_EQ(answerSeek(f, reads, 0), 0);
  f.stop = true;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_EQ(f.calls, 2);
  // A failed read loses the position: nothing more until a seek succeeds.
  f.stop = false;
  f.fail = true;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_TRUE(reads.seekFailed);
  f.fail = false;
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 256), 256);
  EXPECT_EQ(f.calls, 3);
  EXPECT_EQ(answerSeek(f, reads, 10), 10);
  EXPECT_EQ(answerPngRead(f, reads, buf.data(), 16), 16);
  EXPECT_EQ(buf[0], f.bytes[10]);
  EXPECT_EQ(f.calls, 4);
  // JPEGFILE's position is the reads' position, held under the size.
  DecoderReads jpeg = readsOf(1000, 10);
  jpeg.startDecode();
  MemFile g = memFile(1000);
  int32_t iPos = -1;
  EXPECT_EQ(answerJpegRead(g, jpeg, buf.data(), 256, 1000, iPos), 256);
  EXPECT_EQ(iPos, 256);
  EXPECT_EQ(answerJpegSeek(g, jpeg, 990, 1000, iPos), 990);
  EXPECT_EQ(answerJpegRead(g, jpeg, buf.data(), 256, 1000, iPos), 256);
  EXPECT_EQ(iPos, 999);
}

TEST(DecoderReads, RestartMarkersAreDroppedFromTheDecodesReadsAndTheSpanToppedUp) {
  // Scan data with a restart marker after every two bytes, as flat areas with a restart interval of 1 give: the decode
  // answer drops them and fills the span with more of the file.
  MemFile f = memFile(0);
  for (int i = 0; i < 2000; ++i) f.bytes.insert(f.bytes.end(), {0x11, 0x22, 0xFF, static_cast<uint8_t>(0xD0 + i % 8)});
  DecoderReads reads = readsOf(static_cast<uint32_t>(f.bytes.size()), 0);
  reads.startDecode();
  std::vector<uint8_t> span(2048);
  int32_t iPos = 0;
  EXPECT_EQ(answerJpegRead(f, reads, span.data(), 2048, static_cast<int32_t>(f.bytes.size()), iPos), 2048);
  for (size_t i = 0; i < span.size(); ++i) ASSERT_EQ(span[i], i % 2 == 0 ? 0x11 : 0x22) << i;
  EXPECT_EQ(reads.position, 4094u);  // 2,048 data bytes and the markers between them
  EXPECT_EQ(reads.decodeReads, 1u);  // the top-ups are within the one answer
  EXPECT_EQ(f.calls, 11);            // 2,048 bytes read, then 1,024, 512 ... 2
  EXPECT_EQ(iPos, 4094);
  // A scan almost all markers: once the top-ups run out with the file going on, what there is is told, no zeros.
  MemFile m = memFile(0);
  for (int i = 0; i < 3000; ++i) {
    m.bytes.insert(m.bytes.end(),
                   {0xFF, 0xD3, 0xFF, 0xD4, 0xFF, 0xD5, 0xFF, 0xD6, 0xFF, 0xD7, 0xFF, 0xD0, 0xFF, 0xD1, 0xFF, 0xD2,
                    0xFF, 0xD3, 0xFF, 0xD4, 0xFF, 0xD5, 0xFF, 0xD6, 0xFF, 0xD7, 0xFF, 0xD0, 0xFF, 0xD1, 0x5A});
  }
  DecoderReads mostly = readsOf(static_cast<uint32_t>(m.bytes.size()), 0);
  mostly.startDecode();
  const int32_t told = answerJpegRead(m, mostly, span.data(), 2048, static_cast<int32_t>(m.bytes.size()), iPos);
  EXPECT_GT(told, 0);
  EXPECT_LT(told, 2048);
  // (A final 0xFF is half a marker the next read completes, as JPEGFilter carries it.)
  for (int32_t i = 0; i < told; ++i) {
    const uint8_t v = span[static_cast<size_t>(i)];
    ASSERT_TRUE(v == 0x5A || (v == 0xFF && i == told - 1)) << i;
  }
  EXPECT_EQ(m.calls, 1 + JPEG_RESTART_TOP_UPS);
}

TEST(DecoderReads, RestartMarkersAreDroppedAsJpegdecPairsThem) {
  const auto dropped = [](std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> v(bytes);
    v.resize(static_cast<size_t>(jpegDropRestartMarkers(v.data(), static_cast<int32_t>(v.size()))));
    return v;
  };
  EXPECT_EQ(dropped({1, 0xFF, 0xD0, 2}), (std::vector<uint8_t>{1, 2}));
  for (uint8_t rst = 0xD0; rst <= 0xD7; ++rst) EXPECT_EQ(dropped({0xFF, rst}), std::vector<uint8_t>{}) << int{rst};
  EXPECT_EQ(dropped({1, 0xFF, 0x00, 2}), (std::vector<uint8_t>{1, 0xFF, 0x00, 2}));  // a stuffed 0xFF stays
  EXPECT_EQ(dropped({0xFF, 0xD8, 0xFF, 0xCF}), (std::vector<uint8_t>{0xFF, 0xD8, 0xFF, 0xCF}));
  // A pair is a 0xFF and the byte after it, as JPEGFilter takes it: 0xFF 0xFF then D0 keeps all three.
  EXPECT_EQ(dropped({0xFF, 0xFF, 0xD0}), (std::vector<uint8_t>{0xFF, 0xFF, 0xD0}));
  EXPECT_EQ(dropped({1, 2, 0xFF}), (std::vector<uint8_t>{1, 2, 0xFF}));  // a final 0xFF waits
  EXPECT_EQ(dropped({}), std::vector<uint8_t>{});
}

// A task watchdog that records what it was asked: its subscription, the joins, leaves and feeds, failures to add on
// demand, the clock, and the reports.
struct FakeDog {
  WatchdogState initial = WatchdogState::NotSubscribed;
  bool subscribed = false;
  int failAdds = 0;
  int addError = 0x101;
  uint32_t clock = 0;
  std::string log;
  std::vector<std::pair<WatchdogEvent, int>> reports;

  WatchdogState state() {
    if (initial == WatchdogState::Subscribed) subscribed = true;
    return initial;
  }
  int add() {
    if (failAdds > 0) {
      --failAdds;
      log += 'x';
      return addError;
    }
    subscribed = true;
    log += 'a';
    return 0;
  }
  int remove() {
    subscribed = false;
    log += 'r';
    return 0;
  }
  void reset() { log += 'f'; }
  uint32_t nowMs() const { return clock; }
  void report(const WatchdogEvent event, const int error) { reports.emplace_back(event, error); }
};

constexpr uint32_t DOG_LIMIT_MS = 5000;

// One blocking file call of `ms`, after `outsideMs` of work outside it: whether the task left the watchdog for it.
bool fileCall(TaskWatchdog<FakeDog>& watchdog, FakeDog& dog, const uint32_t outsideMs, const uint32_t ms) {
  dog.clock += outsideMs;
  const TaskWatchdog<FakeDog>::Gap gap(&watchdog);
  dog.clock += ms;
  return gap.left();
}

TEST(DecodeWatchdog, TheTaskJoinsForTheReadAndLeavesAtItsEnd) {
  FakeDog dog;
  {
    TaskWatchdog<FakeDog> watchdog(dog, DOG_LIMIT_MS);
    EXPECT_TRUE(watchdog.joined());
    EXPECT_TRUE(watchdog.owned());
    EXPECT_TRUE(dog.subscribed);
    watchdog.progress();
    EXPECT_EQ(dog.log, "af");
  }
  EXPECT_EQ(dog.log, "afr");
  EXPECT_FALSE(dog.subscribed);
  EXPECT_TRUE(dog.reports.empty());
}

TEST(DecodeWatchdog, EveryBlockingFileCallIsAGapAndItsTimeIsNotQuiet) {
  FakeDog dog;
  TaskWatchdog<FakeDog> watchdog(dog, DOG_LIMIT_MS);
  // Calls of 4 s each (a card held by another task), 1 ms apart, no progress: each a gap, left and rejoined (its time
  // not quiet), until the wall time since the last progress reaches twice the limit.
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 4000));
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 4000));
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 4000));
  EXPECT_EQ(dog.log, std::string("a") + "rarara");
  EXPECT_TRUE(dog.subscribed);
  EXPECT_FALSE(fileCall(watchdog, dog, 1, 4000));  // 12 s since the last progress: watched from here
  EXPECT_FALSE(fileCall(watchdog, dog, 1, 10));
  EXPECT_TRUE(dog.subscribed);
  // Progress starts it over.
  watchdog.progress();
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 4000));
}

TEST(DecodeWatchdog, QuietTimeOutsideFileCallsEndsTheGaps) {
  FakeDog dog;
  TaskWatchdog<FakeDog> watchdog(dog, DOG_LIMIT_MS);
  // A loop of short reads 1 s apart without progress: gaps until 5 s outside file calls, then watched.
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(fileCall(watchdog, dog, 1000, 5)) << i;
  EXPECT_FALSE(fileCall(watchdog, dog, 1000, 5));
  EXPECT_FALSE(fileCall(watchdog, dog, 1000, 5));
  // A row or a read that gave bytes: gaps again.
  watchdog.progress();
  EXPECT_TRUE(fileCall(watchdog, dog, 1000, 5));
  EXPECT_EQ(dog.log.back(), 'a');
}

TEST(DecodeWatchdog, ATaskAlreadyWatchedIsFedButNeverLeft) {
  FakeDog dog;
  dog.initial = WatchdogState::Subscribed;
  {
    TaskWatchdog<FakeDog> watchdog(dog, DOG_LIMIT_MS);
    EXPECT_TRUE(watchdog.joined());
    EXPECT_FALSE(watchdog.owned());
    watchdog.progress();
    EXPECT_FALSE(fileCall(watchdog, dog, 1, 3000));
  }
  EXPECT_EQ(dog.log, "f");  // fed; never removed, never added
  EXPECT_TRUE(dog.subscribed);
}

TEST(DecodeWatchdog, AWatchdogNotConfiguredOrAFailedJoinIsReported) {
  FakeDog off;
  off.initial = WatchdogState::NotConfigured;
  {
    TaskWatchdog<FakeDog> watchdog(off, DOG_LIMIT_MS);
    EXPECT_FALSE(watchdog.joined());
    watchdog.progress();
    EXPECT_FALSE(fileCall(watchdog, off, 1, 10));
  }
  EXPECT_EQ(off.log, "");
  ASSERT_EQ(off.reports.size(), 1u);
  EXPECT_EQ(off.reports[0].first, WatchdogEvent::NotConfigured);
  // An add that fails once is tried again.
  FakeDog once;
  once.failAdds = 1;
  {
    TaskWatchdog<FakeDog> watchdog(once, DOG_LIMIT_MS);
    EXPECT_TRUE(watchdog.owned());
  }
  EXPECT_EQ(once.log, "xar");
  EXPECT_TRUE(once.reports.empty());
  // Twice: reported with the error, and nothing else is asked of the watchdog.
  FakeDog never;
  never.failAdds = 2;
  {
    TaskWatchdog<FakeDog> watchdog(never, DOG_LIMIT_MS);
    EXPECT_FALSE(watchdog.joined());
    watchdog.progress();
    EXPECT_FALSE(fileCall(watchdog, never, 1, 10));
  }
  EXPECT_EQ(never.log, "xx");
  ASSERT_EQ(never.reports.size(), 1u);
  EXPECT_EQ(never.reports[0].first, WatchdogEvent::JoinFailed);
  EXPECT_EQ(never.reports[0].second, 0x101);
}

TEST(DecodeWatchdog, AFailedRejoinIsTriedAgainThenReported) {
  FakeDog dog;
  TaskWatchdog<FakeDog> watchdog(dog, DOG_LIMIT_MS);
  dog.failAdds = 1;
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 10));
  EXPECT_TRUE(watchdog.owned());
  EXPECT_EQ(dog.log, "arxa");
  dog.failAdds = 2;
  EXPECT_TRUE(fileCall(watchdog, dog, 1, 10));
  EXPECT_FALSE(watchdog.joined());
  EXPECT_FALSE(dog.subscribed);
  ASSERT_EQ(dog.reports.size(), 1u);
  EXPECT_EQ(dog.reports[0].first, WatchdogEvent::RejoinFailed);
  // Unwatched from here: no more gaps, feeds or a final leave.
  watchdog.progress();
  EXPECT_FALSE(fileCall(watchdog, dog, 1, 10));
  EXPECT_EQ(dog.log, "arxarxx");
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
