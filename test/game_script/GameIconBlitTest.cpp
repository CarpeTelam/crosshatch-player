#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "GameIconBlit.h"
#include "GameIcons.h"
#include "GameIconsRaw.h"

using GameIconBlit::inkAt;
using GameIconBlit::inkRuns;
using GameIconBlit::Source;
using GameIconBlit::sourceFor;

namespace {

using InkRun = std::array<int32_t, 3>;  // y, x, w
using Pixel = std::pair<int, int>;      // x, y

static_assert(GameIcons::find("dice-six", 8) >= 0, "find works at compile time");
static_assert(GameIcons::find("dice", 4) == -1, "a prefix is not a name");

constexpr GameIcons::Weight WEIGHTS[] = {GameIcons::Weight::Regular, GameIcons::Weight::Fill};
static_assert(std::size(WEIGHTS) == GameIcons::WEIGHT_COUNT, "every weight is tested");

// A blank `pixels`-square bitmap in drawIcon's layout (every bit 1: no ink).
std::vector<uint8_t> blank(const int pixels) {
  return std::vector<uint8_t>(static_cast<size_t>(pixels) * ((pixels + 7) / 8), 0xFF);
}

// Clears the stored bit at (row, col): ink there.
void inkStored(std::vector<uint8_t>& bitmap, const int pixels, const int row, const int col) {
  bitmap[row * ((pixels + 7) / 8) + (col >> 3)] &= static_cast<uint8_t>(~(0x80 >> (col & 7)));
}

// Ink at drawn (x, y), through drawIcon's mapping: stored row pixels - 1 - x, col y.
void inkDrawn(std::vector<uint8_t>& bitmap, const int pixels, const int x, const int y) {
  inkStored(bitmap, pixels, pixels - 1 - x, y);
}

// `bytes` as PackBits, as a test writes it: a run of three or more equal bytes as a repeat (at most 128), the bytes
// between them as copies (at most 128 each). Runs cross row boundaries, as the generator's do.
std::vector<uint8_t> packBytes(const std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> out;
  const auto runAt = [&](const size_t i) {
    size_t length = 1;
    while (i + length < bytes.size() && length < 128 && bytes[i + length] == bytes[i]) ++length;
    return length;
  };
  for (size_t i = 0; i < bytes.size();) {
    const size_t run = runAt(i);
    if (run >= 3) {
      out.push_back(static_cast<uint8_t>(257 - run));
      out.push_back(bytes[i]);
      i += run;
      continue;
    }
    size_t end = i;
    while (end < bytes.size() && end - i < 128 && runAt(end) < 3) ++end;
    out.push_back(static_cast<uint8_t>(end - i - 1));
    out.insert(out.end(), bytes.begin() + i, bytes.begin() + end);
    i = end;
  }
  return out;
}

// A length prefix, then `runs`.
std::vector<uint8_t> withLength(const std::vector<uint8_t>& runs, const size_t declared) {
  std::vector<uint8_t> out = {static_cast<uint8_t>(declared & 0xFF), static_cast<uint8_t>(declared >> 8)};
  out.insert(out.end(), runs.begin(), runs.end());
  return out;
}

// The packed form (GameIcons.h) of a bitmap in drawIcon's layout: its drawn rows, PackBits, after the length.
std::vector<uint8_t> pack(const std::vector<uint8_t>& stored, const int pixels) {
  const int rowBytes = pixels / 8;
  std::vector<uint8_t> rows(static_cast<size_t>(pixels * rowBytes), 0xFF);
  for (int y = 0; y < pixels; ++y) {
    for (int x = 0; x < pixels; ++x) {
      const int storedRow = pixels - 1 - x;
      if (((stored[storedRow * rowBytes + (y >> 3)] >> (7 - (y & 7))) & 1) == 0)
        rows[y * rowBytes + (x >> 3)] &= ~(0x80 >> (x & 7));
    }
  }
  const std::vector<uint8_t> runs = packBytes(rows);
  return withLength(runs, runs.size());
}

// The pixels GfxRenderer::drawIcon plots for a bitmap at (0, 0): its loop, with
// drawPixel recording instead of drawing.
std::set<Pixel> drawIconPixels(const uint8_t* bitmap, const int size) {
  std::set<Pixel> plotted;
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) plotted.insert({size - 1 - row, col});
    }
  }
  return plotted;
}

std::set<Pixel> inkAtPixels(const uint8_t* bitmap, const int size) {
  std::set<Pixel> ink;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (inkAt(bitmap, size, x, y)) ink.insert({x, y});
    }
  }
  return ink;
}

std::vector<InkRun> runsOf(const Source& source, const int32_t left, const int32_t top, const int32_t width,
                           const int32_t height) {
  std::vector<InkRun> runs;
  inkRuns(source, left, top, width, height,
          [&](const int32_t y, const int32_t x, const int32_t w) { runs.push_back({y, x, w}); });
  return runs;
}

// Every pixel the runs cover.
std::set<Pixel> covered(const std::vector<InkRun>& runs) {
  std::set<Pixel> pixels;
  for (const InkRun& run : runs) {
    for (int32_t x = run[1]; x < run[1] + run[2]; ++x) pixels.insert({x, run[0]});
  }
  return pixels;
}

TEST(GameIconBlitTest, FindHitsEveryNameAndMissesEverythingElse) {
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    const char* name = GameIcons::ICONS[i].name;
    EXPECT_EQ(GameIcons::find(name, std::strlen(name)), static_cast<int>(i)) << name;
  }
  const std::string hits[] = {"x", "circle", "dice-six", "arrow-u-up-left", "arrows-clockwise", "game-controller"};
  for (const std::string& name : hits) {
    EXPECT_GE(GameIcons::find(name.data(), name.size()), 0) << name;
  }
  const std::string misses[] = {
      "no-such-icon",
      "",
      "dice",
      "dice-",
      "dice_six",
      "dice-sixx",
      "dice-six-fill",
      "x-",
      "xx",
      "mark_x",
      "mark_o",
      "die_6",
      "game_controller",
      "piece_pawn",
      "a",
      "zzz",
      "DICE-SIX",
      std::string("x\0", 2),
      std::string("dice-six\0", 9),
      std::string("\0dice-six", 9),
  };
  for (const std::string& name : misses) {
    EXPECT_EQ(GameIcons::find(name.data(), name.size()), -1) << name;
  }
  // No NUL needed: the length bounds the name.
  const char longer[] = {'c', 'i', 'r', 'c', 'l', 'e', 'X'};
  EXPECT_EQ(GameIcons::find(longer, 6), GameIcons::find("circle", 6));
  EXPECT_EQ(GameIcons::find(longer, 7), -1);
}

TEST(GameIconBlitTest, InkAtMatchesDrawIconsMapping) {
  // One stored bit at a time, in a 32 px bitmap and at the edges of a 64 px one.
  const std::pair<int, std::vector<Pixel>> cases[] = {
      {32, {{0, 0}, {5, 9}, {31, 0}, {0, 31}, {31, 31}, {7, 8}}},
      {64, {{0, 0}, {63, 63}, {40, 3}}},
  };
  for (const auto& [pixels, cells] : cases) {
    for (const auto& [row, col] : cells) {
      std::vector<uint8_t> bitmap = blank(pixels);
      inkStored(bitmap, pixels, row, col);
      const std::vector<uint8_t> packed = pack(bitmap, pixels);
      const std::set<Pixel> expected = {{pixels - 1 - row, col}};
      EXPECT_EQ(drawIconPixels(bitmap.data(), pixels), expected) << row << "," << col;
      EXPECT_EQ(inkAtPixels(packed.data(), pixels), expected) << row << "," << col;
    }
  }
  // And every bitmap of the library, in each weight, against the raw bitmap the generator also computes; the two
  // weights differ.
  ASSERT_EQ(GameIcons::ICON_COUNT, GameIconsRaw::ICON_COUNT);
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    const GameIcons::Icon& icon = GameIcons::ICONS[i];
    const GameIconsRaw::Icon& raw = GameIconsRaw::ICONS[i];
    for (size_t w = 0; w < GameIcons::WEIGHT_COUNT; ++w) {
      EXPECT_EQ(inkAtPixels(icon.small[w], 32), drawIconPixels(raw.small[w], 32)) << icon.name << " " << w;
      EXPECT_EQ(inkAtPixels(icon.medium[w], 64), drawIconPixels(raw.medium[w], 64)) << icon.name << " " << w;
      EXPECT_FALSE(inkAtPixels(icon.small[w], 32).empty()) << icon.name << " " << w;
    }
    EXPECT_NE(inkAtPixels(icon.medium[0], 64), inkAtPixels(icon.medium[1], 64)) << icon.name;
  }
}

// Every bitmap at every size and weight, decoded row by row, is the raw bitmap the generator also computes: the
// same bytes once put back in drawIcon's layout, and the same pixels through the draw path (inkRuns) at 32, 64,
// and 128 px.
TEST(GameIconBlitTest, EveryPackedBitmapDecodesToItsRawBitmapPixelForPixel) {
  ASSERT_EQ(GameIcons::ICON_COUNT, GameIconsRaw::ICON_COUNT) << "regenerate test/game_script/GameIconsRaw.h";
  ASSERT_EQ(GameIcons::SMALL_BYTES, GameIconsRaw::SMALL_BYTES);
  ASSERT_EQ(GameIcons::MEDIUM_BYTES, GameIconsRaw::MEDIUM_BYTES);
  size_t bitmaps = 0;
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    ASSERT_STREQ(GameIcons::ICONS[i].name, GameIconsRaw::ICONS[i].name);
    for (const GameIcons::Weight weight : WEIGHTS) {
      const auto w = static_cast<size_t>(weight);
      for (const int pixels : {32, 64}) {
        SCOPED_TRACE(std::string(GameIcons::ICONS[i].name) + " weight " + std::to_string(w) + " at " +
                     std::to_string(pixels));
        const uint8_t* packed = pixels == 32 ? GameIcons::ICONS[i].small[w] : GameIcons::ICONS[i].medium[w];
        const uint8_t* raw = pixels == 32 ? GameIconsRaw::ICONS[i].small[w] : GameIconsRaw::ICONS[i].medium[w];
        const int rowBytes = pixels / 8;
        // Decode every row, with no run left over, and put the drawn rows back in drawIcon's layout.
        std::vector<uint8_t> stored(static_cast<size_t>(pixels * rowBytes), 0xFF);
        GameIcons::PackedReader reader(packed);
        uint8_t row[GameIcons::MAX_ROW_BYTES];
        for (int y = 0; y < pixels; ++y) {
          ASSERT_TRUE(reader.row(row, static_cast<size_t>(rowBytes))) << "row " << y;
          for (int x = 0; x < pixels; ++x) {
            if (((row[x >> 3] >> (7 - (x & 7))) & 1) == 0) inkDrawn(stored, pixels, x, y);
          }
        }
        ASSERT_TRUE(reader.finished());
        uint8_t extra;
        ASSERT_FALSE(reader.next(extra));
        ASSERT_EQ(std::memcmp(stored.data(), raw, stored.size()), 0);
        // Through the draw path: the runs cover the raw bitmap's pixels, scaled 1x, and 2x for the medium bitmap.
        for (const int scale : pixels == 32 ? std::vector<int>{1} : std::vector<int>{1, 2}) {
          std::set<Pixel> expected;
          for (const auto& [x, y] : drawIconPixels(raw, pixels)) {
            for (int dy = 0; dy < scale; ++dy) {
              for (int dx = 0; dx < scale; ++dx) expected.insert({x * scale + dx, y * scale + dy});
            }
          }
          const int side = pixels * scale;
          EXPECT_EQ(covered(runsOf(Source{packed, pixels, scale}, 0, 0, side, side)), expected) << "scale " << scale;
        }
        ++bitmaps;
      }
    }
  }
  EXPECT_EQ(bitmaps, GameIcons::ICON_COUNT * GameIcons::WEIGHT_COUNT * 2);
}

TEST(GameIconBlitTest, TheCoverGridsControllerIsRawInDrawIconsLayoutAndMatchesTheLibrarys) {
  const char* name = "game-controller";
  const int index = GameIcons::find(name, std::strlen(name));
  ASSERT_GE(index, 0);
  const auto regular = static_cast<size_t>(GameIcons::Weight::Regular);
  const GameIconsRaw::Icon& raw = GameIconsRaw::ICONS[index];
  ASSERT_EQ(sizeof(GameIcons::GAME_CONTROLLER_32), GameIcons::SMALL_BYTES);
  EXPECT_EQ(std::memcmp(GameIcons::GAME_CONTROLLER_32, raw.small[regular], GameIcons::SMALL_BYTES), 0);
  EXPECT_EQ(inkAtPixels(GameIcons::ICONS[index].small[regular], 32), drawIconPixels(GameIcons::GAME_CONTROLLER_32, 32));
}

// A clipped draw is the full draw's runs cut to the canvas, whatever rows and columns the clip hides: the rows above
// the clip are decoded, not drawn.
TEST(GameIconBlitTest, AClippedDrawIsTheFullDrawCutToTheCanvas) {
  for (const char* name : {"dice-six", "circle", "arrow-clockwise", "game-controller"}) {
    const int index = GameIcons::find(name, std::strlen(name));
    ASSERT_GE(index, 0) << name;
    for (const GameIcons::Weight weight : WEIGHTS) {
      for (const int drawn : {32, 64, 128}) {
        Source source;
        ASSERT_TRUE(sourceFor(static_cast<size_t>(index), drawn, weight, source));
        const std::vector<InkRun> full = runsOf(source, 0, 0, 4096, 4096);
        ASSERT_FALSE(full.empty());
        for (const auto& [left, top, width, height] : std::vector<std::array<int32_t, 4>>{{-5, -9, 40, 50},
                                                                                          {3, -drawn + 1, 200, 200},
                                                                                          {-drawn / 2, 7, drawn, 11},
                                                                                          {10, 10, 3, 3},
                                                                                          {0, -1, drawn, 2},
                                                                                          {drawn - 1, 0, 50, 50}}) {
          std::vector<InkRun> expected;
          for (const InkRun& run : full) {
            const int32_t y = run[0] + top;
            const int32_t x0 = std::max(run[1] + left, 0);
            const int32_t x1 = std::min(run[1] + left + run[2], width);
            if (y >= 0 && y < height && x1 > x0) expected.push_back({y, x0, x1 - x0});
          }
          EXPECT_EQ(runsOf(source, left, top, width, height), expected)
              << name << " " << static_cast<int>(weight) << " at " << drawn << " over " << left << "," << top << " "
              << width << "x" << height;
        }
      }
    }
  }
}

// Only a generator bug makes a malformed bitmap; a draw of one keeps the rows before the fault and reads nothing past
// the stored length (the bytes after it here would draw ink if they were read).
TEST(GameIconBlitTest, AMalformedBitmapDrawsOnlyItsRowsBeforeTheFault) {
  const std::vector<uint8_t> inkRow = {0xFD, 0x00};  // a 32 px row of ink: 4 x 0x00
  const auto bitmapOf = [&](const std::vector<std::vector<uint8_t>>& pieces, const size_t declared = SIZE_MAX) {
    std::vector<uint8_t> runs;
    for (const auto& piece : pieces) runs.insert(runs.end(), piece.begin(), piece.end());
    return withLength(runs, declared == SIZE_MAX ? runs.size() : declared);
  };
  const auto inkRows = [&](const std::vector<uint8_t>& bitmap) {
    std::set<int> rows;
    for (const InkRun& run : runsOf(Source{bitmap.data(), 32, 1}, 0, 0, 32, 32)) rows.insert(run[0]);
    return rows;
  };
  const auto upTo = [](const int count) {
    std::set<int> rows;
    for (int y = 0; y < count; ++y) rows.insert(y);
    return rows;
  };
  const std::vector<std::vector<uint8_t>> good(32, inkRow);
  const auto goodUpTo = [&](const size_t count, const std::vector<uint8_t>& then) {
    std::vector<std::vector<uint8_t>> pieces(good.begin(), good.begin() + count);
    pieces.push_back(then);
    return bitmapOf(pieces);
  };
  EXPECT_EQ(inkRows(bitmapOf(good)), upTo(32));
  EXPECT_TRUE(GameIcons::detail::wellFormed(bitmapOf(good).data(), 32));

  // Control 128, a repeat with no byte to repeat, and a copy that runs off the end, each after 5 good rows.
  EXPECT_EQ(inkRows(goodUpTo(5, {0x80, 0x00, 0x00, 0x00, 0x00})), upTo(5));
  EXPECT_EQ(inkRows(goodUpTo(5, {0xFD})), upTo(5));
  EXPECT_EQ(inkRows(goodUpTo(5, {0x03, 0x00, 0x00})), upTo(5));
  // A row cut short by the end of the runs draws nothing, though its first bytes decoded.
  EXPECT_EQ(inkRows(goodUpTo(5, {0x01, 0x00, 0x00})), upTo(5));
  // The stored length ends mid-run: rows 0 to 9 decode (2 bytes each), the rest is never read.
  EXPECT_EQ(inkRows(bitmapOf(good, 2 * 10 + 1)), upTo(10));
  EXPECT_EQ(inkRows(bitmapOf(good, 2 * 10)), upTo(10));
  EXPECT_TRUE(inkRows(bitmapOf(good, 0)).empty());
  EXPECT_TRUE(inkRows(withLength({}, 0)).empty());
  // inkAt reads the same rows: ink before the fault, none at or after it.
  const std::vector<uint8_t> cut = bitmapOf(good, 2 * 10);
  EXPECT_TRUE(inkAt(cut.data(), 32, 3, 9));
  EXPECT_FALSE(inkAt(cut.data(), 32, 3, 10));
  EXPECT_FALSE(inkAt(cut.data(), 32, 3, 31));
  // A bitmap that is not whole bytes a row, or too wide for the row buffer, draws nothing.
  for (const int pixels : {12, 72, 0, -8}) {
    EXPECT_TRUE(runsOf(Source{bitmapOf(good).data(), pixels, 1}, 0, 0, 200, 200).empty()) << pixels;
  }

  // A run may cross rows: 6 bytes of ink, a copy of 2 blank bytes, then 120 blank bytes make row 0 all ink and
  // row 1 ink at x 0 to 15.
  const std::vector<uint8_t> crossing = bitmapOf({{0xFB, 0x00}, {0x01, 0xFF, 0xFF}, {0x89, 0xFF}});
  EXPECT_TRUE(GameIcons::detail::wellFormed(crossing.data(), 32));
  EXPECT_EQ(runsOf(Source{crossing.data(), 32, 1}, 0, 0, 32, 32), (std::vector<InkRun>{{0, 0, 32}, {1, 0, 16}}));
  // One run for the whole bitmap: 128 bytes of ink.
  const std::vector<uint8_t> solid = bitmapOf({{0x81, 0x00}});
  EXPECT_TRUE(GameIcons::detail::wellFormed(solid.data(), 32));
  EXPECT_EQ(inkRows(solid), upTo(32));

  // wellFormed is the build-time check: it also refuses a stream that is not exactly its rows, though a draw of
  // it would show every row.
  EXPECT_FALSE(GameIcons::detail::wellFormed(goodUpTo(32, {0x00, 0xFF}).data(), 32));  // a byte too many
  EXPECT_FALSE(GameIcons::detail::wellFormed(bitmapOf({{0x81, 0x00}, {0xFD, 0x00}}).data(), 32));
  EXPECT_FALSE(GameIcons::detail::wellFormed(bitmapOf({{0x80, 0x00}}).data(), 32));
  EXPECT_FALSE(GameIcons::detail::wellFormed(bitmapOf({{0xFE, 0x00}, {0x81, 0xFF}}).data(), 32));  // a run past the end
  EXPECT_FALSE(GameIcons::detail::wellFormed(bitmapOf(std::vector<std::vector<uint8_t>>(31, inkRow)).data(), 32));
  EXPECT_EQ(inkRows(goodUpTo(32, {0x00, 0xFF})), upTo(32));

  // PackedReader reads no byte at or past the stored length, and keeps failing once it has failed.
  const uint8_t data[] = {0x02, 0x00, 0xFF, 0x11, 0x22, 0x33};  // a length of 2: a repeat of 2 (0x11), then more
  GameIcons::PackedReader reader(data);
  uint8_t byte = 0;
  ASSERT_TRUE(reader.next(byte));
  EXPECT_EQ(byte, 0x11);
  ASSERT_TRUE(reader.next(byte));
  EXPECT_EQ(byte, 0x11);
  EXPECT_TRUE(reader.finished());
  byte = 0xAA;
  EXPECT_FALSE(reader.next(byte));  // the control byte 0x22 is at the end
  EXPECT_EQ(byte, 0xAA);
  EXPECT_FALSE(reader.finished());
  uint8_t row[GameIcons::MAX_ROW_BYTES];
  const uint8_t once[] = {0x01, 0x00, 0xF7, 0x55};  // a length of 1: a repeat of 10 whose byte is at the end
  GameIcons::PackedReader cut2(once);
  EXPECT_FALSE(cut2.row(row, 2));
  EXPECT_FALSE(cut2.row(row, 1));
  GameIcons::PackedReader wide(data);
  EXPECT_FALSE(wide.row(row, GameIcons::MAX_ROW_BYTES + 1));
}

// The build-time check (wellFormed, which walks the runs) and the draw's reader (PackedReader, which expands them)
// are two parsers of one rule: over random streams, some valid and some broken every way the reader knows, they agree
// on whether a `pixels` square bitmap is exactly its rows.
TEST(GameIconBlitTest, TheBuildTimeCheckAndTheReaderAgreeOnWhatIsWellFormed) {
  uint32_t seed = 15;
  const auto pick = [&](const uint32_t below) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % below;
  };
  size_t good = 0;
  for (int round = 0; round < 20000; ++round) {
    const int pixels = pick(2) == 0 ? 8 : 16;  // 8 or 32 decoded bytes
    std::vector<uint8_t> runs;
    const uint32_t wanted = static_cast<uint32_t>(pixels * pixels / 8);
    uint32_t decoded = 0;
    while (decoded < wanted + pick(3)) {
      const uint32_t kind = pick(12);
      if (kind == 0) {
        runs.push_back(0x80);
      } else if (kind < 6) {
        const uint32_t count = 1 + pick(6);
        runs.push_back(static_cast<uint8_t>(count - 1));
        for (uint32_t i = 0; i < count; ++i) runs.push_back(static_cast<uint8_t>(pick(256)));
        decoded += count;
      } else {
        const uint32_t count = 2 + pick(9);
        runs.push_back(static_cast<uint8_t>(257 - count));
        runs.push_back(static_cast<uint8_t>(pick(256)));
        decoded += count;
      }
    }
    if (pick(4) == 0 && !runs.empty()) runs.resize(runs.size() - 1 - pick(static_cast<uint32_t>(runs.size())));
    std::vector<uint8_t> bitmap = withLength(runs, runs.size());
    if (pick(8) == 0) bitmap[0] = static_cast<uint8_t>(bitmap[0] + (pick(2) ? 1 : 0xFF));  // a wrong prefix
    // The reader reads only what the prefix says, so give it exactly the bytes there (a wrong prefix that
    // claims more than the array holds is the generator's fault, and no draw can tell).
    const size_t claimed = GameIcons::packedLength(bitmap.data());
    if (claimed + GameIcons::PACKED_LENGTH_BYTES > bitmap.size())
      bitmap.resize(claimed + GameIcons::PACKED_LENGTH_BYTES, 0);
    GameIcons::PackedReader reader(bitmap.data());
    uint8_t row[GameIcons::MAX_ROW_BYTES];
    bool readable = true;
    for (int y = 0; y < pixels && readable; ++y) readable = reader.row(row, static_cast<size_t>(pixels / 8));
    const bool readerSays = readable && reader.finished();
    ASSERT_EQ(GameIcons::detail::wellFormed(bitmap.data(), pixels), readerSays) << "round " << round;
    good += readerSays;
  }
  EXPECT_GT(good, 500u);  // the streams are not all broken
  EXPECT_LT(good, 19500u);
}

TEST(GameIconBlitTest, SourceForPicksTheBitmapAndScale) {
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    for (const GameIcons::Weight weight : WEIGHTS) {
      const auto w = static_cast<size_t>(weight);
      Source source;
      ASSERT_TRUE(sourceFor(i, 32, weight, source));
      EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].small[w]);
      EXPECT_EQ(source.pixels, 32);
      EXPECT_EQ(source.scale, 1);
      ASSERT_TRUE(sourceFor(i, 64, weight, source));
      EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].medium[w]);
      EXPECT_EQ(source.pixels, 64);
      EXPECT_EQ(source.scale, 1);
      ASSERT_TRUE(sourceFor(i, 128, weight, source));
      EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].medium[w]);
      EXPECT_EQ(source.pixels, 64);
      EXPECT_EQ(source.scale, 2);
    }
    EXPECT_NE(GameIcons::ICONS[i].small[0], GameIcons::ICONS[i].small[1]);
  }
  Source untouched;
  const auto pastWeights = static_cast<GameIcons::Weight>(GameIcons::WEIGHT_COUNT);
  EXPECT_FALSE(sourceFor(GameIcons::ICON_COUNT, 32, GameIcons::Weight::Regular, untouched));
  EXPECT_FALSE(sourceFor(0, 48, GameIcons::Weight::Regular, untouched));
  EXPECT_FALSE(sourceFor(0, 0, GameIcons::Weight::Fill, untouched));
  EXPECT_FALSE(sourceFor(0, 256, GameIcons::Weight::Fill, untouched));
  EXPECT_FALSE(sourceFor(0, 32, pastWeights, untouched));
  EXPECT_FALSE(sourceFor(0, 64, static_cast<GameIcons::Weight>(0xFF), untouched));
  EXPECT_EQ(untouched.bitmap, nullptr);
  EXPECT_EQ((std::vector<int>(std::begin(GameIconBlit::DRAWN_PIXELS), std::end(GameIconBlit::DRAWN_PIXELS))),
            (std::vector<int>{32, 64, 128}));
}

TEST(GameIconBlitTest, RunsMergeAdjacentInkAndSplitAtGaps) {
  std::vector<uint8_t> bitmap = blank(32);
  for (int x = 3; x <= 7; ++x) inkDrawn(bitmap, 32, x, 5);
  inkDrawn(bitmap, 32, 9, 5);
  inkDrawn(bitmap, 32, 31, 5);  // touching the right edge
  inkDrawn(bitmap, 32, 0, 6);
  const std::vector<uint8_t> packed = pack(bitmap, 32);
  const Source source{packed.data(), 32, 1};
  const std::vector<InkRun> expected = {
      {5 + 10, 3 + 20, 5}, {5 + 10, 9 + 20, 1}, {5 + 10, 31 + 20, 1}, {6 + 10, 20, 1}};
  EXPECT_EQ(runsOf(source, 20, 10, 480, 800), expected);
}

TEST(GameIconBlitTest, ScaleTwoDrawsEachPixelAsATwoByTwoBlock) {
  std::vector<uint8_t> bitmap = blank(64);
  inkDrawn(bitmap, 64, 0, 0);
  inkDrawn(bitmap, 64, 10, 20);
  inkDrawn(bitmap, 64, 11, 20);
  inkDrawn(bitmap, 64, 63, 63);
  const std::vector<uint8_t> packed = pack(bitmap, 64);
  const Source source{packed.data(), 64, 2};
  const std::vector<InkRun> expected = {
      {100, 50, 2}, {101, 50, 2}, {140, 70, 4}, {141, 70, 4}, {226, 176, 2}, {227, 176, 2},
  };
  EXPECT_EQ(runsOf(source, 50, 100, 480, 800), expected);
}

TEST(GameIconBlitTest, RunsAreClippedToTheCanvas) {
  const std::vector<uint8_t> full = pack(std::vector<uint8_t>(128, 0x00), 32);  // a 32 px bitmap that is all ink
  const Source source{full.data(), 32, 1};
  const auto square = [](const int left, const int top, const int right, const int bottom) {
    std::set<Pixel> pixels;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) pixels.insert({x, y});
    }
    return pixels;
  };
  // Inside: every pixel, one run per row.
  EXPECT_EQ(runsOf(source, 10, 10, 100, 60).size(), 32u);
  EXPECT_EQ(covered(runsOf(source, 10, 10, 100, 60)), square(10, 10, 42, 42));
  // Over each edge.
  EXPECT_EQ(covered(runsOf(source, -10, 5, 100, 60)), square(0, 5, 22, 37));
  EXPECT_EQ(covered(runsOf(source, 90, 5, 100, 60)), square(90, 5, 100, 37));
  EXPECT_EQ(covered(runsOf(source, 5, -20, 100, 60)), square(5, 0, 37, 12));
  EXPECT_EQ(covered(runsOf(source, 5, 50, 100, 60)), square(5, 50, 37, 60));
  EXPECT_EQ(covered(runsOf(source, -5, -5, 20, 20)), square(0, 0, 20, 20));
  // Wholly off each edge, far off, and on an empty canvas: nothing.
  for (const auto& [left, top] : std::vector<Pixel>{{-32, 0}, {100, 0}, {0, -32}, {0, 60}, {-30000, 30000}}) {
    EXPECT_TRUE(runsOf(source, left, top, 100, 60).empty()) << left << "," << top;
  }
  EXPECT_TRUE(runsOf(source, 0, 0, 0, 0).empty());
  EXPECT_TRUE(runsOf(source, 0, 0, -5, 60).empty());
  // Scaled: a 128 px icon over the canvas's bottom-right corner.
  const Source large{full.data(), 32, 2};  // stand-in: 64 drawn px from the 32 px bitmap
  EXPECT_EQ(covered(runsOf(large, 70, 40, 100, 60)), square(70, 40, 100, 60));
}

TEST(GameIconBlitTest, TheRegularCircleIsARingWithAnEmptyCentre) {
  const int index = GameIcons::find("circle", 6);
  ASSERT_GE(index, 0);
  Source source;
  ASSERT_TRUE(sourceFor(static_cast<size_t>(index), 64, GameIcons::Weight::Regular, source));
  EXPECT_FALSE(inkAt(source.bitmap, 64, 32, 32));
  EXPECT_FALSE(inkAt(source.bitmap, 64, 0, 0));
  // The middle row crosses the ring twice: two runs, far apart.
  std::vector<InkRun> middle;
  for (const InkRun& run : runsOf(source, 0, 0, 64, 64)) {
    if (run[0] == 32) middle.push_back(run);
  }
  ASSERT_EQ(middle.size(), 2u);
  EXPECT_LT(middle[0][1] + middle[0][2], 16);
  EXPECT_GT(middle[1][1], 48);
  // The ring is inked above and below the centre too.
  EXPECT_TRUE(inkAt(source.bitmap, 64, 32, 8));
  EXPECT_TRUE(inkAt(source.bitmap, 64, 32, 56));
}

TEST(GameIconBlitTest, TheFillCircleIsInkedAtTheCentre) {
  const int index = GameIcons::find("circle", 6);
  ASSERT_GE(index, 0);
  for (const int pixels : {32, 64}) {
    Source source;
    ASSERT_TRUE(sourceFor(static_cast<size_t>(index), pixels, GameIcons::Weight::Fill, source));
    EXPECT_TRUE(inkAt(source.bitmap, pixels, pixels / 2, pixels / 2)) << pixels;
    EXPECT_FALSE(inkAt(source.bitmap, pixels, 0, 0)) << pixels;
  }
  // The middle row is one run across the disc.
  Source source;
  ASSERT_TRUE(sourceFor(static_cast<size_t>(index), 64, GameIcons::Weight::Fill, source));
  std::vector<InkRun> middle;
  for (const InkRun& run : runsOf(source, 0, 0, 64, 64)) {
    if (run[0] == 32) middle.push_back(run);
  }
  ASSERT_EQ(middle.size(), 1u);
  EXPECT_LT(middle[0][1], 16);
  EXPECT_GT(middle[0][1] + middle[0][2], 48);
}

// The generated icon data fits its 96 KiB flash budget (docs/crosshatch/game-icons.md, Size), counted in the
// device's layout: each packed bitmap's two length bytes and runs, four per name (small and medium in each weight),
// the raw GAME_CONTROLLER_32, one ICONS entry of five 4-byte pointers on the ESP32 (20 B: the name and two per size,
// whatever sizeof(Icon) is on the host), and each NUL-terminated name. Packing must also beat the raw bitmaps.
TEST(GameIconBlitTest, TheIconDataFitsIn96KiB) {
  static_assert(GameIcons::WEIGHT_COUNT == 2, "four bitmaps a name");
  constexpr size_t DEVICE_ICON_ENTRY_BYTES = (1 + 2 * GameIcons::WEIGHT_COUNT) * 4;
  static_assert(DEVICE_ICON_ENTRY_BYTES == 20, "the device's ICONS entry");
  size_t bitmapBytes = 0;
  size_t rawBytes = 0;
  size_t bytes = sizeof(GameIcons::GAME_CONTROLLER_32) + GameIcons::ICON_COUNT * DEVICE_ICON_ENTRY_BYTES;
  for (const GameIcons::Icon& icon : GameIcons::ICONS) {
    for (size_t w = 0; w < GameIcons::WEIGHT_COUNT; ++w) {
      bitmapBytes += GameIcons::PACKED_LENGTH_BYTES + GameIcons::packedLength(icon.small[w]) +
                     GameIcons::PACKED_LENGTH_BYTES + GameIcons::packedLength(icon.medium[w]);
      rawBytes += GameIcons::SMALL_BYTES + GameIcons::MEDIUM_BYTES;
    }
    bytes += std::strlen(icon.name) + 1;
  }
  EXPECT_LT(bitmapBytes, rawBytes);
  EXPECT_LE(bytes + bitmapBytes, 98304u);
}

}  // namespace
